#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct FileState { std::string path; std::uint64_t size=0; std::int64_t modified=0; std::string quickHash; std::uint64_t fingerprint=0; int kind=0; double duration=0; std::uint64_t mirrorFingerprint=0; std::uint64_t crop4x3=0,crop1x1=0,crop9x16=0; std::uint64_t mirrorCrop4x3=0,mirrorCrop1x1=0,mirrorCrop9x16=0;
  // Analysis-outcome state. fingerprint!=0 is a completed analysis. With
  // fingerprint==0 the row is a skeleton, and this flag separates the two
  // skeleton meanings that used to be indistinguishable:
  //   false -> analysis was never completed for this content (interrupted by
  //            cancel/crash, or not yet reached). Must be retried.
  //   true  -> analysis was attempted against this exact size/modified/quickHash
  //            and produced no fingerprint. Deterministic for unchanged content,
  //            so it is not retried until the content identity changes.
  bool analysisFailed=false; };
struct ChangeSet { std::vector<FileState> unchanged, added, modified, deleted; };
// Persisted duplicate pair. Paths use the same canonical UTF-8 form as FileState.
// Stored ordered (left <= right) so a pair has exactly one row.
struct StoredMatch { std::string left, right; double percent=0; };
class Database {
public: ~Database(); bool open(const std::string& path); void close(); bool initialize(); bool beginTransaction(); bool commitTransaction(); bool rollbackTransaction(); bool upsert(const FileState& state); bool remove(const std::string& path); bool containsUnchanged(const FileState& state) const; std::vector<FileState> all() const;
 bool saveMatches(const std::vector<StoredMatch>& matches); std::vector<StoredMatch> loadMatches() const;
 // Internal versions, independent of the 0.9.2.x build numbers ("M.m.p"):
 // engine verdict generation (match logic) and DB schema generation.
 // Missing/malformed rows read as "0.0.0" (pre-versioning).
   // 1.0.4 adds files.analysis_failed (additive column, old code still reads it).
  static constexpr const char* kDatabaseVersion = "1.0.4";
  std::string engineVersion() const; bool setEngineVersion(const std::string& v);
  std::string dbVersion() const; bool setDbVersion(const std::string& v);
  std::string samplingGeneration() const; bool setSamplingGeneration(const std::string& v);
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
