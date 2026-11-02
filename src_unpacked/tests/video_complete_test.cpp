// 0.9.4.158 P0/P1 regression: cancelled, truncated, or otherwise
// incomplete sweeps must never produce or cache a fingerprint.
// - pre-cancelled build fails and leaves no memory/disk trace
// - async mid-sweep cancel fails and leaves no disk row
// - truncated (EOF-incomplete) file fails and leaves no disk row;
//   decoder-level framesAt() also fails with empty output
// - VideoSimilarityStats submitted/completed accounting (P2 split)
#include "video_fingerprint.h"
#include "gpu_backend.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace fs = std::filesystem;

static int gen(const std::string& out, const std::string& size, int dur) {
    const std::string c = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi"
        " -i testsrc=size=") + size + ":rate=10:duration=" + std::to_string(dur) +
        " -c:v mpeg4 -pix_fmt yuv420p -movflags +faststart \"" + out + "\"";
    return std::system(c.c_str());
}

// Heavy fixture for the mid-sweep race: 4K high-entropy content (noise
// defeats temporal compression, and every frame pays full 4K decode +
// downscale cost) so the sweep genuinely takes seconds. Smaller/flatter
// fixtures decode in ~0.1-0.2 s and cannot lose the race.
static int genNoisy(const std::string& out) {
    const std::string c = std::string("ffmpeg -hide_banner -loglevel error -y -f lavfi"
        " -i testsrc=size=3840x2160:rate=10:duration=20"
        " -vf noise=alls=20:allf=t -c:v mpeg4 -pix_fmt yuv420p -b:v 20M"
        " -movflags +faststart \"") + out + "\"";
    return std::system(c.c_str());
}

static bool diskHit(msf::VideoFingerprintEngine& e, const std::string& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    const auto sz = fs::file_size(p, ec); if (ec) return false;
    const auto mt = (std::uint64_t)fs::last_write_time(p, ec).time_since_epoch().count(); if (ec) return false;
    msf::VideoFingerprint o;
    return e.loadPersistent(p, sz, mt, o, nullptr);
}

