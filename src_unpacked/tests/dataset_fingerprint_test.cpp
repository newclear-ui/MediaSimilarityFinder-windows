#include "benchmark.h"
#include "dataset_fingerprint.h"
#include "path_utils.h"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// D8a regression: the reproducibility properties the D4b / Full D3 / D8
// comparisons rest on. If any of these fail, "same dataset" is not a claim
// this project can make, so they are asserted rather than observed.
//
// A  determinism       - two computations over one fixture agree
// B  content sensitivity - altering a file's bytes changes the fingerprint
// C  root independence - the same content under a different absolute root
//                         yields the same fingerprint (this is why the
//                         fingerprint must not contain a path)
// D  order independence - manifest sorting removes walk order
// E  honest states     - missing / empty dataset report a state, never 0
// F  JSON              - the recorder carries identity and correct state
//
// The fixture is generated here with the same hand-rolled 8x8 24-bit BMP the
// rest of the suite uses. Nothing is committed to the repository and nothing
// depends on an external encoder, so the fixture is byte-deterministic.
namespace {
int failures = 0;
int checks = 0;
bool check(bool ok, const std::string& what) {
  ++checks;
  if (!ok) { ++failures; std::cerr << "FAIL: " << what << "\n"; }
  return ok;
}
std::string gFail;
void expect(bool ok, const char* what) {
  ++checks;
  if (!ok && gFail.empty()) gFail = what;
}

constexpr int kW = 8, kH = 8;

void writeBmp(const std::filesystem::path& p, int seed) {
  const int row = kW * 3, img = row * kH, fileSize = 54 + img;
  std::ofstream f(p, std::ios::binary);
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fileSize & 0xFF); hd[3] = (unsigned char)((fileSize >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40;
  hd[18] = (unsigned char)kW; hd[22] = (unsigned char)kH;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  f.write((const char*)hd, 54);
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      const unsigned char v = (unsigned char)(((x * 2 + y * 3 + seed) * 7) % 256);
      f.put((char)v); f.put((char)v); f.put((char)v);
    }
}

// Two groups of identical files plus individually varied files: the shape
// the D8 dataset uses, so this test exercises the same structure.
void buildFixture(const std::filesystem::path& root) {
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "images" / "exact", ec);
  std::filesystem::create_directories(root / "images" / "varied", ec);
  for (int g = 0; g < 3; ++g) {
    const int seed = 17 + g * 13;
    for (int m = 0; m < 4; ++m) {
      char name[64];
      std::snprintf(name, sizeof(name), "dup%02d_%02d.bmp", g, m);
      writeBmp(root / "images" / "exact" / name, seed);
    }
  }
  for (int i = 0; i < 5; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "v%02d.bmp", i);
    writeBmp(root / "images" / "varied" / name, 101 + i * 29);
  }
}
}  // namespace

