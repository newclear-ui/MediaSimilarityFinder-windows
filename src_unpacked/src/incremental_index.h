#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace msf {
enum class ChangeKind { Unchanged, Added, Modified, Removed };
// NOTE: named IndexedFile (not FileState): database.h owns msf::FileState and
// a second definition in this namespace would violate ODR wherever both meet.
struct IndexedFile {
 std::string path;
 std::uint64_t size=0;
 std::uint64_t modified=0;
 std::uint64_t fingerprint=0;
 std::uint64_t mirrorFingerprint=0;
};
struct Change { ChangeKind kind; IndexedFile current; };
class IncrementalIndex {
 std::unordered_map<std::string,IndexedFile> old_;
public:
 void load(const std::vector<IndexedFile>& states);
 std::vector<Change> diff(const std::vector<IndexedFile>& current) const;
};
}