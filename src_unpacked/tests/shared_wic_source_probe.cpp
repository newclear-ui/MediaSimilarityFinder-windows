// D3-follow-up candidate I-2 probe: shared WIC source/frame + two independent
// scalers (docs/implementation-briefs/I-shared-wic-source.*).
//
// Measurement-only. This file is a separate executable. It never replaces or
// reorders a production call: the baseline is obtained by CALLING
// ImageDecoder::decode() and ImageDecoder::decodePreserveAspect(), and the
// candidate is a private WIC pipeline implemented here. No product behaviour
// can change because of this file.
//
// What it measures, per file:
//   baseline : decode(path,64,64)->f0  +  decodePreserveAspect(path,64)->a0
//   candidate: one CoInitialize / factory / decoder / frame / EXIF-orientation
//              source, then TWO independent scaler+converter+CopyPixels chains
//              reading that same source, producing f1 and a1 directly
//
// There is NO intermediate GrayImage in the candidate, so the two-step Fant
// chain that broke the previous candidate's byte parity is structurally absent.
// Priority is exact output parity over the whole dataset, not cost.
//
// Usage:
//   msf_shared_wic_source_probe <dataset-root>   full parity + cost measurement
//   msf_shared_wic_source_probe --selfcheck      no-dataset unit checks
//   msf_shared_wic_source_probe --exif <dir>     EXIF fixture set from <dir>
#include <cmath>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#endif
#include "dataset_fingerprint.h"
#include "image_decoder.h"
#include "image_verify.h"
#include "path_utils.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
using Microsoft::WRL::ComPtr;
#endif

namespace {

// ------------------------------------------------------------------ helpers

double nowMs(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}
double meanOf(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  double s = 0.0;
  for (double x : v) s += x;
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
  return *std::min_element(v.begin(), v.end());
}
double maxOf(const std::vector<double>& v) {
  if (v.empty()) return 0.0;
  return *std::max_element(v.begin(), v.end());
}
void stats(const char* label, const std::vector<double>& v) {
  if (v.empty()) { std::printf("  %-18s (no samples)\n", label); return; }
  const double mn = minOf(v), mx = maxOf(v);
  std::printf("  %-18s n=%4zu mean %8.4f median %8.4f min %8.4f max %8.4f range %7.4f\n",
              label, v.size(), meanOf(v), medianOf(v), mn, mx, mx - mn);
}
int fail(const char* what) {
  std::cerr << "selfcheck FAILED at " << what << "\n";
  return 1;
}

std::wstring toWide(const std::string& p) {
  const int need = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
  if (!need) return std::wstring();
  std::wstring w((std::size_t)need, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, w.data(), need);
  if (!w.empty() && w.back() == L'\0') w.pop_back();
  return w;
}

struct PixelDiff {
  const char* kind = "identical";  // identical | geometry | pixels
  std::uint64_t diffCount = 0;
  std::uint64_t maxAbs = 0;
  long long sumAbs = 0;
};
// Compares two images. A geometry mismatch is reported as such and the pixels
// are NOT compared, because comparing different shapes would be meaningless.
PixelDiff compareImages(const msf::GrayImage& a, const msf::GrayImage& b) {
  PixelDiff r;
  if (a.width != b.width || a.height != b.height) { r.kind = "geometry"; return r; }
  if (a.pixels.size() != b.pixels.size()) { r.kind = "geometry"; return r; }
  for (std::size_t i = 0; i < a.pixels.size(); ++i) {
    if (a.pixels[i] == b.pixels[i]) continue;
    const std::uint64_t d = a.pixels[i] > b.pixels[i] ? a.pixels[i] - b.pixels[i] : b.pixels[i] - a.pixels[i];
    if (d > r.maxAbs) r.maxAbs = d;
    r.sumAbs += (long long)d;
    ++r.diffCount;
  }
  if (r.diffCount) r.kind = "pixels";
  return r;
}

#ifdef _WIN32
// ------------------------------------------------- candidate WIC pipeline (I-2)

// Identical to the product formula in decodeWicFileAspect
// (src/image_decoder.cpp:293): long side fixed at maxDimension, short side
// lround'ed, clamped to at least 1, with a strict sw>sh branch.
void aspectDims(int sw, int sh, int maxDimension, int& w, int& h) {
  w = sw; h = sh;
  if (sw > sh) {
    w = maxDimension;
    h = (int)std::max<UINT>(1, (UINT)std::lround((double)sh * maxDimension / sw));
  } else {
    h = maxDimension;
    w = (int)std::max<UINT>(1, (UINT)std::lround((double)sw * maxDimension / sh));
  }
}

// The candidate. One COM init, one factory, one decoder, one frame, one
// orientation source; then two independent scaler/converter/CopyPixels chains.
//
// COM lifetime: every WIC object is owned by runSharedPipeline, so all of them
// are released when it returns -- strictly before the caller runs
// CoUninitialize(). That ordering is mandatory, not stylistic: the product
// source records an observed access violation when a WIC object is released
// after CoUninitialize (src/image_decoder.cpp:150-153). Keeping the COM objects
// in a helper that returns, instead of inline before CoUninitialize, is what
// makes that true on every return path, including the failure ones.
// The EXIF Orientation value mapped to the WIC transform, exactly as
// src/image_decoder.cpp maps it. Defined near the EXIF section; forward
// declared here because the candidate pipeline below uses it.
static WICBitmapTransformOptions transformForOrientation(unsigned o);

// The minimal little-endian EXIF APP1 builder. Defined in the EXIF section.
static std::vector<unsigned char> exifApp1(unsigned short orientation);

struct CandResult {
  std::uint32_t failHr = 0;
  int srcW = 0, srcH = 0, aW = 0, aH = 0;
  std::uint64_t orientApplied = 0;
  // metaMs and orientMs are measured separately and never aliased. An earlier
  // revision reported tel.orientMs = r.metaMs, which made a field named
  // "orientation" carry metadata-reader construction time. orientMs is now
  // only the CreateBitmapFlipRotator + Initialize work, and is 0 when no
  // transform was applied.
  double factoryMs = 0, openMs = 0, frameMs = 0, metaMs = 0, orientMs = 0;
  double fScalerMs = 0, fConvMs = 0, fCopyMs = 0;
  double aScalerMs = 0, aConvMs = 0, aCopyMs = 0;
};

// 0.9.4.82: software orientation, matching src/image_decoder.cpp
// applyOrientationPixels exactly. The candidate must mirror the product order
// (scale first, rotate the scaled buffer) for the parity comparison to mean
// anything. Kept in sync by hand; probe7 proves the product helper equals WIC.
static void applyProbeOrientation(std::vector<std::uint8_t>& px, int& w, int& h,
                                  int channels, WICBitmapTransformOptions xform) {
  if (xform == WICBitmapTransformRotate0 || w <= 0 || h <= 0) return;
  const int ch = channels > 0 ? channels : 1;
  auto flipH = [&] {
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w / 2; ++x)
        for (int c = 0; c < ch; ++c)
          std::swap(px[((std::size_t)y * w + x) * ch + c],
                    px[((std::size_t)y * w + (w - 1 - x)) * ch + c]);
  };
  auto flipV = [&] {
    for (int y = 0; y < h / 2; ++y)
      for (int x = 0; x < w; ++x)
        for (int c = 0; c < ch; ++c)
          std::swap(px[((std::size_t)y * w + x) * ch + c],
                    px[((std::size_t)(h - 1 - y) * w + x) * ch + c]);
  };
  auto rot90 = [&] {
    const int W = w, H = h;
    std::vector<std::uint8_t> o((std::size_t)W * H * ch);
    for (int y = 0; y < W; ++y) for (int x = 0; x < H; ++x) for (int c = 0; c < ch; ++c)
      o[((std::size_t)y * H + x) * ch + c] = px[((std::size_t)(H - 1 - x) * W + y) * ch + c];
    px.swap(o); w = H; h = W;
  };
  auto rot180 = [&] {
    const int W = w, H = h;
    std::vector<std::uint8_t> o((std::size_t)W * H * ch);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) for (int c = 0; c < ch; ++c)
      o[((std::size_t)y * W + x) * ch + c] = px[((std::size_t)(H - 1 - y) * W + (W - 1 - x)) * ch + c];
    px.swap(o);
  };
  auto rot270 = [&] {
    const int W = w, H = h;
    std::vector<std::uint8_t> o((std::size_t)W * H * ch);
    for (int y = 0; y < W; ++y) for (int x = 0; x < H; ++x) for (int c = 0; c < ch; ++c)
      o[((std::size_t)y * H + x) * ch + c] = px[((std::size_t)x * W + (W - 1 - y)) * ch + c];
    px.swap(o); w = H; h = W;
  };
  if (xform & WICBitmapTransformFlipHorizontal) flipH();
  if (xform & WICBitmapTransformFlipVertical) flipV();
  switch ((unsigned)xform & 0x3u) { case 1: rot90(); break; case 2: rot180(); break; case 3: rot270(); break; default: break; }
}

