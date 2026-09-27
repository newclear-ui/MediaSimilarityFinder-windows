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
};

}
