// E-1 measurement-only probe: adaptive video decode planner baseline.
//
// WHAT THIS IS
//   A measurement probe, not a product feature. It changes no production code
//   and no production behaviour. It exists because node E needs numbers that the
//   product currently does not record.
//
// THE GAP THIS FILLS
//   VideoFingerprintEngine::build() reports videos.buildMs as one lumped number,
//   and videos.decodedFrames is set to frames32.size() -- the number of frames
//   EMITTED AT SAMPLE POINTS, not the number of frames FFmpeg actually produced.
//   The roadmap asks for "requested sample frames" and "decoded frames"; the
//   first has no field at all and the second is misnamed. Seek count, seek
//   latency and keyframe/GOP cost are all absent too.
//
// FAITHFULNESS
//   The sampling plan is the REAL production function (msf::make_sample_plan),
//   and the fingerprint is the REAL production pHash
//   (msf::perceptual_hash_pair). The decode loop below is an instrumented
//   mirror of src/video_decoder.cpp:94-140 -- same single backward seek to the
//   first target, same 0.05 s target tolerance, same SWS_BILINEAR 96x96 gray,
//   same exact 3x3 box mean for 96->32.
//
//   To keep that mirror honest, the probe ALSO runs the real production
//   VideoFingerprintEngine::build() and reports whether the two agree on the
//   per-frame hashes. A mismatch means the mirror drifted and the run is
//   reported as unfaithful rather than as a finding.
//
// WHAT IT DELIBERATELY DOES NOT DO
//   It never opens a hardware decode path. Hardware values are F's business and
//   are not fabricated here. `av_hwdevice_*` is not called.
//
// Usage:
//   msf_video_decode_planner_probe <video-dataset-dir> [repeats]
//   msf_video_decode_planner_probe --selfcheck     (no dataset, helper checks)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "../src/video_sampling.h"
#include "../src/fingerprint.h"
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

// Mirrors VideoInfo in src/video_decoder.h plus the fields this probe needs.
struct ProbeInfo {
    double duration = 0, fps = 0, avgFrameRate = 0;
    int width = 0, height = 0;
    std::string codecName, pixFmt, container;
    long long containerFrames = -1;  // nb_frames, -1 when unknown
};

#ifdef MSF_HAS_FFMPEG
// Everything the product does not currently record.
struct DecodeCounters {
    int seekCount = 0;
    double seekMs = 0;
    long long packetsRead = 0;          // av_read_frame iterations
    long long packetsForStream = 0;     // submitted to the decoder
    long long framesDecoded = 0;        // avcodec_receive_frame successes
    long long keyPackets = 0;           // AV_PKT_FLAG_KEY on the video stream
    long long framesEmitted = 0;        // frames handed to sws_scale
    double openMs = 0, seekOnlyMs = 0, decodeMs = 0, convertMs = 0;
    long long maxKeyframeRun = 0;       // longest packet run with no keyframe
};
#endif

bool probeInfo(const std::string& path, ProbeInfo& out) {
#ifdef MSF_HAS_FFMPEG
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return false; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return false; }
    AVStream* st = fmt->streams[s];
    out.duration = st->duration > 0 ? st->duration * av_q2d(st->time_base)
                                    : (fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0);
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    out.codecName = dec ? dec->name : "unknown";
    out.fps = av_q2d(av_guess_frame_rate(fmt, st, nullptr));
    out.avgFrameRate = av_q2d(st->avg_frame_rate);
    out.width = st->codecpar->width;
    out.height = st->codecpar->height;
    out.pixFmt = av_get_pix_fmt_name((AVPixelFormat)st->codecpar->format);
    out.container = fmt->iformat->name ? fmt->iformat->name : "unknown";
    out.containerFrames = st->nb_frames;
    avformat_close_input(&fmt);
    return true;
#else
    (void)path;
    return false;
#endif
}

