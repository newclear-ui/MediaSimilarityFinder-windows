#pragma once
// E-3B Adaptive Sampling Planner ??CLASSIFICATION ONLY.
//
// This header deliberately contains no decoding, no FFmpeg calls and no I/O.
// It answers exactly one question from measured inputs: which sampling strategy
// should be used for this file? Execution lives in VideoDecoder, and the two are
// kept separate so E-3C and F can evolve them independently.
//
// Decision order, from the E-3B brief and never reordered:
//   1. correctness / capability gate   (CanExactSparseSeek)
//   2. confidence gate                  (GOP confidence, evidence)
//   3. estimated cost comparison
//   4. strategy selection
//
// A performance figure never outranks the question of whether exactness can be
// guaranteed at all.

#include <cstdint>
#include <string>

namespace msf {

// What the planner decided. Mirrors the three states the brief fixes; the
// executor maps each onto an actual strategy.
enum class SamplingDecision {
    SequentialPreferred,    // run the production sequential sweep
    SparseSeekCandidate,    // exact sparse seek is allowed and expected to pay
    SparseSeekUnavailable,  // no strategy is acceptable for this input
};

enum class SamplingReason {
    ExactSparseVerified,       // fast + exact, cost favourable
    CostNotAdvantageous,       // exactness fine, but sequential is cheaper
    ExactnessUnverified,       // the exactness contract cannot be established
    GOPUnknown,                // GOP confidence too low to estimate seek cost
    HEVCFallback,              // E-3A: no seek strategy reached exactness
    SparseUnsupported,         // codec cannot be decoded at all in this build
    PlannerDisabled,           // planner off; production default
    SequentialSafe,            // nothing indicated sparse was viable
};

const char* samplingDecisionName(SamplingDecision d);
const char* samplingReasonName(SamplingReason r);

enum class GopConfidence {
    Known,       // packet key flags and decoded I-frames agreed
    Estimated,   // one source only, or they disagreed
    Unavailable, // neither source is usable
};

const char* gopConfidenceName(GopConfidence c);

// E-3B verdict on the exactness contract.
//
// Sparse seek was NOT proven equal to the production sequential decode. E-2A and
// E-2B reported bit-identical results, but those runs compared one seek-based
// implementation against ANOTHER seek-based implementation. Both call
// av_seek_frame + avcodec_flush_buffers, so both lose the same decoder reference
// state and they agreed with each other for the wrong reason. E-3B is the first
// run that compares against the real from-zero production sweep, and there a
// 4K H.264 file diverged: the seek path reconstructs frames (reference count
// overflow, error concealment) that the from-zero sweep does not.
//
// The same run also showed sparse is about 17 % SLOWER end to end, so there is
// no performance argument left to weigh against an unproven correctness claim.
//
// RefuseAll is therefore the default. It is the absence of evidence, not a
// permanent rejection: flipping it to AllowVerified is a one-line change once a
// codec has a recorded production-parity proof.
enum class ExactnessPolicy { RefuseAll, AllowVerified };

// Set by the process owner only. Defaults to RefuseAll.
void setExactnessPolicy(ExactnessPolicy p);
ExactnessPolicy exactnessPolicy();

struct SamplingPlanInputs {
    // Raw inputs, kept to the set E-1 reduced to. Derived values are NOT
    // accepted here; the planner computes them so nothing derived can be fed
    // back in as if it were independent.
    std::string codec;
    double durationSec = 0;
    double fps = 0;
    int width = 0, height = 0;
    GopConfidence gopConfidence = GopConfidence::Unavailable;
    double gopFrames = 0;        // valid only when gopConfidence is Known
    std::size_t sampleCount = 0; // sample-plan size, itself derived from duration
};

struct SamplingPlanResult {
    SamplingDecision decision = SamplingDecision::SequentialPreferred;
    SamplingReason reason = SamplingReason::SequentialSafe;
    // Derived costs, recorded so a decision can be explained after the fact.
    double estimatedTotalFrames = 0;
    double estimatedSequentialWork = 0;
    double estimatedSparseWork = 0;
    double estimatedSeekCount = 0;
    const char* evidence = "none";  // EvidenceStrong / Limited / Missing
};

// The classifier. Pure: same inputs always give the same decision, and nothing
// here can change what any decoder does.
SamplingPlanResult planSampling(const SamplingPlanInputs& in);

// Capability gate, exposed separately because it must be answerable on its own:
// "can exact sparse seek be guaranteed for this input at all?" is a different
// question from "would it be cheaper". The brief requires the first to be
// evaluated first and never overridden by cost.
bool canExactSparseSeek(const SamplingPlanInputs& in);

}  // namespace msf
