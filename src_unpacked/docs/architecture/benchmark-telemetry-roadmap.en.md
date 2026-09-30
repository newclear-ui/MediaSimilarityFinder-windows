# Benchmark and Runtime Telemetry Roadmap

## 1. Purpose

The 0.9.4.x CPU/GPU Adaptive Architecture cannot be validated by the current benchmark schema and a simple GPU-duty measurement alone.

Benchmarking is promoted into a diagnostic layer that records actual search performance, CPU/GPU/decoder throughput, the evidence behind Adaptive Scheduler decisions, and execution state including failures, fallbacks, and missing measurements.

Therefore Benchmark / Telemetry / Scheduler Diagnostics become one coherent measurement system in 0.9.4.x.

## 2. Current benchmark limitations

0.9.3.19 recorded wall/stage time, image/video counts, GPU hash count, GPU batch time, resource sampling, and video GPU/fallback counts. 0.9.4.0 (Node A) adds benchmark schemaVersion 1, runId, measurement states, stage objects, decoded/sampled frame separation, scheduler/calibration structures, and cancellation/partial/file-progress records while keeping all existing keys.

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

## 18. Benchmark gates within the Development Roadmap

Benchmark redesign is not tied to a fixed build number. Each roadmap node implements the observations needed to prove that node's exit criteria.

A Foundation
  └─ schema / measurement state / stage instrumentation
        ↓
B Adaptive Scheduler
  └─ scheduler decisions / work share / throttling
        ↓
C Calibration
  └─ calibration / profile confidence / baseline-vs-observed
        ↓
D Pipeline / Queue
  └─ queue depth / wait / batch / transfer / overlap
        ↓
E Adaptive Video Decode
  └─ decoded-vs-sampled / seek / planner decision
        ↓
F Hardware Decode
  └─ backend capability / success / fallback reason
        ↓
G Additional Backends
  └─ capability / parity / fallback / availability
        ↓
H Validation
  └─ end-to-end regression evidence

A benchmark redesign may span several build versions. What matters is whether the current roadmap node is measurable and verifiable, not which patch number happens to contain it.

## 19. Implementation principles



Benchmarking is not decoration.

- Never turn an unmeasured value into zero.
- Never judge GPU performance from utilization alone.
- Record benchmark overhead when relevant.
- Enabling instrumentation must not change search semantics.
- Instrumentation must not change search accuracy.
- Increment schemaVersion when the JSON schema changes.


## 20. Benchmark Execution Model — Run / Suite / Media Scope

Benchmark execution is separated into Run and Suite.

- Run: one measurement under one benchmark mode.
- Suite: a logical group of AUTO / CPU-only / GPU-max Runs using the same source dataset, media scope, and execution conditions.
- GUI can store the same suiteId in each retained JSON without requiring a separate suite.json.
- Console may store suite.json plus individual Run JSON files for long-term analysis.

### Benchmark modes

- AUTO: use the current Adaptive Scheduler.
- CPU-only: disable the GPU backend and measure the CPU baseline.
- GPU-max: send capable work to the GPU while retaining mandatory CPU work and CPU fallback.

GPU-max does not mean GPU-only or CPU 0%.

### Media scope

Console exposes the same image/video selection scope as the GUI.

~~~text
--media images
--media videos
--media all
~~~

- images: image only
- videos: video only
- all: image + video
- default: all
- normal scan and benchmark use the same semantics.

Mode and media scope are orthogonal. A Suite can therefore contain AUTO+images, CPU+images, and GPU-max+images, while another Suite can measure videos only.

Each Run JSON stores at minimum:

~~~text
suiteId
runId
mode
mediaScope
scanImages
scanVideos
sourceRoot
sourceRootLabel
sourceRootId
datasetFingerprint
~~~

## 21. GUI Benchmark Retention

GUI retains only the latest result for each source + benchmark mode.

~~~text
Benchmark/GUI/<source-label>_<root-id-short>/
    auto.json
    cpu.json
    gpu-max.json
~~~