// forceXform: WICBitmapTransformRotate0 means "read EXIF and use it" (the real
// candidate). Any other value forces that transform, which is what lets the
// rotated path be exercised even though the product's own metadata query does
// not currently resolve.
static bool runSharedPipelineXform(const std::wstring& wpath, int maxDimension,
                                   WICBitmapTransformOptions forceXform,
                                   msf::GrayImage& f, msf::GrayImage& a, CandResult& r) {
  ComPtr<IWICImagingFactory> factory;
  {
    const auto tF0 = std::chrono::steady_clock::now();
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
      hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&factory));
    }
    r.factoryMs = nowMs(tF0);
    if (FAILED(hr) || !factory) { r.failHr = (std::uint32_t)hr; return false; }

    ComPtr<IWICBitmapDecoder> dec;
    const auto tO0 = std::chrono::steady_clock::now();
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                           WICDecodeMetadataCacheOnDemand, &dec);
    r.openMs = nowMs(tO0);
    if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }

    ComPtr<IWICBitmapFrameDecode> frame;
    const auto tFr0 = std::chrono::steady_clock::now();
    hr = dec->GetFrame(0, &frame);
    r.frameMs = nowMs(tFr0);
    if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }

    ComPtr<IWICBitmapSource> src;
    const auto tM0 = std::chrono::steady_clock::now();
    const auto tOr0 = std::chrono::steady_clock::now();
    WICBitmapTransformOptions xform = forceXform;
    if (xform == WICBitmapTransformRotate0) {
      // Mirrors the corrected product logic (src/image_decoder.cpp
      // exifOrientationToTransform): Orientation lives in EXIF IFD0, exposed by
      // WIC as /app1/ifd/ for JPEG and /ifd/ for TIFF. The candidate must read
      // the tag the same way the product now does, or a parity comparison would
      // measure the probe's own lag instead of the candidate.
      ComPtr<IWICMetadataQueryReader> meta;
      if (SUCCEEDED(frame->GetMetadataQueryReader(&meta)) && meta) {
        static const wchar_t* const kPaths[] = {
          L"/app1/ifd/{ushort=274}",  // JPEG
          L"/ifd/{ushort=274}",       // TIFF
        };
        for (const wchar_t* p : kPaths) {
          PROPVARIANT v; PropVariantInit(&v);
          if (SUCCEEDED(meta->GetMetadataByName(p, &v)) && v.vt == VT_UI2) {
            xform = transformForOrientation((unsigned)v.uiVal);
            break;
          }
          PropVariantClear(&v);
        }
      }
    }
    r.metaMs = nowMs(tM0);
    // 0.9.4.82: the source stays the raw frame; orientation is applied to each
    // scaled output below (applyProbeOrientation), matching the product.
    src = frame;
    if (xform != WICBitmapTransformRotate0) ++r.orientApplied;
    r.orientMs = nowMs(tOr0);

    UINT sw = 0, sh = 0;
    if (FAILED(src->GetSize(&sw, &sh)) || !sw || !sh) return false;
    r.srcW = (int)sw; r.srcH = (int)sh;
    const bool swapDims = ((unsigned)xform & 0x3u) == 1u || ((unsigned)xform & 0x3u) == 3u;
    const int ow = swapDims ? (int)sh : (int)sw, oh = swapDims ? (int)sw : (int)sh;
    int aw = 0, ah = 0;
    aspectDims(ow, oh, maxDimension, aw, ah);
    r.aW = aw; r.aH = ah;

    // ---- branch A: fixed maxDimension x maxDimension, as decodeWicFile ----
    {
      const UINT fw2 = (UINT)maxDimension, fh2 = (UINT)maxDimension;
      const UINT pw = swapDims ? fh2 : fw2, ph = swapDims ? fw2 : fh2;
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      const auto tS0 = std::chrono::steady_clock::now();
      hr = factory->CreateBitmapScaler(&scaler);
      if (SUCCEEDED(hr)) hr = scaler->Initialize(src.Get(), pw, ph, WICBitmapInterpolationModeFant);
      r.fScalerMs = nowMs(tS0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      const auto tC0 = std::chrono::steady_clock::now();
      hr = factory->CreateFormatConverter(&conv);
      if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom);
      r.fConvMs = nowMs(tC0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      f.width = (int)pw; f.height = (int)ph;
      f.pixels.resize((std::size_t)pw * ph);
      const auto tP0 = std::chrono::steady_clock::now();
      hr = conv->CopyPixels(nullptr, pw, (UINT)f.pixels.size(), f.pixels.data());
      r.fCopyMs = nowMs(tP0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      applyProbeOrientation(f.pixels, f.width, f.height, 1, xform);
    }

    // ---- branch B: aspect, as decodeWicFileAspect ----
    // Same `src`, a second fully independent scaler and converter. The second
    // Fant step reads the same original source, never an intermediate image.
    {
      const UINT pw = swapDims ? (UINT)ah : (UINT)aw, ph = swapDims ? (UINT)aw : (UINT)ah;
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      const auto tS1 = std::chrono::steady_clock::now();
      hr = factory->CreateBitmapScaler(&scaler);
      if (SUCCEEDED(hr)) hr = scaler->Initialize(src.Get(), pw, ph, WICBitmapInterpolationModeFant);
      r.aScalerMs = nowMs(tS1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      const auto tC1 = std::chrono::steady_clock::now();
      hr = factory->CreateFormatConverter(&conv);
      if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom);
      r.aConvMs = nowMs(tC1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      a.width = (int)pw; a.height = (int)ph;
      a.pixels.resize((std::size_t)pw * ph);
      const auto tP1 = std::chrono::steady_clock::now();
      hr = conv->CopyPixels(nullptr, pw, (UINT)a.pixels.size(), a.pixels.data());
      r.aCopyMs = nowMs(tP1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      applyProbeOrientation(a.pixels, a.width, a.height, 1, xform);
    }
  }  // factory, decoder, frame, both scalers and both converters released
  return true;
}

struct CandTelemetry {
  double comMs = 0, factoryMs = 0, openMs = 0, frameMs = 0, metaMs = 0, orientMs = 0;
  double fScalerMs = 0, fConvMs = 0, fCopyMs = 0;
  double aScalerMs = 0, aConvMs = 0, aCopyMs = 0;
  double sharedMs = 0;   // COM + factory + decoder + frame + metadata + orientation
  double totalMs = 0;    // whole call
  std::uint64_t orientApplied = 0;
  int srcW = 0, srcH = 0, aW = 0, aH = 0;
  bool ok = false;
  std::uint32_t failHr = 0;
};

bool candidateSharedSource(const std::wstring& wpath, int maxDimension,
                           msf::GrayImage& f, msf::GrayImage& a, CandTelemetry& tel) {
  const auto tAll0 = std::chrono::steady_clock::now();
  if (maxDimension <= 0 || wpath.empty()) return false;

  const auto tCom0 = std::chrono::steady_clock::now();
  const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool uninit = SUCCEEDED(cohr);
  tel.comMs = nowMs(tCom0);

  if (!uninit) {
    tel.failHr = (std::uint32_t)cohr;
    tel.totalMs = nowMs(tAll0);
    return false;
  }
  CandResult r;
  tel.ok = runSharedPipelineXform(wpath, maxDimension, WICBitmapTransformRotate0, f, a, r);  // releases all WIC objects here
  CoUninitialize();                                           // strictly afterwards

  tel.factoryMs = r.factoryMs; tel.openMs = r.openMs; tel.frameMs = r.frameMs;
  tel.metaMs = r.metaMs; tel.orientMs = r.orientMs;
  tel.fScalerMs = r.fScalerMs; tel.fConvMs = r.fConvMs; tel.fCopyMs = r.fCopyMs;
  tel.aScalerMs = r.aScalerMs; tel.aConvMs = r.aConvMs; tel.aCopyMs = r.aCopyMs;
  tel.orientApplied = r.orientApplied;
  tel.srcW = r.srcW; tel.srcH = r.srcH; tel.aW = r.aW; tel.aH = r.aH;
  tel.failHr = r.failHr;
  tel.sharedMs = tel.comMs + tel.factoryMs + tel.openMs + tel.frameMs + tel.metaMs + tel.orientMs;
  tel.totalMs = nowMs(tAll0);
  return tel.ok;
}

// ---- PGM analogue: read once, scale twice (structure only, not wired in) ----
// P5 header parse that mirrors the product's readPgmFile
// (src/image_decoder.cpp:84-92): token-scan P5/w/h/maxv, then consume exactly
// one whitespace byte, then the pixel bytes. P5-only and no maxv
// normalization, on purpose.
bool readPgmOnce(const std::string& path, std::vector<unsigned char>& src, int& sw, int& sh) {
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, toWide(path).c_str(), L"rb") != 0 || !f) return false;
  char magic[3] = {0, 0, 0};
  int maxv = 0;
  const bool head = fscanf(f, "%2s %d %d %d", magic, &sw, &sh, &maxv) == 4;
  if (head) fgetc(f);  // the single whitespace byte that ends the header
  const long dataStart = ftell(f);
  fclose(f);
  if (!head || std::strcmp(magic, "P5") != 0 || sw <= 0 || sh <= 0 || maxv <= 0) return false;
  const std::size_t need = (std::size_t)sw * sh;

  std::FILE* g = nullptr;
  if (_wfopen_s(&g, toWide(path).c_str(), L"rb") != 0 || !g) return false;
  if (fseek(g, dataStart, SEEK_SET) != 0) { fclose(g); return false; }
  std::vector<unsigned char> raw(need);
  const std::size_t got = fread(raw.data(), 1, need, g);
  fclose(g);
  if (got != need) return false;
  src.swap(raw);
  return true;
}
// Mirrors the product's integer nearest-neighbour scaleGray
// (src/image_decoder.cpp:93-98).
bool scaleGrayLike(const std::vector<unsigned char>& src, int sw, int sh, int w, int h, msf::GrayImage& out) {
  if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return false;
  out.width = w; out.height = h;
  out.pixels.resize((std::size_t)w * h);
  for (int y = 0; y < h; ++y) {
    const int sy = std::min(sh - 1, y * sh / h);
    for (int x = 0; x < w; ++x) {
      const int sx = std::min(sw - 1, x * sw / w);
      out.pixels[(std::size_t)y * w + x] = src[(std::size_t)sy * sw + sx];
    }
  }
  return true;
}
bool candidatePgmDual(const std::string& path, int maxDimension, msf::GrayImage& f, msf::GrayImage& a) {
  std::vector<unsigned char> src; int sw = 0, sh = 0;
  if (!readPgmOnce(path, src, sw, sh)) return false;
  if (!scaleGrayLike(src, sw, sh, maxDimension, maxDimension, f)) return false;
  int aw = 0, ah = 0;
  aspectDims(sw, sh, maxDimension, aw, ah);
  return scaleGrayLike(src, sw, sh, aw, ah, a);
}

// ------------------------------------------------------------ fixture making

// A tiny 8-bit grayscale BMP, built byte by byte so the selfcheck needs no
// dataset and no external encoder.
bool writeTinyBmp(const std::string& path, int w, int h) {
  const int rowBytes = ((w + 3) / 4) * 4;
  const std::size_t pixelBytes = (std::size_t)rowBytes * h;
  std::vector<unsigned char> b(14 + 40 + 1024 + pixelBytes, 0);
  auto put16 = [](std::vector<unsigned char>& v, std::size_t o, unsigned x) { v[o] = (unsigned char)(x & 0xFF); v[o + 1] = (unsigned char)((x >> 8) & 0xFF); };
  auto put32 = [](std::vector<unsigned char>& v, std::size_t o, unsigned long x) {
    v[o] = (unsigned char)(x & 0xFF); v[o + 1] = (unsigned char)((x >> 8) & 0xFF);
    v[o + 2] = (unsigned char)((x >> 16) & 0xFF); v[o + 3] = (unsigned char)((x >> 24) & 0xFF);
  };
  b[0] = 'B'; b[1] = 'M';
  put32(b, 2, (unsigned long)b.size());
  put32(b, 10, 14 + 40 + 1024);
  put32(b, 14, 40);
  put32(b, 18, (unsigned long)w);
  put32(b, 22, (unsigned long)h);
  put16(b, 26, 1); put16(b, 28, 8);
  put32(b, 30, 0); put32(b, 34, (unsigned long)pixelBytes);
  for (int i = 0; i < 256; ++i) {  // grayscale palette
    b[54 + i * 4 + 0] = (unsigned char)i; b[54 + i * 4 + 1] = (unsigned char)i;
    b[54 + i * 4 + 2] = (unsigned char)i; b[54 + i * 4 + 3] = 0;
  }
  for (int y = 0; y < h; ++y) {
    const std::size_t rowOff = 14 + 40 + 1024 + (std::size_t)(h - 1 - y) * rowBytes;  // bottom-up
    for (int x = 0; x < w; ++x) {
      b[rowOff + (std::size_t)x] = (unsigned char)((x * 37 + y * 17) & 0xFF);
    }
  }
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, toWide(path).c_str(), L"wb") != 0 || !f) return false;
  const bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
  fclose(f);
  return ok;
}

bool writeTinyPgm(const std::string& path, int w, int h) {
  std::vector<unsigned char> b;
  char head[64];
  _snprintf_s(head, sizeof(head), _TRUNCATE, "P5\n%d %d\n255\n", w, h);
  const std::size_t hl = std::strlen(head);
  b.assign(head, head + hl);
  for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) b.push_back((unsigned char)((x * 11 + y * 23) & 0xFF));
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, toWide(path).c_str(), L"wb") != 0 || !f) return false;
  const bool ok = fwrite(b.data(), 1, b.size(), f) == b.size();
  fclose(f);
  return ok;
}

// Builds a minimal little-endian EXIF APP1 segment carrying Orientation.
// Layout: FFE1 | len(2) | "Exif\0\0" | "II" 2A00 08000000 | IFD0: count=1,
// entry(tag 0112, type 3 SHORT, count 1, value, pad) | nextIFD=0
std::vector<unsigned char> exifApp1(unsigned short orientation) {
  std::vector<unsigned char> tiff;
  tiff.push_back('I'); tiff.push_back('I');
  tiff.push_back(0x2A); tiff.push_back(0x00);
  tiff.push_back(0x08); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
  tiff.push_back(0x01); tiff.push_back(0x00);  // one entry
  tiff.push_back(0x12); tiff.push_back(0x01);  // tag 0x0112 Orientation
  tiff.push_back(0x03); tiff.push_back(0x00);  // type 3 = SHORT
  tiff.push_back(0x01); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);  // count 1
  tiff.push_back((unsigned char)(orientation & 0xFF));
  tiff.push_back((unsigned char)((orientation >> 8) & 0xFF));
  tiff.push_back(0x00); tiff.push_back(0x00);  // pad to 4 bytes
  tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);  // next IFD = 0

  std::vector<unsigned char> seg;
  seg.push_back(0xFF); seg.push_back(0xE1);
  const unsigned len = 2 + 6 + (unsigned)tiff.size();
  seg.push_back((unsigned char)((len >> 8) & 0xFF));
  seg.push_back((unsigned char)(len & 0xFF));
  const char* id = "Exif";
  for (int i = 0; i < 5; ++i) seg.push_back((unsigned char)id[i]);
  seg.push_back(0x00);
  seg.insert(seg.end(), tiff.begin(), tiff.end());
  return seg;
}

