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
