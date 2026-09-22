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
static std::uint64_t mirroredHash(const GrayImage& img){return perceptual_hash_mirrored(img.pixels,img.width,img.height);}
CropFingerprints cropFingerprints(const GrayImage& src){
 CropFingerprints r; const auto a=centerCropResize(src,4.0/3.0); const auto s=centerCropResize(src,1.0); const auto v=centerCropResize(src,9.0/16.0);
 if(!a.pixels.empty()){r.a4x3=perceptual_hash(a.pixels,a.width,a.height);r.mirrorA4x3=mirroredHash(a);} if(!s.pixels.empty()){r.a1x1=perceptual_hash(s.pixels,s.width,s.height);r.mirrorA1x1=mirroredHash(s);} if(!v.pixels.empty()){r.a9x16=perceptual_hash(v.pixels,v.width,v.height);r.mirrorA9x16=mirroredHash(v);} return r;
}
}
