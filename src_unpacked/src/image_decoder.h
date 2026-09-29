#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
// D9d: per-call sink for the decode sub-stage breakdown. A plain data carrier,
// exactly like AnalyzeTelemetry -- the decoder never learns who reads it, and
// no recorder pointer reaches into decoding code. Null disables every timer,
// which is the default so the hot path can be left uninstrumented.
struct DecodeTelemetry {
  // Timers, in milliseconds. Exclusive within one decode call.
  double totalMs = 0;        // whole decode() or decodePreserveAspect()
  double comInitMs = 0;      // CoInitializeEx / MultiByteToWideChar
  double factoryMs = 0;      // CoCreateInstance of the WIC factory
  double openMs = 0;         // CreateDecoderFromFilename (file open + decoder)
  double metadataMs = 0;     // GetFrame + metadata query + EXIF orientation tag
  double orientMs = 0;       // CreateBitmapFlipRotator + Initialize
  double resizeMs = 0;       // CreateBitmapScaler + Initialize (Fant)
  double convertMs = 0;      // CreateBitmapFormatConverter + Initialize
  double copyMs = 0;         // CopyPixels. NOTE: WIC decodes lazily during
                             // CopyPixels, so the real image decode lands in
                             // this bucket and cannot be split out without
                             // changing the code under measurement.
  double otherMs = 0;        // remainder inside the WIC function
  double pgmFallbackMs = 0;  // the PGM retry path, when WIC fails

  // Counters.
  std::uint64_t calls = 0;          // decode() invocations
  std::uint64_t aspectCalls = 0;    // decodePreserveAspect() invocations
  std::uint64_t wicSucceeded = 0;   // WIC path produced the image
  std::uint64_t pgmFallbacks = 0;   // WIC failed and the PGM reader took over
  std::uint64_t orientApplied = 0;  // EXIF orientation was not identity
  std::uint64_t failures = 0;       // decode returned false

  // ---- D1: why are open and factory expensive? ----
  // Question B: the source attempts CLSID_WICImagingFactory2 and falls back on
  // failure. Whether the fallback ever fires was previously unknown, because
  // the HRESULT was discarded. These count it, and record why.
  std::uint64_t factory2Attempts = 0;
  std::uint64_t factory2Successes = 0;
  std::uint64_t factory2Fallbacks = 0;
  std::uint32_t factory2FirstFailHr = 0;   // raw HRESULT, 0 if never failed
  std::uint64_t factory2Ms = 0;            // time inside the Factory2 attempt
  double factoryFallbackMs = 0;            // time inside the fallback attempt

  // Question A: CreateDecoderFromFilename is one opaque call that opens the
  // file, probes the format and instantiates the decoder. To separate the OS
  // cost from the WIC-specific cost without changing how WIC is used, a plain
  // CreateFileW + CloseHandle on the same path is timed as a reference. It is
  // NOT part of the decode path and is NOT included in openMs; the difference
  // between the two is the WIC-specific part of open.
   double osFileOpenProbeMs = 0;
   std::uint64_t osFileOpenProbeCount = 0;
   std::uint64_t osFileOpenProbeFails = 0;
   std::uint64_t openHrFailCount = 0;
   std::uint32_t openHrFirstFailCode = 0;
};

// D3: folds the two per-call accumulators back into the single combined
// DecodeTelemetry the D9d report reads.
//
// This exists so that the D9d keys (decodeTotalMs, decodeOpenMs, decodeCalls,
// the D1 factory2 split, ...) keep exactly the meaning they had when both
// calls accumulated into one struct. Without this, splitting the accumulator
// would silently turn every D9d number into a first-call-only number and the
// D9d/D1 baselines would stop being comparable. Timers and counters add; the
// two "first failure" HRESULTs take the first non-zero, since two codes cannot
// be summed.
inline void mergeDecodeTelemetry(DecodeTelemetry& dst, const DecodeTelemetry& a, const DecodeTelemetry& b) {
  dst.totalMs = a.totalMs + b.totalMs;
  dst.comInitMs = a.comInitMs + b.comInitMs;
  dst.factoryMs = a.factoryMs + b.factoryMs;
  dst.openMs = a.openMs + b.openMs;
  dst.metadataMs = a.metadataMs + b.metadataMs;
  dst.orientMs = a.orientMs + b.orientMs;
  dst.resizeMs = a.resizeMs + b.resizeMs;
  dst.convertMs = a.convertMs + b.convertMs;
  dst.copyMs = a.copyMs + b.copyMs;
  dst.otherMs = a.otherMs + b.otherMs;
  dst.pgmFallbackMs = a.pgmFallbackMs + b.pgmFallbackMs;

  dst.calls = a.calls + b.calls;
  dst.aspectCalls = a.aspectCalls + b.aspectCalls;
  dst.wicSucceeded = a.wicSucceeded + b.wicSucceeded;
  dst.pgmFallbacks = a.pgmFallbacks + b.pgmFallbacks;
  dst.orientApplied = a.orientApplied + b.orientApplied;
  dst.failures = a.failures + b.failures;

  dst.factory2Attempts = a.factory2Attempts + b.factory2Attempts;
  dst.factory2Successes = a.factory2Successes + b.factory2Successes;
  dst.factory2Fallbacks = a.factory2Fallbacks + b.factory2Fallbacks;
  dst.factory2FirstFailHr = a.factory2FirstFailHr ? a.factory2FirstFailHr : b.factory2FirstFailHr;
  dst.factory2Ms = a.factory2Ms + b.factory2Ms;
  dst.factoryFallbackMs = a.factoryFallbackMs + b.factoryFallbackMs;

  dst.osFileOpenProbeMs = a.osFileOpenProbeMs + b.osFileOpenProbeMs;
  dst.osFileOpenProbeCount = a.osFileOpenProbeCount + b.osFileOpenProbeCount;
  dst.osFileOpenProbeFails = a.osFileOpenProbeFails + b.osFileOpenProbeFails;
  dst.openHrFailCount = a.openHrFailCount + b.openHrFailCount;
  dst.openHrFirstFailCode = a.openHrFirstFailCode ? a.openHrFirstFailCode : b.openHrFirstFailCode;
}

