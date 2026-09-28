// D3-follow-up candidate measurement probe (I-decode-once-resize-twice).
//
// Measurement-only. Production decode paths (decode/decodePreserveAspect,
// verifyBuffersFor, verifyScorePlan) are only CALLED here, never modified,
// replaced, or reimplemented. No product behavior can change because of this
// file: it is a separate executable that reads the dataset and prints numbers.
//
// What it measures, per file and per shared resolution R in {128,192,256,384,512}:
//   baseline : decode(path,64,64)->f0  +  decodePreserveAspect(path,64)->a0
//   candidate: decodePreserveAspect(path,R)->shared,
//              then WIC-scale shared->f1(64x64) and shared->a1(aspect-64)
// and compares f0/f1, a0/a1 (geometry + pixels), the scoring crops derived
// from a0/a1, and verifyScorePlan scores on sampled pairs.
//
// Usage:
//   msf_shared_decode_probe <dataset-root>     full measurement
//   msf_shared_decode_probe --selfcheck        helper unit checks, no dataset
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keep std::max/std::min usable alongside windows.h
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif
#include "crop_fingerprint.h"
#include "dataset_fingerprint.h"
#include "image_decoder.h"
#include "image_verify.h"
#include "path_utils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

double nowMs(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
double meanOf(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double s = 0.0; for (double x : v) s += x;
  return s / (double)v.size();
}
double medianOf(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return (n % 2) ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}
double minOf(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double m = v[0]; for (double x : v) m = x < m ? x : m; return m;
}
double maxOf(const std::vector<double>& v) {
  double m = 0.0; for (double x : v) m = x > m ? x : m; return m;
}

// Aspect target dimensions for maxDim, mirroring decodeWicFileAspect
// (src/image_decoder.cpp): strict sw>sh, lround, floor of 1. The PGM fallback
// uses sw>=sh instead, but both agree everywhere except exact squares, where
// both yield maxDim x maxDim anyway.
void aspectDims(int sw, int sh, int maxDim, int& w, int& h) {
  w = sw; h = sh;
  if (sw <= 0 || sh <= 0 || maxDim <= 0) { w = h = 0; return; }
  if (sw > sh) { w = maxDim; h = std::max(1, (int)std::lround((double)sh * maxDim / sw)); }
  else { h = maxDim; w = std::max(1, (int)std::lround((double)sw * maxDim / sh)); }
}

enum class PixParity { kIdentical, kDifferent };
// Compares two same-geometry buffers. Callers check geometry first; comparing
// across geometries would conflate the two failure classes.
PixParity comparePixels(const msf::GrayImage& a, const msf::GrayImage& b,
                        std::uint64_t& diffCount, std::uint64_t& maxAbs) {
  diffCount = 0; maxAbs = 0;
  if (a.width != b.width || a.height != b.height ||
      a.pixels.size() != b.pixels.size() ||
      a.pixels.size() != (std::size_t)a.width * a.height)
    return PixParity::kDifferent;
  for (std::size_t i = 0; i < a.pixels.size(); ++i) {
    const unsigned d = (unsigned)std::abs((int)a.pixels[i] - (int)b.pixels[i]);
    if (d) { ++diffCount; if (d > maxAbs) maxAbs = d; }
  }
  return diffCount == 0 ? PixParity::kIdentical : PixParity::kDifferent;
}

#ifdef _WIN32
using Microsoft::WRL::ComPtr;

// In-memory WIC Fant downscale grey->grey. Mirrors the scaler+converter+copy
// tail of decodeWicFile (same interpolation, same target pixel format), so the
// derivation cost is comparable with the baseline's resize/convert/copy
// buckets instead of being a different algorithm wearing the same name.
struct DerivResult {
  bool ok = false;
  double scalerMs = 0;    // CreateBitmapFromMemory + scaler create+init
  double convertMs = 0;   // converter create+init
  double copyMs = 0;      // CopyPixels
  double totalMs() const { return scalerMs + convertMs + copyMs; }
};
DerivResult wicScaleGray(IWICImagingFactory* factory, const msf::GrayImage& src,
                         int w, int h, msf::GrayImage& out) {
  DerivResult r;
  if (!factory || src.width <= 0 || src.height <= 0 || w <= 0 || h <= 0) return r;
  if (src.pixels.size() != (std::size_t)src.width * src.height) return r;
  const auto t0 = std::chrono::steady_clock::now();
  ComPtr<IWICBitmap> bmp;
  // The source buffer must outlive every WIC object built from it; src is a
  // const reference owned by the caller for the whole call, so this holds.
  HRESULT hr = factory->CreateBitmapFromMemory(
      (UINT)src.width, (UINT)src.height, GUID_WICPixelFormat8bppGray,
      (UINT)src.width, (UINT)src.pixels.size(),
      const_cast<BYTE*>(src.pixels.data()), &bmp);
  if (FAILED(hr) || !bmp) return r;
  ComPtr<IWICBitmapScaler> scaler;
  hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) hr = scaler->Initialize(bmp.Get(), (UINT)w, (UINT)h, WICBitmapInterpolationModeFant);
  const auto t1 = std::chrono::steady_clock::now();
  r.scalerMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
  if (FAILED(hr)) return r;
  ComPtr<IWICFormatConverter> conv;
  hr = factory->CreateFormatConverter(&conv);
  if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray,
                                           WICBitmapDitherTypeNone, nullptr, 0.0,
                                           WICBitmapPaletteTypeCustom);
  const auto t2 = std::chrono::steady_clock::now();
  r.convertMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
  if (FAILED(hr)) return r;
  out.width = w; out.height = h;
  out.pixels.resize((std::size_t)w * h);
  hr = conv->CopyPixels(nullptr, (UINT)w, out.pixels.size(), out.pixels.data());
  const auto t3 = std::chrono::steady_clock::now();
  r.copyMs = std::chrono::duration<double, std::milli>(t3 - t2).count();
  r.ok = SUCCEEDED(hr);
  return r;
}
#endif  // _WIN32

