#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct VideoFrame { double timestamp=0; int width=0,height=0; std::vector<std::uint8_t> gray; };
struct ColorFrame { double timestamp=0; int width=0,height=0; std::vector<std::uint8_t> rgb; }; // 3 bytes/px, R,G,B order
struct VideoInfo { double duration=0; int width=0,height=0; double fps=0; };
class VideoDecoder {
public:
    bool open(const std::string& path);
    bool info(VideoInfo& out) const;
    bool frameAt(double seconds,int width,int height,VideoFrame& out);
    // Decode multiple monotonically increasing timestamps from a single seek/decode pass.
    bool framesAt(const std::vector<double>& seconds,int width,int height,std::vector<VideoFrame>& out);
    // Single-sweep fingerprint decode: one 96x96 pass plus software-derived
    // 32x32 frames (exact 3x3 box mean, same timestamps, 1:1 aligned). Halves
    // the full-file software decodes per fingerprint build (decode-bound 4K
    // files spent ~50% of build time on the second sweep). Derived 32px
    // frames are low-frequency equivalent for pHash (see parity test).
    bool framesAt96Plus32(const std::vector<double>& seconds,std::vector<VideoFrame>& out96,std::vector<VideoFrame>& out32);
    // Display path (previews): single frame converted to RGB24 instead of gray.
    // Fingerprint paths stay gray. Returns false without linked FFmpeg.
    bool frameAtColor(double seconds,int width,int height,ColorFrame& out);
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
