#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
#include <unordered_map>
#include <utility>
namespace msf {
struct Candidate { std::size_t index=0; unsigned distance=64; };

// Exact Hamming-distance BK-tree. Supports batch queries and lightweight
// statistics so large scans can measure candidate pruning without changing
// the exact result semantics.
class CandidateIndex {
public:
 void clear();
 void reserve(std::size_t expected);
 void add(std::size_t index,std::uint64_t hash);
 void addAll(const std::vector<std::uint64_t>& hashes, std::size_t indexBase=0);
 std::vector<Candidate> query(std::uint64_t hash,unsigned maxDistance=8) const;
 std::vector<std::vector<Candidate>> queryAll(const std::vector<std::uint64_t>& hashes,unsigned maxDistance=8) const;
 std::vector<std::pair<std::size_t,Candidate>> candidatePairs(unsigned maxDistance=8) const;
 std::size_t size() const;
 std::size_t nodeCount() const { return size_; }
private:
 struct Node { std::size_t index; std::uint64_t hash; std::unordered_map<unsigned,std::unique_ptr<Node>> children; };
 std::unique_ptr<Node> root_;
 std::size_t size_=0;
 static unsigned popcount64(std::uint64_t x);
 void queryNode(const Node* node,std::uint64_t hash,unsigned maxDistance,std::vector<Candidate>& out) const;
};
}
