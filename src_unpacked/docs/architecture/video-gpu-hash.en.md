# Video GPU Hash Architecture

## Scope

Only the 32x32 sampled-frame pHash stage in `VideoFingerprintEngine::build()` uses CUDA batches. FFmpeg decoding and 96x96 crop fingerprints remain on the CPU path. The MSSIM stage of temporal verification has used a GPU row batch since 0.9.3.14 (see `video-ssim-gpu.en.md`); crop temporal scoring itself is CPU. Content-level duration cohorts, sampling policy, and temporal matching semantics are defined separately in `video-similarity-search.en.md`; this document covers only the GPU hash execution path.

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

## Hardware decode path (D3D11VA → CUDA, for the post-1.0 HW phase)

- D3D11VA works with the current vcpkg FFmpeg today (measured: H.264 → `pix_fmt: d3d11`, 90 frames, 0 errors, 26.3x, frames kept in device memory). No h264_d3d11va wrapper exists, but the fixed-function decoder engages through the SW decoder's get_format hwaccel path. Vendor-neutral (Intel/AMD/NVIDIA).
- The D3D11 → CUDA zero-copy path is open with no extra SDK (`cuGraphicsD3D11RegisterResource`, present in the installed toolkit's `cuda.h`). D3D11VA decode → D3D11 texture → CUDA registration → straight into the existing pHash/SSIM kernels.
- NPP is the on-device preprocessing vehicle (ships with the toolkit: `nppiResizeSqrPixel_8u_C1R`, `nppiNV12ToRGB_*`, `nppiFilterBoxBorder_8u_C1R`). CV-CUDA is excluded for lack of native Windows support.
- NV12's Y plane is itself the full-res gray image, so a device pipeline goes Y plane → NPP resize (96→32) → existing kernels with no color conversion.
- External patterns: mpv's `--hwdec=auto` negotiation ladder (native → copy-back → SW fallback) + on-GPU frames fed to the renderer; Chromium driver blocklists + SW fallback + diagnostics. Isomorphic to our scheduler+fallback/capability gating.
- Preconditions: HW-decoded pixels may differ byte-wise from SW, so adoption needs the fingerprint parity gate (video_decode_parity_test pattern) + F-1 exactness re-proof. Proceeds in the post-1.0 HW phase after Vulkan (roadmap Nodes G/F).

## Limits

- Small videos may see limited speedup because of host/device transfer and batch preparation overhead.
- Video decoding and crop/temporal verification are still CPU-bound; NVDEC is outside this design scope.
