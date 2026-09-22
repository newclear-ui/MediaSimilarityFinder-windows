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
#ifdef _WIN32
using Microsoft::WRL::ComPtr;
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out) const {
    if(w<=0||h<=0) return false;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
    if(!need) return false;
    std::wstring wp(need,L'\0');
    MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    bool uninit=SUCCEEDED(hr);
    ComPtr<IWICImagingFactory> factory;
    hr=CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,
                        IID_PPV_ARGS(&factory));
    if(FAILED(hr)) hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&factory));
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICBitmapDecoder> dec;
    hr=factory->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,&dec);
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICBitmapFrameDecode> frame;
    hr=dec->GetFrame(0,&frame);
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICBitmapScaler> scaler;
    hr=factory->CreateBitmapScaler(&scaler);
    if(SUCCEEDED(hr)) hr=scaler->Initialize(frame.Get(),w,h,WICBitmapInterpolationModeFant);
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICFormatConverter> conv;
    hr=factory->CreateFormatConverter(&conv);
    if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,
                                           WICBitmapDitherTypeNone,nullptr,0.0,
                                           WICBitmapPaletteTypeCustom);
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    out.width=w; out.height=h; out.pixels.resize(size_t(w)*size_t(h));
    hr=conv->CopyPixels(nullptr,w,out.pixels.size(),out.pixels.data());
    if(uninit)CoUninitialize();
    return SUCCEEDED(hr);
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const {
    if(maxDimension<=0) return false;
    int need=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0); if(!need) return false;
    std::wstring wp(need,L'\0'); MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wp.data(),need);
    HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED); bool uninit=SUCCEEDED(hr); ComPtr<IWICImagingFactory> factory;
    hr=CoCreateInstance(CLSID_WICImagingFactory2,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)); if(FAILED(hr)) hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(FAILED(hr)){if(uninit)CoUninitialize();return false;} ComPtr<IWICBitmapDecoder> dec; hr=factory->CreateDecoderFromFilename(wp.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&dec); if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICBitmapFrameDecode> frame; hr=dec->GetFrame(0,&frame); if(FAILED(hr)){if(uninit)CoUninitialize();return false;} UINT sw=0,sh=0; frame->GetSize(&sw,&sh); if(!sw||!sh){if(uninit)CoUninitialize();return false;}
    UINT w=sw,h=sh; if(sw>sh){w=maxDimension;h=std::max<UINT>(1,(UINT)std::lround((double)sh*maxDimension/sw));} else {h=maxDimension;w=std::max<UINT>(1,(UINT)std::lround((double)sw*maxDimension/sh));}
    ComPtr<IWICBitmapScaler> scaler; hr=factory->CreateBitmapScaler(&scaler); if(SUCCEEDED(hr)) hr=scaler->Initialize(frame.Get(),w,h,WICBitmapInterpolationModeFant); if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    ComPtr<IWICFormatConverter> conv; hr=factory->CreateFormatConverter(&conv); if(SUCCEEDED(hr)) hr=conv->Initialize(scaler.Get(),GUID_WICPixelFormat8bppGray,WICBitmapDitherTypeNone,nullptr,0.0,WICBitmapPaletteTypeCustom); if(FAILED(hr)){if(uninit)CoUninitialize();return false;}
    out.width=(int)w;out.height=(int)h;out.pixels.resize((size_t)w*h); hr=conv->CopyPixels(nullptr,w,out.pixels.size(),out.pixels.data()); if(uninit)CoUninitialize(); return SUCCEEDED(hr);
}
#else
bool ImageDecoder::decode(const std::string& path,int w,int h,GrayImage& out) const {
 if(w<=0||h<=0)return false; std::ifstream f(path,std::ios::binary); if(!f)return false;
 std::string magic; int sw=0,sh=0,maxv=0; f>>magic; if(magic!="P5") return false;
 f>>sw>>sh>>maxv; f.get(); if(sw<=0||sh<=0||maxv<=0)return false; std::vector<unsigned char> src((size_t)sw*sh); f.read((char*)src.data(),src.size()); if((size_t)f.gcount()!=src.size())return false;
 out.width=w;out.height=h;out.pixels.resize((size_t)w*h);
 for(int y=0;y<h;y++){int sy=std::min(sh-1,y*sh/h);for(int x=0;x<w;x++){int sx=std::min(sw-1,x*sw/w);out.pixels[(size_t)y*w+x]=src[(size_t)sy*sw+sx];}} return true;
}
bool ImageDecoder::decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const {
 if(maxDimension<=0)return false; std::ifstream f(path,std::ios::binary); if(!f)return false; std::string magic; int sw=0,sh=0,maxv=0; f>>magic; if(magic!="P5")return false; f>>sw>>sh>>maxv; f.get(); if(sw<=0||sh<=0||maxv<=0)return false; std::vector<unsigned char> src((size_t)sw*sh); f.read((char*)src.data(),src.size()); if((size_t)f.gcount()!=src.size())return false;
 int w=sw,h=sh; if(sw>=sh){w=maxDimension;h=std::max(1,(int)std::lround((double)sh*maxDimension/sw));}else{h=maxDimension;w=std::max(1,(int)std::lround((double)sw*maxDimension/sh));} out.width=w;out.height=h;out.pixels.resize((size_t)w*h); for(int y=0;y<h;y++){int sy=std::min(sh-1,y*sh/h);for(int x=0;x<w;x++){int sx=std::min(sw-1,x*sw/w);out.pixels[(size_t)y*w+x]=src[(size_t)sy*sw+sx];}} return true;
}
#endif

}
