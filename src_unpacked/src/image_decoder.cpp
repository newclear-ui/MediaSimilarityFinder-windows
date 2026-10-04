#include <cmath>
#include "image_decoder.h"
#include "path_utils.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <new>
#include <cstdlib>
#include <cwchar>
#include <vector>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#endif
#include <fstream>
#include <sstream>
#include <algorithm>
// D9d: timing only. The clock is read outside every call it measures, so no
// measured region gains work beyond two steady_clock reads.
#include <chrono>

namespace msf {
namespace {
// Milliseconds between two steady_clock reads.
inline double d9dMs(std::chrono::steady_clock::time_point a,
                    std::chrono::steady_clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// D1: create the WIC factory, recording whether CLSID_WICImagingFactory2
// actually works or whether every call is paying for a failed attempt plus a
// fallback. The HRESULT of the first attempt was previously discarded, so the
// project did not know which of the two it was paying for.
//
// This is the identical two-step pattern the three call sites already used; it
// is only factored out so all three report the same counters. The COM
// activation order and the fallback condition are unchanged.
#ifdef _WIN32
inline HRESULT createWicFactory(IWICImagingFactory** out, DecodeTelemetry* tel) {
  const auto t0 = std::chrono::steady_clock::now();
  HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr,
                                CLSCTX_INPROC_SERVER, IID_PPV_ARGS(out));
  const auto t1 = std::chrono::steady_clock::now();
  if (tel) {
    ++tel->factory2Attempts;
    tel->factory2Ms += d9dMs(t0, t1);
    if (SUCCEEDED(hr)) {
      ++tel->factory2Successes;
    } else {
      ++tel->factory2Fallbacks;
      if (tel->factory2FirstFailHr == 0) tel->factory2FirstFailHr = (std::uint32_t)hr;
    }
  }
  if (FAILED(hr)) {
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                          CLSCTX_INPROC_SERVER, IID_PPV_ARGS(out));
    if (tel) tel->factoryFallbackMs += d9dMs(t1, std::chrono::steady_clock::now());
  }
  return hr;
}

// D1: reference probe for the OS cost of opening the file, so the WIC-specific
// part of the single opaque CreateDecoderFromFilename call can be separated
// from it. Opens and immediately closes a handle; it does not touch the decode
// path, the image bytes, or any result, and its outcome never gates the decode.
inline void probeOsFileOpen(const wchar_t* wpath, DecodeTelemetry* tel) {
  if (!tel) return;
  const auto t0 = std::chrono::steady_clock::now();
  HANDLE h = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  const bool ok = (h != INVALID_HANDLE_VALUE);
  if (ok) CloseHandle(h);
  tel->osFileOpenProbeMs += d9dMs(t0, std::chrono::steady_clock::now());
  ++tel->osFileOpenProbeCount;
  if (!ok) ++tel->osFileOpenProbeFails;
}

// EXIF Orientation, read once for all three decode paths.
//
// The previous code queried L"/app1/ifd/exif/{ushort=274}" at every call site.
// WIC rejects that path with WINCODEC_ERR_BADPROPERTYKEY, so the lookup never
// succeeded and orient_applied stayed 0 for the whole corpus: the product has
// never applied a rotation, in the fingerprint lanes or the display lane.
// Orientation lives in EXIF IFD0, which WIC exposes as /app1/ifd/ for JPEG and
// /ifd/ for TIFF (the System.Photo.Orientation policy).
//
// Trying both known paths removes the need to know the container, so no
// JPEG/TIFF branch is introduced. The first path that yields a value wins;
// a file that has no Orientation tag yields WICBitmapTransformRotate0, which is
// the identity and is also what orientation 1 means.
//
// Query cost is unchanged in shape: one failed lookup on files whose container
// uses the second path. It is inside the metadataMs bucket that D9c/D9d
// already account for.
inline WICBitmapTransformOptions transformForOrientationValue(unsigned o) {
  switch (o) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5: return (WICBitmapTransformOptions)(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7: return (WICBitmapTransformOptions)(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;  // 1, or a value we do not act on
  }
}
// Orientation source precedence (I-XMP contract): a usable EXIF value wins and
// the XMP packet is never even queried; EXIF absent / query failed / wrong
// type / out-of-range value all count as unresolved and fall through to XMP.
// XMP supports exactly one representation: the tiff:Orientation integer,
// which WIC surfaces as a VT_LPWSTR string (measured). Anything else stays
// unresolved and yields the identity transform.
enum class OrientationSource { None, Exif, Xmp };
struct ResolvedOrientation { WICBitmapTransformOptions transform; OrientationSource source; };
inline bool parseOrientationText(const wchar_t* s, unsigned& o) {
  if (!s) return false;
  wchar_t* end = nullptr;
  const long v = std::wcstol(s, &end, 10);
  if (end == s || v < 1 || v > 8) return false;
  while (*end == L' ' || *end == L'\t') ++end;
  if (*end != L'\0') return false;
  o = (unsigned)v;
  return true;
}
inline ResolvedOrientation resolveOrientationToTransform(IWICMetadataQueryReader* meta) {
  static const wchar_t* const kPaths[] = {
      L"/app1/ifd/{ushort=274}",  // JPEG
      L"/ifd/{ushort=274}",       // TIFF
  };
  if (meta) {
    // Only VT_UI2 values are consumed, so the first path that yields one wins and
    // the loop stops there. A JPEG resolves on the first query and never pays the
    // second, which is the common case for a photo corpus.
    for (const wchar_t* path : kPaths) {
      PROPVARIANT v; PropVariantInit(&v);
      const HRESULT hr = meta->GetMetadataByName(path, &v);
      const bool found = SUCCEEDED(hr) && v.vt == VT_UI2;
      if (!found) { PropVariantClear(&v); continue; }
      const unsigned short o = v.uiVal;
      PropVariantClear(&v);
      if (o >= 1 && o <= 8) return {transformForOrientationValue(o), OrientationSource::Exif};
      break;  // present but unusable: fall through to XMP, do not try the other EXIF path
    }
    // XMP fallback, queried only when EXIF left nothing usable.
    {
      PROPVARIANT v; PropVariantInit(&v);
      const HRESULT hr = meta->GetMetadataByName(L"/xmp/tiff:Orientation", &v);
      if (SUCCEEDED(hr)) {
        unsigned o = 0;
        bool ok = false;
        if (v.vt == VT_UI2) { o = v.uiVal; ok = (o >= 1 && o <= 8); }
        else if (v.vt == VT_UI4) { ok = (v.ulVal >= 1 && v.ulVal <= 8); o = (unsigned)v.ulVal; }
        else if (v.vt == VT_I4) { ok = (v.lVal >= 1 && v.lVal <= 8); o = (unsigned)v.lVal; }
        else if (v.vt == VT_LPWSTR) { ok = parseOrientationText(v.pwszVal, o); }
        else if (v.vt == VT_LPSTR && v.pszVal) {
          wchar_t w[32]; const size_t n = std::mbstowcs(w, v.pszVal, 31);
          if (n != (size_t)-1) { w[n] = L'\0'; ok = parseOrientationText(w, o); }
        } else if (v.vt == VT_BSTR && v.bstrVal) { ok = parseOrientationText(v.bstrVal, o); }
        PropVariantClear(&v);
        if (ok) return {transformForOrientationValue(o), OrientationSource::Xmp};
      } else {
        PropVariantClear(&v);
      }
    }
  }
  return {WICBitmapTransformRotate0, OrientationSource::None};
}
inline WICBitmapTransformOptions exifOrientationToTransform(IWICMetadataQueryReader* meta) {
  return resolveOrientationToTransform(meta).transform;
}
#endif
// Minimal P5 grayscale reader shared by all platforms. On Windows it is the
// fallback for formats WIC cannot decode (e.g. PGM test fixtures); on other
// platforms it is the primary reader.
bool readPgmFile(const std::string& path,std::vector<unsigned char>& src,int& sw,int& sh){
    // Open via the wide path: a narrow UTF-8 name outside the ANSI code page
    // would otherwise fail (or throw at path construction) on Windows.
    std::ifstream f(path_from_utf8(path),std::ios::binary); if(!f) return false;
    std::string magic; int maxv=0; f>>magic; if(magic!="P5") return false;
    f>>sw>>sh>>maxv; f.get(); if(sw<=0||sh<=0||maxv<=0) return false;
    src.resize((size_t)sw*sh); f.read((char*)src.data(),src.size());
    return (size_t)f.gcount()==src.size();
}
bool scaleGray(const std::vector<unsigned char>& src,int sw,int sh,int w,int h,GrayImage& out){
    if(w<=0||h<=0) return false;
    out.width=w; out.height=h; out.pixels.resize((size_t)w*h);
    for(int y=0;y<h;y++){int sy=std::min(sh-1,y*sh/h);for(int x=0;x<w;x++){int sx=std::min(sw-1,x*sw/w);out.pixels[(size_t)y*w+x]=src[(size_t)sy*sw+sx];}}
    return true;
}
// PNG IHDR dimensions straight from the header (signature + length + type +
// width + height = 24 bytes). No decode, no zlib needed.
bool parsePngDims(const std::vector<unsigned char>& b,int& w,int& h){
    static const unsigned char kSig[8]={137,80,78,71,13,10,26,10};
    if(b.size()<24) return false;
    for(int i=0;i<8;++i) if(b[i]!=kSig[i]) return false;
    if(b[12]!='I'||b[13]!='H'||b[14]!='D'||b[15]!='R') return false;
    w=(b[16]<<24)|(b[17]<<16)|(b[18]<<8)|b[19];
    h=(b[20]<<24)|(b[21]<<16)|(b[22]<<8)|b[23];
    return w>0&&h>0;
}
// JPEG SOF dimensions by marker walk (baseline + progressive + extended).
// Stops at SOS; RST/TEM/SOI/EOI carry no length. Bounds-checked throughout.
bool parseJpegDims(const std::vector<unsigned char>& b,int& w,int& h){
    if(b.size()<4||b[0]!=0xFF||b[1]!=0xD8) return false;
    std::size_t pos=2;
    while(pos+1<b.size()){
        if(b[pos]!=0xFF){ ++pos; continue; }
        unsigned char m=b[pos+1]; pos+=2;
        if(m==0x00) continue; // stuffed byte (should not appear pre-SOS)
        if(m==0xD8) continue; // SOI (should not repeat)
        if(m==0xD9) return false; // EOI: no SOF found
        if((m>=0xD0&&m<=0xD7)||m==0x01) continue; // standalone, no length
        if(m==0xDA) return false; // SOS: entropy follows, SOF will not appear
        if(pos+1>=b.size()) return false;
        const std::size_t len=((std::size_t)b[pos]<<8)|b[pos+1];
        if(len<2||pos+len>b.size()) return false;
        if(m==0xC0||m==0xC1||m==0xC2||m==0xC3){
            if(len<7) return false;
            h=((int)b[pos+3]<<8)|b[pos+4]; w=((int)b[pos+5]<<8)|b[pos+6];
            return w>0&&h>0;
        }
        pos+=len;
    }
    return false;
}
bool decodePgm(const std::string& path,int w,int h,GrayImage& out){
    std::vector<unsigned char> src; int sw=0,sh=0;
    if(!readPgmFile(path,src,sw,sh)) return false;
    return scaleGray(src,sw,sh,w,h,out);
}
bool decodePgmAspect(const std::string& path,int maxDimension,GrayImage& out){
    std::vector<unsigned char> src; int sw=0,sh=0;
    if(!readPgmFile(path,src,sw,sh)) return false;
    int w=sw,h=sh; if(sw>=sh){w=maxDimension;h=std::max(1,(int)std::lround((double)sh*maxDimension/sw));}else{h=maxDimension;w=std::max(1,(int)std::lround((double)sw*maxDimension/sh));}
    return scaleGray(src,sw,sh,w,h,out);
}
} // namespace
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
namespace {
// Every ComPtr below is destroyed when the helper returns, i.e. strictly
// before the caller runs CoUninitialize. Releasing WIC objects after
// CoUninitialize faults on Windows (observed access violation during
// scope exit), so the helpers must never touch COM lifetime themselves.
// I-2 production integration: one factory, one decoder, one frame and one
// EXIF-orientation source, feeding up to two INDEPENDENT scaler/converter/
// CopyPixels chains. There is no intermediate GrayImage, so each output keeps
// exactly the one-step Fant chain the product always used, and the bytes are
// unchanged (verified over the whole standard dataset).
//
// Replaces the two near-identical bodies that built all of that twice per file.
//
// Telemetry allocation keeps the D3 invariants:
//   * shared cost (COM, factory, open, metadata, orientation) -> telShared
//   * fixed branch  (scaler, convert, copy)                  -> telFixed
//   * aspect branch (scaler, convert, copy)                  -> telAspect
// Every bucket lands in exactly one accumulator, so the two totalMs values
// still sum to the whole call and mergeDecodeTelemetry keeps reconstructing the
// same totals. aspectBranchMs reports the aspect branch's own elapsed time so
// the caller can split the single wall-clock total without double counting it.
// With a single output the one non-null pointer receives everything, as before.
//
// The OS file-open reference probe (D1) now runs once per call rather than once
// per WIC pipeline. That is the honest consequence of doing the work once;
// osFileOpenProbeCount therefore drops on the paired path while keeping its
// meaning ("how many reference probes were run").
bool decodeWicBranches(const wchar_t* wpath,int fw,int fh,int maxDimension,
                       bool wantFixed,bool wantAspect,
                       GrayImage& fixed,GrayImage& aspect,
                       DecodeTelemetry* telFixed,DecodeTelemetry* telAspect,
                       double* aspectBranchMs=nullptr){
    DecodeTelemetry* telShared = telFixed ? telFixed : telAspect;
    // D9d: each timer wraps one WIC step and nothing else. The steps are
    // sequential and mutually exclusive, so the buckets reconstruct the WIC
    // function's time without double counting; "other" is the remainder.
    // D1: factory2 and fallback are timed separately and sum to factoryMs.
    // The helper accumulates into the running totals, so only this call's
    // delta may be added here. Adding the totals themselves would count the
    // first attempt once, the second twice, and so on, which is what a first
    // implementation did and why factoryMs came out in the hundreds of
    // millions of ms.
    ComPtr<IWICImagingFactory> factory;
    const double f2Before = telShared ? telShared->factory2Ms : 0.0;
    const double fbBefore = telShared ? telShared->factoryFallbackMs : 0.0;
    HRESULT hr=createWicFactory(&factory,telShared);
    if(telShared) telShared->factoryMs += (telShared->factory2Ms - f2Before) + (telShared->factoryFallbackMs - fbBefore);
    if(FAILED(hr))return false;
    // D1: reference probe first, so its cost never lands inside openMs.
    probeOsFileOpen(wpath,telShared);
    const auto t1=std::chrono::steady_clock::now();
    ComPtr<IWICBitmapDecoder> dec;
    hr=factory->CreateDecoderFromFilename(wpath,nullptr,GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,&dec);
    const auto t2=std::chrono::steady_clock::now();
    if(telShared){ telShared->openMs+=d9dMs(t1,t2); if(FAILED(hr)){ ++telShared->openHrFailCount; if(telShared->openHrFirstFailCode==0) telShared->openHrFirstFailCode=(std::uint32_t)hr; } }
    if(FAILED(hr))return false;
    // EXIF orientation: the fingerprint must describe the image as displayed.
    // QImageReader::setAutoTransform(true) does this in the display lane, but
    // WIC raw frames do not. Read the tag and swap/flip on the decoded pixels
    // (orientations 2..8) so rotated phone photos match their displayed form.
    ComPtr<IWICBitmapFlipRotator> orient;
    ComPtr<IWICBitmapSource> src;
    UINT sw=0,sh=0;
    const auto tMeta0=std::chrono::steady_clock::now();
    auto tOrient1=std::chrono::steady_clock::time_point{};
    {
      ComPtr<IWICBitmapFrameDecode> frame;
      hr=dec->GetFrame(0,&frame);
      if(FAILED(hr))return false;
      WICBitmapTransformOptions xform=WICBitmapTransformRotate0;
      ComPtr<IWICMetadataQueryReader> meta;
      if(SUCCEEDED(frame->GetMetadataQueryReader(&meta))&&meta)
        xform=exifOrientationToTransform(meta.Get());
      const auto tMeta1=std::chrono::steady_clock::now(); if(telShared) telShared->metadataMs+=d9dMs(tMeta0,tMeta1);
      if(xform!=WICBitmapTransformRotate0){
        if(FAILED(factory->CreateBitmapFlipRotator(&orient))) return false;
        if(FAILED(orient->Initialize(frame.Get(),xform))) return false;
        src=orient;
        if(telShared) ++telShared->orientApplied;
      } else {
        src=frame;
      }
      tOrient1=std::chrono::steady_clock::now(); if(telShared) telShared->orientMs+=d9dMs(tMeta1,tOrient1);
      // Oriented size drives the aspect math: 90/270-degree rotations swap w/h.
      if(FAILED(src->GetSize(&sw,&sh))||!sw||!sh)return false;
    }
    // ---- branch A: fixed size ----
    if(wantFixed){
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      hr=factory->CreateBitmapScaler(&scaler);
      if(SUCCEEDED(hr)) hr=scaler->Initialize(src.Get(),fw,fh,WICBitmapInterpolationModeFant);
      const auto tScale1=std::chrono::steady_clock::now(); if(telFixed) telFixed->resizeMs+=d9dMs(tOrient1,tScale1);
      if(FAILED(hr))return false;
      hr=factory->CreateFormatConverter(&conv);
      if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,
                                             WICBitmapDitherTypeNone,nullptr,0.0,
                                             WICBitmapPaletteTypeCustom);
      const auto tConv1=std::chrono::steady_clock::now(); if(telFixed) telFixed->convertMs+=d9dMs(tScale1,tConv1);
      if(FAILED(hr))return false;
      fixed.width=fw; fixed.height=fh; fixed.pixels.resize(size_t(fw)*size_t(fh));
      // WIC decodes lazily, so the real image decompression happens inside
      // CopyPixels. That is why this bucket is the actual decode cost, and why it
      // cannot be split further without changing the code under measurement.
      hr=conv->CopyPixels(nullptr,fw,fixed.pixels.size(),fixed.pixels.data());
      if(telFixed) telFixed->copyMs+=d9dMs(tConv1,std::chrono::steady_clock::now());
      if(FAILED(hr))return false;
    }
    // ---- branch B: aspect, same shared source, independent scaler ----
    if(wantAspect){
      UINT w=sw,h=sh; if(sw>sh){w=(UINT)maxDimension;h=std::max<UINT>(1,(UINT)std::lround((double)sh*maxDimension/sw));} else {h=(UINT)maxDimension;w=std::max<UINT>(1,(UINT)std::lround((double)sw*maxDimension/sh));}
      ComPtr<IWICBitmapScaler> scaler;
      ComPtr<IWICFormatConverter> conv;
      const auto tAS0=std::chrono::steady_clock::now();
      hr=factory->CreateBitmapScaler(&scaler); if(SUCCEEDED(hr)) hr=scaler->Initialize(src.Get(),w,h,WICBitmapInterpolationModeFant);
      const auto tAS1=std::chrono::steady_clock::now(); if(telAspect) telAspect->resizeMs+=d9dMs(tAS0,tAS1);
      if(FAILED(hr))return false;
      hr=factory->CreateFormatConverter(&conv); if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom);
      const auto tAC1=std::chrono::steady_clock::now(); if(telAspect) telAspect->convertMs+=d9dMs(tAS1,tAC1);
      if(FAILED(hr))return false;
      aspect.width=(int)w;aspect.height=(int)h;aspect.pixels.resize((size_t)w*h);
      hr=conv->CopyPixels(nullptr,w,aspect.pixels.size(),aspect.pixels.data());
      const auto tAEnd=std::chrono::steady_clock::now();
      if(telAspect) telAspect->copyMs+=d9dMs(tAC1,tAEnd);
      if(aspectBranchMs) *aspectBranchMs=d9dMs(tAS0,tAEnd);
      if(FAILED(hr))return false;
    }
    return true;
}
bool decodeWicFileAspectColor(const wchar_t* wpath,int maxDimension,msf::ColorImage& out){
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)); if(FAILED(hr)) hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr))return false; ComPtr<IWICBitmapDecoder> dec; hr=factory->CreateDecoderFromFilename(wpath,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec); if(FAILED(hr))return false;
    // EXIF orientation applies to the color display lane too (same mapping as
    // the gray fingerprint lanes above): decoded previews must match what the
    // fingerprint describes, or rotated photos show one way and match another.
    ComPtr<IWICBitmapFlipRotator> orient;
    ComPtr<IWICBitmapSource> src;
    UINT sw=0,sh=0;
    {
      ComPtr<IWICBitmapFrameDecode> frame; hr=dec->GetFrame(0,&frame); if(FAILED(hr))return false;
      WICBitmapTransformOptions xform=WICBitmapTransformRotate0;
      ComPtr<IWICMetadataQueryReader> meta;
      if(SUCCEEDED(frame->GetMetadataQueryReader(&meta))&&meta)
        xform=exifOrientationToTransform(meta.Get());
      if(xform!=WICBitmapTransformRotate0){
        if(FAILED(factory->CreateBitmapFlipRotator(&orient))) return false;
        if(FAILED(orient->Initialize(frame.Get(),xform))) return false;
        src=orient;
      } else {
        src=frame;
      }
      UINT fw=0,fh=0; if(FAILED(src->GetSize(&fw,&fh))||!fw||!fh)return false; sw=fw; sh=fh;
    }
    UINT w=sw,h=sh; if(sw>sh){w=(UINT)maxDimension;h=std::max<UINT>(1,(UINT)std::lround((double)sh*maxDimension/sw));} else {h=(UINT)maxDimension;w=std::max<UINT>(1,(UINT)std::lround((double)sw*maxDimension/sh));}
    ComPtr<IWICBitmapScaler> scaler; hr=factory->CreateBitmapScaler(&scaler); if(SUCCEEDED(hr)) hr=scaler->Initialize(src.Get(),w,h,WICBitmapInterpolationModeFant); if(FAILED(hr))return false;
    // BGRA bytes land in QImage::Format_ARGB32 order on little-endian.
    ComPtr<IWICFormatConverter> conv; hr=factory->CreateFormatConverter(&conv); if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom); if(FAILED(hr))return false;
    out.width=(int)w;out.height=(int)h;out.bgra.resize((size_t)w*h*4); hr=conv->CopyPixels(nullptr,w*4,out.bgra.size(),out.bgra.data()); return SUCCEEDED(hr);
}
} // namespace
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out,DecodeTelemetry* tel) const {
    if(w<=0||h<=0) return false;
    // D9d: total wraps the whole public call, including the COM init and the
    // PGM fallback, so the reported total always matches what the caller paid.
    const auto tAll0=std::chrono::steady_clock::now();
    if(tel) ++tel->calls;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    if(!need) { if(tel){ ++tel->failures; tel->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); } return false; }
    std::wstring wp(need,L'\0');
    MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    const auto tCom0=std::chrono::steady_clock::now();
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool uninit=SUCCEEDED(hr);
    if(tel) tel->comInitMs+=d9dMs(tCom0,std::chrono::steady_clock::now());
    GrayImage sDummy;
    bool ok=decodeWicBranches(wp.c_str(),w,h,0,true,false,out,sDummy,nullptr,tel);
    if(!ok){ const auto tPgm0=std::chrono::steady_clock::now(); ok=decodePgm(path,w,h,out); if(tel){ tel->pgmFallbackMs+=d9dMs(tPgm0,std::chrono::steady_clock::now()); ++tel->pgmFallbacks; } }
    else if(tel) ++tel->wicSucceeded;
    if(uninit)CoUninitialize();
    if(tel){ if(!ok) ++tel->failures; tel->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); }
    return ok;
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out,DecodeTelemetry* tel) const {
    if(maxDimension<=0) return false;
    const auto tAll0=std::chrono::steady_clock::now();
    if(tel) ++tel->aspectCalls;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0); if(!need) { if(tel){ ++tel->failures; tel->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); } return false; }
    std::wstring wp(need,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    const auto tCom0=std::chrono::steady_clock::now();
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); bool uninit=SUCCEEDED(hr);
    if(tel) tel->comInitMs+=d9dMs(tCom0,std::chrono::steady_clock::now());
    GrayImage sDummy;
    bool ok=decodeWicBranches(wp.c_str(),0,0,maxDimension,false,true,sDummy,out,nullptr,tel);
    if(!ok){ const auto tPgm0=std::chrono::steady_clock::now(); ok=decodePgmAspect(path,maxDimension,out); if(tel){ tel->pgmFallbackMs+=d9dMs(tPgm0,std::chrono::steady_clock::now()); ++tel->pgmFallbacks; } }
    else if(tel) ++tel->wicSucceeded;
    if(uninit)CoUninitialize();
    if(tel){ if(!ok) ++tel->failures; tel->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); }
    return ok;
}
bool ImageDecoder::decodeBoth(const std::string& path,int w,int h,int maxDimension,GrayImage& fixedOut,GrayImage& aspectOut,DecodeTelemetry* telFixed,DecodeTelemetry* telAspect) const {
    // I-2: the only production path that needs both results. One COM/factory/
    // decoder/frame/orientation source, two independent output chains.
    if(w<=0||h<=0||maxDimension<=0) return false;
    const auto tAll0=std::chrono::steady_clock::now();
    if(telFixed) ++telFixed->calls;
    if(telAspect) ++telAspect->aspectCalls;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    if(!need){ if(telFixed){ ++telFixed->failures; telFixed->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); } if(telAspect){ ++telAspect->failures; telAspect->totalMs+=d9dMs(tAll0,std::chrono::steady_clock::now()); } return false; }
    std::wstring wp(need,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    const auto tCom0=std::chrono::steady_clock::now();
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); bool uninit=SUCCEEDED(hr);
    if(telFixed) telFixed->comInitMs+=d9dMs(tCom0,std::chrono::steady_clock::now());
    double aspectBranchMs=0.0;
    bool ok=decodeWicBranches(wp.c_str(),w,h,maxDimension,true,true,fixedOut,aspectOut,telFixed,telAspect,&aspectBranchMs);
    // Failure path is byte-identical to the two separate calls: the existing
    // PGM fallbacks run unchanged, so no new fallback semantics are introduced.
    if(!ok){
        const auto tPgm0=std::chrono::steady_clock::now();
        bool okF=decodePgm(path,w,h,fixedOut);
        bool okA=decodePgmAspect(path,maxDimension,aspectOut);
        ok=okF&&okA;
        const double pgmMs=d9dMs(tPgm0,std::chrono::steady_clock::now());
        aspectBranchMs=pgmMs;
        if(telFixed){ telFixed->pgmFallbackMs+=pgmMs; ++telFixed->pgmFallbacks; }
        if(telAspect){ telAspect->pgmFallbackMs+=pgmMs; ++telAspect->pgmFallbacks; }
    } else {
        if(telFixed) ++telFixed->wicSucceeded;
        if(telAspect) ++telAspect->wicSucceeded;
    }
    if(uninit)CoUninitialize();
    const auto tEnd=std::chrono::steady_clock::now();
    if(telFixed){ if(!ok) ++telFixed->failures; }
    if(telAspect){ if(!ok) ++telAspect->failures; }
    // Split identity: decodeFullTotalMs + decodeAspectOnlyTotalMs must still
    // equal this one wall-clock total, and mergeDecodeTelemetry adds them. So
    // the aspect branch is charged to the aspect accumulator and everything
    // else (COM, factory, open, metadata, orientation, the fixed branch and the
    // caller-side wrapper) to the fixed one. Charging the whole elapsed time to
    // both would double count it and break decodeSplitOverMs.
    double fixedShare=d9dMs(tAll0,tEnd)-aspectBranchMs;
    // Clamp: a nested timer can never exceed the outer one, but a negative
    // total would corrupt the merge, so guard rather than trust it.
    if(fixedShare<0.0) fixedShare=0.0;
    if(telFixed) telFixed->totalMs+=fixedShare;
    if(telAspect) telAspect->totalMs+=aspectBranchMs;
    return ok;
}
bool ImageDecoder::decodeColorAspect(const std::string& path,int maxDimension,ColorImage& out) const {
    if(maxDimension<=0) return false;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0); if(!need) return false;
    std::wstring wp(need,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); bool uninit=SUCCEEDED(hr);
    bool ok=decodeWicFileAspectColor(wp.c_str(),maxDimension,out);
    if(uninit)CoUninitialize(); return ok;
}
#else
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out) const {
  return decodePgm(path,w,h,out);
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const {
  if(maxDimension<=0) return false;
  return decodePgmAspect(path,maxDimension,out);
}
bool ImageDecoder::decodeBoth(const std::string& path,int w,int h,int maxDimension,GrayImage& fixedOut,GrayImage& aspectOut,DecodeTelemetry* telFixed,DecodeTelemetry* telAspect) const {
  if(w<=0||h<=0||maxDimension<=0) return false;
  const auto tAll0=std::chrono::steady_clock::now();
  if(telFixed) ++telFixed->calls;
  if(telAspect) ++telAspect->aspectCalls;
  // No WIC off Windows, so there is no shared source to build: run the two
  // existing PGM paths. Behaviour stays identical to the two separate calls.
  const auto tPgm0=std::chrono::steady_clock::now();
  const bool ok=decodePgm(path,w,h,fixedOut)&&decodePgmAspect(path,maxDimension,aspectOut);
  const double pgmMs=d9dMs(tPgm0,std::chrono::steady_clock::now());
  // Same split identity as the Windows path: the aspect share goes to the aspect
  // accumulator and the remainder to the fixed one, so the two totalMs values
  // still sum to the whole call.
  double fixedShare=d9dMs(tAll0,std::chrono::steady_clock::now())-pgmMs;
  if(fixedShare<0.0) fixedShare=0.0;
  if(telFixed){ telFixed->pgmFallbackMs+=pgmMs; ++telFixed->pgmFallbacks; if(!ok) ++telFixed->failures; telFixed->totalMs+=fixedShare; }
  if(telAspect){ telAspect->pgmFallbackMs+=pgmMs; ++telAspect->pgmFallbacks; if(!ok) ++telAspect->failures; telAspect->totalMs+=pgmMs; }
  return ok;
}
bool ImageDecoder::decodeColorAspect(const std::string&,int,ColorImage&) const {
  return false; // no color WIC outside Windows; callers fall back to file icons
}

// D2: measurement-only. On a non-WIC build this reports not_available rather
// than fabricating zeros, so a zero is never mistaken for a measurement.
void ImageDecoder::probeDecoderPaths(const std::string&, DecoderPathProbe& out) {
  out = DecoderPathProbe{};
  out.available = false;
}
#endif

#ifdef _WIN32
namespace {
// Minimal IStream over a Win32 HANDLE, used only by the D2 Path C probe.
//
// Windows exposes no HANDLE -> IStream adapter (there is no
// CreateStreamOnHANDLE in ole32), and IWICStream offers only
// InitializeFromFilename / InitializeFromIStream. The whole point of Path C is
// to hand WIC a handle the caller already opened, so a thin adapter is the
// honest way to build that path. Seek/Read go straight to the file, and every
// other operation reports that it is unsupported rather than pretending.
class HandleStream final : public IStream {
 public:
  explicit HandleStream(HANDLE h) : h_(h) {}
  // IUnknown
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IStream) { *ppv = static_cast<IStream*>(this); AddRef(); return S_OK; }
    *ppv = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG n = InterlockedDecrement(&ref_);
    if (!n) delete this;
    return n;
  }
  // IStream
  HRESULT STDMETHODCALLTYPE Read(void* pv, ULONG cb, ULONG* pcb) override {
    if (!pv && cb) return E_POINTER;
    if (h_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    ULONG got = 0;
    if (!ReadFile(h_, pv, cb, &got, nullptr)) return HRESULT_FROM_WIN32(GetLastError());
    if (pcb) *pcb = got;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER move, DWORD origin, ULARGE_INTEGER* pn) override {
    if (h_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    DWORD method = origin == STREAM_SEEK_SET   ? FILE_BEGIN
                 : origin == STREAM_SEEK_CUR   ? FILE_CURRENT
                 : origin == STREAM_SEEK_END   ? FILE_END
                                              : 0xFFFFFFFFu;
    if (method == 0xFFFFFFFFu) return STG_E_INVALIDFUNCTION;
    // SetFilePointerEx reports the resulting absolute position, so there is no
    // need for a second call to read the cursor back.
    LARGE_INTEGER landed{};
    if (!SetFilePointerEx(h_, move, pn ? &landed : nullptr, method)) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
    if (pn) pn->QuadPart = (ULONGLONG)landed.QuadPart;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE CopyTo(IStream* dst, ULARGE_INTEGER cb, ULARGE_INTEGER* pcbRead, ULARGE_INTEGER* pcbWrit) override {
    if (!dst) return E_POINTER;
    if (h_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    ULARGE_INTEGER total = cb, doneRead{}, doneWrit{};
    if (!total.QuadPart) {
      // cb == 0 means "copy to the end of this stream", so bound the loop by
      // the real file size rather than looping forever.
      LARGE_INTEGER end{};
      if (!GetFileSizeEx(h_, &end) || end.QuadPart < 0) return STG_E_ACCESSDENIED;
      total.QuadPart = (ULONGLONG)end.QuadPart;
    }
    std::vector<unsigned char> buf(64 * 1024);
    while (total.QuadPart > 0) {
      const ULONG want = (ULONG)std::min<ULONGLONG>(buf.size(), total.QuadPart);
      ULONG got = 0;
      if (FAILED(Read(buf.data(), want, &got)) || !got) break;
      ULONG put = 0;
      const HRESULT hr = dst->Write(buf.data(), got, &put);
      doneRead.QuadPart += got;
      doneWrit.QuadPart += put;
      if (FAILED(hr) || put != got) break;
      total.QuadPart -= got;
    }
    if (pcbRead) *pcbRead = doneRead;
    if (pcbWrit) *pcbWrit = doneWrit;
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return S_OK; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return STG_E_ACCESSDENIED; }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* pst, DWORD) override {
    if (!pst) return E_POINTER;
    *pst = STATSTG{};
    pst->type = STGTY_STREAM;
    pst->grfMode = STGM_READ;
    LARGE_INTEGER size{};
    if (h_ != INVALID_HANDLE_VALUE && GetFileSizeEx(h_, &size) && size.QuadPart >= 0) {
      pst->cbSize.QuadPart = (ULONGLONG)size.QuadPart;
    }
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Clone(IStream** ppstm) override {
    if (!ppstm) return E_POINTER;
    // WIC codecs are not expected to clone, but a second independent view over
    // the same handle is cheap to hand out, so support it rather than fail.
    HANDLE dup = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), h_, GetCurrentProcess(), &dup, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
      if (h_ == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
      *ppstm = nullptr;
      return E_OUTOFMEMORY;
    }
    auto* s = new (std::nothrow) HandleStream(dup);
    if (!s) { CloseHandle(dup); return E_OUTOFMEMORY; }
    *ppstm = s;
    return S_OK;
  }
  HANDLE handle() const { return h_; }
 private:
  HANDLE h_ = INVALID_HANDLE_VALUE;
  volatile LONG ref_ = 1;
};

// D2: validates that a created decoder is actually usable, not merely
// non-null. A decoder that cannot hand back a frame is a failure, and counting
// it as a success would flatter whichever path is more permissive.
inline void validateFrame(IWICBitmapDecoder* dec, DecoderPathTiming& t) {
  if (!dec) return;
  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(dec->GetFrame(0, &frame)) || !frame) return;
  UINT w = 0, h = 0;
  if (FAILED(frame->GetSize(&w, &h)) || !w || !h) return;
  t.width = (int)w;
  t.height = (int)h;
  t.ok = true;
}
}  // namespace

// D2: measurement-only. Compares the three WIC decoder entry points on one
// file. It is never reached from decode()/decodePreserveAspect(), so nothing it
// measures can influence product behaviour.
//
// COM lifetime deliberately belongs to the caller. Calling CoUninitialize()
// here faults on scope exit (the same Windows behaviour already documented
// above decodeWicFile): releasing a WIC decoder can drop the last reference to
// a codec DLL, and tearing the apartment down underneath it leaves the stack
// unusable. This function therefore only initialises COM if nobody has, and
// never uninitialises it; the owning process exits with the apartment intact.
void ImageDecoder::probeDecoderPaths(const std::string& path, DecoderPathProbe& out) {
  out = DecoderPathProbe{};
  const int need = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (!need) return;
  std::wstring wp((std::size_t)need, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wp.data(), need);

  // Match the apartment production uses so the comparison stays apples to
  // apples; S_FALSE / RPC_E_CHANGED_MODE both mean COM is usable already.
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  out.available = true;

  ComPtr<IWICImagingFactory> factory;
  if (SUCCEEDED(createWicFactory(&factory, nullptr))) {
    // Identical metadata options and vendor GUID to production
    // decodeWicFile, so the only variable left is the entry point itself.
    const WICDecodeOptions md = WICDecodeMetadataCacheOnDemand;
    const GUID* vendor = nullptr;

    // ---- Path A: the production entry point ----
    {
      auto& t = out.filename;
      t.attempted = true;
      ComPtr<IWICBitmapDecoder> dec;
      const auto a0 = std::chrono::steady_clock::now();
      t.hr = (std::uint32_t)factory->CreateDecoderFromFilename(
          wp.c_str(), vendor, GENERIC_READ, md, &dec);
      t.decoderMs = d9dMs(a0, std::chrono::steady_clock::now());
      validateFrame(dec.Get(), t);
      t.combinedMs = t.decoderMs;
    }

    // ---- Path B: the caller opens the file, WIC only wraps the handle ----
    {
      auto& t = out.fileHandle;
      t.attempted = true;
      const auto b0 = std::chrono::steady_clock::now();
      HANDLE h = CreateFileW(wp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) {
        t.hr = (std::uint32_t)HRESULT_FROM_WIN32(GetLastError());
        t.combinedMs = d9dMs(b0, std::chrono::steady_clock::now());
        t.fileOpenMs = t.combinedMs;
      } else {
        const auto b1 = std::chrono::steady_clock::now();
        t.fileOpenMs = d9dMs(b0, b1);
        ComPtr<IWICBitmapDecoder> dec;
        t.hr = (std::uint32_t)factory->CreateDecoderFromFileHandle(
            (ULONG_PTR)h, vendor, md, &dec);
        t.decoderMs = d9dMs(b1, std::chrono::steady_clock::now());
        // The decoder still references the handle and may read from it inside
        // GetFrame, so the handle has to outlive validation. Closing it first
        // was a use-after-close.
        validateFrame(dec.Get(), t);
        dec.Reset();
        CloseHandle(h);
        t.combinedMs = t.fileOpenMs + t.decoderMs;
      }
    }

    // ---- Path C: the caller opens the file and hands WIC a stream ----
    {
      auto& t = out.stream;
      t.attempted = true;
      const auto c0 = std::chrono::steady_clock::now();
      HANDLE h = CreateFileW(wp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (h == INVALID_HANDLE_VALUE) {
        t.hr = (std::uint32_t)HRESULT_FROM_WIN32(GetLastError());
        t.combinedMs = d9dMs(c0, std::chrono::steady_clock::now());
        t.fileOpenMs = t.combinedMs;
      } else {
        const auto c1 = std::chrono::steady_clock::now();
        t.fileOpenMs = d9dMs(c0, c1);
        // The handle we opened has to be the one the decoder reads through,
        // otherwise this path would measure a CreateFileW that is thrown away
        // plus a second open hidden inside WIC. Windows offers no
        // HANDLE -> IStream adapter, so HandleStream provides one.
        HandleStream* raw = new (std::nothrow) HandleStream(h);
        ComPtr<IStream> strm;
        if (raw) strm.Attach(raw); else t.hr = (std::uint32_t)E_OUTOFMEMORY;
        const auto c2 = std::chrono::steady_clock::now();
        t.streamInitMs = d9dMs(c1, c2);
        if (SUCCEEDED(t.hr) && strm) {
          ComPtr<IWICBitmapDecoder> dec;
          t.hr = (std::uint32_t)factory->CreateDecoderFromStream(
              strm.Get(), vendor, md, &dec);
          t.decoderMs = d9dMs(c2, std::chrono::steady_clock::now());
          // Validate while the stream, and with it the handle, is still alive.
          validateFrame(dec.Get(), t);
          dec.Reset();
        }
        strm.Reset();
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        t.combinedMs = t.fileOpenMs + t.streamInitMs + t.decoderMs;
      }
    }
  }
}
#endif
bool ImageDecoder::dimensionsFast(const std::string& path,int& w,int& h) const {
    w=h=0;
    // Read only the header: SOF markers sit within the first kilobytes even
    // with large Exif segments; 256 KiB cap bounds the I/O.
    std::ifstream f(path_from_utf8(path),std::ios::binary); if(!f) return false;
    std::vector<unsigned char> b(262144); f.read((char*)b.data(),b.size());
    b.resize((std::size_t)f.gcount());
    return parsePngDims(b,w,h)||parseJpegDims(b,w,h);
}

}