- AUTO / CPU-only / GPU-max are selected by default.
- Existing GUI image/video selection applies to every selected benchmark mode.
- A new result for the same source + mode replaces the previous result.
- If media scope changes, the same mode file is replaced and mediaScope identifies the latest measured scope.
- Historical image-only and video-only comparisons should use Console benchmark storage.
- Existing manual Save JSON can remain as an export function.
- GUI never automatically loads Console benchmark logs.

## 22. Console Benchmark Retention

Console is the long-term comparison and data-mining path. Results are not automatically deleted.

~~~text
Benchmark/Console/suite-<suite-id>/
    suite.json
    auto.json
    cpu.json
    gpu-max.json
~~~

- Retention is cumulative by default.
- --log-dir overrides the output root.
- --log specifies an individual JSON path.
- GUI and Console storage and loading paths remain separate.
- Long-term image/video/all comparisons should use separate Suites.

## 23. Console CLI Design

Normal scan and benchmark use the same media scope.

~~~text
MediaSimilarityFinder.exe --scan <folder> --media all

MediaSimilarityFinder.exe --benchmark <folder> --mode auto --media all
MediaSimilarityFinder.exe --benchmark <folder> --mode cpu --media images
MediaSimilarityFinder.exe --benchmark <folder> --mode gpu-max --media videos
MediaSimilarityFinder.exe --benchmark <folder> --suite auto,cpu,gpu-max --media all
~~~

Benchmark storage options:

~~~text
--log-dir <dir>
--log <file>
~~~

JSON is the canonical machine-readable result. Standard output stays focused on progress and the final summary.

## 24. Benchmark Isolation and Fairness

Reusing the normal Search Index / Video Cache would bias CPU / AUTO / GPU-max comparisons.

Benchmark Runs therefore use dedicated index/cache state.

~~~text
normal search
    └─ Index/<root-id>/...

benchmark
    └─ Benchmark/<GUI|Console>/...
        └─ isolated index/cache state
~~~

- A benchmark must not modify the normal GUI search database.
- Index/cache state must not leak between benchmark Runs.
- Prefer one independent process per Run over CPU → GPU → AUTO in one process.
- OS filesystem cache is not fully controllable and should be recorded as uncontrolled.
- Fresh benchmark index/cache is not the same as a cold OS filesystem cache.
- Runs in one Suite must align on dataset fingerprint, sourceRoot, mediaScope, and relevant execution conditions.

## 25. Benchmark Environment / Schedule Capture

Each Run stores enough execution context to reconstruct how it was produced.

### Environment
- Windows/OS build
- appVersion / build configuration / gitCommit
- CPU model / logical threads / RAM
- GPU model / VRAM / driver
- CUDA/runtime and FFmpeg information
- selected/available backend

### Execution configuration
- benchmark mode
- mediaScope
- distance
- image/video enable state
- CPU Resource Mode
- normalized CPU percentage 10–90
- worker count
- GPU ON/OFF policy
- GPU batch
- scheduler initial estimate / live adjustments
- decoder policy
- benchmark index/cache state
- process isolation state

### Result identity
- suiteId / runId / runIndex
- sourceRoot / sourceRootLabel / rootIdShort
- datasetFingerprint / fileCount / byteCount
- startedAt / completedAt
- completion status

The development **build schedule** is also stored in the documents, but version numbers are not pre-assigned. A new 0.9.4.x version is assigned only when a validated code state exists.

## 26. GUI / Console Capability Mapping

| item | GUI | Console |
| --- | --- | --- |
| normal scan | supported | supported |
| image only | existing selection | --media images |
| video only | existing selection | --media videos |
| all | existing selection | --media all |
| AUTO | supported | supported |
| CPU-only | supported | supported |
| GPU-max | supported | supported |
| latest three only | yes | no automatic pruning |
| long-term accumulation | not default | default |
| custom log dir | export-oriented | supported |

## 27. QuickLook Help

--help should mention Windows Store QuickLook as an optional convenience tool.

Verified Microsoft Store address:
https://www.microsoft.com/store/apps/9nv4bs3l1h4s

QuickLook is not a required MediaSimilarityFinder dependency.

## 28. Benchmark Implementation / Build Schedule

Do not pre-assign version numbers.

