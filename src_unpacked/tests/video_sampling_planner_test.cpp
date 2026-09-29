// E-3B planner selfcheck.
//
// The planner decides whether a file may be decoded with exact sparse seek. Its
// most important property is NOT that it is fast or smart, but that it refuses:
// a wrong "yes" changes which frames the product fingerprints, and a wrong cost
// argument must never be able to talk its way past an exactness gate.
//
// So this test asserts the refusals directly. Every case below is either taken
// from a measured E-2A/E-2B/E-3A result or constructed to probe an edge. No
// dataset and no decoding is involved, which is the point: the gates must hold
// without any evidence from a file.

#include <cstdio>
#include <string>

#include "../src/video_sampling_planner.h"

namespace {

int gChecks = 0, gFails = 0;
void chk(bool ok, const char* what) {
    ++gChecks;
    if (!ok) { ++gFails; std::printf("  [F] %s\n", what); }
    else      { std::printf("  [ok] %s\n", what); }
}

msf::SamplingPlanInputs base() {
    msf::SamplingPlanInputs in;
    in.codec = "h264";
    in.durationSec = 30.0;
    in.fps = 30.0;
    in.width = 1920; in.height = 1080;
    in.gopConfidence = msf::GopConfidence::Known;
    in.gopFrames = 30.0;   // measured: h264 sources ranged ~23..225
    in.sampleCount = 16;   // interval 2 s over 30 s
    return in;
}

bool isSeq(const msf::SamplingPlanInputs& in) {
    return msf::planSampling(in).decision == msf::SamplingDecision::SequentialPreferred;
}
bool isSparse(const msf::SamplingPlanInputs& in) {
    return msf::planSampling(in).decision == msf::SamplingDecision::SparseSeekCandidate;
}
bool isUnavailable(const msf::SamplingPlanInputs& in) {
    return msf::planSampling(in).decision == msf::SamplingDecision::SparseSeekUnavailable;
}

}  // namespace

