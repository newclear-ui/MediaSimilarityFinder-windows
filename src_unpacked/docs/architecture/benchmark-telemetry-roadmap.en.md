# Benchmark and Runtime Telemetry Roadmap

## 1. Purpose

The 0.9.4.x CPU/GPU Adaptive Architecture cannot be validated by the current benchmark schema and a simple GPU-duty measurement alone.

Benchmarking is promoted into a diagnostic layer that records actual search performance, CPU/GPU/decoder throughput, the evidence behind Adaptive Scheduler decisions, and execution state including failures, fallbacks, and missing measurements.

Therefore Benchmark / Telemetry / Scheduler Diagnostics become one coherent measurement system in 0.9.4.x.

## 2. Current benchmark limitations

0.9.3.19 currently records wall/stage time, image/video counts, GPU hash count, GPU batch time, resource sampling, and video GPU/fallback counts.

When detailed benchmarking is disabled, some video and resource measurements may not run. Therefore a numeric 0 does not necessarily mean zero work or zero utilization.

0.9.4.x removes this ambiguity.

An unmeasured value must never be encoded as numeric zero.

## 3. Measurement states

Important measurements must distinguish:

- measured
- not_measured
- not_available
- partial
- failed
- fallback

## 4. Benchmark schema version

Benchmark JSON receives an independent schemaVersion.

Recommended metadata:

- schemaVersion
- benchmarkRunId
- completed
- completionReason
- appVersion
- engineVersion
- databaseVersion

## 5. Search-stage instrumentation

Global stages:
- file enumeration / walk
- incremental classification
- image analysis
- video analysis
- candidate index build
- similarity / verification
- persistence
- revalidation
- total wall time

Image stages:
- decode
- normalization
- resize/conversion
- CPU fingerprint
- GPU fingerprint
- crop fingerprint
- GPU batch queue wait
- GPU submit/kernel time
- GPU transfer time
- fallback

Video stages:
- container open
- metadata
- cache lookup
- cache load
- decoder initialization
- seek/setup
- decode
- sampled frames
- decoded frames
- kept frames
- frame conversion
- resize
- variance filter
- scene detection
- fingerprint
- crop fingerprint
- cache save
- queue wait

sampledFrames and decodedFrames must be separate.

## 6. CPU/GPU scheduler telemetry

When Adaptive Scheduler is active, record:

- initial CPU capacity estimate
- initial GPU capacity estimate
- current effective CPU capacity
- current effective GPU capacity
- CPU work share
- GPU work share
- CPU queue depth
- GPU queue depth
- CPU queue wait
- GPU queue wait
- scheduler adjustment count
- scheduler increase/decrease events
- throttling events
- external-load throttling events
- hysteresis state
- selected backend
- backend fallback count

Do not record only GPU 70%. The log should make the decision basis inspectable.

## 7. Hardware capability record

At scan start, record where available:

CPU:
- vendor
- model
- logical threads
- relevant SIMD capability
- baseline profile id

GPU:
- vendor
- model
- integrated/discrete
- VRAM
- API/backend capability
- driver
- selected backend
- candidate backends
- capability failures

Video decode:
- codec
- profile
- pixel format
- bit depth
- resolution
- attempted backend
- selected backend
- fallback reason

## 8. Calibration benchmark

Do not force a long standalone benchmark. Combine lightweight calibration with early real scan work.

Measure:
- CPU fingerprint throughput
- GPU fingerprint throughput
- CPU/GPU transfer cost
- resize/conversion throughput
- CPU decode throughput
- hardware decode throughput
- queue latency

Recommended events:
- calibration.started
- calibration.completed
- calibration.durationMs
- calibration.confidence

## 9. INI profile and benchmark

INI stores relatively stable baselines. Benchmark stores current-run observations.

Trace:

INI baseline → initial scheduler estimate → live measurements → scheduler adjustments → final measured profile

If the profile is updated, record old profile id, new profile id, reason, and confidence change.

## 10. Runtime resource sampling

The existing 250 ms sampling concept may be retained but expanded to record, where available:

- CPU process/system load
- memory
- GPU utilization
- GPU memory
- GPU active/idle
- video decode activity
- disk read/write
- queue depth
- scheduler state