- S0 Design/pre-register: finalize Run/Suite, mode, media scope, storage/isolation/JSON/CLI contracts
- S1 Console entry foundation: --help, --version, headless scan, --media integration
- S2 Run/Suite benchmark core: connect BenchmarkConfig/Recorder/JSON/environment
- S3 Storage isolation: GUI/Console roots, dedicated benchmark index/cache, atomic/crash-safe persistence
- S4 GUI integration: three mode checkboxes, all selected by default, existing media selection combined, latest-three retention
- S5 Console benchmark execution: --benchmark / --mode / --suite / --media / --log-dir / --log
- S6 Data-mining automation: automated Suites, dataset fingerprint checks, comparison summary
- S7 Help/usability: command examples, media-scope examples, QuickLook guidance, exit codes
- S8 Full verification: CPU build → GPU build → CTest → CLI execution → GUI verification → JSON inspection → documentation → Build History when applicable → commit

Once actual benchmark performance experiments begin, apply the existing pre-register-first rule and record successful, failed, and rejected outcomes in Build History and the Performance / Tuning Experiment Index.

## 29. Final Console Benchmark Execution and Terminal UI Contract — 2026-09-30

This section is the **final design decision** that supplements the earlier S0-S8 outline. Implementations must follow this contract when the earlier outline is less specific.

### 29.1 File-level execution order

Do not run an entire dataset in one mode before moving to the next mode. Repeat the following per file:

~~~text
Prepare/identify file
  -> AUTO
  -> CPU-only
  -> GPU-max
  -> append this file's results to the journal immediately
  -> next file
~~~

- File identification/input preparation may be shared.
- Actual analysis, decode, and intermediate analytical results must not be shared between modes; otherwise CPU/AUTO/GPU comparison conditions become contaminated.
- Treat each mode as an independent analysis context.
- Persist the three mode results immediately after the file completes.

### 29.2 Cancellation and partial-result persistence

- Interactive Console uses Ctrl+C as the cancellation request.
- Finish the current atomic operation safely, then terminate.
- Because completed file/mode results already exist in the journal, an interrupted process must leave a usable partial Suite.
- Finalization records cancelled, completionReason, filesCompleted, filesRemaining, and runsCompleted.
- The terminal is a view, not the source of benchmark truth; the journal/summary is canonical.

### 29.3 CPU Resource Policy

The Console benchmark reuses the existing Resource Policy.

~~~text
--resource maximum|high|balanced|gaming|manual
--cpu-percent 10..90
~~~

- Recommended default benchmark resource: **Balanced (55%)**.
- Users may explicitly select Maximum/High/etc.
- Do not treat a simple linear extrapolation from a Balanced measurement as an actual Maximum benchmark result.
- Maximum should be measured when required. A projection may be displayed separately, but it must not be mixed with measured benchmark results in the first implementation.
- CPU-only and GPU-max comparison conditions must not be silently changed by adaptive throttling during the run.

### 29.4 Fixed console header

The interactive terminal uses **three information rows plus separators**. These rows must never auto-wrap.

~~~text
MediaSimilarityFinder Benchmark
================================================================================================================
Target : D:\\Media\\TestSet                  | Scope : ALL       | Files : IMG 12/640  VID 3/207
Mode   : AUTO → CPU → GPU-MAX              | CPU : Balanced 55% | GPU : ON / CUDA
Distance : 8                               | Suite ID : 20260930-0801-01 | Build : 0.9.4.43 | Git : 22c3ac9
================================================================================================================
~~~

At minimum, retain:

- Target
- media scope
- image/video completion counts
- benchmark mode order
- CPU Resource
- GPU state/backend
- Distance
- Suite ID
- Build and Git identifiers when width permits

### 29.5 No-wrap and width adaptation

- The fixed header must never auto-wrap.
- When terminal width is insufficient, compress lower-priority text first.
- Long target paths use **middle ellipsis** so both the beginning and end remain identifiable.
- Screen strings may be compressed, e.g. Balanced (55%) -> Balanced 55%, ON / CUDA -> CUDA.
- Full, unshortened values remain in JSON/journal.
- Wide/normal/compact presentation modes may be used, but the fixed header must not gain extra rows.

