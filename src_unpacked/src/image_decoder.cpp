#include <cmath>
#include "image_decoder.h"
#include "path_utils.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <vector>
#pragma comment(lib,"windowscodecs.lib")
#pragma comment(lib,"ole32.lib")
#endif
#include <fstream>
#include <sstream>
#include <algorithm>

namespace msf {
namespace {
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
bool decodeWicFile(const wchar_t* wpath,int w,int h,GrayImage& out){
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
    if(FAILED(hr)) hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
    if(FAILED(hr))return false;
    ComPtr<IWICBitmapDecoder> dec;
    hr=factory->CreateDecoderFromFilename(wpath,nullptr,GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,&dec);
    if(FAILED(hr))return false;
    // EXIF orientation: the fingerprint must describe the image as displayed.
    // QImageReader::setAutoTransform(true) does this in the display lane, but
    // WIC raw frames do not. Read the tag and swap/flip on the decoded pixels
    // (orientations 2..8) so rotated phone photos match their displayed form.
    ComPtr<IWICBitmapFlipRotator> orient;
    ComPtr<IWICBitmapSource> src;
    {
      ComPtr<IWICBitmapFrameDecode> frame;
      hr=dec->GetFrame(0,&frame);
      if(FAILED(hr))return false;
      WICBitmapTransformOptions xform=WICBitmapTransformRotate0;
      ComPtr<IWICMetadataQueryReader> meta;
      if(SUCCEEDED(frame->GetMetadataQueryReader(&meta))&&meta){
        PROPVARIANT v; PropVariantInit(&v);
        if(SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}",&v))&&v.vt==VT_UI2){
          switch(v.uiVal){
            case 2: xform=WICBitmapTransformFlipHorizontal; break;
            case 3: xform=WICBitmapTransformRotate180; break;
            case 4: xform=WICBitmapTransformFlipVertical; break;
            case 5: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal); break;
            case 6: xform=WICBitmapTransformRotate90; break;
            case 7: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal); break;
            case 8: xform=WICBitmapTransformRotate270; break;
          }
        }
        PropVariantClear(&v);
      }
      if(xform!=WICBitmapTransformRotate0){
        if(FAILED(factory->CreateBitmapFlipRotator(&orient))) return false;
        if(FAILED(orient->Initialize(frame.Get(),xform))) return false;
        src=orient;
      } else {
        src=frame;
      }
    }
    ComPtr<IWICBitmapScaler> scaler;
    hr=factory->CreateBitmapScaler(&scaler);
    if(SUCCEEDED(hr)) hr=scaler->Initialize(src.Get(),w,h,WICBitmapInterpolationModeFant);
    if(FAILED(hr))return false;
    ComPtr<IWICFormatConverter> conv;
    hr=factory->CreateFormatConverter(&conv);
    if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,
                                           WICBitmapDitherTypeNone,nullptr,0.0,
                                           WICBitmapPaletteTypeCustom);
    if(FAILED(hr))return false;
    out.width=w; out.height=h; out.pixels.resize(size_t(w)*size_t(h));
    hr=conv->CopyPixels(nullptr,w,out.pixels.size(),out.pixels.data());
    return SUCCEEDED(hr);
}
bool decodeWicFileAspect(const wchar_t* wpath,int maxDimension,GrayImage& out){
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)); if(FAILED(hr)) hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr))return false; ComPtr<IWICBitmapDecoder> dec; hr=factory->CreateDecoderFromFilename(wpath,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec); if(FAILED(hr))return false;
    ComPtr<IWICBitmapFlipRotator> orient;
    ComPtr<IWICBitmapSource> src;
    UINT sw=0,sh=0;
    {
      ComPtr<IWICBitmapFrameDecode> frame; hr=dec->GetFrame(0,&frame); if(FAILED(hr))return false;
      WICBitmapTransformOptions xform=WICBitmapTransformRotate0;
      ComPtr<IWICMetadataQueryReader> meta;
      if(SUCCEEDED(frame->GetMetadataQueryReader(&meta))&&meta){
        PROPVARIANT v; PropVariantInit(&v);
        if(SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}",&v))&&v.vt==VT_UI2){
          switch(v.uiVal){
            case 2: xform=WICBitmapTransformFlipHorizontal; break;
            case 3: xform=WICBitmapTransformRotate180; break;
            case 4: xform=WICBitmapTransformFlipVertical; break;
            case 5: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal); break;
            case 6: xform=WICBitmapTransformRotate90; break;
            case 7: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal); break;
            case 8: xform=WICBitmapTransformRotate270; break;
          }
        }
        PropVariantClear(&v);
      }
      if(xform!=WICBitmapTransformRotate0){
        if(FAILED(factory->CreateBitmapFlipRotator(&orient))) return false;
        if(FAILED(orient->Initialize(frame.Get(),xform))) return false;
        src=orient;
      } else {
        src=frame;
      }
      // Oriented size drives the aspect math: 90/270-degree rotations swap w/h.
      UINT fw=0,fh=0; if(FAILED(src->GetSize(&fw,&fh))||!fw||!fh)return false; sw=fw; sh=fh;
    }
    UINT w=sw,h=sh; if(sw>sh){w=maxDimension;h=std::max<UINT>(1,(UINT)std::lround((double)sh*maxDimension/sw));} else {h=maxDimension;w=std::max<UINT>(1,(UINT)std::lround((double)sw*maxDimension/sh));}
    ComPtr<IWICBitmapScaler> scaler; hr=factory->CreateBitmapScaler(&scaler); if(SUCCEEDED(hr)) hr=scaler->Initialize(src.Get(),w,h,WICBitmapInterpolationModeFant); if(FAILED(hr))return false;
    ComPtr<IWICFormatConverter> conv; hr=factory->CreateFormatConverter(&conv); if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom); if(FAILED(hr))return false;
    out.width=(int)w;out.height=(int)h;out.pixels.resize((size_t)w*h); hr=conv->CopyPixels(nullptr,w,out.pixels.size(),out.pixels.data()); return SUCCEEDED(hr);
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
      if(SUCCEEDED(frame->GetMetadataQueryReader(&meta))&&meta){
        PROPVARIANT v; PropVariantInit(&v);
        if(SUCCEEDED(meta->GetMetadataByName(L"/app1/ifd/exif/{ushort=274}",&v))&&v.vt==VT_UI2){
          switch(v.uiVal){
            case 2: xform=WICBitmapTransformFlipHorizontal; break;
            case 3: xform=WICBitmapTransformRotate180; break;
            case 4: xform=WICBitmapTransformFlipVertical; break;
            case 5: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate90|WICBitmapTransformFlipHorizontal); break;
            case 6: xform=WICBitmapTransformRotate90; break;
            case 7: xform=(WICBitmapTransformOptions)(WICBitmapTransformRotate270|WICBitmapTransformFlipHorizontal); break;
            case 8: xform=WICBitmapTransformRotate270; break;
          }
        }
        PropVariantClear(&v);
      }
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
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out) const {
    if(w<=0||h<=0) return false;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    if(!need) return false;
    std::wstring wp(need,L'\0');
    MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool uninit=SUCCEEDED(hr);
    bool ok=decodeWicFile(wp.c_str(),w,h,out);
    if(!ok) ok=decodePgm(path,w,h,out); // e.g. PGM fixtures WIC cannot parse
    if(uninit)CoUninitialize();
    return ok;
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const {
    if(maxDimension<=0) return false;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0); if(!need) return false;
    std::wstring wp(need,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); bool uninit=SUCCEEDED(hr);
    bool ok=decodeWicFileAspect(wp.c_str(),maxDimension,out);
    if(!ok) ok=decodePgmAspect(path,maxDimension,out); // e.g. PGM fixtures WIC cannot parse
    if(uninit)CoUninitialize(); return ok;
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
bool ImageDecoder::decodeColorAspect(const std::string&,int,ColorImage&) const {
  return false; // no color WIC outside Windows; callers fall back to file icons
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
