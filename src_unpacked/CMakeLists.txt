cmake_minimum_required(VERSION 3.20)
project(MediaSimilarityFinder VERSION 0.9.4.88 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# S1: generate the build version header so the reported version cannot drift from
# the build. main.cpp used to hardcode its own string and had fallen behind the
# real version, which made --version and the window title disagree with the build.
#
# S5 decision D: add MSF_BUILD_GIT alongside it, additively. A git problem must
# NEVER fail configure or the build, so the lookup is guarded at every step and
# falls back to the literal "unknown". An optional cache entry lets a build
# override the value without touching the repository (e.g. a source archive that
# has no .git at all).
set(MSF_BUILD_GIT "unknown" CACHE STRING "Short commit id shown in the benchmark header")
if(NOT MSF_BUILD_GIT STREQUAL "unknown")
    # An explicit override was supplied; do not probe git.
else()
    find_package(Git QUIET)
    if(GIT_FOUND)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE MSF_GIT_OUT
            ERROR_QUIET
            RESULT_VARIABLE MSF_GIT_RC
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(MSF_GIT_RC EQUAL 0 AND MSF_GIT_OUT)
            set(MSF_BUILD_GIT "${MSF_GIT_OUT}")
        endif()
    endif()
endif()
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_version.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/msf_build_version.h"
  @ONLY)
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
# Crash-response runbook: Release builds must keep a Program Database so a crash
# dump can be symbolized. Without PDBs a dump only yields module+offset and the
# fault function cannot be resolved (the runbook requires a fault stack). /Zi
# and the linker /DEBUG add debug info only; codegen is unchanged.
if(MSVC)
  # CXX only: passing /Zi to nvcc breaks the CUDA compile (MSB3721). The fault
  # path is host C++ code, so host-object PDBs are what the runbook needs.
  add_compile_options("$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:$<$<CONFIG:Release>:/Zi>>")
  add_link_options("$<$<CONFIG:Release>:/DEBUG>")
endif()
# CUDA host compiler needs /utf-8 too. nvcc does not accept /utf-8 directly; the
# flag must be forwarded to the MSVC host compiler via -Xcompiler. Without this
# the CUDA headers trip MSVC warning C4819 (code page 949) under CMAKE_CUDA_FLAGS.
add_compile_options($<$<AND:$<COMPILE_LANGUAGE:CUDA>,$<CXX_COMPILER_ID:MSVC>>:-Xcompiler=/utf-8>)
option(MSF_ENABLE_CUDA "Enable NVIDIA CUDA when available (deprecated alias of MSF_ENABLE_GPU)" ON)
# Node A: canonical internal concept is GPU. MSF_ENABLE_CUDA stays as a
# deprecated compatibility alias; MSF_GPU_BACKEND selects the concrete
# backend (AUTO = CUDA when present, else CPU fallback; CUDA = pinned).
option(MSF_ENABLE_GPU "Enable GPU acceleration when a backend is available" ${MSF_ENABLE_CUDA})
set(MSF_GPU_BACKEND "AUTO" CACHE STRING "Concrete GPU backend: AUTO, CUDA, CPU")
if(MSF_GPU_BACKEND STREQUAL "CPU")
  # Pinned CPU fallback: concrete backend selection without touching sources.
  set(MSF_ENABLE_GPU OFF)
endif()
set(MSF_CUDA_ARCHITECTURES "75;86;89" CACHE STRING "CUDA SM architectures to compile")
option(MSF_BUILD_TESTS "Build tests" ON)

