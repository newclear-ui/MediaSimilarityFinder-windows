#include <cmath>
#include "image_decoder.h"
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
    std::ifstream f(path,std::ios::binary); if(!f) return false;
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
    ComPtr<IWICBitmapFrameDecode> frame;
    hr=dec->GetFrame(0,&frame);
    if(FAILED(hr))return false;
    ComPtr<IWICBitmapScaler> scaler;
    hr=factory->CreateBitmapScaler(&scaler);
    if(SUCCEEDED(hr)) hr=scaler->Initialize(frame.Get(),w,h,WICBitmapInterpolationModeFant);
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
    ComPtr<IWICBitmapFrameDecode> frame; hr=dec->GetFrame(0,&frame); if(FAILED(hr))return false; UINT sw=0,sh=0; frame->GetSize(&sw,&sh); if(!sw||!sh)return false;
    UINT w=sw,h=sh; if(sw>sh){w=maxDimension;h=std::max<UINT>(1,(UINT)std::lround((double)sh*maxDimension/sw));} else {h=maxDimension;w=std::max<UINT>(1,(UINT)std::lround((double)sw*maxDimension/sh));}
    ComPtr<IWICBitmapScaler> scaler; hr=factory->CreateBitmapScaler(&scaler); if(SUCCEEDED(hr)) hr=scaler->Initialize(frame.Get(),w,h,WICBitmapInterpolationModeFant); if(FAILED(hr))return false;
    ComPtr<IWICFormatConverter> conv; hr=factory->CreateFormatConverter(&conv); if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom); if(FAILED(hr))return false;
    out.width=(int)w;out.height=(int)h;out.pixels.resize((size_t)w*h); hr=conv->CopyPixels(nullptr,w,out.pixels.size(),out.pixels.data()); return SUCCEEDED(hr);
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
#else
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out) const {
  return decodePgm(path,w,h,out);
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const {
  if(maxDimension<=0) return false;
  return decodePgmAspect(path,maxDimension,out);
}
#endif

}
