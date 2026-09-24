#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct FileState { std::string path; std::uint64_t size=0; std::int64_t modified=0; std::string quickHash; std::uint64_t fingerprint=0; int kind=0; double duration=0; std::uint64_t mirrorFingerprint=0; std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0; std::uint64_t mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0; };
struct ChangeSet { std::vector<FileState> unchanged, added, modified, deleted; };
// Persisted duplicate pair. Paths use the same canonical UTF-8 form as FileState.
// Stored ordered (left <= right) so a pair has exactly one row.
struct StoredMatch { std::string left, right; double percent=0; };
class Database {
public: ~Database(); bool open(const std::string& path); void close(); bool initialize(); bool beginTransaction(); bool commitTransaction(); bool rollbackTransaction(); bool upsert(const FileState& state); bool remove(const std::string& path); bool containsUnchanged(const FileState& state) const; std::vector<FileState> all() const;
 bool saveMatches(const std::vector<StoredMatch>& matches); std::vector<StoredMatch> loadMatches() const;
 // Engine-version stamp: which match-verdict generation produced this DB.
 // Missing/unparseable row means 0 (pre-versioning). Bumped only by code
 // changes that can alter verdicts; pair revalidation consumes it.
 int engineVersion() const; bool setEngineVersion(int v);
 // Persistent thumbnail cache (display only): small JPEG previews keyed by
 // path, validated against size+mtime. Lets rescans show thumbs instantly
 // instead of re-decoding thousands of files through the per-tick budget.
 bool putThumb(const std::string& path,std::int64_t modified,std::uint64_t size,const std::vector<unsigned char>& jpeg);
 bool getThumb(const std::string& path,std::int64_t modified,std::uint64_t size,std::vector<unsigned char>& jpeg) const;
 bool pruneThumbs(); // drop rows whose file left the index
private:
    std::string path_;
    void* db_=nullptr;
    void* upsertStmt_=nullptr;
    void* removeStmt_=nullptr;
    void* containsStmt_=nullptr;
    bool exec(const char* sql) const;
    bool prepareStatements();
    void finalizeStatements();
};
}