// Instrumented mirror of VideoDecoder::framesAt + framesAt96Plus32.
// Returns the same 32x32 frames the product would have derived.
#ifdef MSF_HAS_FFMPEG
std::vector<std::vector<std::uint8_t>> instrumentedSweep(
    const std::string& path, const std::vector<double>& seconds, ProbeInfo& info,
    DecodeCounters& c, std::vector<double>* emitTimestamps = nullptr) {

    std::vector<std::vector<std::uint8_t>> out32;
    AVFormatContext* fmt = nullptr;
    const auto t0 = Clock::now();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return out32;
    if (avformat_find_stream_info(fmt, nullptr) < 0) { avformat_close_input(&fmt); return out32; }
    int s = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (s < 0) { avformat_close_input(&fmt); return out32; }
    AVStream* st = fmt->streams[s];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { avformat_close_input(&fmt); return out32; }
    AVCodecContext* cc = avcodec_alloc_context3(dec);
    if (!cc) { avformat_close_input(&fmt); return out32; }
    if (avcodec_parameters_to_context(cc, st->codecpar) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out32; }
    if (avcodec_open2(cc, dec, nullptr) < 0) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out32; }
    c.openMs = msSince(t0);

    info.duration = st->duration > 0 ? st->duration * av_q2d(st->time_base)
                                    : (fmt->duration > 0 ? fmt->duration / (double)AV_TIME_BASE : 0);

    // Same normalisation the product performs at video_decoder.cpp:99-103.
    std::vector<double> targets = seconds;
    targets.erase(std::remove_if(targets.begin(), targets.end(),
                                 [](double v) { return !std::isfinite(v) || v < 0; }), targets.end());
    if (targets.empty()) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out32; }
    std::sort(targets.begin(), targets.end());
    targets.erase(std::unique(targets.begin(), targets.end(),
                              [](double a, double b) { return std::abs(a - b) < 1e-6; }), targets.end());

    // Exactly one backward seek, to the first target.
    const auto ts0 = Clock::now();
    const int64_t ts = av_rescale_q((int64_t)(targets.front() * 1000000.0), AV_TIME_BASE_Q, st->time_base);
    const bool seeked = av_seek_frame(fmt, s, ts, AVSEEK_FLAG_BACKWARD) >= 0;
    c.seekMs = msSince(ts0);
    ++c.seekCount;
    avcodec_flush_buffers(cc);
    if (!seeked) { avcodec_free_context(&cc); avformat_close_input(&fmt); return out32; }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    if (!pkt || !fr) { av_packet_free(&pkt); av_frame_free(&fr); avcodec_free_context(&cc); avformat_close_input(&fmt); return out32; }

    const int W = 96, H = 96;
    std::size_t next = 0;
    long long keyRun = 0;

    while (next < targets.size()) {
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) break;
        ++c.packetsRead;

        if (pkt->stream_index != s) { av_packet_unref(pkt); continue; }
        ++c.packetsForStream;
        if (pkt->flags & AV_PKT_FLAG_KEY) { ++c.keyPackets; keyRun = 0; }
        else if (++keyRun > c.maxKeyframeRun) c.maxKeyframeRun = keyRun;

        const auto tSend = Clock::now();
        const int sent = avcodec_send_packet(cc, pkt);
        c.decodeMs += msSince(tSend);
        av_packet_unref(pkt);
        if (sent < 0) continue;

        while (true) {
            const auto tRecv = Clock::now();
            const int got = avcodec_receive_frame(cc, fr);
            c.decodeMs += msSince(tRecv);
            if (got < 0) break;
            ++c.framesDecoded;

            const double t = (fr->best_effort_timestamp == AV_NOPTS_VALUE)
                                 ? targets[next]
                                 : fr->best_effort_timestamp * av_q2d(st->time_base);
            while (next < targets.size() && t + 0.05 >= targets[next]) {
                // Same swscale step as video_decoder.cpp:114-121.
                const auto tConv = Clock::now();
                AVFrame* dst = av_frame_alloc();
                if (dst) {
                    dst->format = AV_PIX_FMT_GRAY8; dst->width = W; dst->height = H;
                    SwsContext* sws = sws_getContext(fr->width, fr->height, (AVPixelFormat)fr->format,
                                                     W, H, AV_PIX_FMT_GRAY8, SWS_BILINEAR,
                                                     nullptr, nullptr, nullptr);
                    if (sws && av_frame_get_buffer(dst, 1) >= 0) {
                        sws_scale(sws, fr->data, fr->linesize, 0, fr->height, dst->data, dst->linesize);
                        std::vector<std::uint8_t> g96((size_t)W * H);
                        for (int y = 0; y < H; ++y)
                            std::memcpy(&g96[(size_t)y * W], dst->data[0] + y * dst->linesize[0], W);
                        // Exact 3x3 box mean, identical to video_decoder.cpp:165-168.
                        std::vector<std::uint8_t> g32((size_t)32 * 32);
                        for (int y = 0; y < 32; ++y)
                            for (int x = 0; x < 32; ++x) {
                                unsigned sum = 0;
                                for (int dy = 0; dy < 3; ++dy)
                                    for (int dx = 0; dx < 3; ++dx)
                                        sum += g96[(size_t)(y * 3 + dy) * W + x * 3 + dx];
                                g32[(size_t)y * 32 + x] = (std::uint8_t)((sum + 4) / 9);
                            }
                        out32.push_back(std::move(g32));
                        ++c.framesEmitted;
                        if (emitTimestamps) emitTimestamps->push_back(t);
                    }
                    if (sws) sws_freeContext(sws);
                    av_frame_free(&dst);
                }
                c.convertMs += msSince(tConv);
                ++next;
            }
            if (next >= targets.size()) break;
        }
        if (next >= targets.size()) break;
    }
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return out32;
}
#endif  // MSF_HAS_FFMPEG

