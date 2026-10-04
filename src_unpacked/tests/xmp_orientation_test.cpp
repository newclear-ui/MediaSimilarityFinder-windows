// XMP Orientation fallback test (I-XMP contract).
//
// Self-contained fixtures: a 16x8 asymmetric BMP is WIC-encoded to JPEG, then
// EXIF and/or XMP APP1 segments are spliced in (never touching the standard
// dataset). Rotation is observable through decodeBoth() aspect geometry:
// a 90/270-degree transform swaps the oriented source size, so the aspect
// output flips between 32x16 (unrotated) and 16x32 (rotated) at maxDim 32.
//
// Fixture contract (docs/implementation-briefs/I-xmp-orientation-fallback.*):
//   A: no EXIF, XMP=1        -> identity, orientApplied stays 0
//   B: no EXIF, XMP=6        -> 90 CW applied
//   C: no EXIF, XMP=3        -> 180 applied
//   D: no EXIF, XMP=8        -> 270 CW applied
//   E: EXIF=6, XMP=8         -> EXIF 6 wins
//   F: no EXIF, invalid XMP  -> no transform
//   G: neither               -> existing default semantics
//
// Usage:
//   msf_xmp_orientation_test            fixture assertions (registered)
//   msf_xmp_orientation_test --discover probe WIC XMP query paths (manual)
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
#include "image_decoder.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