add_library(msf_core STATIC
# P1 (0.9.4.67): msf_core IS the BackendCore library from the process
# isolation brief (engine, database, monitor, scanner, decoders). It carries
# no Qt linkage and no src header includes Qt, so a Qt6::Core-only Backend
# executable can link it without dragging GUI dependencies. Keep it that
# way: Backend must never gain Qt6::Widgets/Gui (verified at acceptance).
    src/fingerprint.cpp
    src/crop_fingerprint.cpp
    src/similarity.cpp
    src/sampling.cpp
    src/database.cpp
    src/index_manager.cpp
    src/index_janitor.cpp
    src/incremental_scanner.cpp
    src/scanner.cpp
    src/image_decoder.cpp
    src/image_verify.cpp
    src/media_pipeline.cpp
    src/video_decoder.cpp
    src/video_fingerprint.cpp
    src/resource_policy.cpp
    src/gpu_backend.cpp
    src/dataset_fingerprint.cpp
    src/similarity_engine.cpp
    src/candidate_index.cpp
    src/scan_pipeline.cpp
    src/incremental_index.cpp
    src/media_analyzer.cpp
    src/video_sampling.cpp
  # E-3B: the adaptive sampling planner. Classification only — no decode, no
  # FFmpeg calls, no I/O — so the correctness gates are provably independent of
  # whatever the decoder does afterwards.
  src/video_sampling_planner.cpp
  # S1: command line parsing for the console/headless entry points. Pure and
  # dependency free so the rules can be unit tested without launching the app.
  src/command_line.cpp
  # S2: run/suite benchmark core. Owns ordering, status aggregation, cancellation
  # and result preservation; the production adapter reuses the ordinary scan path
  # through an injected BenchmarkExecutor, so no second search engine exists.
  src/benchmark_core.cpp
  # S3: durable storage layer. benchmark_store owns the path policy and the
  # OS-level suite lock; benchmark_journal owns append-only JSONL evidence,
  # recovery and replay. Neither writes anything inside a scanned source folder.
  src/benchmark_store.cpp
  src/benchmark_journal.cpp
  # S3 wiring: binds the S2 runner's hooks to the suite's lock, journal and
  # summary. Decides nothing about execution order or aggregation.
  src/benchmark_session.cpp
  # S5: Console benchmark renderer. Display only: it formats presentation data
  # that S2/S3 already produced and executes nothing, so it carries no Qt and no
  # storage responsibility. Kept in msf_core so the renderer and the layers that
  # will feed it cannot drift apart at link time.
  src/benchmark_console_renderer.cpp
  # S5: Console benchmark CLI orchestration. Builds a BenchmarkRequest, opens an
  # S3 BenchmarkSession, makes the one BenchmarkRunner call and feeds the renderer.
  # It reuses S2/S3 unchanged and holds no scan, search or aggregation logic.
  src/console_benchmark_cli.cpp
  # S6: journal discovery, ingestion and normalization. Library level only: it
  # hands every journal to S3's own replayJournal() rather than parsing anything
# itself, and creates no CLI, report format or benchmark status.
    src/benchmark_data_mining.cpp
    # S6-3 grouping. Sits on the S6-2 normalized dataset and produces deterministic
    # cohorts as separate typed views; it re-reads no journal, recomputes no
    # normalized field, and computes no statistic.
    src/benchmark_data_grouping.cpp
    # S6-4 aggregation. Statistics over the S6-3 cohorts, with run/case/mode kept as
    # separate populations and every excluded sample carrying a reason.
    src/benchmark_data_aggregation.cpp
    # S6-5 comparison eligibility. Decides which two populations may be compared and
    # records every refusal with its reason. Computes no regression, speedup,
    # threshold, anomaly or winner.
    src/benchmark_data_comparison.cpp
    # S6-6 comparison metrics. Numeric deltas over the S6-5 candidates, consuming the
    # S6-4 aggregates. A computed number is not a verdict: no threshold or direction.
    src/benchmark_data_comparison_metrics.cpp
    src/media_search_engine.cpp
    src/monitor.cpp
    src/scan_worker.cpp
    src/backend_session.cpp
    src/backend_ipc.cpp
    src/backend_log.cpp
    src/backend_thumb.cpp
    src/thumbnail_store.cpp
    src/backend_sysinfo.cpp
    src/file_meta.cpp
    src/benchmark.cpp
    src/scheduler.cpp
    src/profile.cpp
    src/calibration.cpp
    src/walker_queue.cpp
)
target_include_directories(msf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# P3a: ScanWorker/backend_log moved into BackendCore and use QtCore
# (QObject/signals/QString/QDateTime — never Widgets/Gui). Qt6::Core comes
# from the same project-local vcpkg tree as everything else.
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
    target_link_libraries(msf_core PUBLIC Qt6::Core)
    # ScanWorker carries Q_OBJECT signals: moc its header like the GUI does.
    set_target_properties(msf_core PROPERTIES AUTOMOC ON)
else()
    message(WARNING "Qt6 Core not found; BackendCore Qt-dependent sources will not compile")
endif()
# S1: expose the generated build version header (msf_build_version.h) to msf_core
# consumers, i.e. the GUI executable, without hardcoding a version anywhere.
target_include_directories(msf_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
if(WIN32)
    target_link_libraries(msf_core PUBLIC windowscodecs ole32 shell32 psapi pdh)
endif()
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    if(TARGET SQLite3::SQLite3)
        target_link_libraries(msf_core PUBLIC SQLite3::SQLite3)
    elseif(TARGET SQLite::SQLite3)
        target_link_libraries(msf_core PUBLIC SQLite::SQLite3)
    else()
        target_link_libraries(msf_core PUBLIC sqlite3)
    endif()
else()
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    target_link_libraries(msf_core PUBLIC unofficial::sqlite3::sqlite3)
endif()

if(MSF_ENABLE_GPU)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        add_library(msf_cuda STATIC src/cuda_backend.cu)
        set_target_properties(msf_cuda PROPERTIES CUDA_ARCHITECTURES "${MSF_CUDA_ARCHITECTURES}")
        target_link_libraries(msf_core PUBLIC msf_cuda)
        # MSF_HAS_GPU is canonical; MSF_HAS_CUDA stays for existing code.
        target_compile_definitions(msf_core PUBLIC MSF_HAS_GPU=1 MSF_HAS_CUDA=1)
    else()
        message(STATUS "CUDA toolkit not found; using CPU backend")
    endif()
endif()

if(MSF_BUILD_TESTS)
    enable_testing()
    # P3b: IPC codec unit test (QtCore only — the exact framing both ends share).
    add_executable(msf_backend_ipc_test tests/backend_ipc_test.cpp)
    target_link_libraries(msf_backend_ipc_test PRIVATE msf_core Qt6::Core)
    target_include_directories(msf_backend_ipc_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME backend_ipc_test COMMAND msf_backend_ipc_test)
    add_executable(msf_core_test tests/core_test.cpp)
    add_executable(msf_fingerprint_fast_test tests/fingerprint_fast_test.cpp)
    add_executable(msf_resource_gpu_test tests/resource_gpu_test.cpp)
    add_executable(msf_gpu_backend_policy_test tests/gpu_backend_policy_test.cpp)
    add_executable(msf_similarity_engine_test tests/similarity_engine_test.cpp)
    add_executable(msf_similarity_parallel_test tests/similarity_parallel_test.cpp)
    add_executable(msf_candidate_index_test tests/candidate_index_test.cpp)
    add_executable(msf_candidate_index_benchmark_test tests/candidate_index_benchmark_test.cpp)
    add_executable(msf_video_similarity_test tests/video_similarity_test.cpp)
    add_executable(msf_video_crop_similarity_test tests/video_crop_similarity_test.cpp)
    add_executable(msf_resource_policy_test tests/resource_policy_test.cpp)
    add_executable(msf_scan_pipeline_test tests/scan_pipeline_test.cpp)
    add_executable(msf_incremental_index_test tests/incremental_index_test.cpp)
    add_executable(msf_media_analyzer_test tests/media_analyzer_test.cpp)
    target_link_libraries(msf_media_analyzer_test PRIVATE msf_core)
    add_test(NAME media_analyzer_test COMMAND msf_media_analyzer_test)
    target_link_libraries(msf_incremental_index_test PRIVATE msf_core)
    add_test(NAME incremental_index_test COMMAND msf_incremental_index_test)
    target_link_libraries(msf_scan_pipeline_test PRIVATE msf_core)
    add_test(NAME scan_pipeline_test COMMAND msf_scan_pipeline_test)
    target_link_libraries(msf_candidate_index_test PRIVATE msf_core)
    add_test(NAME candidate_index_test COMMAND msf_candidate_index_test)
    target_link_libraries(msf_candidate_index_benchmark_test PRIVATE msf_core)
    add_test(NAME candidate_index_benchmark_test COMMAND msf_candidate_index_benchmark_test)
    target_link_libraries(msf_video_similarity_test PRIVATE msf_core)
    target_link_libraries(msf_video_crop_similarity_test PRIVATE msf_core)
    add_test(NAME video_crop_similarity_test COMMAND msf_video_crop_similarity_test)
    add_test(NAME video_similarity_test COMMAND msf_video_similarity_test)
    target_link_libraries(msf_resource_policy_test PRIVATE msf_core)
    add_test(NAME resource_policy_test COMMAND msf_resource_policy_test)
    # Node B1: Minimal Adaptive Allocation decision-table regression.
    add_executable(msf_scheduler_test tests/scheduler_test.cpp)
    target_link_libraries(msf_scheduler_test PRIVATE msf_core)
    add_test(NAME scheduler_test COMMAND msf_scheduler_test)
    # Node C1: Profile Foundation round-trip/verdict/estimate regression.
    add_executable(msf_profile_test tests/profile_test.cpp)
    target_link_libraries(msf_profile_test PRIVATE msf_core)
    add_test(NAME profile_test COMMAND msf_profile_test)
    # Node C2: Initial Calibration bounded-probe regression.
    add_executable(msf_calibration_test tests/calibration_test.cpp)
    target_link_libraries(msf_calibration_test PRIVATE msf_core)
    add_test(NAME calibration_test COMMAND msf_calibration_test)
    target_link_libraries(msf_similarity_engine_test PRIVATE msf_core)
    add_test(NAME similarity_engine_test COMMAND msf_similarity_engine_test)
    target_link_libraries(msf_similarity_parallel_test PRIVATE msf_core)
    add_test(NAME similarity_parallel_test COMMAND msf_similarity_parallel_test)
    target_link_libraries(msf_resource_gpu_test PRIVATE msf_core)
    add_test(NAME resource_gpu_test COMMAND msf_resource_gpu_test)
    if(MSF_ENABLE_GPU AND CMAKE_CUDA_COMPILER)
        add_executable(msf_cuda_backend_test tests/cuda_backend_test.cpp)
        target_link_libraries(msf_cuda_backend_test PRIVATE msf_core)
        add_test(NAME cuda_backend_test COMMAND msf_cuda_backend_test)
    endif()
    # D9b: the optimized verify path must be double-identical to the
    # preserved pre-optimization reference on the same input. Registered in
    # both trees because the verify path is shared CPU/GPU code.
    add_executable(msf_verify_parity_test tests/verify_parity_test.cpp)
    target_link_libraries(msf_verify_parity_test PRIVATE msf_core)
    add_test(NAME verify_parity_test COMMAND msf_verify_parity_test)
    # D9a: analyze internal stage split + verify counters. Registered in both
    # trees — this is pure CPU matching work, so it must be proven where no
    # GPU exists at all.
    add_executable(msf_analyze_telemetry_test tests/analyze_telemetry_test.cpp)
    target_link_libraries(msf_analyze_telemetry_test PRIVATE msf_core)
    add_test(NAME analyze_telemetry_test COMMAND msf_analyze_telemetry_test)
    # D8a: dataset identity contract. Registered in both trees — the
    # reproducibility properties must hold where no GPU exists at all.
    add_executable(msf_dataset_fingerprint_test tests/dataset_fingerprint_test.cpp)
    target_link_libraries(msf_dataset_fingerprint_test PRIVATE msf_core)
    add_test(NAME dataset_fingerprint_test COMMAND msf_dataset_fingerprint_test)
    # D8a: reports the identity of a prepared dataset root, so the value
    # recorded in build-history can be reproduced from any checkout. It also
    # writes a <root>/dataset_fingerprint.json that a scan's benchmark JSON
    # can be compared against.
    add_executable(msf_dataset_report tests/dataset_report.cpp)
    target_link_libraries(msf_dataset_report PRIVATE msf_core)
    # Real engine scan: proves the fingerprint a scan records matches the one
    # computed independently for the same root.
    add_executable(msf_dataset_e2e_test tests/dataset_e2e_test.cpp)
    target_link_libraries(msf_dataset_e2e_test PRIVATE msf_core)
    add_test(NAME dataset_e2e_test COMMAND msf_dataset_e2e_test)
    # D8b review probe: repeated standard-dataset runs, reporting
    # distributions. Not a CTest case -- it is decision input, and it takes
    # a root/app-dir plus a run count. Fails loudly if the dataset is not
    # measured, so a decision can never rest on a missing input.
    add_executable(msf_dataset_baseline tests/dataset_baseline.cpp)
    target_link_libraries(msf_dataset_baseline PRIVATE msf_core)
    # D3-follow-up candidate probe: shared-decode-once plus dual in-memory
    # resize, measured per file per resolution with parity and score comparison.
    # Not a CTest case -- like msf_dataset_baseline it is decision input: it
    # needs a measured dataset root argument and takes minutes, and its
    # --selfcheck mode covers the helper unit checks without a dataset.
    add_executable(msf_shared_decode_probe tests/shared_decode_probe.cpp)
    target_link_libraries(msf_shared_decode_probe PRIVATE msf_core)
    # I-2 candidate probe: one shared WIC factory/decoder/frame/EXIF-orientation
    # source feeding two independent scaler+converter+CopyPixels chains that
    # produce f and a directly, with no intermediate GrayImage. Exactness-first:
    # geometry and byte parity over the whole dataset, then cost. Not a CTest
    # case for the same reason as msf_dataset_baseline (needs a dataset root
    # argument and takes minutes); its --selfcheck mode needs no dataset.
    add_executable(msf_shared_wic_source_probe tests/shared_wic_source_probe.cpp)
    target_link_libraries(msf_shared_wic_source_probe PRIVATE msf_core)
    # Only --selfcheck is registered: it is dataset-free and fast, and it carries
    # the EXIF orientation regression that guards the v0.9.4.35 production fix
    # (a WIC metadata path the product used to query incorrectly, so rotation was
    # silently never applied). The corpus/group modes stay unregistered because
    # they need a measured dataset root and take minutes.
    add_test(NAME shared_wic_source_probe COMMAND msf_shared_wic_source_probe --selfcheck)
    # I-XMP orientation fallback: self-contained EXIF/XMP APP1 fixtures driving
    # ImageDecoder::decodeBoth (geometry + orientApplied). No dataset needed.
    add_executable(msf_xmp_orientation_test tests/xmp_orientation_test.cpp)
    target_link_libraries(msf_xmp_orientation_test PRIVATE msf_core)
    add_test(NAME xmp_orientation_test COMMAND msf_xmp_orientation_test)
    # D4a: backend-internal timing contract. Registered in both trees on
    # purpose — the recorder half must be proven where CUDA does not exist.
    add_executable(msf_gpu_timing_test tests/gpu_timing_test.cpp)
    target_link_libraries(msf_gpu_timing_test PRIVATE msf_core)
    add_test(NAME gpu_timing_test COMMAND msf_gpu_timing_test)
    add_executable(msf_video_sampling_test tests/video_sampling_test.cpp)
    target_link_libraries(msf_video_sampling_test PRIVATE msf_core)
    add_test(NAME video_sampling_test COMMAND msf_video_sampling_test)
    # E-1 measurement-only probe: counts what the product does not record — real
    # av_read_frame / avcodec_receive_frame counts, seek count and latency, and
    # keyframe/GOP structure — against the REAL production sampling plan
    # (msf::make_sample_plan) and the real 96->32 + swscale path. It changes no
    # production code and opens no hardware decode path. Only --selfcheck is
    # registered: the dataset mode needs a measured video dataset root, which is
    # not part of the standard image dataset and must be generated first.
    add_executable(msf_video_decode_planner_probe tests/video_decode_planner_probe.cpp)
    target_link_libraries(msf_video_decode_planner_probe PRIVATE msf_core)
    add_test(NAME video_decode_planner_probe COMMAND msf_video_decode_planner_probe --selfcheck)
    # E-2A measurement-only probe: compares the production sequential sweep
    # against a sparse-seek-per-target candidate on the same sample plan, and
    # measures GOP twice independently (AV_PKT_FLAG_KEY packets AND decoded
    # AV_PICTURE_TYPE_I frames) because E-1 showed the packet flag alone is
    # unreliable. No production code changes, no hardware decode path. Only
    # --selfcheck is registered; the dataset mode needs a real video dataset
    # root, which is not part of the standard image dataset.
    add_executable(msf_video_sampling_strategy_probe tests/video_sampling_strategy_probe.cpp)
    target_link_libraries(msf_video_sampling_strategy_probe PRIVATE msf_core)
    add_test(NAME video_sampling_strategy_probe COMMAND msf_video_sampling_strategy_probe --selfcheck)
    # E-3B planner selfcheck: pure classification, no dataset and no decoding.
    # The planner must be able to say HEVC, AV1, unknown GOP and inexact
    # candidates are sequential BEFORE any cost argument, so that is asserted
    # directly rather than trusted.
    add_executable(msf_video_sampling_planner_test tests/video_sampling_planner_test.cpp)
    target_link_libraries(msf_video_sampling_planner_test PRIVATE msf_core)
    add_test(NAME video_sampling_planner_test COMMAND msf_video_sampling_planner_test)
    # F-1 random-access safety contract selfcheck. Deliberately probe-only: nothing
    # in msf_core includes it and no production decode decision reads it, because
    # NVDEC production adoption is NOT approved. The contract is made executable and
    # tested before any hardware path exists to gate.
    add_executable(msf_f_random_access_safety_test tests/f_random_access_safety_test.cpp)
    add_test(NAME f_random_access_safety_test COMMAND msf_f_random_access_safety_test)
    # S1 command line parser. Pure and dependency free, so the entry-point rules
    # (GUI default, unknown option rejected, --media validation) are tested
    # without launching the application.
    add_executable(msf_command_line_test tests/command_line_test.cpp)
    target_link_libraries(msf_command_line_test PRIVATE msf_core)
    add_test(NAME command_line_test COMMAND msf_command_line_test)
    # S5 Console renderer: header / CURRENT FILE / history / summary formatting,
    # middle-ellipsis abbreviation, TTY vs line-oriented output, and the negative
    # checks that the renderer never invents a status, an ETA or a CPU FB.
    add_executable(msf_benchmark_console_renderer_test
                   tests/benchmark_console_renderer_test.cpp)
    target_link_libraries(msf_benchmark_console_renderer_test PRIVATE msf_core)
    add_test(NAME benchmark_console_renderer_test
             COMMAND msf_benchmark_console_renderer_test)
    # S5 Console orchestration: suite id format and safety, the storage-root
    # collision walk, and the mode order label. Deliberately does not run a
    # benchmark; the S2/S3 run contracts are covered by the tests above.
    add_executable(msf_console_benchmark_cli_test tests/console_benchmark_cli_test.cpp)
    target_link_libraries(msf_console_benchmark_cli_test PRIVATE msf_core)
    add_test(NAME console_benchmark_cli_test COMMAND msf_console_benchmark_cli_test)
    # S6-1 ingestion and normalization: discovery, S3 recovery reuse, run
    # classification, provenance states, missing-field preservation, exclusion
    # accounting and determinism. No comparison, aggregation or statistics.
    add_executable(msf_benchmark_data_mining_test tests/benchmark_data_mining_test.cpp)
    target_link_libraries(msf_benchmark_data_mining_test PRIVATE msf_core)
    add_test(NAME benchmark_data_mining_test COMMAND msf_benchmark_data_mining_test)
    # S6-2 analysis data contract: run/mode/case layering, identity vs provenance,
    # the measured/derived/missing origin of each run-level field, dataset identity
    # tri-state, one-second duration resolution, status preservation, exclusion
    # provenance and determinism. No grouping or aggregation.
    add_executable(msf_benchmark_data_contract_test tests/benchmark_data_contract_test.cpp)
    target_link_libraries(msf_benchmark_data_contract_test PRIVATE msf_core)
    add_test(NAME benchmark_data_contract_test COMMAND msf_benchmark_data_contract_test)
    # S6-3 grouping: separate typed cohort views (dataset, dataset+scope, build,
    # requested+effective mode, case) over the S6-2 normalized dataset. Covers
    # cross-build cohorting, "all" as its own scope, Legacy/Unknown provenance
    # never merged or back-filled, multi-run separation and determinism.
    # Membership and provenance only: no aggregation, statistics or comparability
    # verdict.
    add_executable(msf_benchmark_data_grouping_test tests/benchmark_data_grouping_test.cpp)
    target_link_libraries(msf_benchmark_data_grouping_test PRIVATE msf_core)
    add_test(NAME benchmark_data_grouping_test COMMAND msf_benchmark_data_grouping_test)
    # S6-4 aggregation: count/min/max/mean/median/p95 per metric with the three
    # populations kept apart, observed == eligible + excluded, Skipped/Failed/Cancelled
    # excluded from performance but kept in status tallies, one-second run wall
    # resolution, scopes and build provenance never merged, and determinism.
    add_executable(msf_benchmark_data_aggregation_test tests/benchmark_data_aggregation_test.cpp)
    target_link_libraries(msf_benchmark_data_aggregation_test PRIVATE msf_core)
    add_test(NAME benchmark_data_aggregation_test COMMAND msf_benchmark_data_aggregation_test)
    # S6-5 comparison eligibility: dataset/scope/metric/mode compatibility, Known vs
    # Legacy/Unknown provenance, population-vs-population candidates, (journal, runId)
    # run references under runId collision, limitations carried on every candidate,
    # rejection accounting and determinism. No regression or threshold.
    add_executable(msf_benchmark_data_comparison_test tests/benchmark_data_comparison_test.cpp)
    target_link_libraries(msf_benchmark_data_comparison_test PRIVATE msf_core)
    add_test(NAME benchmark_data_comparison_test COMMAND msf_benchmark_data_comparison_test)
    # S6-6 comparison metrics: absolute delta, relative delta and ratio per statistic
    # over the S6-5 candidates, with a measured zero kept distinct from a missing value,
    # zero references refused rather than divided, sample accounting and limitations
    # carried through, metric level and resolution mismatches refused defensively, and
    # determinism. No threshold, direction or verdict.
    add_executable(msf_benchmark_data_comparison_metrics_test
                   tests/benchmark_data_comparison_metrics_test.cpp)
    target_link_libraries(msf_benchmark_data_comparison_metrics_test PRIVATE msf_core)
    add_test(NAME benchmark_data_comparison_metrics_test
             COMMAND msf_benchmark_data_comparison_metrics_test)
    # S2 benchmark core. Drives the runner through an injected fake executor, so
    # ordering / aggregation / cancellation / result preservation are verified
    # without running real media scans.
    add_executable(msf_benchmark_core_test tests/benchmark_core_test.cpp)
    target_link_libraries(msf_benchmark_core_test PRIVATE msf_core)
    add_test(NAME benchmark_core_test COMMAND msf_benchmark_core_test)
    # S3 journal: append ordering, truncated-tail recovery, incomplete case
    # classification, and duplicate idempotency.
    add_executable(msf_benchmark_journal_test tests/benchmark_journal_test.cpp)
    target_link_libraries(msf_benchmark_journal_test PRIVATE msf_core)
    add_test(NAME benchmark_journal_test COMMAND msf_benchmark_journal_test)
    # S3 store: suite path policy, sanitized naming, per-mode index application
    # directory, and the OS-level exclusive suite lock.
    add_executable(msf_benchmark_store_test tests/benchmark_store_test.cpp)
    target_link_libraries(msf_benchmark_store_test PRIVATE msf_core)
    add_test(NAME benchmark_store_test COMMAND msf_benchmark_store_test)
    # S3 end-to-end: the real S2 runner driving the real journal, plus recovery,
    # lock contention, summary regeneration and source isolation.
    add_executable(msf_benchmark_integration_test tests/benchmark_integration_test.cpp)
    target_link_libraries(msf_benchmark_integration_test PRIVATE msf_core Qt6::Core)
    add_test(NAME benchmark_integration_test COMMAND msf_benchmark_integration_test)
    # S4 GUI detailed logging: the execution-strategy controls in the real
    # MainWindow, the single-select gate, the [Detailed Logs] toggle, and the
    # absence of any benchmark run/stop/status workflow.
    add_executable(msf_ui_detailed_log_test tests/ui_detailed_log_test.cpp
                   gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
    target_link_libraries(msf_ui_detailed_log_test PRIVATE msf_core Qt6::Widgets)
    target_include_directories(msf_ui_detailed_log_test PRIVATE
                               ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    set_target_properties(msf_ui_detailed_log_test PROPERTIES AUTOMOC ON)
    add_test(NAME ui_detailed_log_test COMMAND msf_ui_detailed_log_test)
    # E-3B end-to-end. NOT a CTest entry on purpose: it needs a video dataset
    # and a scratch app dir, and it measures wall clock, so it is run manually
    # with the real dataset (see docs/build-history/0.9.4.42.*). A timing test in
    # the regression suite would be a flaky test, not a useful one.
    add_executable(msf_video_sampling_e2e_test tests/video_sampling_e2e_test.cpp)
    target_link_libraries(msf_video_sampling_e2e_test PRIVATE msf_core)
    add_executable(msf_image_test tests/image_pipeline_test.cpp)
    target_link_libraries(msf_image_test PRIVATE msf_core)
    add_test(NAME image_pipeline_test COMMAND msf_image_test)
    add_executable(msf_color_thumb_test tests/color_thumb_test.cpp)
    target_link_libraries(msf_color_thumb_test PRIVATE msf_core)
    add_test(NAME color_thumb_test COMMAND msf_color_thumb_test)
    add_executable(msf_thumb_cache_test tests/thumb_cache_test.cpp)
    target_link_libraries(msf_thumb_cache_test PRIVATE msf_core)
    add_test(NAME thumb_cache_test COMMAND msf_thumb_cache_test)
    add_executable(msf_image_verify_test tests/image_verify_test.cpp)
    target_link_libraries(msf_image_verify_test PRIVATE msf_core)
    add_test(NAME image_verify_test COMMAND msf_image_verify_test)
    add_executable(msf_image_dim_test tests/image_dim_test.cpp)
    target_link_libraries(msf_image_dim_test PRIVATE msf_core)
    add_test(NAME image_dim_test COMMAND msf_image_dim_test)
    add_executable(msf_match_revalidate_test tests/match_revalidate_test.cpp)
    target_link_libraries(msf_match_revalidate_test PRIVATE msf_core)
    add_test(NAME match_revalidate_test COMMAND msf_match_revalidate_test)
    # 0.9.4.46 DEFECT-A/B: analysis-failure state model and rescan convergence,
    # driven through the real MediaSearchEngine production scan path.
    add_executable(msf_analysis_failure_state_test tests/analysis_failure_state_test.cpp)
    target_link_libraries(msf_analysis_failure_state_test PRIVATE msf_core)
    add_test(NAME analysis_failure_state_test COMMAND msf_analysis_failure_state_test)
    add_executable(msf_scanner_test tests/scanner_test.cpp)
    target_link_libraries(msf_scanner_test PRIVATE msf_core)
    add_test(NAME scanner_test COMMAND msf_scanner_test)
    add_executable(msf_video_real_test tests/video_real_test.cpp)
    target_link_libraries(msf_video_real_test PRIVATE msf_core)
    add_test(NAME video_real_test COMMAND msf_video_real_test)
    add_executable(msf_video_reencode_test tests/video_reencode_test.cpp)
    target_link_libraries(msf_video_reencode_test PRIVATE msf_core)
    add_test(NAME video_reencode_test COMMAND msf_video_reencode_test)
    add_executable(msf_search_engine_test tests/search_engine_test.cpp)
    target_link_libraries(msf_search_engine_test PRIVATE msf_core)
    add_test(NAME search_engine_test COMMAND msf_search_engine_test)
    add_executable(msf_integration_v37_test tests/integration_v37_test.cpp)
    target_link_libraries(msf_integration_v37_test PRIVATE msf_core)
    add_test(NAME integration_v37_test COMMAND msf_integration_v37_test)
    add_executable(msf_candidate_index_batch_test tests/candidate_index_batch_test.cpp)
    target_link_libraries(msf_candidate_index_batch_test PRIVATE msf_core)
    add_test(NAME candidate_index_batch_test COMMAND msf_candidate_index_batch_test)
    add_executable(msf_mirror_similarity_test tests/mirror_similarity_test.cpp)
    add_executable(msf_crop_fingerprint_test tests/crop_fingerprint_test.cpp)
    target_link_libraries(msf_mirror_similarity_test PRIVATE msf_core)
    add_test(NAME mirror_similarity_test COMMAND msf_mirror_similarity_test)
    target_link_libraries(msf_crop_fingerprint_test PRIVATE msf_core)
    add_test(NAME crop_fingerprint_test COMMAND msf_crop_fingerprint_test)
    add_executable(msf_video_cache_test tests/video_cache_test.cpp)
    target_link_libraries(msf_video_cache_test PRIVATE msf_core)
    add_test(NAME video_cache_test COMMAND msf_video_cache_test)
    add_executable(msf_video_ssim_test tests/video_ssim_test.cpp)
    target_link_libraries(msf_video_ssim_test PRIVATE msf_core)
    add_test(NAME video_ssim_test COMMAND msf_video_ssim_test)
    add_executable(msf_video_gpu_benchmark_test tests/video_gpu_benchmark_test.cpp)
    target_link_libraries(msf_video_gpu_benchmark_test PRIVATE msf_core)
    add_test(NAME video_gpu_benchmark_test COMMAND msf_video_gpu_benchmark_test)
    add_executable(msf_video_similarity_quality_test tests/video_similarity_quality_test.cpp)
    target_link_libraries(msf_video_similarity_quality_test PRIVATE msf_core)
    add_test(NAME video_similarity_quality_test COMMAND msf_video_similarity_quality_test)
    add_executable(msf_database_test tests/database_test.cpp)
    add_executable(msf_database_performance_test tests/database_performance_test.cpp)
    add_executable(msf_index_manager_test tests/index_manager_test.cpp)
    target_link_libraries(msf_index_manager_test PRIVATE msf_core)
    add_test(NAME index_manager_test COMMAND msf_index_manager_test)
    target_link_libraries(msf_database_test PRIVATE msf_core)
    target_link_libraries(msf_database_performance_test PRIVATE msf_core)
    add_test(NAME database_performance_test COMMAND msf_database_performance_test)
    add_test(NAME database_test COMMAND msf_database_test)
    add_executable(msf_match_store_test tests/match_store_test.cpp)
    target_link_libraries(msf_match_store_test PRIVATE msf_core)
    add_test(NAME match_store_test COMMAND msf_match_store_test)
    target_link_libraries(msf_core_test PRIVATE msf_core)
    add_test(NAME core_test COMMAND msf_core_test)
    target_link_libraries(msf_fingerprint_fast_test PRIVATE msf_core)
    add_test(NAME fingerprint_fast_test COMMAND msf_fingerprint_fast_test)
    add_executable(msf_candidate_pairs_test tests/candidate_pairs_test.cpp)
    target_link_libraries(msf_candidate_pairs_test PRIVATE msf_core)
    add_test(NAME candidate_pairs_test COMMAND msf_candidate_pairs_test)
    add_executable(msf_analyze_mode_test tests/analyze_mode_test.cpp)
    target_link_libraries(msf_analyze_mode_test PRIVATE msf_core)
    add_test(NAME analyze_mode_test COMMAND msf_analyze_mode_test)
    add_executable(msf_candidate_pairs_benchmark_test tests/candidate_pairs_benchmark_test.cpp)
    target_link_libraries(msf_candidate_pairs_benchmark_test PRIVATE msf_core)
    add_test(NAME candidate_pairs_benchmark_test COMMAND msf_candidate_pairs_benchmark_test)
    add_executable(msf_media_pipeline_batch_test tests/media_pipeline_batch_test.cpp)
    target_link_libraries(msf_media_pipeline_batch_test PRIVATE msf_core)
    add_test(NAME media_pipeline_batch_test COMMAND msf_media_pipeline_batch_test)
    add_executable(msf_monitor_test tests/monitor_test.cpp)
    target_link_libraries(msf_monitor_test PRIVATE msf_core)
    add_test(NAME monitor_test COMMAND msf_monitor_test)
    add_executable(msf_monitor_scheduler_test tests/monitor_scheduler_test.cpp)
    add_executable(msf_monitor_status_test tests/monitor_status_test.cpp)
    add_executable(msf_monitor_pause_test tests/monitor_pause_test.cpp)
    target_link_libraries(msf_monitor_status_test PRIVATE msf_core)
    add_test(NAME monitor_status_test COMMAND msf_monitor_status_test)
    target_link_libraries(msf_monitor_pause_test PRIVATE msf_core)
    add_test(NAME monitor_pause_test COMMAND msf_monitor_pause_test)
    target_link_libraries(msf_monitor_scheduler_test PRIVATE msf_core)
    add_test(NAME monitor_scheduler_test COMMAND msf_monitor_scheduler_test)
    add_executable(msf_search_report_test tests/search_report_test.cpp)
    target_link_libraries(msf_gpu_backend_policy_test PRIVATE msf_core)
    add_test(NAME gpu_backend_policy_test COMMAND msf_gpu_backend_policy_test)
    target_link_libraries(msf_search_report_test PRIVATE msf_core)
    add_test(NAME search_report_test COMMAND msf_search_report_test)
    add_executable(msf_monitor_load_policy_test tests/monitor_load_policy_test.cpp)
    target_link_libraries(msf_monitor_load_policy_test PRIVATE msf_core)
    add_test(NAME monitor_load_policy_test COMMAND msf_monitor_load_policy_test)
    add_executable(msf_proc_capture_test tests/proc_capture_test.cpp)
    target_link_libraries(msf_proc_capture_test PRIVATE msf_core)
    add_test(NAME proc_capture_test COMMAND msf_proc_capture_test)
    add_executable(msf_scan_ignore_test tests/scan_ignore_test.cpp)
    target_link_libraries(msf_scan_ignore_test PRIVATE msf_core)
    add_test(NAME scan_ignore_test COMMAND msf_scan_ignore_test)
    add_executable(msf_unicode_path_test tests/unicode_path_test.cpp)
    target_link_libraries(msf_unicode_path_test PRIVATE msf_core)
    add_test(NAME unicode_path_test COMMAND msf_unicode_path_test)
    add_executable(msf_unicode_video_path_test tests/unicode_video_path_test.cpp)
    target_link_libraries(msf_unicode_video_path_test PRIVATE msf_core)
    add_test(NAME unicode_video_path_test COMMAND msf_unicode_video_path_test)
    add_executable(msf_scan_cancel_test tests/scan_cancel_test.cpp)
    target_link_libraries(msf_scan_cancel_test PRIVATE msf_core)
    add_test(NAME scan_cancel_test COMMAND msf_scan_cancel_test)
    # 0.9.4.58: drain-on-cancel. Stop means "read no new files", not "discard
    # work already read": the pending image batch drains to indexed rows.
    add_executable(msf_scan_cancel_drain_test tests/scan_cancel_drain_test.cpp)
    target_link_libraries(msf_scan_cancel_drain_test PRIVATE msf_core)
    add_test(NAME scan_cancel_drain_test COMMAND msf_scan_cancel_drain_test)
    # Follow-up: video Cancelled/Failed split. Stop during video analysis must
    # not become failed/rollback; completed videos persist and rescan reuses
    # them; genuine decode failures keep failure semantics.
    add_executable(msf_scan_cancel_video_test tests/scan_cancel_video_test.cpp)
    target_link_libraries(msf_scan_cancel_video_test PRIVATE msf_core)
    add_test(NAME scan_cancel_video_test COMMAND msf_scan_cancel_video_test)
    # 0.9.4.60 follow-up: generation scope (image-only/ignored/stamp-failure)
    # plus explicit terminal state (DB error with cancel set stays Failed).
    add_executable(msf_scan_generation_scope_test tests/scan_generation_scope_test.cpp)
    target_link_libraries(msf_scan_generation_scope_test PRIVATE msf_core)
    add_test(NAME scan_generation_scope_test COMMAND msf_scan_generation_scope_test)
    # 0.9.4.47+: Stop responsiveness. The dataset fingerprint runs at scan start
    # and must honor cancellation instead of hashing everything first.
    add_executable(msf_cancel_fingerprint_test tests/cancel_fingerprint_test.cpp)
    target_link_libraries(msf_cancel_fingerprint_test PRIVATE msf_core)
    add_test(NAME cancel_fingerprint_test COMMAND msf_cancel_fingerprint_test)
    # 0.9.4.48+: user-facing summary breakdown. Per-kind scanned/analyzed plus
    # the duplicate groups/files/pairs split, driven on the production path.
    add_executable(msf_summary_breakdown_test tests/summary_breakdown_test.cpp)
    target_link_libraries(msf_summary_breakdown_test PRIVATE msf_core)
    add_test(NAME summary_breakdown_test COMMAND msf_summary_breakdown_test)
    add_executable(msf_benchmark_test tests/benchmark_test.cpp)
    target_link_libraries(msf_benchmark_test PRIVATE msf_core)
    add_test(NAME benchmark_test COMMAND msf_benchmark_test)
    # Benchmark schema compatibility probe. Deliberately links Qt::Core and
    # parses the record with QJsonDocument, because that is the only structured
    # reader of benchmark JSON in the project (gui/mainwindow.cpp). The
    # benchmark_test substring matching cannot answer whether an old consumer
    # tolerates new fields, so this one does.
    add_executable(msf_benchmark_schema_test tests/benchmark_schema_test.cpp)
    target_link_libraries(msf_benchmark_schema_test PRIVATE msf_core Qt6::Core)
    add_test(NAME benchmark_schema_test COMMAND msf_benchmark_schema_test)
    add_executable(msf_video_degenerate_hash_test tests/video_degenerate_hash_test.cpp)
    target_link_libraries(msf_video_degenerate_hash_test PRIVATE msf_core)
    add_test(NAME video_degenerate_hash_test COMMAND msf_video_degenerate_hash_test)
    add_executable(msf_video_decode_parity_test tests/video_decode_parity_test.cpp)
    target_link_libraries(msf_video_decode_parity_test PRIVATE msf_core)
    add_test(NAME video_decode_parity_test COMMAND msf_video_decode_parity_test)
endif()

option(MSF_ENABLE_FFMPEG "Enable FFmpeg video decoding when available" ON)
if(MSF_ENABLE_FFMPEG)
  # Prefer pkg-config on Linux/macOS, but also support Windows/vcpkg where
  # pkg-config is commonly unavailable.
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavformat libavcodec libavutil libswscale)
  endif()
  if(NOT FFMPEG_FOUND)
    find_path(MSF_FFMPEG_INCLUDE_DIR libavformat/avformat.h
      HINTS $ENV{VCPKG_INSTALLED_DIR}/x64-windows/include ${MSF_FFMPEG_ROOT}/include)
    find_library(MSF_FFMPEG_AVFORMAT avformat
      HINTS $ENV{VCPKG_INSTALLED_DIR}/x64-windows/lib ${MSF_FFMPEG_ROOT}/lib)
    find_library(MSF_FFMPEG_AVCODEC avcodec
      HINTS $ENV{VCPKG_INSTALLED_DIR}/x64-windows/lib ${MSF_FFMPEG_ROOT}/lib)
    find_library(MSF_FFMPEG_AVUTIL avutil
      HINTS $ENV{VCPKG_INSTALLED_DIR}/x64-windows/lib ${MSF_FFMPEG_ROOT}/lib)
    find_library(MSF_FFMPEG_SWSCALE swscale
      HINTS $ENV{VCPKG_INSTALLED_DIR}/x64-windows/lib ${MSF_FFMPEG_ROOT}/lib)
    if(MSF_FFMPEG_INCLUDE_DIR AND MSF_FFMPEG_AVFORMAT AND MSF_FFMPEG_AVCODEC AND MSF_FFMPEG_AVUTIL AND MSF_FFMPEG_SWSCALE)
      set(FFMPEG_FOUND TRUE)
      set(FFMPEG_INCLUDE_DIRS ${MSF_FFMPEG_INCLUDE_DIR})
      set(FFMPEG_LIBRARIES ${MSF_FFMPEG_AVFORMAT} ${MSF_FFMPEG_AVCODEC} ${MSF_FFMPEG_AVUTIL} ${MSF_FFMPEG_SWSCALE})
    endif()
  endif()
  if(FFMPEG_FOUND)
    # PUBLIC: msf_core is a static library, so consumers (tests, GUI) must inherit
    # the FFmpeg include/link paths and libraries at their own link step.
    # MSF_HAS_FFMPEG is PUBLIC as well so video_decoder.h exposes the same
    # interface to consumers as the one msf_core was compiled with.
    target_compile_definitions(msf_core PUBLIC MSF_HAS_FFMPEG=1)
    target_include_directories(msf_core PUBLIC ${FFMPEG_INCLUDE_DIRS})
    if(FFMPEG_LIBRARY_DIRS)
      target_link_directories(msf_core PUBLIC ${FFMPEG_LIBRARY_DIRS})
    endif()
    target_link_libraries(msf_core PUBLIC ${FFMPEG_LIBRARIES})
  else()
    message(STATUS "FFmpeg not found; video decoding falls back to ffmpeg/ffprobe command line")
  endif()
endif()

option(MSF_BUILD_GUI "Build Qt6 Windows GUI when Qt6 is available" ON)
if(MSF_BUILD_GUI)
  find_package(Qt6 QUIET COMPONENTS Widgets)
  if(Qt6_FOUND)
    qt_standard_project_setup()
    qt_add_executable(MediaSimilarityFinder
      gui/main.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h
      gui/backend_supervisor.cpp gui/backend_supervisor.h
      # S4 benchmark worker. Listed for AUTOMOC; MainWindow does not use it yet,
      # which is why no layout or control was touched in this stage.
     )
    # GUI subsystem: no console window on double-click. The --version/--help/
    # --smoke flags attach to the parent console explicitly (see main.cpp).
    set_target_properties(MediaSimilarityFinder PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_libraries(MediaSimilarityFinder PRIVATE msf_core Qt6::Widgets)
    target_include_directories(MediaSimilarityFinder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    get_target_property(MSF_QT_CORE_LOC Qt6::Core LOCATION_Release)
    if(NOT MSF_QT_CORE_LOC)
      get_target_property(MSF_QT_CORE_LOC Qt6::Core LOCATION)
    endif()
    if(MSF_QT_CORE_LOC)
      get_filename_component(MSF_QT_BIN_DIR "${MSF_QT_CORE_LOC}" DIRECTORY)
      set(MSF_QT_PLUGIN_PATH "${MSF_QT_BIN_DIR}/../Qt6/plugins")
      foreach(MSF_QT_PLUGIN_CAT platforms styles imageformats generic)
        if(EXISTS "${MSF_QT_PLUGIN_PATH}/${MSF_QT_PLUGIN_CAT}")
          add_custom_command(TARGET MediaSimilarityFinder POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${MSF_QT_PLUGIN_PATH}/${MSF_QT_PLUGIN_CAT}" "$<TARGET_FILE_DIR:MediaSimilarityFinder>/${MSF_QT_PLUGIN_CAT}")
        endif()
      endforeach()
    endif()
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
      file(GLOB MSF_FFMPEG_EXTRA_DLLS
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/avdevice-*.dll"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/avfilter-*.dll")
      foreach(MSF_FFMPEG_TOOL ffmpeg ffprobe)
        add_custom_command(TARGET MediaSimilarityFinder POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/ffmpeg/${MSF_FFMPEG_TOOL}.exe" "$<TARGET_FILE_DIR:MediaSimilarityFinder>/${MSF_FFMPEG_TOOL}.exe")
      endforeach()
      foreach(MSF_FFMPEG_DLL IN LISTS MSF_FFMPEG_EXTRA_DLLS)
        get_filename_component(MSF_FFMPEG_DLL_NAME "${MSF_FFMPEG_DLL}" NAME)
        add_custom_command(TARGET MediaSimilarityFinder POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MSF_FFMPEG_DLL}" "$<TARGET_FILE_DIR:MediaSimilarityFinder>/${MSF_FFMPEG_DLL_NAME}")
      endforeach()
    endif()
    # Cross-process UI-settings round-trip probe (write -> verify via DEPENDS).
    # Same-process read-back would hit QSettings' in-memory cache, so the two
    # phases run as separate processes to genuinely prove disk persistence.
    if(MSF_BUILD_TESTS)
      add_executable(msf_ui_settings_test tests/ui_settings_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_ui_settings_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_ui_settings_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      set(MSF_UI_SETTINGS_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui-settings-probe")
      add_test(NAME ui_settings_write COMMAND msf_ui_settings_test --probe-write "${MSF_UI_SETTINGS_DIR}" 640 480)
      add_test(NAME ui_settings_verify COMMAND msf_ui_settings_test --probe-verify "${MSF_UI_SETTINGS_DIR}" 640 480)
      set_tests_properties(ui_settings_verify PROPERTIES DEPENDS ui_settings_write)
      add_test(NAME ui_reference_order_test COMMAND msf_ui_settings_test --ref-order)
      # Any UI string key that is used but missing from trStr() renders as its
      # own camelCase identifier, because trStr() falls back to returning the
      # key. colFile/colDate/viewGrid were missing and showed up that way.
      add_test(NAME ui_translation_keys_test COMMAND msf_ui_settings_test --tr-keys)
      # The user-visible CPU control, stored policy, and engine input share the
      # same 10-90 range. This offscreen probe checks the displayed value after
      # out-of-range input; the policy mapping itself is covered by
      # resource_policy_test.
      add_test(NAME ui_cpu_policy_test COMMAND msf_ui_settings_test --cpu-policy)
      # 0.9.4.29 settings-identity migration. Every phase is its own process so
      # a same-process QSettings cache can never make a broken migration pass.
      set(MSF_MIGRATE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui-settings-migrate")
      set(MSF_MIGRATE_BOTH_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui-settings-migrate-both")
      set(MSF_MIGRATE_NONE_DIR "${CMAKE_CURRENT_BINARY_DIR}/ui-settings-migrate-none")
      add_test(NAME ui_settings_migrate_plant COMMAND msf_ui_settings_test --migrate-plant "${MSF_MIGRATE_DIR}")
      add_test(NAME ui_settings_migrate_verify COMMAND msf_ui_settings_test --migrate-verify "${MSF_MIGRATE_DIR}")
      set_tests_properties(ui_settings_migrate_verify PROPERTIES DEPENDS ui_settings_migrate_plant)
      # Both locations present: the new one must win and the legacy one survive.
      add_test(NAME ui_settings_migrate_both_plant COMMAND msf_ui_settings_test --migrate-plant-new-only "${MSF_MIGRATE_BOTH_DIR}")
      add_test(NAME ui_settings_migrate_both_verify COMMAND msf_ui_settings_test --migrate-both-verify "${MSF_MIGRATE_BOTH_DIR}")
      set_tests_properties(ui_settings_migrate_both_verify PROPERTIES DEPENDS ui_settings_migrate_both_plant)
      # No legacy file: nothing may be invented, and the new INI must appear.
      add_test(NAME ui_settings_migrate_none_verify COMMAND msf_ui_settings_test --migrate-none-verify "${MSF_MIGRATE_NONE_DIR}")
      # Worker streaming + persistence probe: real scan over identical BMPs,
      # asserts live matches stream AND survive in a fresh engine handle.
      add_executable(msf_scan_streaming_test tests/scan_streaming_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_scan_streaming_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_scan_streaming_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME scan_streaming_test COMMAND msf_scan_streaming_test)
      # D3-Minimal: bounded walker queue (unit mechanism + live integration).
      add_executable(msf_walker_queue_test tests/walker_queue_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_walker_queue_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_walker_queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME walker_queue_test COMMAND msf_walker_queue_test)
      # MainWindow scan-workflow regression: real MainWindow slot path
      # (folder -> startScan -> worker -> drainMatches -> group render),
      # offscreen with isolated QSettings. Covers what --smoke (window only)
      # and ScanWorker-level tests leave open.
      add_executable(msf_scan_workflow_test tests/scan_workflow_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_scan_workflow_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_scan_workflow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME scan_workflow_test COMMAND msf_scan_workflow_test)
      # 0.9.4.61 GUI scroll regression: thumbnail starvation must drive
      # in-place catch-up only (never a full list rebuild mid-scroll), with a
      # scroll gate deferring data-change rebuilds during interaction.
      add_executable(msf_ui_scroll_regression_test tests/ui_scroll_regression_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_ui_scroll_regression_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_ui_scroll_regression_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME ui_scroll_regression_test COMMAND msf_ui_scroll_regression_test)
      # 0.9.4.63 GUI usability: display-settings round-trip, splitter
      # middle-first growth, filename selectability.
      add_executable(msf_ui_usability_test tests/ui_usability_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_ui_usability_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_ui_usability_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME ui_usability_test COMMAND msf_ui_usability_test)
      # 0.9.4.62 crash diagnostics: Qt message file sink plus scan-worker
      # catch-all (non-standard exceptions become failed(), not terminate).
      add_executable(msf_crash_diagnostics_test tests/crash_diagnostics_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_crash_diagnostics_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_crash_diagnostics_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME crash_diagnostics_test COMMAND msf_crash_diagnostics_test)
      # S4 functional Detailed Logs acceptance: real MainWindow scan with
      # [Detailed Logs] on, asserting the produced telemetry (purpose,
      # counts, fingerprint, dialog path) instead of only the dialog.
      add_executable(msf_scan_detailed_log_acceptance_test tests/scan_detailed_log_acceptance_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_scan_detailed_log_acceptance_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_scan_detailed_log_acceptance_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME scan_detailed_log_acceptance_test COMMAND msf_scan_detailed_log_acceptance_test)
      # E2E video probe: copy + libx264 re-encode + trim + stranger through a
      # real scan (full XOR -> CandidateIndex incl. L1 anchors -> temporal).
      add_executable(msf_video_scan_e2e_test tests/video_scan_e2e_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_video_scan_e2e_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_video_scan_e2e_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME video_scan_e2e_test COMMAND msf_video_scan_e2e_test)
      # View-mode round-trip probe: Tiles -> Icons must keep grid items
      # visible (explicit grid size + delegate swap must fully reset), plus
      # the real MainWindow viewBox path (256 Icon -> List/Tiles -> 256 Icon
      # must restore 256 geometry with no overlap).
      add_executable(msf_view_mode_probe tests/view_mode_probe.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_view_mode_probe PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_view_mode_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME view_mode_probe COMMAND msf_view_mode_probe)
      add_executable(msf_thumb_layout_test tests/thumb_layout_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_thumb_layout_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_thumb_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME thumb_layout_test COMMAND msf_thumb_layout_test)
      # QuickLook pipe protocol probe (fake in-process server, exact-bytes check).
      add_executable(msf_quicklook_test tests/quicklook_pipe_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
      target_link_libraries(msf_quicklook_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_quicklook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME quicklook_pipe_test COMMAND msf_quicklook_test)
      if(WIN32)
        add_executable(msf_reveal_window_test tests/reveal_window_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
        target_link_libraries(msf_reveal_window_test PRIVATE msf_core Qt6::Widgets)
        target_include_directories(msf_reveal_window_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
        add_test(NAME reveal_window_test COMMAND msf_reveal_window_test)
        set_tests_properties(reveal_window_test PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${MSF_QT_PLUGIN_PATH}")
        # P3 backend process E2E + crash injection (Windows-only: TerminateProcess).
        # Real MainWindow + real supervisor + real backend OS process.
        add_executable(msf_backend_e2e_test tests/backend_e2e_test.cpp gui/mainwindow.cpp gui/mainwindow.h gui/backend_loopback.cpp gui/backend_client.h gui/backend_supervisor.cpp gui/backend_supervisor.h)
        target_link_libraries(msf_backend_e2e_test PRIVATE msf_core Qt6::Widgets)
        target_include_directories(msf_backend_e2e_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
        add_test(NAME backend_e2e_test COMMAND msf_backend_e2e_test)
        set_tests_properties(backend_e2e_test PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${MSF_QT_PLUGIN_PATH}")
      endif()
      set_tests_properties(ui_settings_write ui_settings_verify ui_reference_order_test ui_settings_migrate_plant ui_settings_migrate_verify ui_settings_migrate_both_plant ui_settings_migrate_both_verify ui_settings_migrate_none_verify scan_streaming_test scan_workflow_test walker_queue_test video_scan_e2e_test view_mode_probe thumb_layout_test quicklook_pipe_test PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${MSF_QT_PLUGIN_PATH}")
    endif()
  else()
    message(STATUS "Qt6 not found; GUI target skipped")
  endif()
endif()
# P1 (0.9.4.67): Backend executable. Links BackendCore (msf_core) with
# Qt6::Core only — never Widgets/Gui (G13). Console subsystem so stdio works
# for --help/--version now and the line-oriented IPC channel in P3. The GUI
# still runs in-process in P1; spawning arrives in P3.
find_package(Qt6 QUIET COMPONENTS Core)
if(Qt6_FOUND)
  # JPEG encoding for backend thumbnails lives in msf_core now
  # (ThumbnailStore serves JPEG end to end; no QtGui per G13).
  find_package(libjpeg-turbo CONFIG REQUIRED)
  target_link_libraries(msf_core PUBLIC libjpeg-turbo::turbojpeg)
  add_executable(MediaSimilarityFinderBackend src/backend_main.cpp)
  target_link_libraries(MediaSimilarityFinderBackend PRIVATE msf_core Qt6::Core)
  target_include_directories(MediaSimilarityFinderBackend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  if(MSF_BUILD_TESTS)
    add_test(NAME backend_version COMMAND MediaSimilarityFinderBackend --version)
    set_tests_properties(backend_version PROPERTIES PASS_REGULAR_EXPRESSION "MediaSimilarityFinderBackend ${PROJECT_VERSION}")
    add_test(NAME backend_help COMMAND MediaSimilarityFinderBackend --help)
    set_tests_properties(backend_help PROPERTIES PASS_REGULAR_EXPRESSION "--backend")
    # 0.9.4.75 Test Mode isolation: same backend scan pipeline, fresh scratch
    # app dir, production index bytes unchanged.
    add_executable(msf_test_mode_test tests/test_mode_test.cpp)
    target_link_libraries(msf_test_mode_test PRIVATE msf_core Qt6::Core)
    target_include_directories(msf_test_mode_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME test_mode_test COMMAND msf_test_mode_test)
    # 0.9.4.78 stall watchdog: pure decision logic under fake clocks.
    add_executable(msf_stall_watchdog_test tests/stall_watchdog_test.cpp)
    target_include_directories(msf_stall_watchdog_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME stall_watchdog_test COMMAND msf_stall_watchdog_test)
    # 0.9.4.78 video-build cancel: cooperative flag must fail fast.
    add_executable(msf_video_cancel_test tests/video_cancel_test.cpp)
    target_link_libraries(msf_video_cancel_test PRIVATE msf_core Qt6::Core)
    target_include_directories(msf_video_cancel_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME video_cancel_test COMMAND msf_video_cancel_test)
    # 0.9.4.79 index janitor: stale per-root indexes + old TestMode scratch.
    add_executable(msf_index_janitor_test tests/index_janitor_test.cpp src/index_janitor.cpp)
    target_include_directories(msf_index_janitor_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME index_janitor_test COMMAND msf_index_janitor_test)
    # 0.9.4.76 crash-frontier durability: mid-batch failure must leave the
    # admission frontier (fingerprint 0, not failed) committed for retry.
    add_executable(msf_scan_crash_frontier_test tests/scan_crash_frontier_test.cpp)
    target_link_libraries(msf_scan_crash_frontier_test PRIVATE msf_core Qt6::Core)
    target_include_directories(msf_scan_crash_frontier_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME scan_crash_frontier_test COMMAND msf_scan_crash_frontier_test)
    # Transitional: the IPC endpoint arrives in P3, so --backend refuses.
    # WILL_FAIL documents the expectation; P3 removes it when serving starts.
    add_test(NAME backend_refuses_before_p3 COMMAND MediaSimilarityFinderBackend --backend)
    set_tests_properties(backend_refuses_before_p3 PROPERTIES WILL_FAIL TRUE)
  endif()
else()
  message(STATUS "Qt6 Core not found; Backend target skipped")
endif()
