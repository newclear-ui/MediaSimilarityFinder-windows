#pragma once
// Analyze-mode selector (0.9.4.81). An experimental option that chooses WHEN
// and in WHAT units candidate-pair verification runs. The match/verdict
// semantics (thresholds, SSIM, temporal rules) are identical across modes;
// only scheduling, work distribution and checkpointing differ.
//
//   Sequential (B) - verify after the walk/analysis phase completes, in ordered
//                    file slices with a persisted frontier so a stop or crash
//                    can resume from the last completed slice. DEFAULT.
//   Live (A)       - verify while files stream in (the streaming pass becomes
//                    authoritative for images); the final pass is reduced.
//   Hybrid (A+B)   - images verified live, remaining video/crop work sliced.
//
// The stable one-word key ("B"/"A"/"AB") is written to the backend log at scan
// start as "scanAnalyzeMode=<key>" so a final comparison test can tell which
// mode produced a run without parsing anything else. The key is intentionally
// decoupled from the localized display name in Settings.
namespace msf {
enum class AnalyzeMode {
    Sequential = 0, // B
    Live = 1,       // A
    Hybrid = 2,     // A+B
};
inline const char* analyzeModeKey(AnalyzeMode m) {
    switch (m) {
        case AnalyzeMode::Live: return "A";
        case AnalyzeMode::Hybrid: return "AB";
        case AnalyzeMode::Sequential:
        default: return "B";
    }
}
inline AnalyzeMode analyzeModeFromInt(int v) {
    switch (v) {
        case 1: return AnalyzeMode::Live;
        case 2: return AnalyzeMode::Hybrid;
        case 0:
        default: return AnalyzeMode::Sequential;
    }
}
} // namespace msf
