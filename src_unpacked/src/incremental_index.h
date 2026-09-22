#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace msf {
enum class ChangeKind { Unchanged, Added, Modified, Removed };
struct FileState {
 std::string path;
 std::uint64_t size=0;
 std::uint64_t modified=0;
 std::uint64_t fingerprint=0;
 std::uint64_t mirrorFingerprint=0;
};
struct Change { ChangeKind kind; FileState current; };
class IncrementalIndex {
 std::unordered_map<std::string,FileState> old_;
public:
 void load(const std::vector<FileState>& states);
 std::vector<Change> diff(const std::vector<FileState>& current) const;
};
}