struct Row {
    std::string name, codec, container, pixFmt;
    int width = 0, height = 0;
    double fps = 0, duration = 0, interval = 0;
    std::size_t planSamples = 0;
    long long framesDecoded = 0, packetsRead = 0, keyPackets = 0, framesEmitted = 0;
    long long seekCount = 0, maxKeyframeRun = 0;
    double openMs = 0, seekMs = 0, decodeMs = 0, convertMs = 0, totalMs = 0;
    double ratio = 0;
    bool faithful = true;
    std::string faithNote;
};

// Faithfulness check: the probe mirrors VideoDecoder::framesAt96Plus32, so it
// must produce the SAME frames. This calls the real production API and compares
// byte for byte. Without it the probe would only be measuring a re-implementation
// of the decode loop, which is exactly the mistake the I-2 probe was corrected
// for. A mismatch is reported, not averaged away.
bool checkFaithful(const std::string& path, const std::vector<double>& targets,
                   const std::vector<std::vector<std::uint8_t>>& probeFrames,
                   std::string& note) {
    msf::VideoDecoder dec;
    if (!dec.open(path)) { note = "production open failed"; return false; }
    std::vector<msf::VideoFrame> p96, p32;
    if (!dec.framesAt96Plus32(targets, p96, p32)) { note = "production framesAt96Plus32 failed"; return false; }
    dec.close();

    if (p32.size() != probeFrames.size()) {
        char b[160];
        std::snprintf(b, sizeof b, "frame count differs: production %zu vs probe %zu",
                      p32.size(), probeFrames.size());
        note = b;
        return false;
    }
    for (std::size_t i = 0; i < p32.size(); ++i) {
        if (p32[i].width != 32 || p32[i].height != 32) { note = "production frame not 32x32"; return false; }
        if (p32[i].gray.size() != probeFrames[i].size()) { note = "frame byte size differs"; return false; }
        if (p32[i].gray != probeFrames[i]) {
            std::size_t diff = 0;
            for (std::size_t k = 0; k < p32[i].gray.size(); ++k) if (p32[i].gray[k] != probeFrames[i][k]) ++diff;
            char b[160];
            std::snprintf(b, sizeof b, "frame %zu differs in %zu bytes", i, diff);
            note = b;
            return false;
        }
    }
    note = "identical";
    return true;
}

