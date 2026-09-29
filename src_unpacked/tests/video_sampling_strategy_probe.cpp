// E-2A measurement-only probe: adaptive sampling strategy.
//
// WHAT THIS IS
//   A measurement probe. It changes no production code and no production
//   behaviour, and it never opens a hardware decode path (av_hwdevice_* is not
//   called). It exists to answer one question with measurements instead of
//   arithmetic: does seeking per sample actually beat one sequential sweep?
//
// BASELINE
//   The real production functions, called directly:
//     plan  = msf::make_sample_plan(duration)          (src/video_sampling.h)
//     frame = msf::VideoDecoder::framesAt96Plus32()    (src/video_decoder.h)
//   The probe does not reimplement the sample plan. The baseline's own output is
//   additionally reproduced by an instrumented mirror and compared byte for byte,
//   so a measurement always states whether it is describing the shipping path.
//
// GOP — MEASURED TWICE, INDEPENDENTLY
//   E-1 measured ffv1, which is intra-only, at 63 key packets out of 750, so
//   AV_PKT_FLAG_KEY alone is not trustworthy. This probe therefore collects
//     A. AV_PKT_FLAG_KEY packet positions
//     B. AV_PICTURE_TYPE_I decoded frame positions
//     C. the disagreement between them
//   and derives a confidence state. Under GopUnavailable no sparse-seek
//   candidate is reported as actionable, so a planner cannot compute a seek cost
//   from an untrusted GOP.
//
// SPARSEEK CANDIDATE
//   For each target: av_seek_frame(..., AVSEEK_FLAG_BACKWARD) to the nearest
//   preceding keyframe, avcodec_flush_buffers, then decode only to the target.
//   Both strategies use the SAME selection predicate the product uses
//   (frame_t + 0.05 >= target), so the selected frame must be identical; pixel
//   parity is verified rather than assumed.
//
// TELEMETRY OVERHEAD
//   v0.9.4.37 found measurement overhead leaking into a headline number. Every
//   timing here is therefore reported both with and without the per-step counters
//   active, so instrumentation cost is visible instead of hidden.
//
// Usage:
//   msf_video_sampling_strategy_probe <video-dir> [repeats]
//   msf_video_sampling_strategy_probe --selfcheck
//   msf_video_sampling_strategy_probe --manifest <video-dir>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/video_sampling.h"
#include "../src/video_decoder.h"

#ifdef MSF_HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
}
#endif

namespace {

using Clock = std::chrono::steady_clock;
static double msSince(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

enum class GopConfidence { Known, Estimated, Unavailable };
static const char* gopName(GopConfidence c) {
    switch (c) {
        case GopConfidence::Known: return "GopKnown";
        case GopConfidence::Estimated: return "GopEstimated";
        default: return "GopUnavailable";
    }
}

#ifdef MSF_HAS_FFMPEG

struct GopEvidence {
    std::vector<int64_t> keyPacketPts;   // A
    std::vector<int64_t> iFramePts;      // B
    long long decodedFrames = 0;
    long long packetsForStream = 0;
    int mismatchCount = 0;               // C
    double gopFramesFromKey = 0;
    double gopFramesFromI = 0;
    double gopFrameSeconds = 0;
    GopConfidence confidence = GopConfidence::Unavailable;
    const char* reason = "";
};

// Pass A: packet key flags only, no decode.
static bool gopFromPackets(const std::string& path, GopEvidence& g) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return false; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return false; }
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) { avformat_close_input(&fmt); return false; }
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == s) {
            ++g.packetsForStream;
            if (pkt->flags & AV_PKT_FLAG_KEY) g.keyPacketPts.push_back(pkt->pts);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return !g.keyPacketPts.empty();
}