int main() {
    std::error_code ec;
    // AGENTS.md item 12: scratch stays under D:\Temp\OpenCodeWork, never C:.
    const auto d = fs::path("D:/Temp/OpenCodeWork/msf_video_complete");
    fs::remove_all(d, ec);
    fs::create_directories(d, ec);
    const std::string clip = (d / "clip.mp4").string();
    if (gen(clip, "160x90", 8) != 0 || !fs::exists(clip, ec)) {
        std::cerr << "fixture generation failed (environment)\n";
        return 2;
    }
    // Heavy clip for the mid-sweep cancel: large enough that the sweep
    // outlasts the delayed cancel below (timing verified at runtime).
    const std::string big = (d / "big.mp4").string();
    if (genNoisy(big) != 0 || !fs::exists(big, ec)) {
        std::cerr << "big fixture generation failed (environment)\n";
        return 2;
    }
    // Truncated copy: header/moov intact (faststart), tail cut -> open may
    // succeed but the sweep can never complete.
    const std::string trunc = (d / "trunc.mp4").string();
    {
        std::ifstream in(clip, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() < 1024) { std::cerr << "fixture too small\n"; return 2; }
        std::ofstream out(trunc, std::ios::binary);
        out.write(bytes.data(), (std::streamsize)(bytes.size() * 8 / 10));
    }

    msf::VideoFingerprintEngine e;
    const std::string db = (d / "cache.sqlite").string();
    if (!e.openPersistentCache(db)) { std::cerr << "cache open failed\n"; return 3; }

    // 1. Baseline: uncancelled build succeeds.
    {
        msf::VideoFingerprint vf;
        msf::VideoBuildStats st;
        if (!e.build(clip, vf, nullptr, nullptr, &st)) { std::cerr << "baseline build failed\n"; return 4; }
        if (vf.hashes.empty()) { std::cerr << "baseline empty\n"; return 4; }
        if (!diskHit(e, clip)) { std::cerr << "baseline not cached\n"; return 4; }
        // 0.9.4.161 (P1): the decode-stage split is populated, not zero.
        // 0.9.4.164 (P2): clean decodes record zero errors.
        // 0.9.4.166 (P1): clean decodes record zero NOPTS fallbacks.
        if (!(st.decodeMs > 0.0)) { std::cerr << "decodeMs not measured\n"; return 4; }
        if (!(st.phashCpuMs >= 0.0)) { std::cerr << "phashCpuMs missing\n"; return 4; }
        if (st.decodeErrors != 0) { std::cerr << "clean decode reported errors\n"; return 4; }
        if (st.noptsFrames != 0) { std::cerr << "clean decode used NOPTS fallback\n"; return 4; }
        if (st.cacheHit) { std::cerr << "fresh prime unexpectedly hit\n"; return 4; }
        std::cout << "  [ok] baseline succeeds and caches\n";
    }
    const std::size_t memBase = e.memoryCacheSize();

    // 2. Pre-cancelled build on a fresh path: fails, no memory or disk trace.
    const std::string clip2 = (d / "clip2.mp4").string();
    fs::copy_file(clip, clip2, ec); if (ec) return 2;
    {
        msf::VideoFingerprint vf;
        std::atomic<bool> cancelled{true};
        if (e.build(clip2, vf, nullptr, nullptr, nullptr, &cancelled)) {
            std::cerr << "cancelled build unexpectedly succeeded\n"; return 5;
        }
        if (e.memoryCacheSize() != memBase) { std::cerr << "cancelled build polluted memory cache\n"; return 5; }
        if (diskHit(e, clip2)) { std::cerr << "cancelled build polluted disk cache\n"; return 5; }
        std::cout << "  [ok] pre-cancelled build fails with no cache trace\n";
    }

    // 3. Async mid-sweep cancel on a fresh heavy path.
    const std::string big2 = (d / "big2.mp4").string();
    fs::copy_file(big, big2, ec); if (ec) return 2;
    {
        // Measure the uncancelled cost once so the cancel delay below lands
        // mid-sweep deterministically instead of racing completion.
        msf::VideoFingerprint probe;
        const auto t0 = std::chrono::steady_clock::now();
        if (!e.build(big, probe)) { std::cerr << "big baseline failed\n"; return 6; }
        const double fullMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        std::cout << "  [info] big build=" << (long long)fullMs << "ms\n";
        if (fullMs < 800.0) { std::cerr << "big fixture too fast for a mid-sweep race\n"; return 6; }
        e.clearCache();

        msf::VideoFingerprint vf;
        std::atomic<bool> cancelled{false};
        bool ok = true;
        std::thread w([&] { ok = e.build(big2, vf, nullptr, nullptr, nullptr, &cancelled); });
        std::this_thread::sleep_for(std::chrono::milliseconds((long long)(fullMs / 4)));
        cancelled.store(true, std::memory_order_relaxed);
        w.join();
        if (ok) { std::cerr << "mid-sweep cancel did not stop the build\n"; return 7; }
        if (diskHit(e, big2)) { std::cerr << "mid-sweep cancel polluted disk cache\n"; return 7; }
        // Engine stays usable after the abort.
        msf::VideoFingerprint vf2;
        if (!e.build(big2, vf2) || vf2.hashes.empty()) { std::cerr << "rebuild after cancel failed\n"; return 7; }
        std::cout << "  [ok] mid-sweep cancel fails with no cache trace\n";
    }

    // 4. Truncated file: build fails, nothing cached; decoder-level
    // completeness also fails with empty output when the file opens.
    {
        msf::VideoFingerprint vf;
        if (e.build(trunc, vf)) { std::cerr << "truncated build unexpectedly succeeded\n"; return 8; }
        if (diskHit(e, trunc)) { std::cerr << "truncated build polluted disk cache\n"; return 8; }
        msf::VideoDecoder dec;
        if (dec.open(trunc)) {
            msf::VideoInfo info;
            if (dec.info(info) && info.duration > 0) {
                // Mirror the engine plan shape (uniform 5 s sweep + exact
                // duration endpoint) so the endpoint target is exercised.
                std::vector<double> ts;
                for (double t = 0; t < info.duration; t += 5.0) ts.push_back(t);
                if (ts.empty() || ts.back() < info.duration - 0.001) ts.push_back(info.duration);
                std::vector<msf::VideoFrame> out;
                if (dec.framesAt(ts, 32, 32, out)) {
                    std::cerr << "truncated framesAt unexpectedly succeeded\n"; return 8;
                }
                if (!out.empty()) { std::cerr << "truncated framesAt left partial output\n"; return 8; }
            }
            dec.close();
        }
        std::cout << "  [ok] truncated file fails with no cache trace\n";
    }

    // 5. P2 stats split: offered (submitted/legacy pairs) vs GPU-executed.
    {
        msf::GpuBackend gpu;
        const bool available = gpu.available();
        constexpr int kT = msf::VideoFingerprint::kThumbSize;
        std::vector<std::uint8_t> a((std::size_t)kT * kT);
        for (int y = 0; y < kT; ++y) for (int x = 0; x < kT; ++x)
            a[(std::size_t)y * kT + x] = (std::uint8_t)((x * 7 + y * 13) & 0xFF);
        msf::VideoFingerprint fa, fb;
        fa.timestamps.resize(8); fb.timestamps.resize(8);
        for (int i = 0; i < 8; ++i) {
            fa.hashes.push_back(0x1111111111111111ULL + (std::uint64_t)i);
            fb.hashes.push_back(fa.hashes.back());
            fa.mirrorHashes.push_back(0); fb.mirrorHashes.push_back(0);
            fa.thumb48.insert(fa.thumb48.end(), a.begin(), a.end());
            fb.thumb48.insert(fb.thumb48.end(), a.begin(), a.end());
        }
        msf::VideoSimilarityOptions o;
        o.thresholdPercent = 50.0;
        msf::VideoSimilarityStats st;
        o.gpu = available ? &gpu : nullptr;
        o.stats = &st;
        (void)msf::video_similarity(fa, fb, o);
        if (!available) {
            if (st.gpuSubmitted != 0 || st.gpuCompleted != 0 || st.gpuPairs != 0) {
                std::cerr << "cpu path touched gpu stats\n"; return 9;
            }
        } else {
            if (st.gpuSubmitted == 0 || st.gpuPairs != st.gpuSubmitted) {
                std::cerr << "submitted accounting wrong\n"; return 9;
            }
            if (st.gpuCompleted > st.gpuSubmitted) {
                std::cerr << "completed exceeds submitted\n"; return 9;
            }
            if (st.gpuCompleted > 0 && !st.gpuUsed) {
                std::cerr << "completed without gpuUsed\n"; return 9;
            }
            if (st.gpuCompleted == 0 && !st.gpuFallback) {
                std::cerr << "all-fallback without gpuFallback\n"; return 9;
            }
        }
        std::cout << "  [ok] stats split submitted=" << st.gpuSubmitted
                  << " completed=" << st.gpuCompleted << " gpu=" << (available ? "yes" : "no") << "\n";
    }

    e.closePersistentCache();
    fs::remove_all(d, ec);
    std::cout << "video_complete=ok\n";
    return 0;
}