namespace {
int gChecks = 0, gFails = 0;
void chk(bool ok, const std::string& what) {
  ++gChecks;
  if (!ok) { ++gFails; std::cout << "  [F] " << what << "\n"; }
  else { std::cout << "  [ok] " << what << "\n"; }
}
#ifdef _WIN32
std::wstring toWide(const std::string& s) {
  if (s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
  std::wstring w(n > 0 ? (size_t)(n - 1) : 0, L'\0');
  if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
  return w;
}
// 16x8 BMP with four distinct quadrants. BMP rows are bottom-up, so the
// y<4 values land at the image bottom: decoded top=(170,255), bottom=(0,85),
// left=85, right=170. Every orientation in 1..8 is observable by quadrant
// means (all assertions below use decoded-image coordinates).
bool writeBmp(const std::string& path) {
  const int w = 16, h = 8, img = w * h * 3, fsz = 54 + img;
  unsigned char hd[54] = {0};
  hd[0] = 'B'; hd[1] = 'M';
  hd[2] = (unsigned char)(fsz & 0xFF); hd[3] = (unsigned char)((fsz >> 8) & 0xFF);
  hd[10] = 54; hd[14] = 40; hd[18] = (unsigned char)w; hd[22] = (unsigned char)h;
  hd[26] = 1; hd[28] = 24;
  hd[34] = (unsigned char)(img & 0xFF); hd[35] = (unsigned char)((img >> 8) & 0xFF);
  std::FILE* f = nullptr;
  if (_wfopen_s(&f, toWide(path).c_str(), L"wb") != 0 || !f) return false;
  bool ok = fwrite(hd, 1, 54, f) == 54;
  for (int y = 0; ok && y < h; ++y)
    for (int x = 0; x < w; ++x) {
      unsigned char v;
      if (y < 4) v = (x < 8) ? 0 : 85;
      else v = (x < 8) ? 170 : 255;
      ok = fputc(v, f) != EOF && fputc(v, f) != EOF && fputc(v, f) != EOF;
    }
  fclose(f);
  return ok;
}
// WIC-encode the BMP to JPEG (same self-contained pattern as the shared-WIC
// probe: no dataset, no external encoder).
bool encodeJpeg(const std::string& bmpPath, std::vector<unsigned char>& out) {
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
    HGLOBAL hglob = nullptr;
    if (FAILED(GetHGlobalFromStream(stream.Get(), &hglob)) || !hglob) goto done;
    {
      const SIZE_T size = GlobalSize(hglob);
      const void* base = GlobalLock(hglob);
      if (!base || size < 4) { if (base) GlobalUnlock(hglob); goto done; }
      const unsigned char* b = (const unsigned char*)base;
      if (b[0] != 0xFF || b[1] != 0xD8) { GlobalUnlock(hglob); goto done; }
      out.assign(b, b + size);
      GlobalUnlock(hglob);
      ok = true;
    }
  }
done:
  CoUninitialize();
  return ok;
}
// Minimal little-endian EXIF APP1 carrying Orientation (same layout the
// shared-WIC probe uses). type is explicit so wrong-type fixtures are
// expressible (3 = SHORT is the conforming form).
std::vector<unsigned char> exifApp1Ex(unsigned short orientation, unsigned short type) {
  std::vector<unsigned char> tiff;
  tiff.push_back('I'); tiff.push_back('I');
  tiff.push_back(0x2A); tiff.push_back(0x00);
  tiff.push_back(0x08); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
  tiff.push_back(0x01); tiff.push_back(0x00);
  tiff.push_back(0x12); tiff.push_back(0x01);
  tiff.push_back((unsigned char)(type & 0xFF)); tiff.push_back((unsigned char)((type >> 8) & 0xFF));
  tiff.push_back(0x01); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
  tiff.push_back((unsigned char)(orientation & 0xFF));
  tiff.push_back((unsigned char)((orientation >> 8) & 0xFF));
  tiff.push_back(0x00); tiff.push_back(0x00);
  tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00); tiff.push_back(0x00);
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
static std::vector<unsigned char> exifApp1(unsigned short orientation) {
  return exifApp1Ex(orientation, 3);
}
// XMP APP1: FF E1 len "http://ns.adobe.com/xap/1.0/\0" + packet carrying
// tiff:Orientation="<value>". value is inserted verbatim so invalid text
// (Case F) is expressible too.
std::vector<unsigned char> xmpApp1(const std::string& value) {
  const std::string packet =
      "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
      "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
      "<rdf:Description rdf:about=\"\" xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\" "
      "tiff:Orientation=\"" + value + "\"/>"
      "</rdf:RDF></x:xmpmeta>";
  const char* head = "http://ns.adobe.com/xap/1.0/";
  std::vector<unsigned char> seg;
  seg.push_back(0xFF); seg.push_back(0xE1);
  const unsigned len = 2 + (unsigned)std::strlen(head) + 1 + (unsigned)packet.size();
  seg.push_back((unsigned char)((len >> 8) & 0xFF));
  seg.push_back((unsigned char)(len & 0xFF));
  for (const char* p = head; *p; ++p) seg.push_back((unsigned char)*p);
  seg.push_back(0x00);
  seg.insert(seg.end(), packet.begin(), packet.end());
  return seg;
}
// Insert APP1 segments after SOI + leading APP0 (camera order; WIC ignores
// EXIF metadata placed ahead of the JFIF APP0).
bool writeJpegWithSegments(const std::vector<unsigned char>& base,
                           const std::vector<std::vector<unsigned char>>& segs,
                           const std::string& dst) {
  if (base.size() < 4 || base[0] != 0xFF || base[1] != 0xD8) return false;
  std::size_t at = 2;
  if (base.size() > 4 && base[2] == 0xFF && base[3] == 0xE0) {
    const std::size_t segLen = ((std::size_t)base[4] << 8) | base[5];
    at = 4 + segLen;
    if (at > base.size()) return false;
  }
  std::vector<unsigned char> out;
  out.reserve(base.size() + 4096);
  out.insert(out.end(), base.begin(), base.begin() + (long)at);
  for (const auto& s : segs) out.insert(out.end(), s.begin(), s.end());
  out.insert(out.end(), base.begin() + (long)at, base.end());
  std::FILE* g = nullptr;
  if (_wfopen_s(&g, toWide(dst).c_str(), L"wb") != 0 || !g) return false;
  const bool ok = fwrite(out.data(), 1, out.size(), g) == out.size();
  fclose(g);
  return ok;
}
struct CaseResult {
  bool decoded = false;
  int aspectW = 0, aspectH = 0;
  std::uint64_t orientApplied = 0;
  msf::GrayImage fixed;
};
CaseResult runCase(const std::string& jpg) {
  CaseResult r;
  msf::ImageDecoder dec;
  msf::GrayImage aspect;
  msf::DecodeTelemetry telF, telA;
  r.decoded = dec.decodeBoth(jpg, 32, 32, 32, r.fixed, aspect, &telF, &telA);
  r.aspectW = aspect.width; r.aspectH = aspect.height;
  r.orientApplied = telF.orientApplied + telA.orientApplied;
  return r;
}
// Manual discovery: report which WIC XMP query paths resolve on a Case-B
// file, with VARIANT types. Not part of the pass/fail contract.
int discover(const std::string& jpg) {
  static const wchar_t* const kPaths[] = {
      L"/xmp/tiff:Orientation",
      L"/xmp/exif:Orientation",
      L"/xmp/{wstr=tiff:Orientation}",
  };
  HRESULT cohr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(cohr)) return 2;
  int rc = 0;
  {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICMetadataQueryReader> meta;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) { CoUninitialize(); return 2; }
    hr = factory->CreateDecoderFromFilename(toWide(jpg).c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &dec);
    if (FAILED(hr)) { CoUninitialize(); return 2; }
    hr = dec->GetFrame(0, &frame);
    if (FAILED(hr)) { CoUninitialize(); return 2; }
    hr = frame->GetMetadataQueryReader(&meta);
    if (FAILED(hr) || !meta) { CoUninitialize(); return 2; }
    for (const wchar_t* path : kPaths) {
      PROPVARIANT v; PropVariantInit(&v);
      hr = meta->GetMetadataByName(path, &v);
      std::wcout << L"path=\"" << path << L"\" hr=0x" << std::hex << (unsigned)hr << std::dec
                 << L" vt=" << v.vt;
      if (SUCCEEDED(hr)) {
        if (v.vt == VT_UI2) std::wcout << L" ui2=" << v.uiVal;
        else if (v.vt == VT_UI4) std::wcout << L" ui4=" << v.ulVal;
        else if (v.vt == VT_I4) std::wcout << L" i4=" << v.lVal;
        else if (v.vt == VT_LPSTR && v.pszVal) std::wcout << L" str=" << v.pszVal;
        else if (v.vt == VT_BSTR && v.bstrVal) std::wcout << L" bstr=" << v.bstrVal;
        else if (v.vt == VT_LPWSTR && v.pwszVal) std::wcout << L" wstr=" << v.pwszVal;
      }
      std::wcout << L"\n";
      PropVariantClear(&v);
    }
  }
  CoUninitialize();
  return rc;
}
}  // namespace
#endif  // _WIN32

