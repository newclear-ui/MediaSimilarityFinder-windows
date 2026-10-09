#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct VideoFrame { double timestamp=0; int width=0,height=0; std::vector<std::uint8_t> gray; };
struct ColorFrame { double timestamp=0; int width=0,height=0; std::vector<std::uint8_t> rgb; }; // 3 bytes/px, R,G,B order
struct VideoInfo { double duration=0; int width=0,height=0; double fps=0; };
// E-3B: executor-side counters. Kept separate from planner reasoning so the two
// can be reported independently, and so a rejected candidate is visible.
struct SparseSeekStats {
    long long seeks=0, seekFailures=0;
    long long decoded=0, emitted=0;
    long long landingViolations=0;  // first decoded frame later than requested
    double seekMs=0, decodeMs=0, convertMs=0, totalMs=0;
};
class VideoDecoder {
public:
    bool open(const std::string& path);
    bool info(VideoInfo& out) const;
    bool frameAt(double seconds,int width,int height,VideoFrame& out);
    // Decode multiple monotonically increasing timestamps from a single seek/decode pass.
    // cancel is polled between frames: a Stop landing mid-sweep winds down
    // promptly instead of decoding to the last timestamp (0.9.4.78). Null = run to the end.
    bool framesAt(const std::vector<double>& seconds,int width,int height,std::vector<VideoFrame>& out,
                  const std::atomic<bool>* cancel=nullptr);
    // Single-sweep fingerprint decode: one 96x96 pass plus software-derived
    // 32x32 frames (exact 3x3 box mean, same timestamps, 1:1 aligned). Halves
    // the full-file software decodes per fingerprint build (decode-bound 4K
    // files spent ~50% of build time on the second sweep). Derived 32px
    // frames are low-frequency equivalent for pHash (see parity test).
    bool framesAt96Plus32(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32,
                          const std::atomic<bool>* cancel=nullptr);
    // Display path (previews): single frame converted to RGB24 instead of gray.
    // Fingerprint paths stay gray. Returns false without linked FFmpeg.
    bool frameAtColor(double seconds,int width,int height,ColorFrame& out);
    // E-3B executor: exact sparse seek, one seek per sample target.
    //
    // CONTRACT, which the planner depends on and which the E-3B brief requires:
    // this returns true only when the result is provably the same sample set the
    // production sequential sweep would have produced. Concretely the product
    // predicate (ft + 0.05 >= target) selects the first frame with
    // pts >= target - 0.05, so that frame is only reachable if the seek landed
    // at or before it. The landing invariant is therefore CHECKED here, and any
    // violation makes this return false rather than return a plausible but
    // different sample. That is what stops a fallback from being used to excuse
    // a pixel mismatch: a violated candidate is never produced at all.
    //
    // It never falls back internally. The caller decides, so a fallback is
    // visible in telemetry instead of being hidden inside the decoder.
    bool framesAt96Plus32ExactSparse(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32,SparseSeekStats* stats=nullptr);
    // E-3B: cheap facts the planner needs, computed from the already-open
    // format context. Neither decodes anything, and gopFramesFromIndex() reads
    // the container index rather than scanning packets, so planning stays far
    // cheaper than decoding. Returns <= 0 when the GOP cannot be established,
    // which the planner treats as GopUnavailable and therefore as sequential.
    std::string codecName() const;
    double gopFramesFromIndex() const;
    void close();
private:
#ifdef MSF_HAS_FFMPEG
    void* fmt_=nullptr; void* codec_=nullptr; void* frame_=nullptr; void* sws_=nullptr;
    int stream_=-1;
#endif
    std::string path_;
    VideoInfo info_;
};
}