// Writes <srcJpeg> + an EXIF APP1 orientation segment. The standard dataset is
// never modified: the fixture is written to a separate directory.
//
// The segment is inserted after any leading APP0/JFIF rather than immediately
// after SOI. Inserting straight after SOI produces FF D8 FF E1 ... FF E0, i.e. an
// APP1 ahead of the JFIF APP0, and WIC's metadata reader then reports
// WINCODEC_ERR_PROPERTYNOTFOUND for every EXIF property -- the whole orientation
// check silently becomes vacuous. Camera files put APP0 first, so the fixture
// does too.
bool writeExifJpeg(const std::string& srcJpeg, const std::string& dst, unsigned short orientation) {
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, toWide(srcJpeg).c_str(), L"rb") != 0 || !f) return false;
  std::vector<unsigned char> in;
  unsigned char buf[8192];
  std::size_t got = 0;
  while ((got = fread(buf, 1, sizeof(buf), f)) > 0) in.insert(in.end(), buf, buf + got);
  fclose(f);
  if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xD8) return false;  // must be a JPEG

  // Find the insertion point: past SOI, and past a leading APP0 if present.
  std::size_t at = 2;
  if (in.size() > 4 && in[2] == 0xFF && in[3] == 0xE0) {
    const std::size_t segLen = ((std::size_t)in[4] << 8) | in[5];
    at = 4 + segLen;
    if (at > in.size()) return false;
  }

  const std::vector<unsigned char> app1 = exifApp1(orientation);
  std::vector<unsigned char> out;
  out.reserve(in.size() + app1.size());
  out.insert(out.end(), in.begin(), in.begin() + (long)at);
  out.insert(out.end(), app1.begin(), app1.end());
  out.insert(out.end(), in.begin() + (long)at, in.end());
  std::FILE* g = nullptr;
  if (_wfopen_s(&g, toWide(dst).c_str(), L"wb") != 0 || !g) return false;
  const bool ok = fwrite(out.data(), 1, out.size(), g) == out.size();
  fclose(g);
  return ok;
}
#endif  // _WIN32

// ------------------------------------------------------------------ selfcheck

#ifdef _WIN32
// Builds a JPEG carrying an EXIF Orientation tag, from scratch and without any
// dataset. WIC cannot encode from raw memory directly, so the tiny BMP already
// written above is decoded to a source and re-encoded as JPEG, then the EXIF
// APP1 is inserted. That keeps the regression test self-contained.
//
// This is the guard for the production defect fixed in v0.9.4.35: the product
// queried a path WIC rejects, so orientation was never applied anywhere.
static bool writeTinyJpegWithExif(const std::string& bmpPath, const std::string& jpgPath,
                                   unsigned short orientation) {
  HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(cohr)) return false;
  bool ok = false;
  {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> encFrame;
    ComPtr<IStream> stream;
    {
      HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
      if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
      if (FAILED(hr) || !factory) goto done;
      hr = factory->CreateDecoderFromFilename(toWide(bmpPath).c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, &dec);
      if (FAILED(hr)) goto done;
      hr = dec->GetFrame(0, &frame);
      if (FAILED(hr)) goto done;
      hr = CreateStreamOnHGlobal(nullptr, TRUE, &stream);
      if (FAILED(hr)) goto done;
      hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);
      if (FAILED(hr)) goto done;
      hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
      if (FAILED(hr)) goto done;
      hr = encoder->CreateNewFrame(&encFrame, nullptr);
      if (FAILED(hr)) goto done;
      hr = encFrame->Initialize(nullptr);
      if (FAILED(hr)) goto done;
      hr = encFrame->WriteSource(frame.Get(), nullptr);
      if (FAILED(hr)) goto done;
      hr = encFrame->Commit();
      if (FAILED(hr)) goto done;
      hr = encoder->Commit();
      if (FAILED(hr)) goto done;
    }
    // Pull the encoded JPEG bytes back out of the stream.
    {
      HGLOBAL hglob = nullptr;
      if (FAILED(GetHGlobalFromStream(stream.Get(), &hglob)) || !hglob) goto done;
      const SIZE_T size = GlobalSize(hglob);
      const void* base = GlobalLock(hglob);
      if (!base || size < 4) { if (base) GlobalUnlock(hglob); goto done; }
      const unsigned char* b = (const unsigned char*)base;
      if (b[0] != 0xFF || b[1] != 0xD8) { GlobalUnlock(hglob); goto done; }
      // Insert after SOI and after any leading APP0/JFIF, matching what a camera
      // writes and what the production reader must therefore handle.
      std::size_t at = 2;
      if (size > 4 && b[2] == 0xFF && b[3] == 0xE0) {
        const std::size_t segLen = ((std::size_t)b[4] << 8) | b[5];
        at = 4 + segLen;
        if (at > size) { GlobalUnlock(hglob); goto done; }
      }
      const std::vector<unsigned char> app1 = exifApp1(orientation);
      std::vector<unsigned char> out;
      out.reserve(size + app1.size());
      out.insert(out.end(), b, b + at);
      out.insert(out.end(), app1.begin(), app1.end());
      out.insert(out.end(), b + at, b + size);
      GlobalUnlock(hglob);

      std::FILE* g = nullptr;
      if (_wfopen_s(&g, toWide(jpgPath).c_str(), L"wb") != 0 || !g) goto done;
      const bool wrote = fwrite(out.data(), 1, out.size(), g) == out.size();
      fclose(g);
      ok = wrote;
    }
  }
done:
  CoUninitialize();
  return ok;
}

// Verification item C: a non-identity transform source (FlipRotator) shared by
// two independent scaler chains, plus a second independent readout of the same
// source to prove the source is re-readable rather than single-use.
//
// The EXIF metadata branch that normally produces a FlipRotator is exercised
// separately against synthesized EXIF JPEGs; this drives the rotator directly so
// the check needs no JPEG.
//
// Like runSharedPipeline, every WIC object is owned here and released on return,
// so the caller can safely run CoUninitialize afterwards. Getting this wrong is
// not hypothetical: an earlier revision of this file called CoUninitialize while
// the factory, decoder, frame, rotator and both converters were still alive, and
// the process died with an access violation on scope exit -- the same failure the
// product source documents at src/image_decoder.cpp:150-153.
static bool verifySharedRotatorSource(const std::string& bmp) {
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> d;
  ComPtr<IWICBitmapFrameDecode> fr;
  ComPtr<IWICBitmapFlipRotator> rot;
  ComPtr<IWICBitmapScaler> sc2;
  ComPtr<IWICFormatConverter> cv2;
  msf::GrayImage g1, g2, g3;
  {
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) { std::printf("  (C) factory failed 0x%08lX\n", (unsigned long)hr); return false; }
    hr = factory->CreateDecoderFromFilename(toWide(bmp).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &d);
    if (FAILED(hr)) { std::printf("  (C) decoder failed 0x%08lX\n", (unsigned long)hr); return false; }
    hr = d->GetFrame(0, &fr);
    if (FAILED(hr)) { std::printf("  (C) frame failed 0x%08lX\n", (unsigned long)hr); return false; }
    hr = factory->CreateBitmapFlipRotator(&rot);
    if (SUCCEEDED(hr)) hr = rot->Initialize(fr.Get(), WICBitmapTransformRotate90);
    if (FAILED(hr)) { std::printf("  (C) rotator failed 0x%08lX\n", (unsigned long)hr); return false; }
    UINT rw = 0, rh = 0;
    if (FAILED(rot->GetSize(&rw, &rh)) || rw != 6 || rh != 8) {
      std::printf("  (C) rotated source size %ux%u (expected 6x8)\n", rw, rh);
      return false;
    }
    int gw = 0, gh = 0;
    aspectDims((int)rw, (int)rh, 64, gw, gh);
    // Two independent scaler+converter chains over the one rotator source.
    for (int k = 0; k < 2; ++k) {
      ComPtr<IWICBitmapScaler> sc;
      ComPtr<IWICFormatConverter> cv;
      const int w = (k == 0) ? 64 : gw, h = (k == 0) ? 64 : gh;
      hr = factory->CreateBitmapScaler(&sc);
      if (SUCCEEDED(hr)) hr = sc->Initialize(rot.Get(), (UINT)w, (UINT)h, WICBitmapInterpolationModeFant);
      if (FAILED(hr)) { std::printf("  (C) branch %d scaler failed 0x%08lX\n", k, (unsigned long)hr); return false; }
      hr = factory->CreateFormatConverter(&cv);
      if (SUCCEEDED(hr)) hr = cv->Initialize(sc.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
      if (FAILED(hr)) { std::printf("  (C) branch %d converter failed 0x%08lX\n", k, (unsigned long)hr); return false; }
      msf::GrayImage& dst = (k == 0) ? g1 : g2;
      dst.width = w; dst.height = h;
      dst.pixels.resize((std::size_t)w * h);
      hr = cv->CopyPixels(nullptr, (UINT)w, (UINT)dst.pixels.size(), dst.pixels.data());
      if (FAILED(hr)) { std::printf("  (C) branch %d CopyPixels failed 0x%08lX\n", k, (unsigned long)hr); return false; }
    }
    // Second, independent readout of the same shared source: a source that could
    // only be consumed once would disagree here.
    hr = factory->CreateBitmapScaler(&sc2);
    if (SUCCEEDED(hr)) hr = sc2->Initialize(rot.Get(), (UINT)gw, (UINT)gh, WICBitmapInterpolationModeFant);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&cv2);
    if (SUCCEEDED(hr)) hr = cv2->Initialize(sc2.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { std::printf("  (C) second readout failed 0x%08lX\n", (unsigned long)hr); return false; }
    g3.width = gw; g3.height = gh;
    g3.pixels.resize((std::size_t)gw * gh);
    hr = cv2->CopyPixels(nullptr, (UINT)gw, (UINT)g3.pixels.size(), g3.pixels.data());
    if (FAILED(hr)) { std::printf("  (C) second readout CopyPixels failed 0x%08lX\n", (unsigned long)hr); return false; }
    if (compareImages(g2, g3).kind != "identical") {
      std::printf("  (C) shared source is not re-readable: second readout differs\n");
      return false;
    }
  }  // every WIC object above is released here
  return true;
}

// Diagnostic: what does WIC's metadata reader actually see in a synthesized
// fixture? Used to tell "orientation not applied" apart from "orientation tag
// never parsed", which are very different findings.
static void dumpExifMetadata(const std::string& path) {
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> dec;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICMetadataQueryReader> meta;
  {
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return;
    hr = factory->CreateDecoderFromFilename(toWide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec);
    if (FAILED(hr)) { std::printf("    meta: decoder hr=0x%08lX\n", (unsigned long)hr); return; }
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) { std::printf("    meta: frame hr=0x%08lX\n", (unsigned long)hr); return; }
    hr = frame->GetMetadataQueryReader(&meta);
    if (FAILED(hr) || !meta) { std::printf("    meta: query reader hr=0x%08lX\n", (unsigned long)hr); return; }
    // Enumerate whatever the reader actually exposes, so the query path is not
    // guessed: if a synthesized EXIF is invisible here, the orientation check
    // would be vacuous and must be reported as such.
    // The query paths must be real wide strings. An earlier revision of this file
    // kept them as `const char*` and passed `LPCWSTR(nm)`, which reinterprets the
    // UTF-8 bytes as UTF-16 code units; every lookup then addressed a garbage
    // path and reported WINCODEC_ERR_PROPERTYNOTFOUND. That was a defect in the
    // probe, not evidence about the product's EXIF path. The first entry is the
    // exact literal the product uses (src/image_decoder.cpp:194/267/321).
    //
    // Enumerating every node would need the WICMetadataSDK headers, which this
    // project does not depend on, so the paths are probed directly instead.
    const wchar_t* names[] = {
      L"/app1/ifd/exif/{ushort=274}",
      L"/app1/ifd0/exif/{ushort=274}",
      L"/app1/ifd/{ushort=274}",
      L"/app1/exif/{ushort=274}",
      L"/app1/ifd0/{ushort=274}",
      L"/app1/ifd0/{ushort=271}",
    };
    for (const wchar_t* nm : names) {
      PROPVARIANT v; PropVariantInit(&v);
      const HRESULT q = meta->GetMetadataByName(nm, &v);
      std::printf("    meta: %-34ls hr=0x%08lX vt=%u", nm, (unsigned long)q, (unsigned)v.vt);
      if (SUCCEEDED(q) && v.vt == VT_UI2) std::printf(" value=%u", (unsigned)v.uiVal);
      if (SUCCEEDED(q) && v.vt == VT_UI4) std::printf(" value=%u", (unsigned)v.ulVal);
      if (SUCCEEDED(q) && v.vt == VT_LPWSTR && v.pwszVal) std::wprintf(L" value=%s", v.pwszVal);
      std::printf("\n");
      PropVariantClear(&v);
    }
  }
}