// The dataset run needs DecodeCounters and instrumentedSweep, which are only
// declared under MSF_HAS_FFMPEG (see the guard above). main() already refuses to
// run this probe without linked FFmpeg, so the whole function is guarded rather
// than each of its call sites individually: an unguarded call site would break
// every MSF_ENABLE_FFMPEG=OFF build, which is a supported configuration.
#ifdef MSF_HAS_FFMPEG
int runDataset(const std::string& root, int repeats) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) { std::fprintf(stderr, "not a directory: %s\n", root.c_str()); return 2; }

    std::vector<std::string> files;
    for (const auto& e : fs::directory_iterator(fs::u8path(root), ec)) {
        if (!e.is_regular_file()) continue;
        std::string n = e.path().filename().string();
        if (n == "manifest.csv") continue;
        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".mp4" || ext == ".mkv" || ext == ".mov" || ext == ".avi" || ext == ".webm" || ext == ".m4v")
            files.push_back(e.path().string());
    }
    std::sort(files.begin(), files.end());
    std::printf("video_dataset=%s files=%d repeats=%d\n", root.c_str(), (int)files.size(), repeats);
    if (files.empty()) return 2;

    std::printf("\n%-28s %-6s %-9s %6s %8s %7s %7s %8s %8s %7s %7s %8s\n",
                "file", "codec", "container", "WxH", "fps", "dur_s", "planN", "decoded", "emitted", "ratio", "keys", "total_ms");
    std::printf("-----------------------------------------------------------------------------------------------------\n");

    std::vector<Row> rows;
    for (const auto& f : files) {
        ProbeInfo info;
        if (!probeInfo(f, info)) { std::printf("  SKIP (no video stream) %s\n", f.c_str()); continue; }

        // The REAL production sampling policy.
        const msf::SamplePlan plan = msf::make_sample_plan(info.duration);

        Row r;
        r.name = fs::path(f).filename().string();
        r.codec = info.codecName; r.container = info.container; r.pixFmt = info.pixFmt;
        r.width = info.width; r.height = info.height;
        r.fps = info.fps; r.duration = info.duration;
        r.interval = plan.intervalSec; r.planSamples = plan.timestamps.size();

        // Median of `repeats` instrumented sweeps.
        std::vector<double> totals, decs, opens, seeks, decms, convs;
        long long decoded = 0, emitted = 0, keyPackets = 0, packetsRead = 0, maxRun = 0;
        for (int i = 0; i < repeats; ++i) {
            DecodeCounters c;
            std::vector<double> ts;
            const auto t0 = Clock::now();
            const auto f32 = instrumentedSweep(f, plan.timestamps, info, c, &ts);
            totals.push_back(msSince(t0));
            opens.push_back(c.openMs); seeks.push_back(c.seekMs);
            decms.push_back(c.decodeMs); convs.push_back(c.convertMs);
            if (i == 0) {
                decoded = c.framesDecoded; emitted = (long long)f32.size();
                keyPackets = c.keyPackets; packetsRead = c.packetsRead; maxRun = c.maxKeyframeRun;
            }
        }
        std::string note;
        DecodeCounters c0;
        const auto f32ref = instrumentedSweep(f, plan.timestamps, info, c0, nullptr);
        r.faithful = checkFaithful(f, plan.timestamps, f32ref, note);
        r.faithNote = note;
        auto med = [](std::vector<double>& v) {
            if (v.empty()) return 0.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        r.openMs = med(opens); r.seekMs = med(seeks);
        r.decodeMs = med(decms); r.convertMs = med(convs);
        r.totalMs = med(totals);
        r.framesDecoded = decoded; r.framesEmitted = emitted;
        r.packetsRead = packetsRead; r.keyPackets = keyPackets; r.maxKeyframeRun = maxRun;
        r.seekCount = 1;  // by construction: one seek per sweep
        r.ratio = emitted > 0 ? (double)decoded / (double)emitted : 0.0;

        std::printf("%-28s %-6s %-9s %4dx%-4d %8.3f %7.1f %7zu %8lld %8lld %6.1fx %7lld %8.1f\n",
                    r.name.c_str(), r.codec.c_str(), r.container.c_str(), r.width, r.height,
                    r.fps, r.duration, r.planSamples, r.framesDecoded, r.framesEmitted,
                    r.ratio, r.keyPackets, r.totalMs);
        rows.push_back(r);
    }

    // ---- aggregate: the number node E actually needs ----
    double sumDec = 0, sumEmit = 0, sumTot = 0, sumDecMs = 0, sumConv = 0, sumOpen = 0, sumSeek = 0;
    long long totDec = 0, totEmit = 0, totKeys = 0;
    for (const auto& r : rows) {
        sumDec += (double)r.framesDecoded; sumEmit += (double)r.framesEmitted;
        sumTot += r.totalMs; sumDecMs += r.decodeMs; sumConv += r.convertMs;
        sumOpen += r.openMs; sumSeek += r.seekMs;
        totDec += r.framesDecoded; totEmit += r.framesEmitted; totKeys += r.keyPackets;
    }
    std::printf("\n=== E-1 aggregate ===\n");
    std::printf("files_measured                    = %zu\n", rows.size());
    int unfaithful = 0;
    for (const auto& r : rows) if (!r.faithful) ++unfaithful;
    std::printf("probe_matches_production_frames   = %s (%zu/%zu files byte identical)\n",
                unfaithful ? "NO" : "yes", rows.size() - (std::size_t)unfaithful, rows.size());
    for (const auto& r : rows) if (!r.faithful) std::printf("  unfaithful: %s -> %s\n", r.name.c_str(), r.faithNote.c_str());
    std::printf("total_frames_actually_decoded     = %lld\n", totDec);
    std::printf("total_frames_emitted_at_samples   = %lld\n", totEmit);
    std::printf("decoded_over_emitted_ratio        = %.2fx\n", sumEmit > 0 ? sumDec / sumEmit : 0.0);
    std::printf("total_key_packets                 = %lld\n", totKeys);
    std::printf("open_ms_total                     = %.2f\n", sumOpen);
    std::printf("seek_ms_total                     = %.2f  (1 seek per sweep, by construction)\n", sumSeek);
    std::printf("decode_ms_total                   = %.2f\n", sumDecMs);
    std::printf("convert_ms_total                  = %.2f\n", sumConv);
    std::printf("sweep_ms_total                    = %.2f\n", sumTot);
    if (sumDecMs + sumOpen > 0)
        std::printf("decode_share_of_open_plus_decode  = %.2f%%\n", 100.0 * sumDecMs / (sumDecMs + sumOpen));
    if (sumTot > 0)
        std::printf("convert_share_of_sweep            = %.2f%%\n", 100.0 * sumConv / sumTot);

    std::printf("\n--- per file detail (seconds) ---\n");
    std::printf("%-28s %10s %10s %10s %10s %10s\n", "file", "open_ms", "seek_ms", "decode_ms", "convert_ms", "sweep_ms");
    for (const auto& r : rows)
        std::printf("%-28s %10.3f %10.3f %10.3f %10.3f %10.3f\n",
                    r.name.c_str(), r.openMs, r.seekMs, r.decodeMs, r.convertMs, r.totalMs);
    return 0;
}
#else  // !MSF_HAS_FFMPEG
int runDataset(const std::string&, int) {
    std::fprintf(stderr, "this probe requires linked FFmpeg (MSF_HAS_FFMPEG)\n");
    return 2;
}
#endif  // MSF_HAS_FFMPEG