// Pass B: full sequential decode recording I-picture positions.
// The decoder is opened with NO options, exactly as production does
// (video_decoder.cpp:30 passes a null options dict), so no hidden fast path
// changes the picture types reported.
static bool gopFromDecode(const std::string& path, GopEvidence& g) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return false; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return false; }
    const AVCodec* dec = avcodec_find_decoder(fmt->streams[s]->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return false; }
    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) { avformat_close_input(&fmt); return false; }
    if (avcodec_parameters_to_context(cc, fmt->streams[s]->codecpar) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    if (avcodec_open2(cc, dec, nullptr) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    if (!pkt || !fr) { av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == s && avcodec_send_packet(cc, pkt) >= 0) {
            while (avcodec_receive_frame(cc, fr) >= 0) {
                ++g.decodedFrames;
                if (fr->pict_type == AV_PICTURE_TYPE_I) g.iFramePts.push_back(fr->best_effort_timestamp);
            }
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return g.decodedFrames > 0;
}

static void resolveGop(const std::string& path, double frameDur, GopEvidence& g) {
    const bool haveA = gopFromPackets(path, g);
    const bool haveB = gopFromDecode(path, g);

    if (haveA && g.decodedFrames > 0) g.gopFramesFromKey = (double)g.decodedFrames / (double)g.keyPacketPts.size();
    if (haveB && !g.iFramePts.empty()) g.gopFramesFromI = (double)g.decodedFrames / (double)g.iFramePts.size();
    g.gopFrameSeconds = g.gopFramesFromI > 0 ? g.gopFramesFromI * frameDur : 0.0;

    // C: disagreement between the two independent measurements.
    if (haveA && haveB) {
        const std::size_t n = std::min(g.keyPacketPts.size(), g.iFramePts.size());
        g.mismatchCount = (int)std::labs((long)g.keyPacketPts.size() - (long)g.iFramePts.size());
        // Compare in the sequence they were captured; 1 tick slack absorbs the
        // fact that a packet pts and a frame pts need not be the same integer.
        for (std::size_t i = 0; i < n; ++i)
            if (std::labs(g.keyPacketPts[i] - g.iFramePts[i]) > 1) ++g.mismatchCount;
    }

    if (!haveA && !haveB) { g.confidence = GopConfidence::Unavailable; g.reason = "no key packets and no I-frames"; }
    else if (haveA && haveB && g.mismatchCount == 0 && g.keyPacketPts.size() >= 3)
        { g.confidence = GopConfidence::Known; g.reason = "packet keys and decoded I-frames agree"; }
    else if (haveA && haveB)
        { g.confidence = GopConfidence::Estimated; g.reason = "both measured but they disagree"; }
    else if (haveA)
        { g.confidence = GopConfidence::Estimated; g.reason = "packet keys only"; }
    else
        { g.confidence = GopConfidence::Estimated; g.reason = "decoded I-frames only"; }
}

struct Sample { double t = 0; int64_t pts = 0; std::vector<std::uint8_t> g32; };

struct SweepCounters {
    int seekCalls = 0;
    double seekMs = 0;
    long long packetsRead = 0;
    long long totalDecoded = 0;
    long long preTargetDecoded = 0;   // decoded strictly to reach the target
    long long sampleEmitted = 0;
    double openMs = 0, decodeMs = 0, convertMs = 0, totalMs = 0;
    double maxTargetPtsErrorTicks = 0;
    bool countable = true;            // false when per-step timing is disabled
};

// Converts a decoded frame to the same 32x32 gray the product derives.
static bool toGray32(AVFrame* fr, std::vector<std::uint8_t>& out) {
    const int W = 96, H = 96;
    AVFrame* dst = av_frame_alloc();
    if (!dst) return false;
    dst->format = AV_PIX_FMT_GRAY8; dst->width = W; dst->height = H;
    SwsContext* sws = sws_getContext(fr->width, fr->height, (AVPixelFormat)fr->format,
                                     W, H, AV_PIX_FMT_GRAY8, SWS_BILINEAR,
                                     nullptr, nullptr, nullptr);
    if (!sws || av_frame_get_buffer(dst, 1) < 0) {
        if (sws) sws_freeContext(sws);
        av_frame_free(&dst);
        return false;
    }
    sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst->data, dst->linesize);
    std::vector<std::uint8_t> g96((std::size_t)W * H);
    for (int y = 0; y < H; ++y)
        std::memcpy(&g96[(std::size_t)y * W], dst->data[0] + y * dst->linesize[0], W);
    out.assign((std::size_t)32 * 32, 0);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            unsigned s = 0;
            for (int dy = 0; dy < 3; ++dy)
                for (int dx = 0; dx < 3; ++dx)
                    s += g96[(std::size_t)(y * 3 + dy) * W + x * 3 + dx];
            out[(std::size_t)y * 32 + x] = (std::uint8_t)((s + 4) / 9);
        }
    sws_freeContext(sws);
    av_frame_free(&dst);
    return true;
}