// ---------------------------------------------------------------- D2
// Measurement-only probe. It is a separate entry point on purpose: it is never
// called from decode()/decodePreserveAspect(), and it does not change what
// production does. Its purpose is to compare the three WIC decoder entry
// points on the same file with the same metadata options, so that "another
// entry point might be cheaper" becomes a measurement instead of a guess.
//
// Every field is one call. combinedMs is the sum of that path's own
// components and is reported next to them, never added to them.
struct DecoderPathTiming {
  bool attempted = false;
  bool ok = false;              // decoder created AND GetFrame(0) yielded a size
  std::uint32_t hr = 0;         // HRESULT of the decoder-creation call
  int width = 0, height = 0;    // frame size, proving the decoder is usable
   double fileOpenMs = 0;        // CreateFileW                      (B, C)
   double streamCreateMs = 0;    // unused by D2: see DecoderPathProbe::stream
   double streamInitMs = 0;      // IStream adapter construction      (C)
  double decoderMs = 0;         // the decoder-creation call itself
  double combinedMs = 0;        // sum of the above for this path
};

struct DecoderPathProbe {
  DecoderPathTiming filename;   // Path A: CreateDecoderFromFilename
  DecoderPathTiming fileHandle; // Path B: CreateFileW + CreateDecoderFromFileHandle
   DecoderPathTiming stream;     // Path C: CreateFileW + IStream + CreateDecoderFromStream
  bool available = false;       // false on non-WIC builds -> not_available, not 0
};
struct GrayImage { int width=0,height=0; std::vector<std::uint8_t> pixels; };
struct ColorImage { int width=0,height=0; std::vector<std::uint8_t> bgra; }; // 4 bytes/px, B,G,R,A order
class ImageDecoder {
public:
    // The telemetry parameter is additive and defaulted, so every existing
    // caller keeps compiling and keeps its current behaviour untouched.
    bool decode(const std::string& path,int width,int height,GrayImage& out, DecodeTelemetry* tel=nullptr) const;
    bool decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out, DecodeTelemetry* tel=nullptr) const;
    // I-2: one shared WIC source (factory, decoder, frame, EXIF orientation)
    // producing both the fixed-size and the aspect output, with two independent
    // scaler/converter/CopyPixels chains and no intermediate GrayImage.
    // Fails if either output would have failed, exactly like the two calls.
    // Only the caller that needs both at once should use this; single-output
    // callers keep using decode()/decodePreserveAspect().
    bool decodeBoth(const std::string& path,int width,int height,int maxDimension,GrayImage& fixedOut, GrayImage& aspectOut, DecodeTelemetry* telFixed=nullptr, DecodeTelemetry* telAspect=nullptr) const;
    // Display path (previews): color via WIC BGRA. Fingerprint paths stay gray.
    bool decodeColorAspect(const std::string& path,int maxDimension,ColorImage& out) const;
    // Header-only dimensions: PNG IHDR + JPEG SOF parsed from the first bytes,
    // no decode, no process spawn. Lets the GUI show resolutions without
    // paying an ffprobe child per file (console flash + ~100ms on the UI
    // thread). Returns false for anything else (caller falls back).
    bool dimensionsFast(const std::string& path,int& w,int& h) const;
    // D2 measurement-only probe. Compares the three WIC decoder entry points
    // on one file. It is deliberately NOT a mode of decode(): production has no
    // way to reach it, so nothing measured here can change product behaviour.
    static void probeDecoderPaths(const std::string& path, DecoderPathProbe& out);
};
}