// Reads the EXIF orientation tag the same way the product does, with the same
// wide literal (src/image_decoder.cpp:194). Returns true only when the tag is
// actually found, so a caller can tell "orientation applied" from "tag never
// parsed" instead of inferring it.
static bool readOrientationTag(const std::string& path, unsigned& out) {
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> dec;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICMetadataQueryReader> meta;
  {
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) return false;
    hr = factory->CreateDecoderFromFilename(toWide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) return false;
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) return false;
    hr = frame->GetMetadataQueryReader(&meta);
    if (FAILED(hr) || !meta) return false;
    PROPVARIANT v; PropVariantInit(&v);
    const bool ok = SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}", &v)) && v.vt == VT_UI2;
    if (ok) out = (unsigned)v.uiVal;
    PropVariantClear(&v);
    return ok;
  }
}

// Reads the EXIF orientation tag twice: once through the exact literal the
// product uses, and once through the path that WIC actually accepts. Reporting
// both is the point -- a single path proving "not found" would be ambiguous
// between a missing tag and a malformed query.
//
// Evidence from this probe (v0.9.4.34):
//   /app1/ifd/exif/{ushort=274}  -> WINCODEC_ERR_BADPROPERTYKEY (0x88982F40)
//   /app1/ifd/{ushort=274}       -> S_OK, VT_UI2, the expected value
// The orientation tag lives in IFD0, which WIC exposes as /app1/ifd/, and the
// "exif" segment of that path is rejected as a bad key.
static bool readOrientationTagBothPaths(const std::string& path, unsigned& productPathOk,
                                        unsigned& workingPathOk, std::uint32_t& productHr,
                                        unsigned& productValue, unsigned& workingValue) {
  productPathOk = 0; workingPathOk = 0; productHr = 0; productValue = 0; workingValue = 0;
  // COM must be initialized on this thread before any WIC object is created.
  // Without this the factory call fails and every result below would look like
  // "property absent", which is exactly the kind of false negative this whole
  // correction is about.
  const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(cohr)) return false;
  bool ok = false;
  {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> meta;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) goto done;
    hr = factory->CreateDecoderFromFilename(toWide(path).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) goto done;
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) goto done;
    hr = frame->GetMetadataQueryReader(&meta);
    if (FAILED(hr) || !meta) goto done;

    PROPVARIANT v; PropVariantInit(&v);
    const HRESULT q1 = meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}", &v);
    productHr = (std::uint32_t)q1;
    if (SUCCEEDED(q1) && v.vt == VT_UI2) { productValue = (unsigned)v.uiVal; productPathOk = 1; }
    PropVariantClear(&v);

    PROPVARIANT w; PropVariantInit(&w);
    if (SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/{ushort=274}", &w)) && w.vt == VT_UI2) {
      workingValue = (unsigned)w.uiVal; workingPathOk = 1;
    }
    PropVariantClear(&w);
    ok = true;
  }
done:
  CoUninitialize();
  return ok;
}

// The EXIF Orientation value mapped to the WIC transform, exactly as
// src/image_decoder.cpp:194-203 maps it.
static WICBitmapTransformOptions transformForOrientation(unsigned o) {
  switch (o) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
  }
}

// Shared-source candidate with the orientation transform FORCED, so the rotated
// path can be exercised even though the product's own metadata query does not
// resolve. Same two-independent-scaler structure as the measured candidate.
static bool candidateSharedSourceXform(const std::wstring& wpath, int maxDim,
                                       WICBitmapTransformOptions xform,
                                       msf::GrayImage& f, msf::GrayImage& a, CandTelemetry& tel) {
  const auto tAll0 = std::chrono::steady_clock::now();
  const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(cohr)) { tel.failHr = (std::uint32_t)cohr; tel.totalMs = nowMs(tAll0); return false; }
  CandResult r;
  tel.ok = runSharedPipelineXform(wpath, maxDim, xform, f, a, r);
  CoUninitialize();
  tel.factoryMs = r.factoryMs; tel.openMs = r.openMs; tel.frameMs = r.frameMs;
  tel.metaMs = r.metaMs; tel.orientMs = r.orientMs;
  tel.fScalerMs = r.fScalerMs; tel.fConvMs = r.fConvMs; tel.fCopyMs = r.fCopyMs;
  tel.aScalerMs = r.aScalerMs; tel.aConvMs = r.aConvMs; tel.aCopyMs = r.aCopyMs;
  tel.orientApplied = r.orientApplied;
  tel.srcW = r.srcW; tel.srcH = r.srcH; tel.aW = r.aW; tel.aH = r.aH;
  tel.failHr = r.failHr;
  tel.sharedMs = tel.comMs + tel.factoryMs + tel.openMs + tel.frameMs + tel.metaMs + tel.orientMs;
  tel.totalMs = nowMs(tAll0);
  return tel.ok;
}

// Reference: TWO fully independent pipelines, each with its own factory,
// decoder, frame and rotator, producing f0 then a0. Used to check that sharing
// one rotated source changes nothing.
static bool independentPairXform(const std::wstring& wpath, int maxDim,
                                 WICBitmapTransformOptions xform,
                                 msf::GrayImage& f0, msf::GrayImage& f1,
                                 msf::GrayImage& a0, msf::GrayImage& a1,
                                 CandResult& r) {
  // COM must be initialized on this thread (same reason as
  // readOrientationTagBothPaths), and every WIC object is scoped inside the loop
  // body so it is released before CoUninitialize.
  const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(cohr)) return false;
  bool allOk = true;
  for (int k = 0; k < 2 && allOk; ++k) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapFlipRotator> rot;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> conv;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) { allOk = false; break; }
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr)) { allOk = false; break; }
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) { allOk = false; break; }
    ComPtr<IWICBitmapSource> src = frame;
    if (xform != WICBitmapTransformRotate0) {
      hr = factory->CreateBitmapFlipRotator(&rot);
      if (SUCCEEDED(hr)) hr = rot->Initialize(frame.Get(), xform);
      if (FAILED(hr)) { allOk = false; break; }
      src = rot;
    }
    UINT sw = 0, sh = 0;
    if (FAILED(src->GetSize(&sw, &sh)) || !sw || !sh) { allOk = false; break; }
    int aw = 0, ah = 0;
    aspectDims((int)sw, (int)sh, maxDim, aw, ah);
    const int w = (k == 0) ? maxDim : aw, h = (k == 0) ? maxDim : ah;
    if (k == 0) { r.srcW = (int)sw; r.srcH = (int)sh; r.aW = aw; r.aH = ah; }
    hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) hr = scaler->Initialize(src.Get(), (UINT)w, (UINT)h, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) { allOk = false; break; }
    hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) { allOk = false; break; }
    msf::GrayImage& dst = (k == 0) ? f0 : a0;
    dst.width = w; dst.height = h; dst.pixels.resize((std::size_t)w * h);
    hr = conv->CopyPixels(nullptr, (UINT)w, (UINT)dst.pixels.size(), dst.pixels.data());
    if (FAILED(hr)) { allOk = false; break; }
  }
  CoUninitialize();   // strictly after every WIC object above was released
  return allOk;
}

// ------------------------------------------------------------- full-scan groups
//
// Compares the grouping the scan would produce under the baseline buffers with
// the grouping it would produce under the candidate buffers.
//
// Both sides use the product's own deciding function, verifyScorePlan
// (src/image_verify.h:30), which is what verifyImagePair calls internally. The
// buffers differ only in which pipeline produced them. Pairs are enumerated
// exhaustively over the files that decoded on both sides, so no prefilter and no
// sampling decides which pairs are considered.
int runGroups(const std::string& root, int stride, bool useProductionCandidate) {
  msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(msf::path_to_utf8(root));
  if (fp.state != "measured") { std::cerr << "dataset fingerprint state=" << fp.state << "\n"; return 2; }
  std::printf("dataset_matches_standard=%s files=%llu bytes=%llu fingerprint=%s\n",
              (fp.fileCount == 3347u && fp.totalBytes == 102475315ull &&
               fp.fingerprint == "e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a") ? "yes" : "no",
              (unsigned long long)fp.fileCount, (unsigned long long)fp.totalBytes, fp.fingerprint.c_str());

  struct G { std::string rel, abs; msf::GrayImage f0, a0, f1, a1; };
  std::vector<G> items;
  {
    std::error_code ec;
    std::set<std::string> seen;
    for (const auto& f : std::filesystem::recursive_directory_iterator(msf::path_from_utf8(root), ec)) {
      if (!f.is_regular_file()) continue;
      const std::string rel = msf::canonicalRelativePath(msf::path_to_utf8(root), msf::path_to_utf8(f.path()));
      if (!seen.insert(rel).second) continue;
      const std::string abs = msf::path_to_utf8(std::filesystem::path(root) / std::filesystem::path(rel));
      msf::DecodeTelemetry telF, telA;
      G g; g.rel = rel; g.abs = abs;
      const bool okBase = msf::ImageDecoder().decode(abs, 64, 64, g.f0, &telF) &&
                          msf::ImageDecoder().decodePreserveAspect(abs, 64, g.a0, &telA);
      // The candidate is the production entry point when asked for it, so the
      // exhaustive sweep validates the code that actually ships rather than the
      // probe's own re-implementation of the I-2 structure.
      bool okCand = false;
      if (useProductionCandidate) {
        msf::DecodeTelemetry cF, cA;
        okCand = msf::ImageDecoder().decodeBoth(abs, 64, 64, 64, g.f1, g.a1, &cF, &cA);
      } else {
        CandTelemetry ct;
        okCand = candidateSharedSource(toWide(abs), 64, g.f1, g.a1, ct);
      }
      if (okBase && okCand) items.push_back(std::move(g));
    }
  }
  std::sort(items.begin(), items.end(), [](const G& a, const G& b) { return a.rel < b.rel; });
  std::printf("group_corpus_files=%d stride=%d candidate=%s (1 = exhaustive)\n",
              (int)items.size(), stride, useProductionCandidate ? "production-decodeBoth" : "probe-I-2");
  if (items.size() < 2) return 2;

  const double kThreshold = 87.5;  // scan maxDistance 8, thresholdFor()
  long long pairs = 0, matchB = 0, matchC = 0, verdictDiff = 0;
  double maxAbsScoreDiff = 0.0;
  std::vector<std::string> diffExamples;
  for (std::size_t i = 0; i < items.size(); ++i) {
    for (std::size_t j = i + 1; j < items.size(); j += (std::size_t)stride) {
      ++pairs;
      const double sb = msf::verifyScorePlan(items[i].f0, items[i].a0, items[j].f0, items[j].a0, 90.0, nullptr);
      const double sc = msf::verifyScorePlan(items[i].f1, items[i].a1, items[j].f1, items[j].a1, 90.0, nullptr);
      const double d = std::fabs(sb - sc);
      if (d > maxAbsScoreDiff) maxAbsScoreDiff = d;
      const bool mb = (sb >= kThreshold), mc = (sc >= kThreshold);
      if (mb) ++matchB;
      if (mc) ++matchC;
      if (mb != mc) {
        ++verdictDiff;
        if (diffExamples.size() < 10) {
          char buf[512];
          _snprintf_s(buf, sizeof(buf), _TRUNCATE, "  verdict_diff %s <> %s base=%.6f cand=%.6f",
                      items[i].rel.c_str(), items[j].rel.c_str(), sb, sc);
          diffExamples.push_back(buf);
        }
      }
    }
  }
  for (const auto& s : diffExamples) std::printf("%s\n", s.c_str());
  std::printf("pairs_compared=%lld\n", pairs);
  std::printf("baseline_groups=%lld\n", matchB);
  std::printf("candidate_groups=%lld\n", matchC);
  std::printf("group_membership_verdict_diffs=%lld\n", verdictDiff);
  std::printf("max_abs_score_diff=%.9f\n", maxAbsScoreDiff);
  std::printf("group_parity=%s\n", (verdictDiff == 0 && matchB == matchC) ? "identical" : "DIFFERS");
  return 0;
}

