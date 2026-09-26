#include "profile.h"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace msf {
namespace {
namespace fs = std::filesystem;
// FNV-1a 64-bit over the canonical identity; rendered as 16 hex chars.
// Deterministic per hardware unit, opaque by design (see header).
std::uint64_t fnv1a(const std::string& s) {
  std::uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
  return h;
}
std::string escapeIni(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\\') o += "\\\\";
    else if (c == '=') o += "\\=";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else o += c;
  }
  return o;
}
// Returns false on a bad escape (strict: malformed files never half-load).
bool unescapeIni(const std::string& s, std::string& out) {
  out.clear();
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\') { out += s[i]; continue; }
    if (i + 1 >= s.size()) return false;
    const char e = s[++i];
    if (e == '\\') out += '\\';
    else if (e == '=') out += '=';
    else if (e == 'n') out += '\n';
    else if (e == 'r') out += '\r';
    else return false;
  }
  return true;
}
bool parseDouble(const std::string& s, double& out) {
  try {
    std::size_t pos = 0;
    out = std::stod(s, &pos);
    return pos == s.size();
  } catch (...) { return false; }
}
bool parseLong(const std::string& s, long long& out) {
  try {
    std::size_t pos = 0;
    out = std::stoll(s, &pos);
    return pos == s.size();
  } catch (...) { return false; }
}
bool parseState(const std::string& s, MeasureState& out) {
  if (s == "measured") { out = MeasureState::Measured; return true; }
  if (s == "not_measured") { out = MeasureState::NotMeasured; return true; }
  if (s == "not_available") { out = MeasureState::NotAvailable; return true; }
  if (s == "partial") { out = MeasureState::Partial; return true; }
  if (s == "failed") { out = MeasureState::Failed; return true; }
  if (s == "fallback") { out = MeasureState::Fallback; return true; }
  return false;
}
void writeMetric(std::ostringstream& o, const char* section, const char* name, const ProfileMetric& m) {
  o << "[" << section << "]\n"
    << name << "=" << m.value << "\n"
    << name << ".state=" << measureStateName(m.state) << "\n";
}
} // namespace
std::string ProfileStore::deriveId(const ProfileIdentity& id) {
  std::ostringstream o;
  o << "v" << PerformanceProfile::kProfileVersion << "|"
    << id.cpuModel << "|" << id.cpuThreads << "|"
    << id.gpuName << "|" << id.gpuBackend << "|" << id.driver << "|"
    << id.appVersion << "|" << id.engineVersion;
  const std::uint64_t h = fnv1a(o.str());
  char buf[17];
  std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return buf;
}
const char* ProfileStore::matchName(ProfileMatch m) {
  switch (m) {
    case ProfileMatch::Exact: return "exact";
    case ProfileMatch::Soft: return "soft";
    case ProfileMatch::Hard: return "hard";
    case ProfileMatch::Stale: return "stale";
    default: return "missing";
  }
}
ProfileMatch ProfileStore::classify(const ProfileIdentity& cur, long long maxAgeDays, long long nowSec) const {
  if (!has_) return ProfileMatch::Missing;
  const ProfileIdentity& st = profile_.identity;
  const bool gpuWas = !st.gpuName.empty() && st.gpuBackend != "CPU";
  const bool gpuIs = !cur.gpuName.empty() && cur.gpuBackend != "CPU";
  // Hard: capability set changed, or two known names disagree.
  if (gpuWas != gpuIs) return ProfileMatch::Hard;
  if (gpuWas && gpuIs && (st.gpuName != cur.gpuName || st.gpuBackend != cur.gpuBackend))
    return ProfileMatch::Hard;
  if (!st.cpuModel.empty() && !cur.cpuModel.empty() && st.cpuModel != cur.cpuModel)
    return ProfileMatch::Hard;
  // Stale outranks soft reuse: time says re-measure before trusting.
  if (maxAgeDays >= 0 && nowSec > profile_.updatedAt + maxAgeDays * 86400LL)
    return ProfileMatch::Stale;
  // Soft: version drift, thread-count change, or one-sided identity info
  // (including driver appearing/disappearing while the GPU itself matches).
  if (st.appVersion != cur.appVersion || st.engineVersion != cur.engineVersion)
    return ProfileMatch::Soft;
  if (st.cpuThreads != cur.cpuThreads) return ProfileMatch::Soft;
  if (st.cpuModel.empty() != cur.cpuModel.empty()) return ProfileMatch::Soft;
  if (st.gpuName.empty() != cur.gpuName.empty()) return ProfileMatch::Soft;
  if (st.driver != cur.driver) return ProfileMatch::Soft;
  return ProfileMatch::Exact;
}
InitialEstimate ProfileStore::initialEstimate(const ProfileIdentity& current,
                                               long long maxAgeDays, long long nowSec) const {
  InitialEstimate e;
  if (!has_) return e;
  const ProfileMatch m = classify(current, maxAgeDays, nowSec);
  if (m != ProfileMatch::Exact && m != ProfileMatch::Soft) return e;
  if (profile_.cpuThroughput.state == MeasureState::Measured &&
      profile_.gpuThroughput.state == MeasureState::Measured &&
      profile_.cpuThroughput.value > 0 && profile_.gpuThroughput.value > 0) {
    e.cpuKnown = e.gpuKnown = true;
    e.cpu = profile_.cpuThroughput.value;
    e.gpu = profile_.gpuThroughput.value;
  }
  return e;
}
bool ProfileStore::load(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  PerformanceProfile p;
  bool seenVersion = false;
  int fileVersion = 0;
  std::string section, line;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    if (line.front() == '[' && line.back() == ']') {
      section = line.substr(1, line.size() - 2);
      continue;
    }
    // Split on the first UNESCAPED '='.
    std::string key, val;
    bool found = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
      if (line[i] == '\\' && i + 1 < line.size()) { key += line[i]; key += line[i + 1]; ++i; continue; }
      if (line[i] == '=') { val = line.substr(i + 1); found = true; break; }
      key += line[i];
    }
    if (!found) return false; // malformed line: reject the whole file
    std::string dkey, dval;
    if (!unescapeIni(key, dkey) || !unescapeIni(val, dval)) return false;
    const std::string q = section + "." + dkey;
    if (q == "profile.version") {
      long long v = 0;
      if (!parseLong(dval, v)) return false;
      fileVersion = (int)v;
      seenVersion = true;
    } else if (q == "profile.id") p.id = dval;
    else if (q == "profile.confidence") { if (!parseDouble(dval, p.confidence)) return false; }
    else if (q == "profile.createdAt") { if (!parseLong(dval, p.createdAt)) return false; }
    else if (q == "profile.updatedAt") { if (!parseLong(dval, p.updatedAt)) return false; }
    else if (q == "identity.cpuModel") p.identity.cpuModel = dval;
    else if (q == "identity.cpuThreads") { long long v = 0; if (!parseLong(dval, v)) return false; p.identity.cpuThreads = (int)v; }
    else if (q == "identity.gpuName") p.identity.gpuName = dval;
    else if (q == "identity.gpuBackend") p.identity.gpuBackend = dval;
    else if (q == "identity.driver") p.identity.driver = dval;
    else if (q == "identity.appVersion") p.identity.appVersion = dval;
    else if (q == "identity.engineVersion") p.identity.engineVersion = dval;
    else if (q == "metric.cpuThroughput") { if (!parseDouble(dval, p.cpuThroughput.value)) return false; }
    else if (q == "metric.cpuThroughput.state") { if (!parseState(dval, p.cpuThroughput.state)) return false; }
    else if (q == "metric.gpuThroughput") { if (!parseDouble(dval, p.gpuThroughput.value)) return false; }
    else if (q == "metric.gpuThroughput.state") { if (!parseState(dval, p.gpuThroughput.state)) return false; }
    else if (q == "metric.gpuBatchThroughput") { if (!parseDouble(dval, p.gpuBatchThroughput.value)) return false; }
    else if (q == "metric.gpuBatchThroughput.state") { if (!parseState(dval, p.gpuBatchThroughput.state)) return false; }
    else if (q == "metric.resizeThroughput") { if (!parseDouble(dval, p.resizeThroughput.value)) return false; }
    else if (q == "metric.resizeThroughput.state") { if (!parseState(dval, p.resizeThroughput.state)) return false; }
    else if (q == "metric.decodeThroughput") { if (!parseDouble(dval, p.decodeThroughput.value)) return false; }
    else if (q == "metric.decodeThroughput.state") { if (!parseState(dval, p.decodeThroughput.state)) return false; }
    else if (q == "metric.transferBandwidthMBps") { if (!parseDouble(dval, p.transferBandwidthMBps.value)) return false; }
    else if (q == "metric.transferBandwidthMBps.state") { if (!parseState(dval, p.transferBandwidthMBps.state)) return false; }
    else if (q == "metric.queueLatencyMs") { if (!parseDouble(dval, p.queueLatencyMs.value)) return false; }
    else if (q == "metric.queueLatencyMs.state") { if (!parseState(dval, p.queueLatencyMs.state)) return false; }
    // Unknown keys/sections ignored: forward compatibility for new fields.
  }
  if (!seenVersion) return false;
  if (fileVersion > PerformanceProfile::kProfileVersion) return false; // cannot interpret newer
  if (fileVersion < 1) return false;
  profile_ = std::move(p);
  has_ = true;
  return true;
}
bool ProfileStore::save(const std::string& path) {
  if (!has_) return false;
  const long long now = (long long)std::time(nullptr);
  if (profile_.createdAt <= 0) profile_.createdAt = now;
  profile_.updatedAt = now;
  if (profile_.id.empty()) profile_.id = deriveId(profile_.identity);
  std::error_code ec;
  const fs::path dst(path);
  if (dst.has_parent_path()) fs::create_directories(dst.parent_path(), ec);
  if (ec) return false;
  static unsigned long long seq = 0;
  const fs::path tmp = dst.parent_path() /
      (dst.filename().string() + ".tmp." + std::to_string((unsigned long long)now) + "." + std::to_string(++seq));
  {
    std::ostringstream o;
    o << "# MediaSimilarityFinder performance profile v" << PerformanceProfile::kProfileVersion << "\n";
    o << "[profile]\nversion=" << PerformanceProfile::kProfileVersion << "\n";
    o << "id=" << escapeIni(profile_.id) << "\n";
    o << "confidence=" << profile_.confidence << "\n";
    o << "createdAt=" << profile_.createdAt << "\nupdatedAt=" << profile_.updatedAt << "\n";
    // Informational provenance (brief §5 meta). Not identity inputs: version
    // drift is judged on app/engineVersion inside [identity] instead.
    o << "benchmarkSchemaVersion=" << BenchmarkRecorder::kBenchmarkSchemaVersion << "\n";
    o << "[identity]\n";
    o << "cpuModel=" << escapeIni(profile_.identity.cpuModel) << "\n";
    o << "cpuThreads=" << profile_.identity.cpuThreads << "\n";
    o << "gpuName=" << escapeIni(profile_.identity.gpuName) << "\n";
    o << "gpuBackend=" << escapeIni(profile_.identity.gpuBackend) << "\n";
    o << "driver=" << escapeIni(profile_.identity.driver) << "\n";
    o << "appVersion=" << escapeIni(profile_.identity.appVersion) << "\n";
    o << "engineVersion=" << escapeIni(profile_.identity.engineVersion) << "\n";
    writeMetric(o, "metric", "cpuThroughput", profile_.cpuThroughput);
    writeMetric(o, "metric", "gpuThroughput", profile_.gpuThroughput);
    writeMetric(o, "metric", "gpuBatchThroughput", profile_.gpuBatchThroughput);
    writeMetric(o, "metric", "resizeThroughput", profile_.resizeThroughput);
    writeMetric(o, "metric", "decodeThroughput", profile_.decodeThroughput);
    writeMetric(o, "metric", "transferBandwidthMBps", profile_.transferBandwidthMBps);
    writeMetric(o, "metric", "queueLatencyMs", profile_.queueLatencyMs);
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << o.str();
    f.flush();
    if (!f) return false;
  }
  fs::rename(tmp, dst, ec);
  if (ec) {
    // Windows replace semantics vary: remove-then-rename fallback. A crash
    // inside this window loses the profile (regenerable by C2), never data.
    std::error_code ec2;
    fs::remove(dst, ec2);
    fs::rename(tmp, dst, ec);
    if (ec) { fs::remove(tmp, ec2); return false; }
  }
  return true;
}
}