int main() {
  namespace fs = std::filesystem;
  const auto base = fs::temp_directory_path() / "msf_dataset_fp_test";
  const std::string baseRoot = msf::path_to_utf8(base / "rootA");
  const std::string otherRoot = msf::path_to_utf8(base / "somewhere_else" / "rootB");
  buildFixture(base / "rootA");
  buildFixture(base / "somewhere_else" / "rootB");

  // --- A: determinism -------------------------------------------------
  const auto a1 = msf::computeDatasetFingerprint(baseRoot);
  const auto a2 = msf::computeDatasetFingerprint(baseRoot);
  expect(a1.state == "measured", "A: fixture fingerprint is measured");
  expect(a1.fingerprint.size() == 64, "A: fingerprint is 64 hex chars");
  expect(a1.fingerprint == a2.fingerprint, "A: same dataset -> same fingerprint");
  expect(a1.fileCount == 17, "A: fileCount counts every fixture file");
  expect(a1.totalBytes == static_cast<std::uint64_t>(17) * 246, "A: totalBytes sums file sizes");
  {
    bool hex = !a1.fingerprint.empty();
    for (char c : a1.fingerprint)
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) hex = false;
    expect(hex, "A: fingerprint is lowercase hex");
  }
  if (!gFail.empty()) { std::cerr << "A section: " << gFail << "\n"; return 1; }
  {
    // NIST FIPS 180-4 vectors. Without these the self-contained SHA-256
    // could be subtly wrong and every dataset fingerprint would drift
    // silently, which is exactly the failure this whole step prevents.
    const char* emptyWant = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";
    const char* abcWant = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const char* longWant = "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    const char* foxWant = "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592";
    const std::string empty = msf::sha256Hex(reinterpret_cast<const unsigned char*>(""), 0);
    const std::string abc = msf::sha256Hex(reinterpret_cast<const unsigned char*>("abc"), 3);
    const std::string longIn =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const std::string longMsg = msf::sha256Hex(
        reinterpret_cast<const unsigned char*>(longIn.data()), longIn.size());
    const std::string foxIn = "The quick brown fox jumps over the lazy dog";
    const std::string fox = msf::sha256Hex(
        reinterpret_cast<const unsigned char*>(foxIn.data()), foxIn.size());
    expect(empty == emptyWant, "A: SHA-256 vector (empty input)");
    expect(abc == abcWant, "A: SHA-256 vector (abc)");
    expect(longMsg == longWant, "A: SHA-256 vector (56 bytes)");
    expect(fox == foxWant, "A: SHA-256 vector (43 bytes)");
  }
  if (!gFail.empty()) { std::cerr << "A vectors: " << gFail << "\n"; return 2; }

  // --- C: root independence (checked before B so B's copy is untouched) --
  const auto c1 = msf::computeDatasetFingerprint(otherRoot);
  expect(c1.state == "measured", "C: second root fingerprint is measured");
  expect(c1.fingerprint == a1.fingerprint, "C: different absolute root -> same fingerprint");
  {
    // A root that does not exist must not fabricate an identity.
    const auto missing = msf::computeDatasetFingerprint(msf::path_to_utf8(base / "no_such_root"));
    expect(missing.state == "not_available", "E: missing root -> not_available");
    expect(missing.fingerprint.empty(), "E: missing root has no fingerprint string");
    expect(missing.fileCount == 0, "E: missing root has zero files");
  }
  // Empty directory is likewise unmeasured rather than the hash of nothing.
  {
    const std::string emptyRoot = msf::path_to_utf8(base / "empty");
    std::error_code ec;
    fs::create_directories(fs::path(msf::path_from_utf8(emptyRoot)), ec);
    const auto e1 = msf::computeDatasetFingerprint(emptyRoot);
    expect(e1.state == "not_available", "E: empty dataset -> not_available");
    expect(e1.fingerprint.empty(), "E: empty dataset has no fingerprint string");
  }
  if (!gFail.empty()) { std::cerr << "C/E section: " << gFail << "\n"; return 3; }

  // --- B: content sensitivity ----------------------------------------
  // A copy is altered, never the original, so later sections keep the
  // reference fingerprint valid.
  {
    const std::string copyRoot = msf::path_to_utf8(base / "rootC");
    std::error_code ec;
    fs::remove_all(fs::path(msf::path_from_utf8(copyRoot)), ec);
    fs::copy(base / "rootA", fs::path(msf::path_from_utf8(copyRoot)),
             fs::copy_options::recursive, ec);
    const auto before = msf::computeDatasetFingerprint(copyRoot);
    expect(before.fingerprint == a1.fingerprint, "B: copy starts identical");
    // One byte of one file changes; everything else is untouched.
    const fs::path victim = fs::path(msf::path_from_utf8(copyRoot)) / "images" / "varied" / "v00.bmp";
    {
      std::fstream f(victim, std::ios::binary | std::ios::in | std::ios::out);
      f.seekp(60, std::ios::beg);
      const char flipped = 0x5A;
      f.write(&flipped, 1);
    }
    const auto after = msf::computeDatasetFingerprint(copyRoot);
    expect(after.state == "measured", "B: altered dataset still measured");
    expect(after.fingerprint != a1.fingerprint, "B: content change -> fingerprint change");
    // A rename also changes identity: the manifest includes the relative path.
    {
      const std::string renamedRoot = msf::path_to_utf8(base / "rootD");
      fs::remove_all(fs::path(msf::path_from_utf8(renamedRoot)), ec);
      fs::create_directories(fs::path(msf::path_from_utf8(renamedRoot)) / "images", ec);
      fs::copy_file(victim,
                    fs::path(msf::path_from_utf8(renamedRoot)) / "images" / "moved.bmp", ec);
      const auto moved = msf::computeDatasetFingerprint(renamedRoot);
      expect(moved.state == "measured", "B: renamed single-file dataset measured");
      expect(moved.fileCount == 1, "B: renamed dataset has one file");
      expect(moved.fingerprint != after.fingerprint, "B: rename -> fingerprint change");
    }
  }
  if (!gFail.empty()) { std::cerr << "B section: " << gFail << "\n"; return 4; }

  // --- D: order independence -----------------------------------------
  // Same names, same bytes, but the directories are rebuilt in a different
  // creation order. A sorted manifest cannot tell the difference.
  {
    const std::string revRoot = msf::path_to_utf8(base / "rootE");
    std::error_code ec;
    fs::remove_all(fs::path(msf::path_from_utf8(revRoot)), ec);
    fs::create_directories(fs::path(msf::path_from_utf8(revRoot)) / "zz" / "last", ec);
    fs::create_directories(fs::path(msf::path_from_utf8(revRoot)) / "aa" / "first", ec);
    writeBmp(fs::path(msf::path_from_utf8(revRoot)) / "zz" / "last" / "b.bmp", 5);
    writeBmp(fs::path(msf::path_from_utf8(revRoot)) / "aa" / "first" / "a.bmp", 5);
    const auto d1 = msf::computeDatasetFingerprint(revRoot);
    // Re-create from scratch so the walk encounters the other file first.
    fs::remove_all(fs::path(msf::path_from_utf8(revRoot)), ec);
    fs::create_directories(fs::path(msf::path_from_utf8(revRoot)) / "aa" / "first", ec);
    fs::create_directories(fs::path(msf::path_from_utf8(revRoot)) / "zz" / "last", ec);
    writeBmp(fs::path(msf::path_from_utf8(revRoot)) / "aa" / "first" / "a.bmp", 5);
    writeBmp(fs::path(msf::path_from_utf8(revRoot)) / "zz" / "last" / "b.bmp", 5);
    const auto d2 = msf::computeDatasetFingerprint(revRoot);
    expect(d1.state == "measured" && d2.state == "measured", "D: order fixtures measured");
    expect(d1.fingerprint == d2.fingerprint, "D: creation order -> same fingerprint");
    expect(d1.fingerprint != a1.fingerprint, "D: different file set -> different fingerprint");
  }
  if (!gFail.empty()) { std::cerr << "D section: " << gFail << "\n"; return 5; }

  // --- F: benchmark JSON ---------------------------------------------
  {
    msf::BenchmarkRecorder rec;
    msf::BenchmarkConfig cfg;
    cfg.root = baseRoot; cfg.build = "0.9.4.21"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
    rec.start(cfg);
    rec.setDatasetFingerprint(a1);
    rec.finalize(true, 0, 0, 0, 0, 0, 0, 0.0, 0, 0);
    const std::string js = rec.toJson();
    // The dataset keys are additive, so assert against the recorder's own
    // constant rather than pinning this step's schema number.
    expect(js.find("\"schemaVersion\":" +
                   std::to_string(msf::BenchmarkRecorder::kBenchmarkSchemaVersion)) !=
               std::string::npos,
           "F: schemaVersion matches the recorder constant");
    expect(js.find("\"dataset\":{") != std::string::npos, "F: dataset object present");
    expect(js.find("\"state\":\"measured\"") != std::string::npos, "F: dataset state measured");
    expect(js.find("\"fingerprintVersion\":1") != std::string::npos, "F: fingerprintVersion 1");
    expect(js.find("\"fingerprint\":\"" + a1.fingerprint + "\"") != std::string::npos,
           "F: recorded fingerprint matches the computed one");
    expect(js.find("\"fileCount\":17") != std::string::npos, "F: fileCount in JSON");
    expect(js.find("\"totalBytes\":") != std::string::npos, "F: totalBytes in JSON");
    // root must remain untouched: it is a location, not an identity. It is
    // compared through escapeJson because the recorder escapes backslashes
    // on Windows paths.
    expect(js.find("\"root\":\"" + msf::BenchmarkRecorder::escapeJson(baseRoot) + "\"") !=
               std::string::npos,
           "F: root key preserved");
  }
  if (!gFail.empty()) { std::cerr << "F section: " << gFail << "\n"; return 6; }
  {
    // A recorder that was never told the dataset must say so, and must not
    // carry a value forward from an earlier run.
    msf::BenchmarkRecorder rec;
    msf::BenchmarkConfig cfg;
    cfg.root = baseRoot; cfg.build = "0.9.4.21"; cfg.engine = "1.5.0"; cfg.db = "1.0.3";
    rec.start(cfg);
    rec.setDatasetFingerprint(a1);
    rec.start(cfg);   // fresh run, no fingerprint attached
    const std::string js = rec.toJson();
    expect(js.find("\"fingerprint\":null") != std::string::npos, "F: unmeasured -> null fingerprint");
    expect(js.find("\"state\":\"not_available\"") != std::string::npos,
           "F: unmeasured -> not_available state");
    expect(js.find(a1.fingerprint) == std::string::npos, "F: stale fingerprint not carried over");
  }
  if (!gFail.empty()) { std::cerr << "F states: " << gFail << "\n"; return 7; }

  std::error_code ec;
  fs::remove_all(base, ec);
  if (failures) { std::cerr << "dataset_fingerprint=failed " << failures << " failure(s)\n"; return 1; }
  std::cout << "dataset_fingerprint=ok checks=" << checks
            << " fingerprint=" << a1.fingerprint
            << " files=" << a1.fileCount
            << " bytes=" << a1.totalBytes << "\n";
  return 0;
}