int selfcheck() {
  int checks = 0, failures = 0;
  auto expect = [&](bool ok, const char* what) {
    ++checks;
    if (!ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
  };
  { int w = 0, h = 0;
    aspectDims(800, 600, 64, w, h);
    expect(w == 64 && h == 48, "aspectDims landscape 800x600@64 -> 64x48"); }
  { int w = 0, h = 0;
    aspectDims(600, 800, 64, w, h);
    expect(w == 48 && h == 64, "aspectDims portrait 600x800@64 -> 48x64"); }
  { int w = 0, h = 0;
    aspectDims(64, 64, 64, w, h);
    expect(w == 64 && h == 64, "aspectDims square stays 64x64"); }
  { int w = 0, h = 0;
    aspectDims(1500, 100, 64, w, h);
    expect(w == 64 && h == 4, "aspectDims 1500x100@64 -> 64x4"); }
  { int w = 0, h = 0;
    aspectDims(1, 1, 64, w, h);
    expect(w == 64 && h == 64, "aspectDims 1x1 clamps to 64x64"); }
  { int w = 0, h = 0;
    aspectDims(0, 100, 64, w, h);
    expect(w == 0 && h == 0, "aspectDims degenerate input yields 0x0"); }
  { msf::GrayImage a, b;
    a.width = b.width = 4; a.height = b.height = 4;
    a.pixels.assign(16, 7); b.pixels.assign(16, 7);
    std::uint64_t dc = 0, ma = 0;
    expect(comparePixels(a, b, dc, ma) == PixParity::kIdentical && dc == 0,
           "comparePixels identical buffers"); }
  { msf::GrayImage a, b;
    a.width = b.width = 4; a.height = b.height = 4;
    a.pixels.assign(16, 7); b.pixels.assign(16, 7); b.pixels[3] = 200;
    std::uint64_t dc = 0, ma = 0;
    expect(comparePixels(a, b, dc, ma) == PixParity::kDifferent && dc == 1 && ma == 193,
           "comparePixels counts differing pixels and max abs diff"); }
  { msf::GrayImage a, b;
    a.width = 4; a.height = 4; a.pixels.assign(16, 7);
    b.width = 4; b.height = 5; b.pixels.assign(20, 7);
    std::uint64_t dc = 0, ma = 0;
    expect(comparePixels(a, b, dc, ma) == PixParity::kDifferent,
           "comparePixels flags geometry mismatch without touching pixels"); }
#ifdef _WIN32
  { msf::GrayImage src;
    src.width = 64; src.height = 64; src.pixels.assign(64 * 64, 128);
    // Factory setup mirrors production order (Factory2 then fallback).
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&factory));
    expect(SUCCEEDED(hr) && factory, "selfcheck factory creation");
    if (factory) {
      msf::GrayImage out;
      DerivResult r = wicScaleGray(factory.Get(), src, 64, 64, out);
      expect(r.ok && out.width == 64 && out.height == 64 && out.pixels.size() == 4096,
             "wicScaleGray 64->64 runs and yields 64x64");
      msf::GrayImage tiny;
      DerivResult r2 = wicScaleGray(factory.Get(), src, 32, 32, tiny);
      expect(r2.ok && tiny.width == 32 && tiny.height == 32,
             "wicScaleGray 64->32 runs and yields 32x32");
      msf::GrayImage bad;
      DerivResult r3 = wicScaleGray(factory.Get(), src, 0, 32, bad);
      expect(!r3.ok, "wicScaleGray rejects degenerate target");
    }
    msf::GrayImage crop = msf::centerCropResize(src, 1.0);
    expect(crop.width == 32 && crop.height == 32 && crop.pixels.size() == 1024,
           "centerCropResize yields default 32x32");
  }
