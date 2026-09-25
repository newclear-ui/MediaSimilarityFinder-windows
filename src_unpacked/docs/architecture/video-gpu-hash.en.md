# Video GPU Hash Architecture

## Scope

Only the 32x32 sampled-frame pHash stage in `VideoFingerprintEngine::build()` uses CUDA batches. FFmpeg decoding and 96x96 crop fingerprints remain on the CPU path. The MSSIM stage of temporal verification has used a GPU row batch since 0.9.3.14 (see `video-ssim-gpu.en.md`); crop temporal scoring itself is CPU.

## Execution flow

1. FFmpeg decodes 32x32 frames using the existing sampling plan.
2. Frames that pass the low-variance filter are packed into normal and mirrored input batches.
3. The shared `GpuBackend::hashBatch()` calculates both pHash batches.
4. Unsupported CUDA, runtime failures, and failed partial batches fall back to the existing CPU `perceptual_hash_pair()` for those frames.
5. GPU-used videos, fallback videos, and GPU hash time are recorded in Benchmark JSON.

## Concurrency and semantic preservation

- Multiple video workers can share one backend, so `hashBatch()` calls are serialized with a mutex.
- GPU replaces only hash calculation; sample timestamps, frame filtering, mirror hashes, crop hashes, and temporal-verification rules remain unchanged.
- GPU failure does not discard a fingerprint; the CPU fallback completes it.

## Limits

- Small videos may see limited speedup because of host/device transfer and batch preparation overhead.
- Video decoding and crop/temporal verification are still CPU-bound; NVDEC is outside this design scope.
