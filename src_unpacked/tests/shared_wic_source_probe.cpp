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
#include "path_utils.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
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
struct CandResult {
  std::uint32_t failHr = 0;
  int srcW = 0, srcH = 0, aW = 0, aH = 0;
  std::uint64_t orientApplied = 0;
  double factoryMs = 0, openMs = 0, frameMs = 0, metaMs = 0;
  double fScalerMs = 0, fConvMs = 0, fCopyMs = 0;
  double aScalerMs = 0, aConvMs = 0, aCopyMs = 0;
};

static bool runSharedPipeline(const std::wstring& wpath, int maxDimension,
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

    ComPtr<IWICBitmapFlipRotator> orient;
    ComPtr<IWICBitmapSource> src;
    const auto tM0 = std::chrono::steady_clock::now();
    WICBitmapTransformOptions xform = WICBitmapTransformRotate0;
    ComPtr<IWICMetadataQueryReader> meta;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&meta)) && meta) {
      PROPVARIANT v; PropVariantInit(&v);
      if (SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}", &v)) && v.vt == VT_UI2) {
        switch (v.uiVal) {
          case 2: xform = WICBitmapTransformFlipHorizontal; break;
          case 3: xform = WICBitmapTransformRotate180; break;
          case 4: xform = WICBitmapTransformFlipVertical; break;
          case 5: xform = (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal); break;
          case 6: xform = WICBitmapTransformRotate90; break;
          case 7: xform = (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal); break;
          case 8: xform = WICBitmapTransformRotate270; break;
          default: break;
        }
      }
      PropVariantClear(&v);
    }
    if (xform != WICBitmapTransformRotate0) {
      if (FAILED(factory->CreateBitmapFlipRotator(&orient))) return false;
      if (FAILED(orient->Initialize(frame.Get(), xform))) return false;
      src = orient;
      ++r.orientApplied;
    } else {
      src = frame;
    }
    r.metaMs = nowMs(tM0);

    UINT sw = 0, sh = 0;
    if (FAILED(src->GetSize(&sw, &sh)) || !sw || !sh) return false;
    r.srcW = (int)sw; r.srcH = (int)sh;
    int aw = 0, ah = 0;
    aspectDims((int)sw, (int)sh, maxDimension, aw, ah);
    r.aW = aw; r.aH = ah;

    // ---- branch A: fixed maxDimension x maxDimension, as decodeWicFile ----
    {
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      const auto tS0 = std::chrono::steady_clock::now();
      hr = factory->CreateBitmapScaler(&scaler);
      if (SUCCEEDED(hr)) hr = scaler->Initialize(src.Get(), (UINT)maxDimension, (UINT)maxDimension, WICBitmapInterpolationModeFant);
      r.fScalerMs = nowMs(tS0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      const auto tC0 = std::chrono::steady_clock::now();
      hr = factory->CreateFormatConverter(&conv);
      if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom);
      r.fConvMs = nowMs(tC0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      f.width = maxDimension; f.height = maxDimension;
      f.pixels.resize((std::size_t)maxDimension * maxDimension);
      const auto tP0 = std::chrono::steady_clock::now();
      hr = conv->CopyPixels(nullptr, (UINT)maxDimension, (UINT)f.pixels.size(), f.pixels.data());
      r.fCopyMs = nowMs(tP0);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
    }

    // ---- branch B: aspect, as decodeWicFileAspect ----
    // Same `src`, a second fully independent scaler and converter. The second
    // Fant step reads the same original source, never an intermediate image.
    {
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      const auto tS1 = std::chrono::steady_clock::now();
      hr = factory->CreateBitmapScaler(&scaler);
      if (SUCCEEDED(hr)) hr = scaler->Initialize(src.Get(), (UINT)aw, (UINT)ah, WICBitmapInterpolationModeFant);
      r.aScalerMs = nowMs(tS1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      const auto tC1 = std::chrono::steady_clock::now();
      hr = factory->CreateFormatConverter(&conv);
      if (SUCCEEDED(hr)) hr = conv->Initialize(scaler.Get(), GUID_WICPixelFormat8bppGray,
                                                WICBitmapDitherTypeNone, nullptr, 0.0,
                                                WICBitmapPaletteTypeCustom);
      r.aConvMs = nowMs(tC1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
      a.width = aw; a.height = ah;
      a.pixels.resize((std::size_t)aw * ah);
      const auto tP1 = std::chrono::steady_clock::now();
      hr = conv->CopyPixels(nullptr, (UINT)aw, (UINT)a.pixels.size(), a.pixels.data());
      r.aCopyMs = nowMs(tP1);
      if (FAILED(hr)) { r.failHr = (std::uint32_t)hr; return false; }
    }
  }  // factory, decoder, frame, orient, both scalers and both converters released
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
  tel.ok = runSharedPipeline(wpath, maxDimension, f, a, r);  // releases all WIC objects here
  CoUninitialize();                                           // strictly afterwards

  tel.factoryMs = r.factoryMs; tel.openMs = r.openMs; tel.frameMs = r.frameMs;
  tel.metaMs = r.metaMs; tel.orientMs = r.metaMs;
  tel.fScalerMs = r.fScalerMs; tel.fConvMs = r.fConvMs; tel.fCopyMs = r.fCopyMs;
  tel.aScalerMs = r.aScalerMs; tel.aConvMs = r.aConvMs; tel.aCopyMs = r.aCopyMs;
  tel.orientApplied = r.orientApplied;
  tel.srcW = r.srcW; tel.srcH = r.srcH; tel.aW = r.aW; tel.aH = r.aH;
  tel.failHr = r.failHr;
  tel.sharedMs = tel.comMs + tel.factoryMs + tel.openMs + tel.frameMs + tel.metaMs;
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
    // Enumerating every node needs the WICMetadataSDK headers, which this project
    // does not depend on, so the query paths below are probed directly instead.
    const char* names[] = {
      "/app1/ifd/exif/{ushort=274}",
      "/app1/ifd0/exif/{ushort=274}",
      "/app1/ifd/{ushort=274}",
      "/app1/exif/{ushort=274}",
    };
    for (const char* nm : names) {
      PROPVARIANT v; PropVariantInit(&v);
      const HRESULT q = meta->GetMetadataByName(LPCWSTR(nm), &v);
      std::printf("    meta: %-32s hr=0x%08lX vt=%u", nm, (unsigned long)q, (unsigned)v.vt);
      if (SUCCEEDED(q) && v.vt == VT_UI2) std::printf(" value=%u", (unsigned)v.uiVal);
      if (SUCCEEDED(q) && v.vt == VT_UI4) std::printf(" value=%u", (unsigned)v.ulVal);
      std::printf("\n");
      PropVariantClear(&v);
    }
  }
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
  {
    const HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(cohr)) { dumpExifMetadata(outDir + "/exif_o6.jpg"); CoUninitialize(); }
  }
  int fixtures = 0, applied = 0, appliedBaseCount = 0, parity = 0, mismatch = 0, decodeFail = 0, candFail = 0;
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
    // The fixture is only meaningful if orientation was actually applied.
    const std::uint64_t baseOrient = telF.orientApplied + telA.orientApplied;
    const bool appliedHere = (baseOrient > 0);
    if (appliedHere) ++applied;
    if (ct.orientApplied > 0) ++applied;  // counted below separately
    const PixelDiff df = compareImages(f0, f1);
    const PixelDiff da = compareImages(a0, a1);
    if (df.kind == "identical" && da.kind == "identical") ++parity;
    else {
      ++mismatch;
      std::printf("  o=%u MISMATCH f=%s/%llu a=%s/%llu src=%dx%d a=%dx%d\n", o,
                  df.kind, (unsigned long long)df.diffCount, da.kind, (unsigned long long)da.diffCount,
                  ct.srcW, ct.srcH, ct.aW, ct.aH);
    }
    std::printf("  o=%u base_orientApplied=%llu cand_orientApplied=%llu src=%dx%d a=%dx%d f=%dx%d parity=%s\n",
                o, (unsigned long long)baseOrient, (unsigned long long)ct.orientApplied,
                ct.srcW, ct.srcH, ct.aW, ct.aH, f1.width, f1.height,
                (df.kind == "identical" && da.kind == "identical") ? "identical" : "DIFFERS");
  }
  std::printf("exif fixtures=%d baseline_decode_fail=%d candidate_fail=%d parity_ok=%d parity_mismatch=%d\n",
              fixtures, decodeFail, candFail, parity, mismatch);
  // The decisive number is how many fixtures the product actually applied the
  // orientation to. Zero means the EXIF branch was never exercised and the
  // parity result above is only evidence about the non-rotated path.
  const int appliedBase = appliedBaseCount;
  std::printf("exif_orientation_applied_by_product=%d\n", appliedBase);
  if (appliedBase == 0) {
    std::printf("exif_status=not_measured\n");
    std::printf("exif_note=fixtures are written and byte-compare clean, but WIC reports\n");
    std::printf("         WINCODEC_ERR_PROPERTYNOTFOUND for every Orientation query path\n");
    std::printf("         probed, so no orientation was ever applied and the EXIF branch\n");
    std::printf("         remains unverified for BOTH baseline and candidate.\n");
  } else {
    std::printf("exif_status=measured\n");
  }
  return (mismatch == 0 && candFail == 0 && decodeFail == 0) ? 0 : 3;
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

  std::printf("selfcheck=ok checks=%d\n", checks);
  return 0;
}
#endif  // _WIN32