// ------------------------------------------------------------ EXIF fixtures
//
// The standard dataset reports orient_applied_files = 0, so the EXIF branch is
// never exercised by the corpus run. The standard dataset is NOT modified: an
// EXIF APP1 segment carrying Orientation is inserted after the SOI of a JPEG
// that is already in the dataset, and the result is written to a separate
// directory. The source JPEG is a fixed dataset file, so the fixtures are
// reproducible from the recorded dataset fingerprint.
int runExif(const std::string& root, const std::string& outDir) {
  std::string srcJpeg;
  {
    std::error_code ec;
    const std::string jpgDir = msf::path_to_utf8(std::filesystem::path(root) / "images" / "format" / "jpg");
    std::vector<std::string> all;
    for (const auto& f : std::filesystem::directory_iterator(msf::path_from_utf8(jpgDir), ec)) {
      if (f.is_regular_file()) all.push_back(msf::path_to_utf8(f.path()));
    }
    std::sort(all.begin(), all.end());
    for (const auto& p : all) {
      // Prefer a small file so the fixture set stays cheap to write.
      std::error_code ec2;
      const auto sz = std::filesystem::file_size(msf::path_from_utf8(p), ec2);
      if (ec2) continue;
      if (!srcJpeg.empty() && std::filesystem::file_size(msf::path_from_utf8(srcJpeg), ec2) <= sz) continue;
      srcJpeg = p;
    }
  }
  if (srcJpeg.empty()) { std::cerr << "no source JPEG found\n"; return 2; }
  std::printf("exif_source=%s\n", srcJpeg.c_str());
  std::error_code ec;
  std::filesystem::create_directories(msf::path_from_utf8(outDir), ec);

  msf::ImageDecoder dec;
  int fixtures = 0;
  int baselineOrientApplied = 0, candidateOrientApplied = 0;
  int metadataReadOk = 0, orientationValueMatched = 0;
  int productPathOk = 0, productPathBadKey = 0;
  int sharedVsIndependentTotal = 0, sharedVsIndependentFailTotal = 0, rotationExercisedTotal = 0;
  int parity = 0, mismatch = 0, decodeFail = 0, candFail = 0;
  for (unsigned o = 1; o <= 8; ++o) {
    char name[64];
    _snprintf_s(name, sizeof(name), _TRUNCATE, "exif_o%u.jpg", o);
    const std::string path = outDir + "/" + name;
    if (!writeExifJpeg(srcJpeg, path, (unsigned short)o)) { std::printf("  o=%u write failed\n", o); continue; }
    ++fixtures;

    msf::DecodeTelemetry telF, telA;
    msf::GrayImage f0, a0;
    const bool okBase = dec.decode(path, 64, 64, f0, &telF) && dec.decodePreserveAspect(path, 64, a0, &telA);
    if (!okBase) { ++decodeFail; std::printf("  o=%u baseline decode FAILED\n", o); continue; }

    msf::GrayImage f1, a1;
    CandTelemetry ct;
    if (!candidateSharedSource(toWide(path), 64, f1, a1, ct)) {
      ++candFail; std::printf("  o=%u candidate FAILED hr=0x%08lX\n", o, (unsigned long)ct.failHr); continue;
    }
    // The two counters are independent measurements. An earlier revision routed
    // the baseline count into one variable and read a never-incremented second
    // one, which always reported 0; that bookkeeping error is fixed here.
    if (telF.orientApplied + telA.orientApplied > 0) ++baselineOrientApplied;
    if (ct.orientApplied > 0) ++candidateOrientApplied;

    // Read the orientation through BOTH paths, and additionally verify the
    // shared-vs-independent pipeline equality while a rotation is genuinely
    // applied. The product cannot be used as the reference here, because its
    // own query path never resolves, so the reference is an independent
    // two-pipeline decode of the same file with the same transform.
    unsigned pOk = 0, wOk = 0, pVal = 0, wVal = 0;
    std::uint32_t pHr = 0;
    const bool anyRead = readOrientationTagBothPaths(path, pOk, wOk, pHr, pVal, wVal);
    const unsigned readValue = wOk ? wVal : 0;
    if (anyRead && wOk) ++metadataReadOk;
    if (anyRead && wOk && readValue == o) ++orientationValueMatched;
    if (anyRead && pOk) ++productPathOk;
    if (anyRead && pHr == 0x88982F40u) ++productPathBadKey;

    // I-2's actual question: does sharing one rotated source across two scaler
    // branches still match two fully independent pipelines? Run it with the
    // transform the fixture declares, so rotation is genuinely exercised.
    int sharedVsIndependent = 0, sharedVsIndependentFail = 0, rotationExercised = 0;
    {
      const WICBitmapTransformOptions xf = transformForOrientation(o);
      if (xf != WICBitmapTransformRotate0) {
        msf::GrayImage sf, sa, if0, if1, ia0, ia1;
        CandTelemetry ctS;
        CandResult cr;
        const bool okShared = candidateSharedSourceXform(toWide(path), 64, xf, sf, sa, ctS);
        const bool okInd = independentPairXform(toWide(path), 64, xf, if0, if1, ia0, ia1, cr);
        if (okShared && okInd) {
          ++rotationExercised;
          if (compareImages(sf, if0).kind == "identical" && compareImages(sa, ia0).kind == "identical")
            ++sharedVsIndependent;
          else
            ++sharedVsIndependentFail;
        } else {
          ++sharedVsIndependentFail;
        }
      }
    }
    if (rotationExercised) ++rotationExercisedTotal;
    if (sharedVsIndependent) ++sharedVsIndependentTotal;
    if (sharedVsIndependentFail) ++sharedVsIndependentFailTotal;

    const PixelDiff df = compareImages(f0, f1);
    const PixelDiff da = compareImages(a0, a1);
    if (df.kind == "identical" && da.kind == "identical") ++parity;
    else {
      ++mismatch;
      std::printf("  o=%u MISMATCH f=%s/%llu a=%s/%llu src=%dx%d a=%dx%d\n", o,
                  df.kind, (unsigned long long)df.diffCount, da.kind, (unsigned long long)da.diffCount,
                  ct.srcW, ct.srcH, ct.aW, ct.aH);
    }
    std::printf("  o=%u productPath_ok=%u productPath_hr=0x%08lX workingPath_ok=%u workingValue=%u base_orientApplied=%llu cand_orientApplied=%llu sharedVsIndep=%s src=%dx%d a=%dx%d parity=%s\n",
                o, pOk, (unsigned long)pHr, wOk, readValue,
                (unsigned long long)(telF.orientApplied + telA.orientApplied),
                (unsigned long long)ct.orientApplied,
                (rotationExercised ? (sharedVsIndependent > 0 ? "identical" : "DIFFERS") : "n/a"),
                ct.srcW, ct.srcH, ct.aW, ct.aH,
                (df.kind == "identical" && da.kind == "identical") ? "identical" : "DIFFERS");
  }
  std::printf("exif fixtures=%d baseline_decode_fail=%d candidate_fail=%d parity_ok=%d parity_mismatch=%d\n",
              fixtures, decodeFail, candFail, parity, mismatch);
  std::printf("exif_metadata_read_ok=%d exif_orientation_value_matched=%d\n", metadataReadOk, orientationValueMatched);
  std::printf("exif_product_path_resolved=%d exif_product_path_badkey=%d\n", productPathOk, productPathBadKey);
  std::printf("exif_rotation_exercised=%d exif_shared_vs_independent_identical=%d exif_shared_vs_independent_differs=%d\n",
              rotationExercisedTotal, sharedVsIndependentTotal, sharedVsIndependentFailTotal);
  // Orientation 1 is the identity transform, so it is CORRECT that it is not
  // counted as applied. The expected applied count is therefore fixtures-1
  // (every orientation except 1), not fixtures.
  const int expectedApplied = fixtures - 1;
  std::printf("exif_expected_applied=%d (orientations 2..8; orientation 1 is the identity and is not counted as applied)\n",
              expectedApplied);
  std::printf("exif_baseline_orientation_applied=%d/%d\n", baselineOrientApplied, expectedApplied);
  std::printf("exif_candidate_orientation_applied=%d/%d\n", candidateOrientApplied, expectedApplied);
  const bool parityOk = (mismatch == 0 && candFail == 0 && decodeFail == 0);
  // The metadata dump runs after the fixtures exist, so it inspects a real file.
  if (fixtures > 0) {
    const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(cohr)) { dumpExifMetadata(outDir + "/exif_o6.jpg"); CoUninitialize(); }
  }

  // Two independent questions, reported separately.
  //
  // (1) I-2's structural question: does sharing ONE rotated source across two
  //     scaler branches still match two fully independent pipelines? Measured
  //     with the transform forced, so rotation really happens.
  // (2) End-to-end EXIF through the product path. The product queries
  //     /app1/ifd/exif/{ushort=274}, which WIC rejects with
  //     WINCODEC_ERR_BADPROPERTYKEY on every fixture, so the product never
  //     applies an orientation and this cannot be measured.
  const bool sharedUnderRotation = (rotationExercisedTotal > 0 && sharedVsIndependentFailTotal == 0);
  std::printf("exif_shared_source_under_rotation=%s (%d/%d identical)\n",
              sharedUnderRotation ? "PASS" : "FAIL",
              sharedVsIndependentTotal, rotationExercisedTotal);
  std::printf("exif_metadata_fixtures_valid=%s (%d/%d read, %d/%d value matched)\n",
              (metadataReadOk == fixtures && fixtures > 0) ? "yes" : "no",
              metadataReadOk, fixtures, orientationValueMatched, fixtures);
  std::printf("exif_legacy_query_path=/app1/ifd/exif/{ushort=274} (removed from product in 0.9.4.35) resolved=%d/%d badpropertykey=%d/%d\n",
              productPathOk, fixtures, productPathBadKey, fixtures);
  std::printf("exif_working_query_path=/app1/ifd/{ushort=274} resolved=%d/%d\n", metadataReadOk, fixtures);

  if (metadataReadOk == fixtures && parityOk && rotationExercisedTotal > 0 &&
      sharedVsIndependentFailTotal == 0 && fixtures > 0 &&
      baselineOrientApplied == expectedApplied && candidateOrientApplied == expectedApplied) {
    std::printf("exif_status=PASS\n");
  } else if (baselineOrientApplied > 0 || candidateOrientApplied > 0) {
    std::printf("exif_status=PARTIAL\n");
  } else {
    std::printf("exif_status=NOT_MEASURED\n");
  }
  std::printf("exif_status_reason=derived_from_measured_counts_see_exif_note\n");
  std::printf("exif_note=metadata_read_ok=%d/%d value_matched=%d legacy_path_resolved=%d/%d legacy_path_badkey=%d rotation_exercised=%d shared_vs_independent_identical=%d differs=%d baseline_applied=%d/%d candidate_applied=%d/%d parity_ok=%d/%d\n",
              metadataReadOk, fixtures, orientationValueMatched,
              productPathOk, fixtures, productPathBadKey,
              rotationExercisedTotal, sharedVsIndependentTotal, sharedVsIndependentFailTotal,
              baselineOrientApplied, fixtures, candidateOrientApplied, fixtures, parity, fixtures);
  return 0;
}