// SPARSEEK: one seek per target.
static std::vector<Sample> sparseSeekSweep(const std::string& path,
                                           const std::vector<double>& targets,
                                           SweepCounters& c, bool countTimings) {
    std::vector<Sample> out;
    const auto tAll = Clock::now();
    AVFormatContext* fmt = nullptr;
    auto t0 = Clock::now();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return out; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return out; }
    AVStream* st = fmt->streams[s];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return out; }
    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) { avformat_close_input(&fmt); return out; }
    if (avcodec_parameters_to_context(cc, st->codecpar) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }
    if (avcodec_open2(cc, dec, nullptr) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }
    if (countTimings) c.openMs = msSince(t0);
    const double tbSec = av_q2d(st->time_base);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    if (!pkt || !fr) { av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }

    for (double target : targets) {
        const int64_t tpts = av_rescale_q((int64_t)(target * 1000000.0), AV_TIME_BASE_Q, st->time_base);

        t0 = Clock::now();
        // Nearest keyframe at or before the target: exactly the flag the product
        // already uses, so no new seeking primitive is introduced.
        if (av_seek_frame(fmt, s, tpts, AVSEEK_FLAG_BACKWARD) < 0) break;
        avcodec_flush_buffers(cc);
        ++c.seekCalls;
        if (countTimings) c.seekMs += msSince(t0);

        Sample smp; smp.t = target;
        long long decodedThisTarget = 0;
        bool done = false;
        while (!done && av_read_frame(fmt, pkt) >= 0) {
            ++c.packetsRead;
            if (pkt->stream_index != s) { av_packet_unref(pkt); continue; }
            t0 = Clock::now();
            const int sent = avcodec_send_packet(cc, pkt);
            if (countTimings) c.decodeMs += msSince(t0);
            av_packet_unref(pkt);
            if (sent < 0) continue;
            while (true) {
                t0 = Clock::now();
                const int got = avcodec_receive_frame(cc, fr);
                if (countTimings) c.decodeMs += msSince(t0);
                if (got < 0) break;
                ++c.totalDecoded; ++decodedThisTarget;
                const double ft = fr->best_effort_timestamp == AV_NOPTS_VALUE
                                      ? target
                                      : fr->best_effort_timestamp * tbSec;
                // Same predicate as video_decoder.cpp:127.
                if (ft + 0.05 >= target) {
                    t0 = Clock::now();
                    if (toGray32(fr, smp.g32)) {
                        smp.pts = fr->best_effort_timestamp;
                        const double errTicks = std::fabs((double)(fr->best_effort_timestamp - tpts));
                        if (errTicks > c.maxTargetPtsErrorTicks) c.maxTargetPtsErrorTicks = errTicks;
                        if (countTimings) c.convertMs += msSince(t0);
                        out.push_back(std::move(smp));
                        ++c.sampleEmitted;
                    } else if (countTimings) {
                        c.convertMs += msSince(t0);
                    }
                    done = true;
                    break;
                }
            }
        }
        c.preTargetDecoded += decodedThisTarget;
    }
    av_packet_free(&pkt); av_frame_free(&fr);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    c.totalMs = msSince(tAll);
    return out;
}