### 29.6 CURRENT FILE detail area

The current file's global position and name remain in the lower detailed region.

~~~text
CURRENT FILE
----------------------------------------------------------------------------------------------------------------
[16 / 847] sample_0012.jpg
Type : Image | Size : 4.82 MB | IMG : 12/640

AUTO                  CPU                   GPU-MAX
------------------    ------------------    ------------------
DONE                  DONE                  RUNNING
12.41 ms              18.08 ms              7.32 ms
Scheduler             Software              CUDA
                      Workers : 10          CPU FB : NO
----------------------------------------------------------------------------------------------------------------
~~~

For videos, prefer media-specific details such as codec, resolution, fps, duration, decoder/backend, and CPU fallback reason.

### 29.7 Completed history and final state

Completed history is intentionally compact, one line per file:

~~~text
0011 image_0011.jpg         AUTO 10.8ms | CPU 14.7ms | GPU  8.2ms
0012 image_0012.jpg         AUTO 12.4ms | CPU 18.1ms | GPU  9.6ms
0013 sample_0013.mp4        AUTO 842ms  | CPU 711ms  | GPU 438ms
~~~

On cancellation, show CANCELLATION REQUESTED, partial-save completion, and completed/remaining file counts. On normal completion, show BENCHMARK COMPLETE and cumulative AUTO/CPU/GPU-max summaries.

### 29.8 Interactive / non-interactive split

- TTY/Interactive: fixed header + CURRENT FILE + accumulated history + final/partial summary
- Redirect/CI/non-interactive: line-oriented output that does not rely on ANSI screen rewriting
- Both paths write the same journal/JSON benchmark data.

### 29.9 Legacy Benchmark Preservation

The existing benchmark source and schema are **permanent legacy baselines**.

- Do not overwrite the historical benchmark implementation and lose its traceability when introducing the new architecture.
- Keep the v0.9.4.43 Git tag/source backup and existing documentation records as the legacy reference point.
- Future benchmark architecture changes must retain traceability among legacy source, tag, backup, and documentation snapshot.
- This decision is documentation-only; it does not delete or replace the existing product benchmark implementation.

## 30. Final S0-S8 responsibilities

- **S0**: finalize Run/Suite, mode, media scope, Resource Budget, journal, isolation, cancellation, terminal contract
- **S1**: Console entry, help/version, headless scan, media/resource option wiring
- **S2**: file-level AUTO->CPU->GPU-max core, independent mode contexts, per-file result event/journal contract
- **S3**: benchmark storage isolation, append-only journal, crash-safe/partial persistence, summary creation
- **S4**: GUI benchmark integration and latest-three retention
- **S5**: Console benchmark CLI execution and interactive/non-interactive terminal renderer
- **S6**: Suite automation, fingerprint validation, comparison/data-mining
- **S7**: help/usability/exit codes/verbose output
- **S8**: CPU/GPU builds, tests, CLI/GUI verification, journal/JSON verification, documentation and release gate

The existing **pre-register-first** rule remains mandatory as soon as actual performance experiments begin.
## 31. Console Benchmark Visual Mockups — 2026-09-30

Text examples alone are not sufficient to review the final Console UI density and fixed-header no-wrap behavior, so the finalized design is also preserved as visual mockups.

Project mockup files:

~~~text
project/uimock/benchmark-console-mockup.html
JPG preview: benchmark-console-mockup.jpg (generated review artifact)
~~~

The mockups reflect the final contract:

- three fixed information rows at the top
- Target / Scope / IMG+VID progress on one row
- Mode / CPU Resource / GPU on one row
- Distance / Suite ID / Build / Git on one row
- no automatic wrapping in the fixed header
- middle ellipsis for long Target paths on screen
- full original values preserved in JSON/journal
- CURRENT FILE moved to the detailed lower area
- AUTO / CPU / GPU-max shown as three horizontal columns
- completed files accumulated as compact one-line history

These mockups are **static references for pre-implementation UI review**, not actual benchmark output. The real implementation may vary displayed values according to terminal width, data, and media type.