int selfcheck() {
  int checks = 0;
  // Fixtures live in the system temp directory, never in the repository tree.
  const std::string dir = msf::path_to_utf8(std::filesystem::temp_directory_path() /
                                            "msf_shared_wic_source_selfcheck");
  std::error_code ec;
  std::filesystem::create_directories(msf::path_from_utf8(dir), ec);
  const std::string bmp = dir + "/tiny.bmp";
  const std::string pgm = dir + "/tiny.pgm";
  if (!writeTinyBmp(bmp, 8, 6)) return fail("writeTinyBmp");
  ++checks;
  if (!writeTinyPgm(pgm, 8, 6)) return fail("writeTinyPgm");
  ++checks;

  msf::ImageDecoder dec;
  msf::DecodeTelemetry telF, telA;
  msf::GrayImage f0, a0;
  if (!dec.decode(bmp, 64, 64, f0, &telF)) return fail("baseline decode f");
  if (!dec.decodePreserveAspect(bmp, 64, a0, &telA)) return fail("baseline decode aspect");
  ++checks;

  msf::GrayImage f1, a1;
  CandTelemetry ct;
  if (!candidateSharedSource(toWide(bmp), 64, f1, a1, ct)) return fail("candidateSharedSource");
  ++checks;

  // H. output geometry
  if (f1.width != f0.width || f1.height != f0.height) return fail("f geometry");
  if (a1.width != a0.width || a1.height != a0.height) return fail("a geometry");
  ++checks;

  // I. byte compare
  const PixelDiff df = compareImages(f0, f1);
  const PixelDiff da = compareImages(a0, a1);
  if (df.kind != "identical") {
    std::printf("  f: kind=%s diff=%llu max=%llu\n", df.kind, (unsigned long long)df.diffCount, (unsigned long long)df.maxAbs);
    return fail("f byte parity");
  }
  if (da.kind != "identical") {
    std::printf("  a: kind=%s diff=%llu max=%llu\n", da.kind, (unsigned long long)da.diffCount, (unsigned long long)da.maxAbs);
    return fail("a byte parity");
  }
  ++checks;

  // A/E: the two branches really used two separate scaler+converter pairs.
  if (!(ct.fScalerMs >= 0 && ct.aScalerMs >= 0 && ct.fConvMs >= 0 && ct.aConvMs >= 0)) return fail("branch telemetry");
  if (ct.fCopyMs <= 0.0 || ct.aCopyMs <= 0.0) return fail("CopyPixels timing");
  ++checks;

  // C: a non-identity transform source shared by two independent scaler chains.
  // Owns and releases every WIC object before returning, so CoUninitialize may
  // follow safely. See the note on verifySharedRotatorSource about the earlier
  // access violation caused by inverting that order.
  {
    const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(cohr)) return fail("CoInitializeEx (C)");
    const bool ok = verifySharedRotatorSource(bmp);
    CoUninitialize();  // strictly after every WIC object was released
    if (!ok) return fail("shared rotator source (C)");
  }
  ++checks;
  // F: does the second CopyPixels reuse the first decode? Measured, not assumed.
  double copyA = 0.0, copyB = 0.0;
  for (int rep = 0; rep < 3; ++rep) {
    msf::GrayImage x, y;
    CandTelemetry t2;
    if (!candidateSharedSource(toWide(bmp), 64, x, y, t2)) return fail("repeat candidate (F)");
    copyA += t2.fCopyMs; copyB += t2.aCopyMs;
  }
  std::printf("  [F] CopyPixels A=%.4f ms  B=%.4f ms  ratio=%.2f (informational)\n",
              copyA / 3.0, copyB / 3.0, copyB > 0 ? copyA / copyB : 0.0);
  ++checks;

  // G: the candidate released everything before CoUninitialize; a second call
  // after the first must still work (no leaked-but-live WIC object).
  {
    msf::GrayImage x, y;
    CandTelemetry t3;
    if (!candidateSharedSource(toWide(bmp), 64, x, y, t3)) return fail("post-uninit reuse (G)");
  }
  ++checks;

  // J: PGM branch. WIC cannot read P5, so the product falls back; the candidate
  // analogue reads the file once and scales twice. Both must agree.
  {
    msf::GrayImage pf, pa, cf, ca;
    if (!dec.decode(pgm, 64, 64, pf)) return fail("baseline PGM fixed");
    if (!dec.decodePreserveAspect(pgm, 64, pa)) return fail("baseline PGM aspect");
    if (!candidatePgmDual(pgm, 64, cf, ca)) return fail("candidate PGM dual");
    if (compareImages(pf, cf).kind != "identical") return fail("PGM f parity");
    if (compareImages(pa, ca).kind != "identical") return fail("PGM a parity");
  }
  ++checks;

  // EXIF orientation regression, dataset-free. Guards the production defect
  // fixed in v0.9.4.35: with the old query path the product applied nothing, so
  // this would have reported applied=0 for every orientation.
  {
    const std::string jpg1 = dir + "/exif1.jpg";
    const std::string jpg6 = dir + "/exif6.jpg";
    if (!writeTinyJpegWithExif(bmp, jpg1, 1)) return fail("write EXIF orientation 1 fixture");
    if (!writeTinyJpegWithExif(bmp, jpg6, 6)) return fail("write EXIF orientation 6 fixture");
    ++checks;

    msf::DecodeTelemetry t1, t6, t1b;
    msf::GrayImage f1, a1, f6, a6, f6b, a6b;
    if (!dec.decode(jpg1, 64, 64, f1, &t1)) return fail("decode EXIF orientation 1");
    if (!dec.decodePreserveAspect(jpg1, 64, a1, &t1)) return fail("aspect decode EXIF orientation 1");
    if (!dec.decode(jpg6, 64, 64, f6, &t6)) return fail("decode EXIF orientation 6");
    if (!dec.decodePreserveAspect(jpg6, 64, a6, &t6)) return fail("aspect decode EXIF orientation 6");
    ++checks;

    // Orientation 1 is the identity: nothing must be applied.
    if (t1.orientApplied != 0) {
      std::printf("  EXIF orientation 1 applied %llu times, expected 0\n",
                  (unsigned long long)t1.orientApplied);
      return fail("EXIF orientation 1 must not be applied");
    }
    // Orientation 6 is a 90 degree rotation: the product must apply it.
    if (t6.orientApplied == 0) return fail("EXIF orientation 6 was not applied");
    ++checks;

    // The rotation must actually change the pixels, otherwise "applied" would
    // only mean a counter moved.
    if (compareImages(f1, f6).kind == "identical") {
      return fail("EXIF rotation produced identical fixed output");
    }
    if (compareImages(a1, a6).kind == "identical") {
      return fail("EXIF rotation produced identical aspect output");
    }
    ++checks;

    // The candidate must agree with the product on a rotated file, end to end.
    CandTelemetry telX;
    if (!candidateSharedSource(toWide(jpg6), 64, f6b, a6b, telX)) {
      return fail("candidate decode of EXIF-rotated file");
    }
    if (compareImages(f6, f6b).kind != "identical") return fail("EXIF fixed parity baseline vs candidate");
    if (compareImages(a6, a6b).kind != "identical") return fail("EXIF aspect parity baseline vs candidate");
    ++checks;

    std::printf("  [EXIF] orientation1 applied=%llu  orientation6 applied=%llu  parity=identical\n",
                (unsigned long long)t1.orientApplied, (unsigned long long)t6.orientApplied);
  }

  std::printf("selfcheck=ok checks=%d\n", checks);
  return 0;
}
#endif  // _WIN32

// --------------------------------------------------------------- full run

#ifdef _WIN32
struct File { std::string format, path, abs; };