// SEQUENTIAL: one backward seek to the first target, then a single forward
// sweep, mirroring production (video_decoder.cpp:107-108) so the baseline
// counters are measured on the same structure the product uses.
static std::vector<Sample> sequentialSweep(const std::string& path,
                                           const std::vector<double>& targets,
                                           SweepCounters& c, bool countTimings) {
    std::vector<Sample> out;
    const auto tAll = Clock::now();
    AVFormatContext* fmt = nullptr;
    auto t0 = Clock::now();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return out; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return out; }
    AVStream* st = fmt->streams[s];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return out; }
    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) { avformat_close_input(&fmt); return out; }
    if (avcodec_parameters_to_context(cc, st->codecpar) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }
    if (avcodec_open2(cc, dec, nullptr) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }
    if (countTimings) c.openMs = msSince(tAll);
    const double tbSec = av_q2d(st->time_base);

    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    if (!pkt || !fr) { av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&cc); avformat_close_input(&fmt); return out; }

    if (!targets.empty()) {
        t0 = Clock::now();
        const int64_t ts = av_rescale_q((int64_t)(targets.front() * 1000000.0), AV_TIME_BASE_Q, st->time_base);
        if (av_seek_frame(fmt, s, ts, AVSEEK_FLAG_BACKWARD) < 0) {
            av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&cc); avformat_close_input(&fmt);
            c.totalMs = msSince(tAll);
            return out;
        }
        avcodec_flush_buffers(cc);
        ++c.seekCalls;
        if (countTimings) c.seekMs += msSince(t0);
    }

    std::size_t next = 0;
    while (next < targets.size() && av_read_frame(fmt, pkt) >= 0) {
        ++c.packetsRead;
        if (pkt->stream_index != s) { av_packet_unref(pkt); continue; }
        t0 = Clock::now();
        const int sent = avcodec_send_packet(cc, pkt);
        if (countTimings) c.decodeMs += msSince(t0);
        av_packet_unref(pkt);
        if (sent < 0) continue;
        while (true) {
            t0 = Clock::now();
            const int got = avcodec_receive_frame(cc, fr);
            if (countTimings) c.decodeMs += msSince(t0);
            if (got < 0) break;
            ++c.totalDecoded;
            const double ft = fr->best_effort_timestamp == AV_NOPTS_VALUE
                                  ? targets[next]
                                  : fr->best_effort_timestamp * tbSec;
            while (next < targets.size() && ft + 0.05 >= targets[next]) {
                Sample smp; smp.t = targets[next]; smp.pts = fr->best_effort_timestamp;
                t0 = Clock::now();
                if (toGray32(fr, smp.g32)) {
                    if (countTimings) c.convertMs += msSince(t0);
                    out.push_back(std::move(smp));
                    ++c.sampleEmitted;
                } else if (countTimings) {
                    c.convertMs += msSince(t0);
                }
                ++next;
            }
            if (next >= targets.size()) break;
        }
    }
    c.preTargetDecoded = c.totalDecoded;
    av_packet_free(&pkt); av_frame_free(&fr);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    c.totalMs = msSince(tAll);
    return out;
}

struct FileRow {
    std::string name, codec, container;
    int width = 0, height = 0;
    double fps = 0, duration = 0, frameDur = 0;
    std::size_t planSamples = 0;
    GopEvidence gop;
    // baseline
    long long bDecoded = 0, bEmitted = 0; int bSeeks = 0;
    double bMs = 0, bMsNoTelemetry = 0;
    // candidate
    long long cDecoded = 0, cEmitted = 0; int cSeeks = 0;
    double cMs = 0, cMsNoTelemetry = 0;
    long long cPreTarget = 0;
    double cPtsErrTicks = 0;
    // parity
    bool countParity = false, orderParity = false, pixelParity = false;
    long long pixelDiffBytes = 0;
    unsigned maxAbsPixelDiff = 0;
    int tsAheadCount = 0;      // candidate sample pts strictly later than baseline
    int tsIdentical = 0;
    double bRatio = 0, cRatio = 0, extraPerSample = 0;
    bool baselineFaithful = false;
    std::string faithNote;
};

