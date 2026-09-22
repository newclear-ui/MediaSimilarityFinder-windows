# CandidateIndex Architecture and Design Decisions

## Purpose

CandidateIndex is not the final similarity scorer. It is the **first-stage candidate filter** that uses Hamming distance on 64-bit perceptual hashes to reduce the work passed to the final SimilarityEngine. Its primary requirement is zero candidate misses within the supported search range, followed by scalable search and memory behavior.

## Evolution

### Initial structure — linear search

The initial CandidateIndex scanned every stored hash and calculated Hamming distance. It was simple and exact, but query cost grew linearly with the number of files.

### BK-tree

The implementation was later changed to an exact Hamming-distance BK-tree. Metric-tree pruning reduced candidate traversal compared with a full linear scan.

The trade-off was a separately allocated heap node for each hash, with `unique_ptr` and a per-node `unordered_map` for children. This is acceptable at small scale but can become expensive at large scale because of pointer/heap fragmentation, allocator overhead, poorer cache locality, and tree traversal cost.

### 0.9.2.18 — transform-aware BK-tree

After mirror-aware matching was introduced, the separate mirror-only BK-trees were removed. Normal and mirrored fingerprints were handled by a shared CandidateIndex while preserving exact BK-tree candidate semantics.

### 0.9.2.26 — 9-part Multi-Index Hash

To improve large-index scaling, the BK-tree storage and lookup structure was replaced with a 9-part Multi-Index Hash.

The 64-bit hash is split into `8 + 7×8` bits across nine partitions. For the normal search range `D <= 8`, two hashes that differ in at most eight bits must share at least one complete partition. Therefore, querying all exact buckets for the query's nine partitions cannot miss a true candidate.

Bucket results are deduplicated and then verified using the full 64-bit Hamming distance. The bucket structure is therefore a **candidate narrowing mechanism**, while the full Hamming calculation is the **exact verification step**.

For `D > 8`, the pigeonhole guarantee no longer applies, so the implementation conservatively falls back to an exact full scan.

## Design decisions

1. **Preserve exactness** — CandidateIndex remains an exact filter; candidates are rechecked with full Hamming distance.
2. **Optimize the normal range** — The current project primarily benefits from exact acceleration for `D <= 8`.
3. **Simplify large-scale storage** — Remove per-node heap allocation and pointer-heavy tree structure in favor of an entry array plus bucket indexes.
4. **Do not alter upper layers** — Final similarity thresholds and mirror/crop/video-temporal semantics remain unchanged by the CandidateIndex replacement.
5. **Conservative fallback** — For `D > 8`, correctness is preferred over speed.

## 0.9.2.26 validation

- Core Release build: PASS
- CTest: 35/35 PASS
- 6k benchmark: indexed query about 64.45 ms
- 25k benchmark: indexed query about 1.09 s
- 50k benchmark: indexed query about 4.62 s
- 100k benchmark: indexed query about 19.61 s
- 100k max RSS: about 17.9 MB
- 8-bit partition-boundary regression: PASS
- `D > 8` exact fallback regression: PASS

Note: the benchmark's indexed/linear comparison includes the existing full-query workload rather than a strictly identical single-query operation count. The speedup figure should therefore be treated as a version-trend indicator rather than a universal algorithmic multiplier.

## Future optimization targets

- Reduce duplicate bucket lookup/reservation work
- Optimize candidate deduplication
- Test bucket-heavy/worst-case distributions
- Measure large-scale `candidatePairs()` generation cost
- Explicitly define thread-safety if concurrent query/add access becomes necessary


## 0.9.2.28 candidatePairs optimization

The 9-part Multi-Index Hash from 0.9.2.26 remains responsible for normal `query()` candidate filtering. In 0.9.2.28, the all-pairs path `candidatePairs()` was optimized separately: each entry scans its partition buckets directly and a reusable generation-stamp array removes duplicate candidates, eliminating per-query candidate-vector construction and sorting. `D=0` uses only partition 0 because equal hashes share it. `D>8` retains the exact fallback. Final Hamming verification and existing result ordering are preserved.


## 0.9.2.29 streaming candidate consumption

0.9.2.29 adds `CandidateIndex::forEachCandidatePair()` so ScanPipeline can consume pairs through a callback without materializing complete pair vectors. Multiple fingerprint variants may belong to one media index, so entries carry an internal index-group identifier and partner groups are deduplicated with generation stamps. If the full index already covers every possible pair for a media kind, crop indexes for that kind are skipped. The existing `candidatePairs()` API remains for compatibility and uses the same pair semantics.