// ------------------------------------------------- production entry-point test
//
// The probe's own candidateSharedSource() re-implements I-2, so it proves the
// STRUCTURE but not the code that actually ships. This mode compares the real
// production entry point ImageDecoder::decodeBoth() against the real
// pre-integration two-call baseline (decode + decodePreserveAspect) over every
// file in the dataset:
//
//   baseline  : decode(abs,64,64)                -> f0   + decodePreserveAspect(abs,64) -> a0
//   candidate : decodeBoth(abs,64,64,64)         -> f1, a1
//
// A candidate-side difference therefore means the integration changed bytes,
// geometry or failure behaviour, which is the thing that must never happen.
int runProduction(const std::string& root) {
  msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(msf::path_to_utf8(root));
  if (fp.state != "measured") {
    std::cerr << "dataset fingerprint state=" << fp.state << " (measurement stopped)\n";
    return 2;
  }
  std::printf("dataset_files=%llu dataset_bytes=%llu fingerprint=%s version=%d\n",
              (unsigned long long)fp.fileCount, (unsigned long long)fp.totalBytes,
              fp.fingerprint.c_str(), (int)msf::kDatasetFingerprintVersion);

  std::vector<File> files;
  {
    std::error_code ec;
    std::set<std::string> seen;
    int dupes = 0;
    for (const auto& f : std::filesystem::recursive_directory_iterator(msf::path_from_utf8(root), ec)) {
      if (!f.is_regular_file()) continue;
      const std::string p = msf::canonicalRelativePath(msf::path_to_utf8(root), msf::path_to_utf8(f.path()));
      if (!seen.insert(p).second) { ++dupes; continue; }
      const std::string abs = msf::path_to_utf8(std::filesystem::path(root) / std::filesystem::path(p));
      files.push_back({"all", p, abs});
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.path < b.path; });
    std::printf("files_collected=%d unique_files=%d duplicate_paths=%d\n",
                (int)files.size(), (int)seen.size(), dupes);
  }

  int bothOk = 0, bothFail = 0, baseOnlyFail = 0, candOnlyFail = 0;
  int fGeomDiff = 0, fPixDiff = 0, aGeomDiff = 0, aPixDiff = 0;
  long long fDiffPx = 0, aDiffPx = 0;
  unsigned fMaxAbs = 0, aMaxAbs = 0;
  std::vector<std::string> diffExamples;
  // Telemetry invariants that the integration must not break.
  int callsViolations = 0, aspectCallsViolations = 0, splitViolations = 0;
  std::vector<double> baseTotalMs, candTotalMs, candAspectBranchMs;

  for (const auto& f : files) {
    msf::GrayImage f0, a0, f1, a1;
    msf::DecodeTelemetry bF, bA;
    const bool okBase = msf::ImageDecoder().decode(f.abs, 64, 64, f0, &bF) &&
                        msf::ImageDecoder().decodePreserveAspect(f.abs, 64, a0, &bA);
    msf::DecodeTelemetry cF, cA;
    const bool okCand = msf::ImageDecoder().decodeBoth(f.abs, 64, 64, 64, f1, a1, &cF, &cA);

    if (okBase && okCand) {
      ++bothOk;
      if (f0.width != f1.width || f0.height != f1.height) { ++fGeomDiff;
        if (diffExamples.size() < 8) diffExamples.push_back(f.path + " fixed geometry"); }
      if (a0.width != a1.width || a0.height != a1.height) { ++aGeomDiff;
        if (diffExamples.size() < 8) diffExamples.push_back(f.path + " aspect geometry"); }
      if (f0.width == f1.width && f0.height == f1.height && f0.pixels != f1.pixels) {
        ++fPixDiff;
        const std::size_t n = f0.pixels.size() < f1.pixels.size() ? f0.pixels.size() : f1.pixels.size();
        for (std::size_t i = 0; i < n; ++i) {
          const int d = std::abs((int)f0.pixels[i] - (int)f1.pixels[i]);
          if (d) { ++fDiffPx; if ((unsigned)d > fMaxAbs) fMaxAbs = (unsigned)d; }
        }
        if (diffExamples.size() < 8) diffExamples.push_back(f.path + " fixed pixels");
      }
      if (a0.width == a1.width && a0.height == a1.height && a0.pixels != a1.pixels) {
        ++aPixDiff;
        const std::size_t n = a0.pixels.size() < a1.pixels.size() ? a0.pixels.size() : a1.pixels.size();
        for (std::size_t i = 0; i < n; ++i) {
          const int d = std::abs((int)a0.pixels[i] - (int)a1.pixels[i]);
          if (d) { ++aDiffPx; if ((unsigned)d > aMaxAbs) aMaxAbs = (unsigned)d; }
        }
        if (diffExamples.size() < 8) diffExamples.push_back(f.path + " aspect pixels");
      }
    } else if (!okBase && !okCand) {
      ++bothFail;
    } else if (okBase) {
      ++baseOnlyFail;
      if (diffExamples.size() < 8) diffExamples.push_back(f.path + " BASELINE_OK_CANDIDATE_FAIL");
    } else {
      ++candOnlyFail;
      if (diffExamples.size() < 8) diffExamples.push_back(f.path + " CANDIDATE_OK_BASELINE_FAIL");
    }

    // Per-file counters, not run totals: verifyBuffersFor uses one fresh
    // ImageDecoder per file, so each accumulator must see exactly 1 call and
    // 1 aspectCall, and the two totals must reconstruct the one call.
    if (okCand) {
      if (cF.calls != 1) ++callsViolations;
      if (cA.aspectCalls != 1) ++aspectCallsViolations;
      const double merged = cF.totalMs + cA.totalMs;
      const double bucketSum = cF.comInitMs + cF.factoryMs + cF.openMs + cF.metadataMs + cF.orientMs
                             + cF.resizeMs + cF.convertMs + cF.copyMs
                             + cA.resizeMs + cA.convertMs + cA.copyMs;
      // Buckets are measured spans inside the call, so they may not exceed it;
      // a large excess would mean a bucket was double counted.
      if (merged <= 0.0 || bucketSum > merged * 1.05) ++splitViolations;
      baseTotalMs.push_back(bF.totalMs + bA.totalMs);
      candTotalMs.push_back(merged);
      candAspectBranchMs.push_back(cA.totalMs);
    }
  }

  const int compared = bothOk;
  std::printf("both_success=%d base_only_fail=%d cand_only_fail=%d both_fail=%d\n",
              bothOk, baseOnlyFail, candOnlyFail, bothFail);
  std::printf("fixed_geometry_mismatch=%d fixed_pixel_mismatch=%d fixed_diff_px=%lld fixed_max_abs=%u\n",
              fGeomDiff, fPixDiff, fDiffPx, fMaxAbs);
  std::printf("aspect_geometry_mismatch=%d aspect_pixel_mismatch=%d aspect_diff_px=%lld aspect_max_abs=%u\n",
              aGeomDiff, aPixDiff, aDiffPx, aMaxAbs);
  std::printf("telemetry_calls_violations=%d aspect_calls_violations=%d split_identity_violations=%d\n",
              callsViolations, aspectCallsViolations, splitViolations);
  for (const auto& e : diffExamples) std::printf("  diff: %s\n", e.c_str());

  auto med = [](std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  };
  std::printf("baseline_two_call_total_median_ms=%.6f\n", med(baseTotalMs));
  std::printf("production_decodeBoth_total_median_ms=%.6f\n", med(candTotalMs));
  std::printf("production_aspect_branch_median_ms=%.6f\n", med(candAspectBranchMs));

  const bool pass = (compared > 0) && candOnlyFail == 0 && baseOnlyFail == 0 &&
                    fGeomDiff == 0 && fPixDiff == 0 && aGeomDiff == 0 && aPixDiff == 0 &&
                    callsViolations == 0 && aspectCallsViolations == 0 && splitViolations == 0;
  std::printf("production_exactness=%s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runFull(const std::string& root) {
  msf::DatasetFingerprint fp = msf::computeDatasetFingerprint(msf::path_to_utf8(root));
  if (fp.state != "measured") {
    std::cerr << "dataset fingerprint state=" << fp.state << " (measurement stopped)\n";
    return 2;
  }
  std::printf("dataset_files=%llu dataset_bytes=%llu fingerprint=%s version=%d\n",
              (unsigned long long)fp.fileCount, (unsigned long long)fp.totalBytes,
              fp.fingerprint.c_str(), (int)msf::kDatasetFingerprintVersion);
  // The sweep is only valid as a full-corpus result if the corpus matches the
  // recorded standard dataset. Otherwise the numbers describe something else.
  {
    const bool match = (fp.fileCount == 3347u && fp.totalBytes == 102475315ull &&
                        fp.fingerprint == "e8f8fa6ab0257f1e2b7d839b73ec14b1cc726695dac2efe79b55a13a13e2640a");
    std::printf("dataset_matches_standard=%s (expected 3347 files / 102475315 bytes / "
                "e8f8fa6a..e2640a)\n", match ? "yes" : "no");
  }

  std::vector<File> files;
  {
    std::error_code ec;
    // FULL CORPUS: every regular file under the dataset root, deduplicated by
    // path. The earlier revision collected images/format plus a ~200-file BMP
    // stride sample and reported 854 files, which is a probe corpus and NOT the
    // 3,347-file standard dataset. That distinction is the correction this
    // version makes, so no sampling is applied here.
    std::set<std::string> seen;
    int dupes = 0;
    for (const auto& f : std::filesystem::recursive_directory_iterator(msf::path_from_utf8(root), ec)) {
      if (!f.is_regular_file()) continue;
      const std::string p = msf::canonicalRelativePath(msf::path_to_utf8(root), msf::path_to_utf8(f.path()));
      if (!seen.insert(p).second) { ++dupes; continue; }
      // Keep the relative path for dedup/sorting/reporting, but the decoder
      // needs a path it can open, so store the joined absolute one as well.
      const std::string abs = msf::path_to_utf8(std::filesystem::path(root) / std::filesystem::path(p));
      files.push_back({"all", p, abs});
    }
    std::sort(files.begin(), files.end(), [](const File& a, const File& b) { return a.path < b.path; });
    std::printf("files_collected=%d unique_files=%d duplicate_paths=%d\n",
                (int)files.size(), (int)seen.size(), dupes);
  }

  struct Agg {
    int n = 0, baseFail = 0, candFail = 0, candOnlyFail = 0;
    int fIdent = 0, fGeomDiff = 0, fPixDiff = 0;
    int aIdent = 0, aGeomDiff = 0, aPixDiff = 0;
    std::uint64_t fDiffPx = 0, aDiffPx = 0, fMaxAbs = 0, aMaxAbs = 0;
    long long fSumAbs = 0, aSumAbs = 0;
    std::uint64_t orientApplied = 0;
    std::vector<double> fMs, aMs, baseTotal, baseTotalAdj, candTotal;
    std::vector<double> sharedMs, fBranchMs, aBranchMs, fCopyMs, aCopyMs;
    std::vector<double> baseFCopy, baseACopy;
    // Metadata cost is separated because the v0.9.4.35 EXIF fix adds a second
    // query path attempt on files that carry no EXIF at all, and the baseline
    // median rose with it. Measuring metadataMs is what separates "the extra
    // query" from "the machine got slower".
    double metaMsSum = 0, openMsSum = 0, orientMsSum = 0, copyMsSum = 0;
    std::string firstFail;
  };
  std::map<std::string, Agg> agg;
  std::vector<double> allBase, allBaseAdj, allCand, allShared, allFBranch, allABranch;
  std::vector<double> allCandFCopy, allCandACopy, allBaseFCopy, allBaseACopy;
  msf::ImageDecoder dec_;
  // Failure classification is four-way and exhaustive, so no file is silently
  // dropped: both_success / baseline_only_fail / candidate_only_fail / both_fail.
  int gBothSuccess = 0, gBaselineOnlyFail = 0, gCandidateOnlyFail = 0, gBothFail = 0;
  int gFIdent = 0, gFGeom = 0, gFPix = 0, gAIdent = 0, gAGeom = 0, gAPix = 0;
  int gFGeomMatch = 0, gAGeomMatch = 0;
  int gFixedMismatchFiles = 0, gAspectMismatchFiles = 0;
  std::uint64_t gFMaxAbs = 0, gAMaxAbs = 0;
  long long gFSumAbs = 0, gASumAbs = 0, gFComparedPx = 0, gAComparedPx = 0;
  int gFIdentTotal = 0, gAIdentTotal = 0;
  std::uint64_t gOrient = 0;

  for (const File& fl : files) {
    Agg& g = agg[fl.format];
    msf::DecodeTelemetry telF, telA;
    msf::GrayImage f0, a0;
    const bool okBase = dec_.decode(fl.abs, 64, 64, f0, &telF) && dec_.decodePreserveAspect(fl.abs, 64, a0, &telA);
    msf::GrayImage f1, a1;
    CandTelemetry ct;
    const bool okCand = candidateSharedSource(toWide(fl.abs), 64, f1, a1, ct);
    // Both sides recorded independently; a short-circuit here would hide one
    // side's failures behind the other's.
    if (okBase && okCand) ++gBothSuccess;
    else if (!okBase && okCand) { ++gBaselineOnlyFail; if (g.firstFail.empty()) g.firstFail = fl.path; }
    else if (okBase && !okCand) ++gCandidateOnlyFail;
    else {
      ++gBothFail;
      if (g.firstFail.empty()) g.firstFail = fl.path;
      if (g.candFail <= 8) {
        std::printf("  both_fail hr=0x%08lX %s\n", (unsigned long)ct.failHr, fl.path.c_str());
      }
    }
    if (!okCand) { ++g.candFail; ++g.candFail; }
    else if (!okBase) { ++g.baseFail; }
    if (!okBase || !okCand) continue;
    ++g.n;
    g.orientApplied += ct.orientApplied;
    gOrient += ct.orientApplied;

    const PixelDiff df = compareImages(f0, f1);
    if (df.kind == "identical") { ++g.fIdent; ++gFIdent; }
    else if (df.kind == "geometry") { ++g.fGeomDiff; ++gFGeom; }
    else {
      ++g.fPixDiff; ++gFPix; ++gFixedMismatchFiles;
      g.fDiffPx += df.diffCount; g.fSumAbs += df.sumAbs; gFSumAbs += df.sumAbs;
      gFComparedPx += (long long)f0.width * f0.height;
      if (df.maxAbs > g.fMaxAbs) g.fMaxAbs = df.maxAbs;
      if (df.maxAbs > gFMaxAbs) gFMaxAbs = df.maxAbs;
    }
    // Geometry match is a separate acceptance item from pixel match.
    if (f0.width == f1.width && f0.height == f1.height) ++gFGeomMatch;
    const PixelDiff da = compareImages(a0, a1);
    if (da.kind == "identical") { ++g.aIdent; ++gAIdent; }
    else if (da.kind == "geometry") { ++g.aGeomDiff; ++gAGeom; }
    else {
      ++g.aPixDiff; ++gAPix; ++gAspectMismatchFiles;
      g.aDiffPx += da.diffCount; g.aSumAbs += da.sumAbs; gASumAbs += da.sumAbs;
      gAComparedPx += (long long)a0.width * a0.height;
      if (da.maxAbs > g.aMaxAbs) g.aMaxAbs = da.maxAbs;
      if (da.maxAbs > gAMaxAbs) gAMaxAbs = da.maxAbs;
    }
    if (a0.width == a1.width && a0.height == a1.height) ++gAGeomMatch;

    g.metaMsSum += telF.metadataMs + telA.metadataMs;
    g.orientMsSum += telF.orientMs + telA.orientMs;
    g.openMsSum += telF.openMs + telA.openMs;
    g.copyMsSum += telF.copyMs + telA.copyMs;
    const double base = telF.totalMs + telA.totalMs;
    // The product decoder runs an extra CreateFileW+CloseHandle reference probe
    // when a telemetry sink is present. The candidate has no such probe, so the
    // adjusted baseline is reported next to the raw one to keep the comparison
    // honest.
    const double probe = telF.osFileOpenProbeMs + telA.osFileOpenProbeMs;
    const double baseAdj = base - probe;
    const double fbranch = ct.fScalerMs + ct.fConvMs + ct.fCopyMs;
    const double abranch = ct.aScalerMs + ct.aConvMs + ct.aCopyMs;
    g.fMs.push_back(telF.totalMs); g.aMs.push_back(telA.totalMs);
    g.baseTotal.push_back(base); g.baseTotalAdj.push_back(baseAdj); g.candTotal.push_back(ct.totalMs);
    g.sharedMs.push_back(ct.sharedMs); g.fBranchMs.push_back(fbranch); g.aBranchMs.push_back(abranch);
    g.fCopyMs.push_back(ct.fCopyMs); g.aCopyMs.push_back(ct.aCopyMs);
    g.baseFCopy.push_back(telF.copyMs); g.baseACopy.push_back(telA.copyMs);
    allBase.push_back(base); allBaseAdj.push_back(baseAdj); allCand.push_back(ct.totalMs);
    allShared.push_back(ct.sharedMs); allFBranch.push_back(fbranch); allABranch.push_back(abranch);
    allCandFCopy.push_back(ct.fCopyMs); allCandACopy.push_back(ct.aCopyMs);
    allBaseFCopy.push_back(telF.copyMs); allBaseACopy.push_back(telA.copyMs);
  }

  std::printf("\n=== parity and cost per format ===\n");
  for (auto& kv : agg) {
    Agg& g = kv.second;
    std::printf("format %-6s n=%d baseFail=%d candFail=%d candOnlyFail=%d\n", kv.first.c_str(), g.n, g.baseFail, g.candFail, g.candOnlyFail);
    std::printf("  parity  f: ident %d / geomdiff %d / pixdiff %d | a: ident %d / geomdiff %d / pixdiff %d\n",
                g.fIdent, g.fGeomDiff, g.fPixDiff, g.aIdent, g.aGeomDiff, g.aPixDiff);
    if (g.fPixDiff) std::printf("  f diffpx %llu max %llu meanabs %.3f\n", (unsigned long long)g.fDiffPx, (unsigned long long)g.fMaxAbs, g.fDiffPx ? (double)g.fSumAbs / (double)g.fDiffPx : 0.0);
    if (g.aPixDiff) std::printf("  a diffpx %llu max %llu meanabs %.3f\n", (unsigned long long)g.aDiffPx, (unsigned long long)g.aMaxAbs, g.aDiffPx ? (double)g.aSumAbs / (double)g.aDiffPx : 0.0);
    if (g.orientApplied) std::printf("  orient_applied=%llu\n", (unsigned long long)g.orientApplied);
    stats("baseline ms", g.baseTotal);
    stats("baseline adj ms", g.baseTotalAdj);
    stats("candidate ms", g.candTotal);
  }
  std::printf("\n=== full corpus classification ===\n");
  gFIdentTotal = gFIdent + gFGeom + gFPix;
  gAIdentTotal = gAIdent + gAGeom + gAPix;
  std::printf("both_success=%d\n", gBothSuccess);
  std::printf("baseline_only_fail=%d\n", gBaselineOnlyFail);
  std::printf("candidate_only_fail=%d\n", gCandidateOnlyFail);
  std::printf("both_fail=%d\n", gBothFail);
  std::printf("orient_applied=%llu\n", (unsigned long long)gOrient);
  // EXIF coverage over the standard corpus, now that the product query path is
  // fixed. Before the fix this was 0 for every file, which was a broken query
  // rather than an absence of EXIF.
  {
    long long metaPresent = 0, applied = 0, queryFail = 0, byValue[9] = {0,0,0,0,0,0,0,0,0};
    for (const File& fl : files) {
      unsigned val = 0;
      const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
      if (FAILED(cohr)) { ++queryFail; continue; }
      ComPtr<IWICImagingFactory> factory;
      ComPtr<IWICBitmapDecoder> dec;
      ComPtr<IWICBitmapFrameDecode> frame;
      ComPtr<IWICMetadataQueryReader> meta;
      bool ok = false;
      HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
      if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
      if (SUCCEEDED(hr) && factory) {
        hr = factory->CreateDecoderFromFilename(toWide(fl.abs).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
        if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
        if (SUCCEEDED(hr)) hr = frame->GetMetadataQueryReader(&meta);
        if (SUCCEEDED(hr) && meta) {
          static const wchar_t* const kPaths[] = { L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}" };
          for (const wchar_t* p : kPaths) {
            PROPVARIANT v; PropVariantInit(&v);
            if (SUCCEEDED(meta->GetMetadataByName(p, &v)) && v.vt == VT_UI2) { val = v.uiVal; ok = true; PropVariantClear(&v); break; }
            PropVariantClear(&v);
          }
        }
      }
      CoUninitialize();
      if (!ok) { if (val == 0) { /* no tag: not a failure, just no EXIF */ } }
      if (ok) {
        ++metaPresent;
        if (val >= 1 && val <= 8) ++byValue[val];
        if (val >= 2) ++applied;
        // Name the files, so "the dataset has no rotation" is a checkable claim
        // rather than an aggregate nobody can audit.
        if (metaPresent <= 12) std::printf("  exif_present %s orientation=%u\n", fl.path.c_str(), val);
      }
    }
    // Cost of the fix itself. Before it, one GetMetadataByName call on a path
    // WIC rejects as a bad key; after it, a file with no EXIF still pays a
    // second attempt on the other container's path. Timing one query against
    // two, on the same files, is what attributes the baseline median rise to
    // the fix instead of to machine drift.
    {
      const int kSample = 200;
      double t1q = 0, t2q = 0;
      int taken = 0;
      for (const File& fl : files) {
        if (taken >= kSample) break;
        const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(cohr)) break;
        ComPtr<IWICImagingFactory> factory;
        ComPtr<IWICBitmapDecoder> dec;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICMetadataQueryReader> meta;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr)) hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        bool usable = SUCCEEDED(hr) && factory;
        if (usable) {
          hr = factory->CreateDecoderFromFilename(toWide(fl.abs).c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec);
          if (SUCCEEDED(hr)) hr = dec->GetFrame(0, &frame);
          if (SUCCEEDED(hr)) hr = frame->GetMetadataQueryReader(&meta);
          usable = SUCCEEDED(hr) && meta;
        }
        if (usable) {
          const auto a0 = std::chrono::steady_clock::now();
          { PROPVARIANT v; PropVariantInit(&v); meta->GetMetadataByName(L"/app1/ifd/{ushort=274}", &v); PropVariantClear(&v); }
          const auto a1 = std::chrono::steady_clock::now();
          { PROPVARIANT v; PropVariantInit(&v); meta->GetMetadataByName(L"/ifd/{ushort=274}", &v); PropVariantClear(&v); }
          const auto a2 = std::chrono::steady_clock::now();
          t1q += std::chrono::duration<double, std::milli>(a1 - a0).count();
          t2q += std::chrono::duration<double, std::milli>(a2 - a0).count();
          ++taken;
        }
        CoUninitialize();
      }
      if (taken > 0) {
        std::printf("exif_query_cost sample=%d  one_path %.5f ms  two_paths %.5f ms  delta_per_file %.5f ms\n",
                    taken, t1q / taken, t2q / taken, (t2q - t1q) / taken);
      }
    }
    std::printf("exif_corpus_total_files=%d\n", (int)files.size());
    std::printf("exif_corpus_metadata_present=%lld\n", metaPresent);
    std::printf("exif_corpus_orientation_applied_expected=%lld\n", applied);
    std::printf("exif_corpus_query_failures=%lld\n", queryFail);
    for (int v = 1; v <= 8; ++v) std::printf("exif_corpus_orientation_%d=%lld\n", v, byValue[v]);
  }
  std::printf("fixed_geometry_match=%d/%d\n", gFGeomMatch, gBothSuccess);
  std::printf("fixed_pixel_match=%d/%d\n", gFIdent, gBothSuccess);
  std::printf("aspect_geometry_match=%d/%d\n", gAGeomMatch, gBothSuccess);
  std::printf("aspect_pixel_match=%d/%d\n", gAIdent, gBothSuccess);
  std::printf("fixed_geometry_diff=%d fixed_pixel_diff=%d aspect_geometry_diff=%d aspect_pixel_diff=%d\n",
              gFGeom, gFPix, gAGeom, gAPix);
  std::printf("fixed_mismatch_files=%d aspect_mismatch_files=%d\n", gFixedMismatchFiles, gAspectMismatchFiles);
  std::printf("fixed_max_abs_delta=%llu aspect_max_abs_delta=%llu\n",
              (unsigned long long)gFMaxAbs, (unsigned long long)gAMaxAbs);
  std::printf("fixed_mean_abs_delta=%.6f aspect_mean_abs_delta=%.6f (over compared pixels of mismatching files)\n",
              gFComparedPx ? (double)gFSumAbs / (double)gFComparedPx : 0.0,
              gAComparedPx ? (double)gASumAbs / (double)gAComparedPx : 0.0);
  std::printf("parity_f_identical=%s parity_a_identical=%s\n",
              (gFIdent == gFIdentTotal && gFIdentTotal > 0) ? "yes" : "no",
              (gAIdent == gAIdentTotal && gAIdentTotal > 0) ? "yes" : "no");
  stats("baseline ms", allBase);
  stats("baseline adj ms", allBaseAdj);
  stats("candidate ms", allCand);
  stats("cand shared ms", allShared);
  stats("cand f branch ms", allFBranch);
  stats("cand a branch ms", allABranch);
  if (!allBase.empty()) {
    const double bm = medianOf(allBase), ba = medianOf(allBaseAdj), cm = medianOf(allCand);
    std::printf("median raw baseline %.4f  adjusted %.4f  candidate %.4f\n", bm, ba, cm);
    if (ba > 0) std::printf("cost_reduction_vs_raw=%.1f%%  vs_adjusted=%.1f%%\n", 100.0 * (1 - cm / bm), 100.0 * (1 - cm / ba));
  }
  stats("base CopyPixels f", allBaseFCopy);
  stats("cand CopyPixels f", allCandFCopy);
  stats("base CopyPixels a", allBaseACopy);
  stats("cand CopyPixels a", allCandACopy);
  // Per-file mean of the baseline decode buckets. metadataMs is the bucket the
  // EXIF query lives in, so this is where the cost of the second query attempt
  // shows up if that is what changed.
  {
    double metaSum = 0, openSum = 0, orientSum = 0, copySum = 0;
    for (auto& kv : agg) { metaSum += kv.second.metaMsSum; openSum += kv.second.openMsSum;
                           orientSum += kv.second.orientMsSum; copySum += kv.second.copyMsSum; }
    if (gBothSuccess > 0) {
      const double n = (double)gBothSuccess;
      std::printf("baseline bucket means per file: metadata %.5f  open %.5f  orient %.5f  copyPixels %.5f ms\n",
                  metaSum / n, openSum / n, orientSum / n, copySum / n);
    }
  }
  return (gFIdent == gFIdentTotal && gAIdent == gAIdentTotal &&
          gFIdentTotal > 0 && gAIdentTotal > 0) ? 0 : 3;
}
#endif  // _WIN32

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  // Unbuffered: a crash must not swallow the markers that locate it.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  if (argc >= 2 && std::strcmp(argv[1], "--selfcheck") == 0) return selfcheck();
  if (argc >= 3 && std::strcmp(argv[1], "--exif") == 0) {
    return runExif(argv[2], (argc >= 4) ? argv[3] : "exif_fixture_out");
  }
  if (argc >= 2 && std::strcmp(argv[1], "--groups") == 0) {
    const std::string root = (argc >= 3) ? argv[2] : ".";
    const int stride = (argc >= 4) ? std::atoi(argv[3]) : 1;
    return runGroups(root, stride > 0 ? stride : 1, false);
  }
  // Exhaustive sweep with the PRODUCTION decodeBoth() as the candidate, so the
  // scan-level parity statement covers the code that actually ships.
  if (argc >= 2 && std::strcmp(argv[1], "--production-groups") == 0) {
    const std::string root = (argc >= 3) ? argv[2] : ".";
    const int stride = (argc >= 4) ? std::atoi(argv[3]) : 1;
    return runGroups(root, stride > 0 ? stride : 1, true);
  }
  if (argc >= 3 && std::strcmp(argv[1], "--production") == 0) {
    return runProduction(argv[2]);
  }
  if (argc < 2) {
    std::cerr << "usage: msf_shared_wic_source_probe <dataset-root> | --selfcheck"
                 " | --production <dataset-root> | --groups <root> <stride>"
                 " | --production-groups <root> <stride> | --exif <dir>\n";
    return 2;
  }
  return runFull(argv[1]);
#else
  std::cerr << "shared WIC source probe requires Windows/WIC\n";
  return 2;
#endif
}


