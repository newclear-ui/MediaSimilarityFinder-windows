#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
#include <unordered_map>
#include <functional>
namespace msf {
struct Candidate { std::size_t index=0; unsigned distance=64; };

// Exact Hamming-distance candidate index. For the normal D<=8 search range it
// uses a 9-part multi-index hash: by the pigeonhole principle, any item within
// Hamming distance 8 must share at least one exact partition with the query.
// Candidates are then verified with the full 64-bit Hamming distance, so this
// is an exact replacement for the previous BK-tree candidate semantics.
class CandidateIndex {
public:
 struct Stats { std::size_t nodeCount=0; std::size_t edgeCount=0; std::size_t maxDepth=0; std::size_t bucketCount=0; std::size_t storedRefs=0; };
 void clear();
 void reserve(std::size_t expected);
 void add(std::size_t index,std::uint64_t hash);
 void addAll(const std::vector<std::uint64_t>& hashes, std::size_t indexBase=0);
 std::vector<Candidate> query(std::uint64_t hash,unsigned maxDistance=8) const;
 std::vector<std::vector<Candidate>> queryAll(const std::vector<std::uint64_t>& hashes,unsigned maxDistance=8) const;
 std::vector<std::pair<std::size_t,Candidate>> candidatePairs(unsigned maxDistance=8) const;
 void forEachCandidatePair(unsigned maxDistance, const std::function<void(std::size_t,const Candidate&)>& visitor) const;
 std::size_t size() const { return entries_.size(); }
 std::size_t nodeCount() const { return entries_.size(); }
 Stats stats() const;
private:
 static constexpr unsigned kPartitions=9;
 struct Entry { std::size_t index=0; std::uint64_t hash=0; std::size_t group=0; };
 struct BucketKey { std::uint8_t part=0; std::uint8_t bits=0; bool operator==(const BucketKey& o) const{return part==o.part&&bits==o.bits;} };
 struct BucketKeyHash { std::size_t operator()(BucketKey k) const{return (static_cast<std::size_t>(k.part)<<8)|k.bits;} };
 std::vector<Entry> entries_;
 std::unordered_map<BucketKey,std::vector<std::size_t>,BucketKeyHash> buckets_;
 std::unordered_map<std::size_t,std::size_t> indexGroups_;
 static unsigned popcount64(std::uint64_t x);
 static BucketKey key(std::uint64_t hash,unsigned part);
 static unsigned partitionBits(unsigned part);
 static unsigned partitionShift(unsigned part);
 void queryBKFallback(std::uint64_t hash,unsigned maxDistance,std::vector<Candidate>& out) const;
};
}
