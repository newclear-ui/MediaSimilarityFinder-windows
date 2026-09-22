# MediaSimilarityFinder 0.9.2.5 — Portable Index hardening

## Implemented
1. Application-owned central `Index` storage for portable mode.
2. Stable per-root index identity with canonical paths and collision verification.
3. Atomic metadata creation/update using temporary files and rename.
4. Metadata schema/application version fields.
5. GUI continues to resolve the portable root from `QApplication::applicationDirPath()`.
6. Portable packaging script creates `Index/` and `portable.json`.
7. Regression coverage verifies index reuse, separation, metadata, and that scanned folders remain untouched.

## Verification
- Linux Release core build: PASS
- CTest: 26/26 PASS
- CandidateIndex benchmark: 6000 items, indexed query 312.387 ms, linear query 868.814 ms, speedup 2.781x.
- Index manager regression: PASS

## Not verified here
- Windows Qt6 GUI build/runtime.
- NVIDIA CUDA compilation/runtime (`nvcc` and physical NVIDIA GPU are unavailable in this environment).