#endif
  std::cout << "selfcheck=" << (failures ? "FAIL" : "ok") << " checks=" << checks << "\n";
  return failures ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // COM is initialized once for the process and deliberately never
  // uninitialized here: tearing the apartment down while codec DLLs are still
  // referenced faults on scope exit (the D2 access-violation lesson).
  HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hrCo) && hrCo != RPC_E_CHANGED_MODE) {
    std::cerr << "CoInitializeEx failed\n";
    return 2;
  }
#endif
  if (argc >= 2 && std::strcmp(argv[1], "--selfcheck") == 0) return selfcheck();
#ifdef _WIN32
  if (argc < 2) {
    std::cerr << "usage: msf_shared_decode_probe <dataset-root>\n";
    return 2;
  }
  const std::string root = argv[1];

  const msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(root);
  if (fp.state != "measured") {
    std::cerr << "dataset not measured: " << fp.state << "\n";
    return 3;
  }
  std::printf("dataset_fingerprint=%s files=%llu bytes=%llu version=%d\n", fp.fingerprint.c_str(),
              (unsigned long long)fp.fileCount, (unsigned long long)fp.totalBytes,
              msf::kDatasetFingerprintVersion);

  // File collection: the whole multi-format corpus plus a stride sample of the
  // bulk BMP fixtures (the deterministic 8px-heavy set is the tiny-image
  // corner, so it is sampled rather than skipped).
  struct File { std::string format; std::string path; };
  std::vector<File> files;
  {
    std::error_code ec;
    const std::string fmtRoot = msf::path_to_utf8(std::filesystem::path(root) / "images" / "format");
    for (const auto& de : std::filesystem::directory_iterator(msf::path_from_utf8(fmtRoot), ec)) {
      if (!de.is_directory()) continue;
      const std::string fmt = msf::path_to_utf8(de.path().filename());
      for (const auto& f : std::filesystem::directory_iterator(de.path(), ec)) {
        if (f.is_regular_file()) files.push_back({fmt, msf::path_to_utf8(f.path())});
      }
    }
    std::vector<std::string> bmp;
    for (const auto& f : std::filesystem::recursive_directory_iterator(msf::path_from_utf8(root), ec)) {
      if (!f.is_regular_file()) continue;
      const std::string p = msf::path_to_utf8(f.path());
      if (p.size() >= 4 && (p.compare(p.size() - 4, 4, ".bmp") == 0)) bmp.push_back(p);
    }
    std::sort(bmp.begin(), bmp.end());
    const std::size_t step = bmp.size() > 200 ? bmp.size() / 200 : 1;
    for (std::size_t i = 0; i < bmp.size(); i += step) files.push_back({"bmp", bmp[i]});
  }
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) {
    return a.format < b.format || (a.format == b.format && a.path < b.path);
  });
  std::printf("files_collected=%d\n", (int)files.size());

  ComPtr<IWICImagingFactory> factory;
  {
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) { std::cerr << "WIC factory creation failed\n"; return 2; }
  }

  struct FileBase {
    std::string format, path;
    msf::GrayImage f0, a0;
    msf::DecodeTelemetry telF, telA;
    bool ok = false;
  };
  std::vector<FileBase> base;
  base.reserve(files.size());
  msf::ImageDecoder dec;
  int baseFail = 0;
  for (const File& f : files) {
    FileBase b;
    b.format = f.format; b.path = f.path;
    const bool okF = dec.decode(f.path, 64, 64, b.f0, &b.telF);
    const bool okA = okF && dec.decodePreserveAspect(f.path, 64, b.a0, &b.telA);
    b.ok = okF && okA && b.f0.width == 64 && b.f0.height == 64 &&
           b.f0.pixels.size() == 4096 && b.a0.width > 0 && b.a0.height > 0 &&
           b.a0.pixels.size() == (std::size_t)b.a0.width * b.a0.height;
    if (!b.ok) { ++baseFail; continue; }
    base.push_back(std::move(b));
  }
  std::printf("baseline_ok=%d baseline_fail=%d\n", (int)base.size(), baseFail);

  // Baseline cost anatomy per format (R-independent; the comparator for §9).
  {
    std::map<std::string, std::vector<double>> fMs, aMs, bMs, fOpen, aOpen, fCopy, aCopy;
    for (const FileBase& b : base) {
      fMs[b.format].push_back(b.telF.totalMs);
      aMs[b.format].push_back(b.telA.totalMs);
      bMs[b.format].push_back(b.telF.totalMs + b.telA.totalMs);
      fOpen[b.format].push_back(b.telF.openMs);
      aOpen[b.format].push_back(b.telA.openMs);
      fCopy[b.format].push_back(b.telF.copyMs);
      aCopy[b.format].push_back(b.telA.copyMs);
    }
    std::printf("--- baseline (decode 64x64 + aspect 64) ---\n");
    for (auto& kv : bMs) {
      const std::string& fmt = kv.first;
      std::printf("format %-6s n=%d f_ms %.4f a_ms %.4f base_ms %.4f f_open %.4f a_open %.4f f_copy %.4f a_copy %.4f\n",
                  fmt.c_str(), (int)kv.second.size(), meanOf(fMs[fmt]), meanOf(aMs[fmt]),
                  meanOf(kv.second), meanOf(fOpen[fmt]), meanOf(aOpen[fmt]),
                  meanOf(fCopy[fmt]), meanOf(aCopy[fmt]));
    }
  }

  // EXIF coverage evidence: how many baseline aspect decodes applied orientation.
  {
    long long oriented = 0, pgm = 0;
    for (const FileBase& b : base) {
      if (b.telA.orientApplied > 0) ++oriented;
      if (b.telA.pgmFallbacks > 0) ++pgm;
    }
    std::printf("orient_applied_files=%lld pgm_fallback_files=%lld\n", oriented, pgm);
  }

  const int kResolutions[] = {128, 192, 256, 384, 512};
  const double kAspects[] = {4.0 / 3.0, 1.0, 9.0 / 16.0};
  const double kVerdict = 87.5;  // scan maxDistance 8 (scan_pipeline.cpp thresholdFor)

  for (int R : kResolutions) {
    std::printf("\n=== shared resolution R=%d ===\n", R);
    struct Agg {
      std::vector<double> sharedMs, fDerivMs, aDerivMs, candTotalMs;
      int n = 0, sharedFail = 0, derivFail = 0;
      int fIdent = 0, fDiff = 0;
      int aGeomSame = 0, aGeomDiff = 0, aPixIdent = 0, aPixDiff = 0;
      int cropMatch = 0, cropTotal = 0;
      long long cropDiffPx = 0;
      int scoreExact = 0, scorePairs = 0, verdictFlip = 0;
      double scoreMaxAbs = 0;
      std::string scoreMaxPair;
    };
    std::map<std::string, Agg> agg;
    // Sliding window of one previous file enables neighbor-pair scoring while
    // streaming, so candidate buffers never accumulate for the whole corpus.
    struct Prev { std::string format, path; msf::GrayImage f1, a1; bool valid = false; };
    std::map<std::string, Prev> prevByFormat;
    for (const FileBase& b : base) {
      Agg& g = agg[b.format];
      msf::GrayImage shared;
      msf::DecodeTelemetry telS;
      const auto ts0 = std::chrono::steady_clock::now();
      const bool okS = dec.decodePreserveAspect(b.path, R, shared, &telS);
      const double sharedMs = nowMs(ts0);
      if (!okS || shared.width <= 0 || shared.height <= 0 ||
          shared.pixels.size() != (std::size_t)shared.width * shared.height) {
        ++g.sharedFail; continue;
      }
      int aw = 0, ah = 0;
      aspectDims(shared.width, shared.height, 64, aw, ah);
      msf::GrayImage f1, a1;
      DerivResult rf = wicScaleGray(factory.Get(), shared, 64, 64, f1);
      DerivResult ra = (aw > 0 && ah > 0) ? wicScaleGray(factory.Get(), shared, aw, ah, a1) : DerivResult{};
      if (!rf.ok || !ra.ok) { ++g.derivFail; continue;
      }
      ++g.n;
      g.sharedMs.push_back(sharedMs);
      g.sharedMs.back() = telS.totalMs > 0 ? telS.totalMs : sharedMs;
      g.fDerivMs.push_back(rf.totalMs());
      g.aDerivMs.push_back(ra.totalMs());
      g.candTotalMs.push_back(g.sharedMs.back() + rf.totalMs() + ra.totalMs());

      // f parity: geometry is fixed 64x64 by construction on both sides.
      {
        std::uint64_t dc = 0, ma = 0;
        if (comparePixels(b.f0, f1, dc, ma) == PixParity::kIdentical) ++g.fIdent;
        else ++g.fDiff;
      }
      // a parity: geometry first, pixels only on equal geometry.
      {
        bool geomSame = (b.a0.width == a1.width && b.a0.height == a1.height);
        if (geomSame) {
          ++g.aGeomSame;
          std::uint64_t dc = 0, ma = 0;
          if (comparePixels(b.a0, a1, dc, ma) == PixParity::kIdentical) ++g.aPixIdent;
          else ++g.aPixDiff;
        } else {
          ++g.aGeomDiff;
        }
      }
      // Scoring-crop parity: this is what verifyScorePlan actually consumes.
      for (double asp : kAspects) {
        msf::GrayImage c0 = msf::centerCropResize(b.a0, asp);
        msf::GrayImage c1 = msf::centerCropResize(a1, asp);
        ++g.cropTotal;
        std::uint64_t dc = 0, ma = 0;
        if (c0.width == 32 && c0.height == 32 && comparePixels(c0, c1, dc, ma) == PixParity::kIdentical)
          ++g.cropMatch;
        else g.cropDiffPx += (long long)dc;
      }
      // Pair scoring: self-pair plus neighbor pair inside the format window.
      {
        const double sBase = msf::verifyScorePlan(b.f0, b.a0, b.f0, b.a0, 90.0, nullptr);
        const double sCand = msf::verifyScorePlan(f1, a1, f1, a1, 90.0, nullptr);
        ++g.scorePairs;
        if (sBase == sCand) ++g.scoreExact;
        const double d = std::fabs(sBase - sCand);
        if (d > g.scoreMaxAbs) { g.scoreMaxAbs = d; g.scoreMaxPair = "self:" + b.path; }
        if ((sBase >= kVerdict) != (sCand >= kVerdict)) {
          ++g.verdictFlip;
          std::printf("  flip R=%d SELF %s base=%.6f cand=%.6f\n", R, b.path.c_str(), sBase, sCand);
        }
      }
      Prev& pv = prevByFormat[b.format];
      if (pv.valid) {
        const double sBase = msf::verifyScorePlan(b.f0, b.a0, pv.f1, pv.a1, 90.0, nullptr);
        const double sCand = msf::verifyScorePlan(f1, a1, pv.f1, pv.a1, 90.0, nullptr);
        ++g.scorePairs;
        if (sBase == sCand) ++g.scoreExact;
        const double d = std::fabs(sBase - sCand);
        if (d > g.scoreMaxAbs) { g.scoreMaxAbs = d; g.scoreMaxPair = "pair:" + b.path + " <> " + pv.path; }
        if ((sBase >= kVerdict) != (sCand >= kVerdict)) {
          ++g.verdictFlip;
          std::printf("  flip R=%d PAIR %s <> %s base=%.6f cand=%.6f\n",
                      R, b.path.c_str(), pv.path.c_str(), sBase, sCand);
        }
      }
      pv = {b.format, b.path, f1, a1, true};
    }
    for (auto& kv : agg) {
      const std::string& fmt = kv.first;
      Agg& g = kv.second;
      std::printf("format %-6s n=%d sharedFail=%d derivFail=%d\n", fmt.c_str(), g.n, g.sharedFail, g.derivFail);
      std::printf("  shared_ms  mean %8.4f median %8.4f min %8.4f max %8.4f\n",
                  meanOf(g.sharedMs), medianOf(g.sharedMs), minOf(g.sharedMs), maxOf(g.sharedMs));
      std::printf("  fderiv_ms  mean %8.4f median %8.4f min %8.4f max %8.4f\n",
                  meanOf(g.fDerivMs), medianOf(g.fDerivMs), minOf(g.fDerivMs), maxOf(g.fDerivMs));
      std::printf("  aderiv_ms  mean %8.4f median %8.4f min %8.4f max %8.4f\n",
                  meanOf(g.aDerivMs), medianOf(g.aDerivMs), minOf(g.aDerivMs), maxOf(g.aDerivMs));
      std::printf("  cand_ms    mean %8.4f median %8.4f min %8.4f max %8.4f\n",
                  meanOf(g.candTotalMs), medianOf(g.candTotalMs), minOf(g.candTotalMs), maxOf(g.candTotalMs));
      std::printf("  f_parity   identical %d / different %d\n", g.fIdent, g.fDiff);
      std::printf("  a_parity   geom_same %d geom_diff %d pix_ident %d pix_diff %d\n",
                  g.aGeomSame, g.aGeomDiff, g.aPixIdent, g.aPixDiff);
      std::printf("  crops      match %d / %d  diffpixels %lld\n", g.cropMatch, g.cropTotal, g.cropDiffPx);
      std::printf("  scores     pairs %d exact %d maxabs %.6f (%s) flips@87.5 %d\n",
                  g.scorePairs, g.scoreExact, g.scoreMaxAbs,
                  g.scoreMaxPair.empty() ? "-" : g.scoreMaxPair.c_str(), g.verdictFlip);
    }
    {
      // Cross-format totals for the §9-style summary line.
      long long n = 0, sf = 0, df = 0, fi = 0, fd = 0, ag = 0, ad = 0, pi = 0, pd = 0;
      long long cm = 0, ct = 0, se = 0, sp = 0, vf = 0;
      double sm = 0, fm = 0, am = 0, cmn = 0, mx = 0;
      for (auto& kv : agg) {
        Agg& g = kv.second;
        n += g.n; sf += g.sharedFail; df += g.derivFail;
        fi += g.fIdent; fd += g.fDiff; ag += g.aGeomSame; ad += g.aGeomDiff;
        pi += g.aPixIdent; pd += g.aPixDiff; cm += g.cropMatch; ct += g.cropTotal;
        se += g.scoreExact; sp += g.scorePairs; vf += g.verdictFlip;
        sm += meanOf(g.sharedMs) * g.n; fm += meanOf(g.fDerivMs) * g.n;
        am += meanOf(g.aDerivMs) * g.n; cmn += meanOf(g.candTotalMs) * g.n;
        if (g.scoreMaxAbs > mx) mx = g.scoreMaxAbs;
      }
      std::printf("TOTAL R=%d n=%lld sharedFail=%lld derivFail=%lld\n", R, n, sf, df);
      std::printf("  ms shared %.4f fderiv %.4f aderiv %.4f cand %.4f (file-weighted means)\n",
                  n ? sm / n : 0, n ? fm / n : 0, n ? am / n : 0, n ? cmn / n : 0);
      std::printf("  f ident %lld diff %lld | a geomsame %lld geomdiff %lld pixident %lld pixdiff %lld\n",
                  fi, fd, ag, ad, pi, pd);
      std::printf("  crops %lld/%lld | scores exact %lld/%lld maxabs %.6f flips %lld\n",
                  cm, ct, se, sp, mx, vf);
    }
  }
  std::printf("\nproduction path unchanged: baseline used decode/decodePreserveAspect only;\n");
  std::printf("candidate derivation used an isolated in-memory WIC scaler; no product call was replaced.\n");
  return 0;
#else
  if (argc >= 2 && std::strcmp(argv[1], "--selfcheck") == 0) {
    // aspectDims/comparePixels unit checks are platform-independent.
    return selfcheck();
  }
  std::cout << "not_available: shared decode probe needs WIC (Windows)\n";
  return 2;
#endif
}