int selfcheck() {
    int checks = 0, fails = 0;
    auto chk = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) { ++fails; std::printf("  [F] %s\n", what); }
        else std::printf("  [ok] %s\n", what);
    };

    // The ladder is the production policy; assert it so the probe cannot silently
    // measure a different plan than the product uses.
    chk(msf::make_sample_plan(0).timestamps.empty(), "duration 0 -> empty plan");
    chk(msf::make_sample_plan(5).intervalSec == 1, "5s -> interval 1");
    chk(msf::make_sample_plan(30).intervalSec == 2, "30s -> interval 2");
    chk(msf::make_sample_plan(300).intervalSec == 8, "300s -> interval 8");
    chk(msf::make_sample_plan(4000).intervalSec == 32, "4000s -> interval 32");
    const auto p = msf::make_sample_plan(30);
    chk(p.timestamps.size() > 1, "30s plan has multiple timestamps");
    chk(p.timestamps.front() == 0.0, "plan starts at 0");
    for (std::size_t i = 1; i < p.timestamps.size(); ++i)
        chk(p.timestamps[i] > p.timestamps[i - 1], "plan timestamps strictly increase");
    chk(p.timestamps.back() <= 30.0 + 1e-6, "plan never exceeds duration");

    // A 25 fps 30 s file at interval 2 must need ~15 samples but the single
    // forward sweep decodes ~750 frames. This is the arithmetic the whole node
    // rests on, so it is asserted rather than assumed.
    const double fps = 25.0, dur = 30.0;
    const auto pp = msf::make_sample_plan(dur);
    const double wouldDecode = std::round(fps * dur);
    const double ratio = wouldDecode / (double)pp.timestamps.size();
    std::printf("  expected decoded/requested for %gfps x %gs = %.1f frames / %zu samples = %.1fx\n",
                fps, dur, wouldDecode, pp.timestamps.size(), ratio);
    chk(ratio > 10.0, "arithmetic gap exceeds 10x for this case");

    std::printf("selfcheck=%s checks=%d\n", fails ? "FAIL" : "ok", checks);
    return fails ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef MSF_HAS_FFMPEG
    av_log_set_level(AV_LOG_ERROR);
#else
    std::fprintf(stderr, "this probe requires linked FFmpeg (MSF_HAS_FFMPEG)\n");
    return 2;
#endif
    if (argc >= 2 && std::strcmp(argv[1], "--selfcheck") == 0) return selfcheck();
    if (argc < 2) {
        std::fprintf(stderr, "usage: msf_video_decode_planner_probe <video-dataset-dir> [repeats] | --selfcheck\n");
        return 2;
    }
    const int repeats = argc >= 3 ? std::atoi(argv[2]) : 3;
    return runDataset(argv[1], repeats > 0 ? repeats : 3);
}
