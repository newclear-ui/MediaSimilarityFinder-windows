#pragma once
#include <cstdint>

namespace msf {

// D9a: telemetry sink for the analyze stage.
//
// This is a plain data carrier on purpose. It has no dependency on
// BenchmarkRecorder, ScanPipeline, or image_verify, which is what keeps the
// hot path from taking a new dependency back up into engine telemetry:
// image_verify and scan_pipeline only fill this struct, the engine copies it
// into the recorder, and the recorder renders it. No recorder pointer is ever
// passed down into verification code.
//
// It counts events; it never infers a quantity. "Measured as 0" and
// "never ran" are kept apart by the *Entered/Ran flags, because a stage that
// was never entered must report not_measured rather than a zero.
struct AnalyzeTelemetry {
    // Per-stage wall time measured inside ScanPipeline::analyze().
    // The four are non-overlapping by construction: index, verify and video
    // are timed at their own call sites, and scan is defined as the
    // remainder of the analyze total. So their sum never exceeds the total.
    double indexMs = 0;   // (A) candidate index build
    double scanMs = 0;    // (B) enumeration + Hamming + unattributed remainder
    double verifyMs = 0;  // (C) wall time inside verifyImagePair calls
    double videoMs = 0;   // (D) wall time inside the temporal flush

    // Counters, each counting the event it names.
    std::uint64_t verifyCalls = 0;         // verifyImagePair calls with isImage
    std::uint64_t verifyDecodeMisses = 0;  // buffer lookups that had to decode
    std::uint64_t verifyCacheHits = 0;     // buffer lookups served from cache
    std::uint64_t ssimEvals = 0;           // ssimBuf invocations
    std::uint64_t frameSsimEvals = 0;      // actual frame_ssim invocations
    std::uint64_t videoTemporalPairs = 0;  // pairs queued for temporal work

    // Code-path entry flags. A stage that never ran is not "0 ms measured".
    bool analyzeRan = false;
    bool videoStageEntered = false;

    // ------------------------------------------------------------ D9c
    // Exclusive breakdown of the time already counted in verifyMs. These are
    // NOT additional cost: they partition verifyMs, they do not add to it.
    // The stages below are disjoint code regions, so
    //
    //   keyMs + decodeMs + cacheStoreMs + cacheCopyMs
    //         + cropMs + flipMs + frameSsimMs + otherMs
    //
    // reconstructs verifyMs without double counting. otherMs is the remainder,
    // defined the same way D9a defined scanMs, so a mis-scoped timer shows up
    // as a negative or oversized "other" rather than a plausible-looking but
    // wrong distribution. The measured-bounds are:
    //
    //   keyMs        stat + 64 KiB quick-hash read, runs on EVERY buffer
    //                lookup, including cache hits
    //   decodeMs     ImageDecoder::decode x2 + dimension validation (misses)
    //   cacheStoreMs cache insert + LRU eviction (misses)
    //   cacheCopyMs  buffer copy out of the cache (hits)
    //   cropMs       the 8 centerCropResize calls
    //   flipMs       the 10 mirror flips inside ssimBuf
    //   frameSsimMs  the 20 frame_ssim calls
    //
    // resize is deliberately NOT split out of crop: centerCropResize does both
    // in one function, so separating them would require changing the code
    // under measurement. frame_ssim internals are also unmeasured, for the
    // same reason: it is one function with no separable sub-stage.
    //
    // D9c is instrumentation, so these numbers describe a build that pays for
    // its own timers. They are a relative cost distribution, not a production
    // performance claim.
    double verifyKeyMs = 0;        // stat + quick-hash read, hits included
    double verifyDecodeMs = 0;     // image decode, misses only
    double verifyCacheStoreMs = 0; // cache insert/evict, misses only
    double verifyCacheCopyMs = 0;  // buffer copy out of cache, hits only
    double verifyCropMs = 0;       // 8x centerCropResize
    double verifyFlipMs = 0;       // 10x mirror flip
    double verifyFrameSsimMs = 0;  // 20x frame_ssim
    double verifyOtherMs = 0;      // remainder of verifyMs

    // D9c stage counters. verifyCalls / verifyDecodeMisses / verifyCacheHits /
    // ssimEvals / frameSsimEvals keep their D9a meaning and are untouched.
    std::uint64_t verifyBufferLookups = 0;  // verifyBuffersFor entries
    std::uint64_t verifyQuickHashReads = 0; // 64 KiB quick-hash reads performed
    std::uint64_t verifyQuickHashBytes = 0; // bytes fed to the FNV loop
    std::uint64_t verifyDecodes = 0;        // ImageDecoder::decode invocations
    std::uint64_t verifyCacheCopies = 0;    // GrayImage copies out of the cache
    std::uint64_t verifyCropCalls = 0;      // centerCropResize invocations
    std::uint64_t verifyFlipCalls = 0;      // flipBuf invocations
};

}