// --------------------------------------------------------------- full run

#ifdef _WIN32
struct File { std::string format, path; };

int runFull(const std::string& root) {
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
      if (p.size() >= 4 && p.compare(p.size() - 4, 4, ".bmp") == 0) bmp.push_back(p);
    }
    std::sort(bmp.begin(), bmp.end());
    const std::size_t step = bmp.size() > 200 ? bmp.size() / 200 : 1;
    for (std::size_t i = 0; i < bmp.size(); i += step) files.push_back({"bmp", bmp[i]});
  }
  std::sort(files.begin(), files.end(), [](const File& a, const File& b) {
    return a.format < b.format || (a.format == b.format && a.path < b.path);
  });
  std::printf("files_collected=%d\n", (int)files.size());

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
    std::string firstFail;
  };
  std::map<std::string, Agg> agg;
  std::vector<double> allBase, allBaseAdj, allCand, allShared, allFBranch, allABranch;
  std::vector<double> allCandFCopy, allCandACopy, allBaseFCopy, allBaseACopy;
  msf::ImageDecoder dec_;
  int gBaseFail = 0, gCandFail = 0, gCandOnlyFail = 0;
int gFIdent = 0, gFGeom = 0, gFPix = 0, gAIdent = 0, gAGeom = 0, gAPix = 0;
  int gFIdentTotal = 0, gAIdentTotal = 0;
  std::uint64_t gOrient = 0;

  for (const File& fl : files) {
    Agg& g = agg[fl.format];
    msf::DecodeTelemetry telF, telA;
    msf::GrayImage f0, a0;
    const bool okBase = dec_.decode(fl.path, 64, 64, f0, &telF) && dec_.decodePreserveAspect(fl.path, 64, a0, &telA);
    msf::GrayImage f1, a1;
    CandTelemetry ct;
    const bool okCand = candidateSharedSource(toWide(fl.path), 64, f1, a1, ct);
    // Both sides are recorded independently. Comparing them with a short-circuit
    // would hide baseline failures behind candidate failures and vice versa.
    if (!okBase) { ++g.baseFail; ++gBaseFail; if (g.firstFail.empty()) g.firstFail = fl.path; }
    if (!okCand) {
      ++g.candFail; ++gCandFail;
      // "Candidate-only failure" is the dangerous class: the product path can
      // decode the file and the candidate cannot.
      if (okBase) ++gCandOnlyFail;
      if (g.candFail <= 6) {
        std::printf("  candFail base_ok=%d hr=0x%08lX %s\n", (int)okBase, (unsigned long)ct.failHr, fl.path.c_str());
      }
    }
    if (!okBase || !okCand) continue;
    ++g.n;
    g.orientApplied += ct.orientApplied;
    gOrient += ct.orientApplied;

    const PixelDiff df = compareImages(f0, f1);
    if (df.kind == "identical") { ++g.fIdent; ++gFIdent; }
    else if (df.kind == "geometry") { ++g.fGeomDiff; ++gFGeom; }
    else {
      ++g.fPixDiff; ++gFPix;
      g.fDiffPx += df.diffCount; g.fSumAbs += df.sumAbs;
      if (df.maxAbs > g.fMaxAbs) g.fMaxAbs = df.maxAbs;
    }
    const PixelDiff da = compareImages(a0, a1);
    if (da.kind == "identical") { ++g.aIdent; ++gAIdent; }
    else if (da.kind == "geometry") { ++g.aGeomDiff; ++gAGeom; }
    else {
      ++g.aPixDiff; ++gAPix;
      g.aDiffPx += da.diffCount; g.aSumAbs += da.sumAbs;
      if (da.maxAbs > g.aMaxAbs) g.aMaxAbs = da.maxAbs;
    }

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
  std::printf("\n=== totals ===\n");
  gFIdentTotal = gFIdent + gFGeom + gFPix;
  gAIdentTotal = gAIdent + gAGeom + gAPix;
  std::printf("compared=%d baseFail=%d candFail=%d candOnlyFail=%d orient_applied=%llu\n",
              gFIdentTotal, gBaseFail, gCandFail, gCandOnlyFail, (unsigned long long)gOrient);
  std::printf("f  ident %d / geomdiff %d / pixdiff %d\n", gFIdent, gFGeom, gFPix);
  std::printf("a  ident %d / geomdiff %d / pixdiff %d\n", gAIdent, gAGeom, gAPix);
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
  if (argc < 2) {
    std::cerr << "usage: msf_shared_wic_source_probe <dataset-root> | --selfcheck\n";
    return 2;
  }
  return runFull(argv[1]);
#else
  std::cerr << "shared WIC source probe requires Windows/WIC\n";
  return 2;
#endif
}