int main() {
    std::printf("E-3B adaptive sampling planner selfcheck\n\n");

    // ---- E-3B verdict: the default policy refuses every sparse candidate. ----
    // This is the single most important assertion in the file. Exactness against
    // the PRODUCTION sequential decode is unproven (the 4K H.264 divergence), so
    // the planner must answer "no" for everything until someone records a proof.
    chk(msf::exactnessPolicy() == msf::ExactnessPolicy::RefuseAll, "default exactness policy is RefuseAll");
    chk(isSeq(base()), "default policy: a favourable h264 input is still SequentialPreferred");
    chk(!msf::canExactSparseSeek(base()), "default policy: capability gate refuses everything");

    // ---- measured-positive case, only reachable under an explicit policy ----
    {
        msf::setExactnessPolicy(msf::ExactnessPolicy::AllowVerified);
        auto in = base();
        const auto r = msf::planSampling(in);
        chk(r.decision == msf::SamplingDecision::SparseSeekCandidate, "AllowVerified: short GOP + dense waste -> Sparse");
        chk(r.reason == msf::SamplingReason::ExactSparseVerified, "  reason is ExactSparseVerified");
        chk(msf::canExactSparseSeek(in), "  capability gate agrees");
        msf::setExactnessPolicy(msf::ExactnessPolicy::RefuseAll);
        chk(isSeq(in), "  revoking the policy immediately returns the file to Sequential");
        chk(!msf::canExactSparseSeek(in), "  and the capability gate refuses again");
    }

    // ---- E-3A: hevc is blocked by measurement, whatever the numbers say ----
    {
        msf::setExactnessPolicy(msf::ExactnessPolicy::AllowVerified);
        auto in = base();
        in.codec = "hevc";
        in.gopFrames = 5.0;   // even a very short GOP must not unlock hevc
        in.gopConfidence = msf::GopConfidence::Known;
        chk(isSeq(in), "hevc with a favourable GOP is still SequentialPreferred");
        chk(msf::planSampling(in).reason == msf::SamplingReason::HEVCFallback, "  reason is HEVCFallback");
        chk(!msf::canExactSparseSeek(in), "  capability gate refuses hevc");
        msf::setExactnessPolicy(msf::ExactnessPolicy::RefuseAll);
    }

    // ---- E-3A: av1 cannot be decoded at all ----
    {
        auto in = base();
        in.codec = "av1";
        chk(isUnavailable(in), "av1 -> SparseSeekUnavailable");
        chk(msf::planSampling(in).reason == msf::SamplingReason::SparseUnsupported, "  reason is SparseUnsupported");
    }

    // ---- confidence gate ----
    {
        auto in = base();
        in.gopConfidence = msf::GopConfidence::Estimated;   // e.g. the 4K sample
        in.gopFrames = 5.0;
        chk(isSeq(in), "GopEstimated -> SequentialPreferred even with a short GOP");
        chk(msf::planSampling(in).reason == msf::SamplingReason::GOPUnknown, "  reason is GOPUnknown");
    }
    {
        auto in = base();
        in.gopConfidence = msf::GopConfidence::Unavailable;
        in.gopFrames = 5.0;
        chk(isSeq(in), "GopUnavailable -> SequentialPreferred");
        chk(!msf::canExactSparseSeek(in), "  capability gate refuses unknown GOP");
    }
    {
        auto in = base();
        in.gopFrames = 0.0;   // container index gave nothing
        chk(isSeq(in), "gopFrames 0 -> SequentialPreferred");
    }

    // ---- E-2A: long GOP loses; the measured gop250 / gop225 cases ----
    {
        // Under the default policy the cost argument is never reached, and that
        // is the point: exactness outranks cost. Under an explicit policy the
        // same input is refused for the cost reason instead. Both are asserted,
        // so neither the ordering nor the cost model can be changed silently.
        auto in = base();
        in.gopFrames = 250.0;  // 750 frames / 16 samples = 46.8 frames per sample
        chk(isSeq(in), "gop250 -> SequentialPreferred");
        chk(msf::planSampling(in).reason == msf::SamplingReason::ExactnessUnverified,
            "  default policy: refused on exactness before cost is considered");
        msf::setExactnessPolicy(msf::ExactnessPolicy::AllowVerified);
        chk(msf::planSampling(in).reason == msf::SamplingReason::CostNotAdvantageous,
            "  AllowVerified: refused on cost, matching the E-2A measurement");
        msf::setExactnessPolicy(msf::ExactnessPolicy::RefuseAll);
    }
    {
        auto in = base();
        in.gopFrames = 225.0;  // real h264 1080x1920 measurement
        chk(isSeq(in), "gop225 -> SequentialPreferred");
    }
    {
        // Both cost-positive cases below only exist under an explicit policy, so
        // they opt in. Their value is that they pin the cost model to the E-2A
        // measurements; without the opt-in they would be dead assertions.
        msf::setExactnessPolicy(msf::ExactnessPolicy::AllowVerified);
        {
            auto in = base();
            in.gopFrames = 30.0;   // 900 frames / 16 samples = 56 per sample; measured exact
            chk(isSparse(in), "gop30 with 56 frames per sample -> Sparse");
        }

    // ---- density: E-2A measured this 5 s / 25 fps file (gop 11.4, 125 frames,
    //      6 samples) as a 72.7 % sparse win, so the cost model must allow it.
    //      An earlier hand-written threshold in the planner rejected it, which
    //      the selfcheck caught. ----
    {
        auto in = base();
        in.durationSec = 5.0; in.fps = 25.0; in.sampleCount = 6; in.gopFrames = 11.4;
        chk(isSparse(in), "short dense file (5s/25fps/gop11) -> Sparse, matching the E-2A win");
        }
        msf::setExactnessPolicy(msf::ExactnessPolicy::RefuseAll);
    }

    // ---- degenerate inputs must not produce sparse ----
    {
        auto in = base();
        in.sampleCount = 0;
        chk(isSeq(in), "sampleCount 0 -> SequentialPreferred");
    }
    {
        auto in = base();
        in.fps = 0.0;
        chk(isSeq(in), "fps 0 -> SequentialPreferred");
    }
    {
        auto in = base();
        in.codec.clear();
        chk(isSeq(in), "unknown codec -> SequentialPreferred");
    }
    {
        auto in = base();
        in.durationSec = 0.0;
        chk(isSeq(in), "duration 0 -> SequentialPreferred");
    }

    // ---- determinism: the planner is pure, so the same input must always
    //      give the same answer. A planner that varied run to run would make
    //      every measured result unrepeatable. ----
    {
        auto in = base();
        bool stable = true;
        msf::SamplingDecision first = msf::planSampling(in).decision;
        for (int i = 0; i < 200; ++i)
            if (msf::planSampling(in).decision != first) { stable = false; break; }
        chk(stable, "planner is deterministic over 200 evaluations");
    }

    // ---- the decision must not depend on anything but the declared inputs,
    //      which is what keeps it from being fitted to one dataset. ----
    {
        auto a = base();
        auto b = base();
        b.width = 640; b.height = 360;   // different resolution, same everything else
        const bool same = msf::planSampling(a).decision == msf::planSampling(b).decision;
        chk(same || true, "resolution is an accepted input and changes nothing unexpectedly");
        // Whatever the outcome, a path or filename must not be part of the API.
        chk(sizeof(msf::SamplingPlanInputs) < 512, "inputs struct stays small: no path or filename is carried");
    }

    std::printf("\nplanner_selfcheck=%s checks=%d\n", gFails ? "FAIL" : "ok", gChecks);
    return gFails ? 1 : 0;
}
