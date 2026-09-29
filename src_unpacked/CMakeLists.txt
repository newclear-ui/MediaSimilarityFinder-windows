cmake_minimum_required(VERSION 3.20)
project(MediaSimilarityFinder VERSION 0.9.4.40 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options($<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/utf-8>)
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
    src/fingerprint.cpp
    src/crop_fingerprint.cpp
    src/similarity.cpp
    src/sampling.cpp
    src/database.cpp
    src/index_manager.cpp
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
    src/media_search_engine.cpp
    src/monitor.cpp
    src/benchmark.cpp
    src/scheduler.cpp
    src/profile.cpp
    src/calibration.cpp
    src/walker_queue.cpp
)
target_include_directories(msf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
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
      gui/main.cpp gui/mainwindow.cpp gui/mainwindow.h)
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
      add_executable(msf_ui_settings_test tests/ui_settings_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
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
      add_executable(msf_scan_streaming_test tests/scan_streaming_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_scan_streaming_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_scan_streaming_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME scan_streaming_test COMMAND msf_scan_streaming_test)
      # D3-Minimal: bounded walker queue (unit mechanism + live integration).
      add_executable(msf_walker_queue_test tests/walker_queue_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_walker_queue_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_walker_queue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME walker_queue_test COMMAND msf_walker_queue_test)
      # MainWindow scan-workflow regression: real MainWindow slot path
      # (folder -> startScan -> worker -> drainMatches -> group render),
      # offscreen with isolated QSettings. Covers what --smoke (window only)
      # and ScanWorker-level tests leave open.
      add_executable(msf_scan_workflow_test tests/scan_workflow_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_scan_workflow_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_scan_workflow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME scan_workflow_test COMMAND msf_scan_workflow_test)
      # E2E video probe: copy + libx264 re-encode + trim + stranger through a
      # real scan (full XOR -> CandidateIndex incl. L1 anchors -> temporal).
      add_executable(msf_video_scan_e2e_test tests/video_scan_e2e_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_video_scan_e2e_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_video_scan_e2e_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME video_scan_e2e_test COMMAND msf_video_scan_e2e_test)
      # View-mode round-trip probe: Tiles -> Icons must keep grid items
      # visible (explicit grid size + delegate swap must fully reset).
      add_executable(msf_view_mode_probe tests/view_mode_probe.cpp)
      target_link_libraries(msf_view_mode_probe PRIVATE Qt6::Widgets)
      add_test(NAME view_mode_probe COMMAND msf_view_mode_probe)
      add_executable(msf_thumb_layout_test tests/thumb_layout_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_thumb_layout_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_thumb_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME thumb_layout_test COMMAND msf_thumb_layout_test)
      # QuickLook pipe protocol probe (fake in-process server, exact-bytes check).
      add_executable(msf_quicklook_test tests/quicklook_pipe_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
      target_link_libraries(msf_quicklook_test PRIVATE msf_core Qt6::Widgets)
      target_include_directories(msf_quicklook_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
      add_test(NAME quicklook_pipe_test COMMAND msf_quicklook_test)
      if(WIN32)
        add_executable(msf_reveal_window_test tests/reveal_window_test.cpp gui/mainwindow.cpp gui/mainwindow.h)
        target_link_libraries(msf_reveal_window_test PRIVATE msf_core Qt6::Widgets)
        target_include_directories(msf_reveal_window_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/gui)
        add_test(NAME reveal_window_test COMMAND msf_reveal_window_test)
        set_tests_properties(reveal_window_test PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${MSF_QT_PLUGIN_PATH}")
      endif()
      set_tests_properties(ui_settings_write ui_settings_verify ui_reference_order_test ui_settings_migrate_plant ui_settings_migrate_verify ui_settings_migrate_both_plant ui_settings_migrate_both_verify ui_settings_migrate_none_verify scan_streaming_test scan_workflow_test walker_queue_test video_scan_e2e_test view_mode_probe thumb_layout_test quicklook_pipe_test PROPERTIES ENVIRONMENT "QT_PLUGIN_PATH=${MSF_QT_PLUGIN_PATH}")
    endif()
  else()
    message(STATUS "Qt6 not found; GUI target skipped")
  endif()
endif()
