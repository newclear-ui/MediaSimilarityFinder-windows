#pragma once
#include <string>
#include <vector>
namespace msf {
// Minimal "major.minor.patch" comparison for internal versions (engine verdict
// generation, DB schema generation). Independent of the 0.9.2.x build numbers.
// Missing/malformed components count as 0, so "" and garbage sort below any
// real version. Pure header-only for direct unit testing.
inline std::vector<long> parseSemver(const std::string& v) {
  std::vector<long> out{0, 0, 0};
  std::size_t pos = 0;
  for (int i = 0; i < 3; ++i) {
    if (pos >= v.size()) break;
    std::size_t dot = v.find('.', pos);
    const std::string part = (dot == std::string::npos) ? v.substr(pos) : v.substr(pos, dot - pos);
    if (part.empty()) break;
    long n = 0;
    for (char c : part) {
      if (c < '0' || c > '9') { n = 0; for (int k = i; k < 3; ++k) out[k] = 0; return out; }
      n = n * 10 + (c - '0');
      if (n > 1000000) { n = 1000000; }
    }
    out[i] = n;
    if (dot == std::string::npos) break;
    pos = dot + 1;
  }
  return out;
}
// -1 if a<b, 0 if equal, 1 if a>b.
inline int compareSemver(const std::string& a, const std::string& b) {
  const auto va = parseSemver(a), vb = parseSemver(b);
  for (int i = 0; i < 3; ++i) {
    if (va[i] < vb[i]) return -1;
    if (va[i] > vb[i]) return 1;
  }
  return 0;
}
inline bool semverLess(const std::string& a, const std::string& b) { return compareSemver(a, b) < 0; }
}
