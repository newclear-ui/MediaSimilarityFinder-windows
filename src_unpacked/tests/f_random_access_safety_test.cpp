// F-1 Random-Access Safety Contract ??selfcheck.
//
// PROBE ONLY. Nothing in production includes this file, and no production decode
// decision reads it. It exists so the F-1 contract is executable and testable
// before any NVDEC path is wired in (directive 짠32).
//
// The contract answers one question per stream: may this stream be decoded by the
// hardware backend, or must it fall back to CPU?
//
// The finding that shaped this design is in the header comment below: a stream
// starting at an IDR is NOT sufficient for safety. Two IDR-start H.264 fixtures
// mismatched NVDEC, one of them at every frame. So structural properties classify
// a stream as Unknown, and only a recorded exactness proof can make it Safe.
// Anything not proven is CPU ??never "probably fine".

#include <cstdio>
#include <string>

namespace f1 {

// ---------------------------------------------------------------------------
// Contract states (brief 짠3-1)
// ---------------------------------------------------------------------------
enum class RandomAccessSafety {
    Safe,      // safety established: random access is self-contained AND exactness proven
    Unsafe,    // known not to be usable: random access is not self-contained
    Unknown,   // not established. Treated exactly like Unsafe for policy purposes.
};

// ---------------------------------------------------------------------------
// Evidence available WITHOUT parsing SPS/PPS or writing a new parser
// (brief 짠3-2 forbids new parsers, so only values FFmpeg already exposes count)
// ---------------------------------------------------------------------------
struct RandomAccessEvidence {
    // Condition A: is the decode start point self-contained?
    // Whether the first packet presented for decode is a keyframe, i.e. whether
    // every reference picture needed from there on is inside the decode sequence.
    bool startIsKeyFrame = false;

    // Condition B: does the first decoded frame stay at or before the request?
    // Adapts the E-3A `firstDecodedPts <= seekRequestPts` condition. A stream that
    // cannot satisfy this cannot reproduce the production sampling predicate.
    bool firstDecodedAtOrBeforeRequest = true;

    // Condition C: can the production sampling predicate be reproduced?
    // `ft + 0.05 >= target` unchanged. Recorded, not re-implemented.
    bool samplingPredicateReproducible = true;

    // Hardware capability: can this build decode the codec in hardware at all?
    // Kept deliberately separate from safety (brief 짠7).
    bool hardwareCapability = false;

    // Condition D / brief 짠4: has a production-parity exactness proof been
    // recorded for this stream class (codec + container structure)?
    bool exactnessVerified = false;
};

inline const char* toString(RandomAccessSafety s) {
    switch (s) {
        case RandomAccessSafety::Safe:    return "Safe";
        case RandomAccessSafety::Unsafe:  return "Unsafe";
        case RandomAccessSafety::Unknown: return "Unknown";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// Classification
// ---------------------------------------------------------------------------
//
// Order is deliberate and must not be reordered:
//
//   1. capability   ??no hardware decode at all, so the question is moot
//   2. random access ??a non-self-contained start is a known-unsafe fact
//   3. exactness    ??self-contained start is NECESSARY but NOT SUFFICIENT
//
// Step 3 is the part that measurement forced. The first assumption was that
// "starts at a keyframe" implied "safe", and the 1360x808 H.264 fixture
// disproved it: IDR-start, and yet it mismatched NVDEC on every frame tested,
// including when decoding was restarted from a confirmed IDR. So a self-contained
// start with no recorded exactness proof yields Unknown, not Safe.
inline RandomAccessSafety classify(const RandomAccessEvidence& e) {
    if (!e.hardwareCapability) return RandomAccessSafety::Unknown;  // moot, treated as CPU
    if (!e.startIsKeyFrame)      return RandomAccessSafety::Unsafe;  // known-unsafe structure
    if (!e.firstDecodedAtOrBeforeRequest) return RandomAccessSafety::Unsafe;
    if (!e.samplingPredicateReproducible) return RandomAccessSafety::Unknown;
    if (!e.exactnessVerified)    return RandomAccessSafety::Unknown;  // necessary, not sufficient
    return RandomAccessSafety::Safe;
}

// ---------------------------------------------------------------------------
// Backend policy (brief 짠5, 짠24, 짠25)
// ---------------------------------------------------------------------------
//
// Unsafe and Unknown are collapsed on purpose: both go to CPU. A stream whose
// safety is merely unproven must not be sent to the hardware path, so there is
// no "optimistic Unknown" branch anywhere in this function.
inline bool useHardwareDecode(RandomAccessSafety s) {
    return s == RandomAccessSafety::Safe;
}

} // namespace f1

// ---------------------------------------------------------------------------
// Selfcheck (brief 짠30)
// ---------------------------------------------------------------------------
namespace {
int gChecks = 0, gFails = 0;
void chk(bool ok, const char* what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what); }
    else     { std::printf("  [ok] %s\n", what); }
}

f1::RandomAccessEvidence verifiedH264() {
    f1::RandomAccessEvidence e;
    e.startIsKeyFrame = true;
    e.firstDecodedAtOrBeforeRequest = true;
    e.samplingPredicateReproducible = true;
    e.hardwareCapability = true;
    e.exactnessVerified = true;
    return e;
}
} // namespace