int main(int argc, char** argv) {
#ifdef _WIN32
  const bool disc = argc > 1 && std::string(argv[1]) == "--discover";
  std::error_code ec;
  auto d = fs::temp_directory_path() / "msf_xmp_test";
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  const std::string bmp = (d / "base.bmp").string();
  if (!writeBmp(bmp)) { std::cout << "bmp write failed\n"; return 2; }
  std::vector<unsigned char> jpeg;
  if (!encodeJpeg(bmp, jpeg)) { std::cout << "jpeg encode failed\n"; return 2; }
  auto make = [&](const std::string& name, const std::vector<std::vector<unsigned char>>& segs) {
    const std::string p = (d / name).string();
    if (!writeJpegWithSegments(jpeg, segs, p)) { std::cout << "splice failed: " << name << "\n"; std::exit(2); }
    return p;
  };
  const std::string b = make("caseB.jpg", {xmpApp1("6")});
  if (disc) return discover(b);
  std::cout << "XMP orientation fallback selfcheck\n\n";
  // Case G first (no metadata at all): the pre-existing default.
  const std::string g = make("caseG.jpg", {});
  // Case A: XMP=1 identity.
  const std::string a = make("caseA.jpg", {xmpApp1("1")});
  // Case C/D rotations.
  const std::string c = make("caseC.jpg", {xmpApp1("3")});
  const std::string dd = make("caseD.jpg", {xmpApp1("8")});
  // Case E: EXIF=6 beats XMP=8.
  const std::string e = make("caseE.jpg", {exifApp1(6), xmpApp1("8")});
  // Case F: invalid XMP text.
  const std::string f = make("caseF.jpg", {xmpApp1("banana")});

  auto expectDims = [&](const CaseResult& r, int w, int h, const char* what) {
    chk(r.decoded && r.aspectW == w && r.aspectH == h, what);
  };
  // Mean of a fixed-output quadrant (fixed is always 32x32). JPEG is lossy,
  // so bands are wide; the four quadrant levels (0/85/170/255) stay apart.
  auto quadMean = [&](const CaseResult& r, int qx, int qy) {
    if (!r.decoded || r.fixed.width != 32 || r.fixed.height != 32) return -1.0;
    double sum = 0; int n = 0;
    for (int y = qy * 16; y < qy * 16 + 16; ++y)
      for (int x = qx * 16; x < qx * 16 + 16; ++x) {
        sum += r.fixed.pixels[(size_t)y * 32 + x]; ++n;
      }
    return sum / n;
  };
  auto expectBand = [&](double v, double lo, double hi, const char* what) {
    chk(v >= lo && v <= hi, what);
  };
  // Unrotated quadrant layout: TL=0 TR=85 BL=170 BR=255, i.e.
  // left=85 right=170 top=42 bottom=212.
  {
    CaseResult r = runCase(g);
    expectDims(r, 32, 16, "G: no metadata -> unrotated 32x16");
    chk(r.orientApplied == 0, "G: orientApplied stays 0");
  }
  {
    CaseResult r = runCase(a);
    expectDims(r, 32, 16, "A: XMP=1 -> identity 32x16");
    chk(r.orientApplied == 0, "A: orientApplied stays 0");
  }
  {
    CaseResult r = runCase(b);
    expectDims(r, 16, 32, "B: XMP=6 -> rotated 16x32");
    chk(r.orientApplied > 0, "B: orientApplied incremented");
    // 90CW: old left column (mean 85) becomes the new top half.
    expectBand(quadMean(r, 0, 0) + quadMean(r, 1, 0), 120, 220, "B: top half dark (~85)");
    expectBand(quadMean(r, 0, 1) + quadMean(r, 1, 1), 290, 390, "B: bottom half bright (~170)");
  }
  {
    CaseResult r = runCase(c);
    expectDims(r, 32, 16, "C: XMP=3 -> 32x16 (180 keeps geometry)");
    chk(r.orientApplied > 0, "C: orientApplied incremented");
    // 180: left/right and top/bottom both swap (left=170 right=85).
    expectBand(quadMean(r, 0, 0) + quadMean(r, 0, 1), 290, 390, "C: left half bright (~170)");
    expectBand(quadMean(r, 1, 0) + quadMean(r, 1, 1), 120, 220, "C: right half dark (~85)");
  }
  {
    CaseResult r = runCase(dd);
    expectDims(r, 16, 32, "D: XMP=8 -> rotated 16x32");
    chk(r.orientApplied > 0, "D: orientApplied incremented");
    // 270CW: old right column (mean 170) becomes the new top half.
    expectBand(quadMean(r, 0, 0) + quadMean(r, 1, 0), 290, 390, "D: top half bright (~170)");
    expectBand(quadMean(r, 0, 1) + quadMean(r, 1, 1), 120, 220, "D: bottom half dark (~85)");
  }
  {
    CaseResult r = runCase(e);
    expectDims(r, 16, 32, "E: EXIF=6 wins over XMP=8 -> 16x32");
    chk(r.orientApplied > 0, "E: orientApplied incremented");
  }
  {
    CaseResult r = runCase(f);
    expectDims(r, 32, 16, "F: invalid XMP -> unrotated 32x16");
    chk(r.orientApplied == 0, "F: orientApplied stays 0");
  }
  // Flip family (previously unverified mappings 2/4/5/7).
  const std::string h2 = make("caseH2.jpg", {xmpApp1("2")});
  const std::string h4 = make("caseH4.jpg", {xmpApp1("4")});
  const std::string h5 = make("caseH5.jpg", {xmpApp1("5")});
  const std::string h7 = make("caseH7.jpg", {xmpApp1("7")});
  {
    CaseResult r = runCase(h2);
    expectDims(r, 32, 16, "H2: XMP=2 flipH keeps 32x16");
    chk(r.orientApplied > 0, "H2: orientApplied incremented");
    // flipH: left/right swap (left=170 right=85).
    expectBand(quadMean(r, 0, 0) + quadMean(r, 0, 1), 290, 390, "H2: left half bright");
    expectBand(quadMean(r, 1, 0) + quadMean(r, 1, 1), 120, 220, "H2: right half dark");
  }
  {
    CaseResult r = runCase(h4);
    expectDims(r, 32, 16, "H4: XMP=4 flipV keeps 32x16");
    chk(r.orientApplied > 0, "H4: orientApplied incremented");
    // flipV: top/bottom swap. (BMP rows are bottom-up, so the decoded image
    // top already holds the y>=4 values: top=212 bottom=42 unrotated.)
    expectBand(quadMean(r, 0, 0) + quadMean(r, 1, 0), 34, 134, "H4: top half dark");
    expectBand(quadMean(r, 0, 1) + quadMean(r, 1, 1), 374, 474, "H4: bottom half bright");
  }
  {
    // 5/7 combine rotation with flip; the exact arrangement is pinned by
    // byte-equality with the same transform through the EXIF path, since
    // both share transformForOrientationValue.
    CaseResult rx = runCase(h5);
    expectDims(rx, 16, 32, "H5: XMP=5 -> 16x32");
    chk(rx.orientApplied > 0, "H5: orientApplied incremented");
    const std::string e5 = make("caseE5.jpg", {exifApp1(5)});
    CaseResult re = runCase(e5);
    chk(re.decoded && re.fixed.pixels == rx.fixed.pixels,
        "H5: XMP=5 bytes equal EXIF=5");
  }
  {
    CaseResult rx = runCase(h7);
    expectDims(rx, 16, 32, "H7: XMP=7 -> 16x32");
    chk(rx.orientApplied > 0, "H7: orientApplied incremented");
    const std::string e7 = make("caseE7.jpg", {exifApp1(7)});
    CaseResult re = runCase(e7);
    chk(re.decoded && re.fixed.pixels == rx.fixed.pixels,
        "H7: XMP=7 bytes equal EXIF=7");
  }
  // EXIF present-but-unusable falls through to XMP (contract fallback).
  {
    // Out-of-range SHORT value 9.
    const std::string i9 = make("caseI9.jpg", {exifApp1Ex(9, 3), xmpApp1("6")});
    CaseResult r = runCase(i9);
    expectDims(r, 16, 32, "I9: EXIF=9 invalid -> XMP=6 applies");
    chk(r.orientApplied > 0, "I9: orientApplied incremented");
  }
  {
    // Wrong field type (ASCII "6" instead of SHORT): not a usable EXIF value.
    const std::string it = make("caseIT.jpg", {exifApp1Ex(0, 2), xmpApp1("8")});
    CaseResult r = runCase(it);
    expectDims(r, 16, 32, "IT: EXIF wrong-type -> XMP=8 applies");
    chk(r.orientApplied > 0, "IT: orientApplied incremented");
  }
  fs::remove_all(d, ec);
  std::cout << "\nxmp_orientation_selfcheck=" << (gFails ? "FAIL" : "ok")
            << " checks=" << gChecks << "\n";
  return gFails ? 1 : 0;
#else
  (void)argc; (void)argv;
  std::cout << "xmp_orientation_selfcheck=SKIP non-Windows\n";
  return 0;
#endif
}
