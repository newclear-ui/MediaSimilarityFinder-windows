#include "crop_fingerprint.h"
#include "fingerprint.h"
#include <algorithm>
#include <cmath>
namespace msf {
GrayImage centerCropResize(const GrayImage& src, double targetAspect, int outSize){
 GrayImage out; if(src.width<=0||src.height<=0||src.pixels.empty()||targetAspect<=0||outSize<=0)return out;
 double srcAspect=static_cast<double>(src.width)/src.height; int cw=src.width,ch=src.height;
 if(srcAspect>targetAspect) cw=std::max(1,(int)std::lround(src.height*targetAspect));
 else if(srcAspect<targetAspect) ch=std::max(1,(int)std::lround(src.width/targetAspect));
 int x0=(src.width-cw)/2,y0=(src.height-ch)/2; out.width=outSize;out.height=outSize;out.pixels.resize((size_t)outSize*outSize);
 for(int y=0;y<outSize;++y){int sy=y0+std::min(ch-1,y*ch/outSize);for(int x=0;x<outSize;++x){int sx=x0+std::min(cw-1,x*cw/outSize);out.pixels[(size_t)y*outSize+x]=src.pixels[(size_t)sy*src.width+sx];}}
 return out;
}
static void hashPair(const GrayImage& img,std::uint64_t& normal,std::uint64_t& mirrored){const auto h=perceptual_hash_pair(img.pixels,img.width,img.height);normal=h.normal;mirrored=h.mirrored;}
CropFingerprints cropFingerprints(const GrayImage& src){
 CropFingerprints r; const auto a=centerCropResize(src,4.0/3.0); const auto s=centerCropResize(src,1.0); const auto v=centerCropResize(src,9.0/16.0);
 if(!a.pixels.empty())hashPair(a,r.a4x3,r.mirrorA4x3); if(!s.pixels.empty())hashPair(s,r.a1x1,r.mirrorA1x1); if(!v.pixels.empty())hashPair(v,r.a9x16,r.mirrorA9x16); return r;
}
}