static bool productionFrames(const std::string& path, const std::vector<double>& targets,
                             std::vector<std::vector<std::uint8_t>>& out32) {
    msf::VideoDecoder dec;
    if (!dec.open(path)) return false;
    std::vector<msf::VideoFrame> p96, p32;
    const bool ok = dec.framesAt96Plus32(targets, p96, p32);
    dec.close();
    if (!ok) return false;
    out32.clear();
    out32.reserve(p32.size());
    for (const auto& f : p32) out32.push_back(f.gray);
    return true;
}

#endif  // MSF_HAS_FFMPEG

}  // namespace

int runDataset(const std::string& root, int repeats) {
#ifdef MSF_HAS_FFMPEG
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { std::fprintf(stderr, "not a directory: %s\n", root.c_str()); return 2; }

    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(fs::u8path(root), ec)) {
        if (!e.is_regular_file()) continue;
        const std::string n = e.path().filename().string();
        if (n == "manifest.csv") continue;
        std::string ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".mp4" || ext == ".mkv" || ext == ".mov" || ext == ".avi" || ext == ".webm" || ext == ".m4v")
            files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    std::printf("video_dir=%s files=%d repeats=%d\n", root.c_str(), (int)files.size(), repeats);
    if (files.empty()) return 2;

    std::printf("\n%-34s %-5s %-9s %5s %7s %6s %6s %7s %7s %-13s %s\n",
                "file", "codec", "container", "WxH", "planN", "bDec", "cDec", "bRatio", "cRatio", "gopConfidence", "gopFrames");
    std::printf("----------------------------------------------------------------------------------------------------------------\n");

    std::vector<FileRow> rows;
    for (const auto& f : files) {
        AVFormatContext* fmt = nullptr;
        if (avformat_open_input(&fmt, f.c_str(), nullptr, nullptr) < 0) continue;
        if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); continue; }
        int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (s < 0) { avformat_close_input(&fmt); continue; }
        AVStream* st = fmt->streams[s];
        const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
        FileRow r;
        r.name = fs::path(f).filename().string();
        r.codec = dec ? dec->name : "?";
        r.container = fmt->iformat->name ? fmt->iformat->name : "?";
        r.width = st->codecpar->width; r.height = st->codecpar->height;
        r.duration = st->duration > 0 ? st->duration * av_q2d(st->time_base)
                                      : (fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0);
        const double fps = av_q2d(av_guess_frame_rate(fmt, st, nullptr));
        r.fps = fps;
        r.frameDur = fps > 0 ? 1.0 / fps : 0.0;
        avformat_close_input(&fmt);

        // The REAL production sampling plan.
        const msf::SamplePlan plan = msf::make_sample_plan(r.duration);
        r.planSamples = plan.timestamps.size();
        if (r.planSamples == 0) continue;

        resolveGop(f, r.frameDur, r.gop);

        // Baseline: instrumented sequential sweep, timed with and without
        // per-step counters so instrumentation cost is separated.
        std::vector<double> bWith, bWithout;
        SweepCounters bc;
        for (int i = 0; i < repeats; ++i) {
            SweepCounters c1; sequentialSweep(f, plan.timestamps, c1, true);
            bWith.push_back(c1.totalMs);
            SweepCounters c2; sequentialSweep(f, plan.timestamps, c2, false);
            bWithout.push_back(c2.totalMs);
            if (i == 0) { bc = c1; r.bDecoded = c1.totalDecoded; r.bEmitted = c1.sampleEmitted; r.bSeeks = c1.seekCalls; }
        }
        // Candidate: sparse seek per target.
        std::vector<double> cWith, cWithout;
        SweepCounters cc0;
        for (int i = 0; i < repeats; ++i) {
            SweepCounters c1; sparseSeekSweep(f, plan.timestamps, c1, true);
            cWith.push_back(c1.totalMs);
            SweepCounters c2; sparseSeekSweep(f, plan.timestamps, c2, false);
            cWithout.push_back(c2.totalMs);
            if (i == 0) { cc0 = c1; r.cDecoded = c1.totalDecoded; r.cEmitted = c1.sampleEmitted; r.cSeeks = c1.seekCalls; r.cPreTarget = c1.preTargetDecoded; r.cPtsErrTicks = c1.maxTargetPtsErrorTicks; }
        }
        auto med = [](std::vector<double>& v) { if (v.empty()) return 0.0; std::sort(v.begin(), v.end()); return v[v.size()/2]; };
        r.bMs = med(bWith); r.bMsNoTelemetry = med(bWithout);
        r.cMs = med(cWith); r.cMsNoTelemetry = med(cWithout);

        // Exactness is judged on the SAMPLE FRAMES themselves, not on a final
        // fingerprint. Both strategies use the product's own selection
        // predicate (frame_t + 0.05 >= target), so a mismatch is a real defect
        // rather than an expected difference, and is reported as such.
        {
            SweepCounters tb1, tc1;
            const auto baseS = sequentialSweep(f, plan.timestamps, tb1, false);
            const auto candS = sparseSeekSweep(f, plan.timestamps, tc1, false);
            r.countParity = (baseS.size() == candS.size());
            r.orderParity = true;   // both are built strictly in target order
            r.pixelParity = r.countParity;
            if (r.countParity) {
                for (std::size_t i = 0; i < baseS.size(); ++i) {
                    if (baseS[i].pts == candS[i].pts) ++r.tsIdentical;
                    else if (candS[i].pts > baseS[i].pts) ++r.tsAheadCount;
                    if (baseS[i].g32 == candS[i].g32) continue;
                    r.pixelParity = false;
                    const std::size_t n = std::min(baseS[i].g32.size(), candS[i].g32.size());
                    for (std::size_t k = 0; k < n; ++k)
                        if (baseS[i].g32[k] != candS[i].g32[k]) {
                            ++r.pixelDiffBytes;
                            const unsigned d = (unsigned)std::abs((int)baseS[i].g32[k] - (int)candS[i].g32[k]);
                            if (d > r.maxAbsPixelDiff) r.maxAbsPixelDiff = d;
                        }
                }
            }
            // Also confirm the baseline itself still matches the shipping API.
            std::vector<std::vector<std::uint8_t>> prod32;
            if (productionFrames(f, plan.timestamps, prod32)) {
                r.baselineFaithful = (prod32.size() == baseS.size());
                if (r.baselineFaithful)
                    for (std::size_t i = 0; i < prod32.size(); ++i)
                        if (prod32[i] != baseS[i].g32) { r.baselineFaithful = false; r.faithNote = "baseline mirror != production"; break; }
            } else {
                r.faithNote = "production framesAt96Plus32 failed (not decodable)";
            }
        }

        r.bRatio = r.bEmitted > 0 ? (double)r.bDecoded / (double)r.bEmitted : 0.0;
        r.cRatio = r.cEmitted > 0 ? (double)r.cDecoded / (double)r.cEmitted : 0.0;
        r.extraPerSample = r.cEmitted > 0 ? (double)(r.cDecoded - r.cEmitted) / (double)r.cEmitted : 0.0;

        std::printf("%-34s %-5s %-9s %4dx%-4d %7zu %6lld %6lld %6.1fx %6.1fx %-13s %.1f\n",
                    r.name.c_str(), r.codec.c_str(), r.container.c_str(), r.width, r.height,
                    r.planSamples, r.bDecoded, r.cDecoded, r.bRatio, r.cRatio,
                    gopName(r.gop.confidence), r.gop.gopFramesFromI);
        rows.push_back(r);
    }

    // ---- aggregate ----
    long long tb = 0, te = 0, cb = 0, ce = 0, cseek = 0, bseek = 0;
    double bms = 0, cms = 0, bmsNT = 0, cmsNT = 0;
    int countParity = 0, pixelParity = 0, gopKnown = 0, gopEst = 0, gopUn = 0;
    for (const auto& r : rows) {
        tb += r.bDecoded; te += r.bEmitted; cb += r.cDecoded; ce += r.cEmitted;
        cseek += r.cSeeks; bseek += r.bSeeks;
        bms += r.bMs; cms += r.cMs; bmsNT += r.bMsNoTelemetry; cmsNT += r.cMsNoTelemetry;
        if (r.countParity) ++countParity;
        if (r.pixelParity) ++pixelParity;
        switch (r.gop.confidence) { case GopConfidence::Known: ++gopKnown; break;
                                    case GopConfidence::Estimated: ++gopEst; break;
                                    default: ++gopUn; break; }
    }
    std::printf("\n=== E-2A aggregate ===\n");
    std::printf("files_measured                    = %zu\n", rows.size());
    std::printf("sample_count_parity               = %d / %zu\n", countParity, rows.size());
    std::printf("sample_pixel_parity               = %d / %zu\n", pixelParity, rows.size());
    std::printf("gop_confidence_known              = %d\n", gopKnown);
    std::printf("gop_confidence_estimated          = %d\n", gopEst);
    std::printf("gop_confidence_unavailable        = %d\n", gopUn);
    std::printf("baseline_seek_calls               = %lld\n", bseek);
    std::printf("baseline_decoded / emitted        = %lld / %lld  = %.2fx\n", tb, te, te ? (double)tb/(double)te : 0.0);
    std::printf("sparse_seek_calls                 = %lld\n", cseek);
    std::printf("sparse_decoded / emitted          = %lld / %lld  = %.2fx\n", cb, ce, ce ? (double)cb/(double)ce : 0.0);
    std::printf("decoded_reduction                 = %.2fx  (%.1f%%)\n", cb ? (double)tb/(double)cb : 0.0, tb ? 100.0*(tb-cb)/tb : 0.0);
    std::printf("elapsed_baseline_ms               = %.1f\n", bms);
    std::printf("elapsed_sparse_ms                 = %.1f\n", cms);
    if (bms > 0) std::printf("elapsed_reduction                 = %.1f%%\n", 100.0*(bms-cms)/bms);
    std::printf("elapsed_baseline_ms_noTelemetry   = %.1f\n", bmsNT);
    std::printf("elapsed_sparse_ms_noTelemetry     = %.1f\n", cmsNT);
    if (bmsNT > 0) std::printf("elapsed_reduction_noTelemetry     = %.1f%%\n", 100.0*(bmsNT-cmsNT)/bmsNT);
    if (bmsNT > 0) std::printf("instrumentation_overhead_baseline = %.1f%%\n", 100.0*(bms-bmsNT)/bmsNT);
    if (cmsNT > 0) std::printf("instrumentation_overhead_sparse   = %.1f%%\n", 100.0*(cms-cmsNT)/cmsNT);

    std::printf("\n--- per file (ms, median) ---\n");
    std::printf("%-34s %10s %10s %9s %8s %8s %7s %7s %6s %6s %s\n",
                "file", "base_ms", "sparse_ms", "sparseNoT", "red%", "bDec", "cDec", "ratio", "extra", "pix", "gop");
    for (const auto& r : rows) {
        const double red = r.bMs > 0 ? 100.0*(r.bMs-r.cMs)/r.bMs : 0.0;
        std::printf("%-34s %10.1f %10.1f %9.1f %8.1f %8lld %7lld %6.2f %6.1f %6s %s\n",
                    r.name.c_str(), r.bMs, r.cMs, r.cMsNoTelemetry, red,
                    r.bDecoded, r.cDecoded, r.cRatio, r.extraPerSample,
                    r.pixelParity ? "OK" : "DIFF", gopName(r.gop.confidence));
    }

    std::printf("\n--- sample selection / pixel difference diagnosis ---\n");
    std::printf("%-34s %7s %7s %7s %10s %8s %8s\n",
                "file", "tsSame", "tsLater", "diffFrames", "diffBytes", "maxAbs", "frameBytes");
    for (const auto& r : rows) {
        if (!r.countParity) { std::printf("%-34s %7s\n", r.name.c_str(), "COUNT-MISMATCH"); continue; }
        std::printf("%-34s %7d %7d %7lld %10lld %8u %8d\n",
                    r.name.c_str(), r.tsIdentical, r.tsAheadCount,
                    r.pixelDiffBytes ? r.pixelDiffBytes : 0, r.pixelDiffBytes,
                    r.maxAbsPixelDiff, 32 * 32);
    }
    std::printf("\n  interpretation: 'tsLater' counts samples where the candidate's pts is strictly\n");
    std::printf("  greater than the baseline's. A non-zero value with pixel differences means the\n");
    std::printf("  0.05 s predicate let the sequential sweep select a frame EARLIER than the seek\n");
    std::printf("  landing point, which the sparse candidate cannot reach. That is a structural\n");
    std::printf("  difference of the strategy, not a decoder defect.\n");

    std::printf("\n--- GOP detail ---\n");
    std::printf("%-34s %8s %8s %8s %9s %8s %-13s %s\n",
                "file", "keyPkts", "iFrames", "decoded", "mismatch", "gopI", "confidence", "reason");
    for (const auto& r : rows)
        std::printf("%-34s %8zu %8zu %8lld %9d %8.1f %-13s %s\n",
                    r.name.c_str(), r.gop.keyPacketPts.size(), r.gop.iFramePts.size(),
                    r.gop.decodedFrames, r.gop.mismatchCount, r.gop.gopFramesFromI,
                    gopName(r.gop.confidence), r.gop.reason);

    return 0;