int main() {
    std::printf("F-1 random-access safety contract selfcheck\n\n");

    // --- required case 1: H264 mid-GOP -> Unsafe -> CPU fallback -------------
    {
        auto e = verifiedH264();
        e.startIsKeyFrame = false;                 // measured: 23 orphan P-frames, first IDR at 23
        e.exactnessVerified = true;                // even WITH a proof it stays unsafe
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unsafe, "mid-GOP start -> Unsafe");
        chk(!f1::useHardwareDecode(s), "  Unsafe -> CPU fallback (no hardware decode)");
    }

    // --- required case 2: Confirmed-IDR decode -> Safe -> NVDEC candidate -----
    {
        auto e = verifiedH264();
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Safe, "confirmed IDR + exactness proof -> Safe");
        chk(f1::useHardwareDecode(s), "  Safe -> NVDEC candidate");
    }

    // --- required case 3/4: exactness gate is not bypassable ---------------
    // This is the case measurement forced. A self-contained start WITHOUT a
    // recorded exactness proof must NOT become Safe, because an IDR-start fixture
    // was measured to mismatch the hardware decoder on every frame.
    {
        auto e = verifiedH264();
        e.exactnessVerified = false;
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unknown, "IDR-start WITHOUT exactness proof -> Unknown (not Safe)");
        chk(!f1::useHardwareDecode(s), "  Unknown -> CPU fallback");
    }
    {
        auto e = verifiedH264();
        e.exactnessVerified = false;
        e.startIsKeyFrame = false;
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unsafe, "no keyframe and no proof -> Unsafe (structure wins over proof)");
    }

    // --- required case 5: unknown safety -> CPU fallback ---------------------
    {
        auto e = verifiedH264();
        e.hardwareCapability = false;              // capability gate precedes safety
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unknown, "no hardware capability -> Unknown");
        chk(!f1::useHardwareDecode(s), "  Unknown -> CPU fallback");
    }
    {
        auto e = verifiedH264();
        e.samplingPredicateReproducible = false;
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unknown, "predicate not reproducible -> Unknown -> CPU fallback");
        chk(!f1::useHardwareDecode(s), "  fallback taken");
    }

    // --- condition B: first decoded frame after the request point ----------
    {
        auto e = verifiedH264();
        e.firstDecodedAtOrBeforeRequest = false;
        const auto s = f1::classify(e);
        chk(s == f1::RandomAccessSafety::Unsafe, "firstDecodedPts > request point -> Unsafe -> CPU fallback");
    }

    // --- capability and safety are separate axes (brief 짠7, 짠29) -------------
    {
        auto capable = verifiedH264();
        auto incapable = verifiedH264();
        incapable.hardwareCapability = false;
        chk(f1::classify(capable) == f1::RandomAccessSafety::Safe,
            "capability and safety are independent: capable+proven -> Safe");
        chk(f1::classify(incapable) == f1::RandomAccessSafety::Unknown,
            "  incapable -> Unknown even with a proof (nothing to prove on)");
    }

    // --- Unsafe and Unknown are never optimistically merged -----------------
    {
        auto unsafe = verifiedH264();  unsafe.startIsKeyFrame = false;
        auto unknown = verifiedH264(); unknown.exactnessVerified = false;
        chk(!f1::useHardwareDecode(f1::classify(unsafe)), "policy: Unsafe never uses hardware");
        chk(!f1::useHardwareDecode(f1::classify(unknown)), "policy: Unknown never uses hardware");
    }

    // --- determinism: same evidence always yields the same decision ----------
    {
        auto e = verifiedH264(); e.exactnessVerified = false;
        bool stable = true;
        const auto first = f1::classify(e);
        for (int i = 0; i < 500; ++i) if (f1::classify(e) != first) { stable = false; break; }
        chk(stable, "classification is deterministic over 500 evaluations");
    }

    // --- naming sanity, so a mistake in a log is visible ---------------------
    chk(std::string(f1::toString(f1::RandomAccessSafety::Safe)) == "Safe", "state name Safe");
    chk(std::string(f1::toString(f1::RandomAccessSafety::Unsafe)) == "Unsafe", "state name Unsafe");
    chk(std::string(f1::toString(f1::RandomAccessSafety::Unknown)) == "Unknown", "state name Unknown");

    std::printf("\nf1_safety_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