Unavailable metrics become not_available.

## 11. Human-readable vs machine-readable output

Keep the user-facing summary compact.

At minimum show:
- total scan time
- analyzed files
- image/video time
- average throughput
- CPU mean/max
- GPU active time
- selected GPU backend
- GPU fallback count
- video decode backend
- slowest file
- dominant bottleneck stage
- calibration status
- scheduler throttling count
- completed/cancelled/failed state

Detailed diagnostics remain in JSON.

## 12. Benchmark UI

Evolve the simple GPU-duty display toward concepts such as:

GPU: AUTO · CUDA · Active 42% · Fallback 3
CPU: Balanced · 8 workers · Adaptive
Decoder: NVDEC H.264 / Software HEVC

Expose enough information for diagnosis without forcing internal implementation details onto normal users.

## 13. Cancellation and partial execution

A single completed boolean is insufficient.

Recommended information:
- completed
- cancelled
- paused
- failed
- failed_stage
- completion_reason
- files_started
- files_completed
- files_remaining

Partial benchmark results must be explicitly marked partial.

## 14. Slow-file diagnostics

Keep the existing top-slow-files feature and add where possible:

- path
- media type
- size
- duration
- resolution
- codec
- decoder backend
- backend fallback
- total analysis time
- decode time
- conversion/resize time
- fingerprint time
- queue wait
- scheduler state
- error/fallback reason

This is especially useful for unusual encodings or codec/profile combinations.

## 15. Backend fallback diagnostics

Do not only record gpuFallback=1.

Use reason codes such as:
- GPU_BACKEND_UNAVAILABLE
- CODEC_UNSUPPORTED
- PROFILE_UNSUPPORTED
- PIXEL_FORMAT_UNSUPPORTED
- BIT_DEPTH_UNSUPPORTED
- INITIALIZATION_FAILED
- SEEK_FAILED
- FRAME_MAP_FAILED
- DECODE_ERROR
- TRANSFER_ERROR
- RUNTIME_ERROR
- OUT_OF_MEMORY
- PERFORMANCE_NOT_BENEFICIAL
- EXTERNAL_LOAD_THROTTLE

## 16. Benchmark as architecture validation

The 0.9.4.x benchmark must answer:

A. Is video slow because of decoding?
→ decodedFrames / sampledFrames / decodeMs

B. Is the GPU actually doing useful work?
→ selected backend / GPU work / kernel time / transfer time

C. Did GPU acceleration improve end-to-end speed?
→ CPU-only baseline vs GPU-assisted throughput

D. Is a low-end GPU faster than CPU?
→ workload-specific CPU/GPU throughput

E. Did external load cause correct throttling?
→ throttling events / resource samples

F. Did hardware-decode fallback preserve correctness?
→ fallback reason + CPU/reference parity

G. Was the saved INI profile useful on the next run?
→ initial estimate vs measured throughput

## 17. Regression benchmark

Maintain scenarios for:
- CPU-only
- GPU OFF
- GPU ON / AUTO
- GPU available but acceleration not beneficial
- low-end GPU simulation
- high-end GPU
- external CPU load
- external GPU load
- hardware decode success
- hardware decode fallback
- mixed image/video workload
- cancelled scan
- partial scan

Prioritize end-to-end throughput, accuracy parity, fallback correctness, and system stability over a single GPU-utilization number.

## 18. Required benchmark changes for 0.9.4.0

0.9.4.0 begins the benchmark redesign together with the scheduler redesign.

Required:
1. benchmark schemaVersion
2. measurement states
3. scheduler decision telemetry
4. calibration results
5. backend capability / selected backend
6. decoder/backend/fallback reason
7. detailed video decode stages
8. queue/transfer telemetry
9. explicit cancellation/partial result state
10. compact human summary plus detailed JSON

## 19. Implementation principles

Benchmarking is not decoration.

- Never turn an unmeasured value into zero.
- Never judge GPU performance from utilization alone.
- Record benchmark overhead when relevant.
- Enabling instrumentation must not change search semantics.
- Instrumentation must not change search accuracy.
- Increment schemaVersion when the JSON schema changes.