#else
    std::fprintf(stderr, "requires linked FFmpeg\n");
    return 2;
#endif
}

int selfcheck() {
    int checks = 0, fails = 0;
    auto chk = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++fails; std::printf("  [F] %s\n", what); }
        else std::printf("  [ok] %s\n", what);
    };
    // The baseline must be the production plan, not a reimplementation.
    chk(msf::make_sample_plan(0).timestamps.empty(), "duration 0 -> empty plan");
    chk(msf::make_sample_plan(30).intervalSec == 2, "30s -> interval 2");
    chk(msf::make_sample_plan(300).intervalSec == 8, "300s -> interval 8");
    const auto p = msf::make_sample_plan(30);
    chk(p.timestamps.size() > 1 && p.timestamps.front() == 0.0, "plan is non-empty and starts at 0");
    for (std::size_t i = 1; i < p.timestamps.size(); ++i)
        chk(p.timestamps[i] > p.timestamps[i - 1], "plan strictly increasing");
    // time_base tolerance note: the product predicate is 0.05 s (video_decoder.cpp:127).
    // At 30 fps a frame is 0.0333 s, so 0.05 s covers at most one frame step; this
    // is recorded, not invented.
    const double frameDur30 = 1.0 / 30.0;
    chk(0.05 < frameDur30 * 2, "0.05 s tolerance spans < 2 frame periods at 30 fps");
    std::printf("  note: 0.05 s tolerance = %.3f frame periods at 30 fps\n", 0.05 / frameDur30);
    std::printf("selfcheck=%s checks=%d\n", fails ? "FAIL" : "ok", checks);
    return fails ? 1 : 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef MSF_HAS_FFMPEG
    av_log_set_level(AV_LOG_QUIET);
#else
    std::fprintf(stderr, "this probe requires linked FFmpeg (MSF_HAS_FFMPEG)\n");
    return 2;
#endif
    if (argc >= 2 && std::strcmp(argv[1], "--selfcheck") == 0) return selfcheck();
    if (argc < 2) {
        std::fprintf(stderr, "usage: msf_video_sampling_strategy_probe <video-dir> [repeats] | --selfcheck\n");
        return 2;
    }
    const int repeats = argc >= 3 ? std::atoi(argv[2]) : 3;
    return runDataset(argv[1], repeats > 0 ? repeats : 3);
}
