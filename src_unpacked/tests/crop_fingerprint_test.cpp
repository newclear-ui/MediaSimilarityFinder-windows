#include "crop_fingerprint.h"
#include "fingerprint.h"
#include "similarity.h"
#include <iostream>
#include <algorithm>
static msf::GrayImage makePattern(int w,int h){msf::GrayImage g;g.width=w;g.height=h;g.pixels.resize((size_t)w*h,18);for(int y=0;y<h;++y)for(int x=0;x<w;++x){int dx=x-w/2,dy=y-h/2;if((dx*dx*9+dy*dy*16)<(w*h/7))g.pixels[(size_t)y*w+x]=235;if(x>w/8&&x<w/3&&y>h/5&&y<h/3)g.pixels[(size_t)y*w+x]=90;}return g;}
static double sim(const msf::GrayImage&a,const msf::GrayImage&b){return msf::hash_similarity(msf::perceptual_hash(a.pixels,a.width,a.height),msf::perceptual_hash(b.pixels,b.width,b.height));}
int main(){auto src=makePattern(160,90);auto c4=msf::centerCropResize(src,4.0/3.0),c1=msf::centerCropResize(src,1.0),c916=msf::centerCropResize(src,9.0/16.0);auto fp=msf::cropFingerprints(src);if(!fp.a4x3||!fp.a1x1||!fp.a9x16)return 1;if(c4.width!=32||c1.width!=32||c916.width!=32)return 2;if(sim(c4,msf::centerCropResize(src,4.0/3.0))<99.9)return 3;if(sim(c1,msf::centerCropResize(src,1.0))<99.9)return 4;if(sim(c916,msf::centerCropResize(src,9.0/16.0))<99.9)return 5;auto mirrored=src;for(int y=0;y<src.height;++y)for(int x=0;x<src.width;++x)mirrored.pixels[(size_t)y*src.width+x]=src.pixels[(size_t)y*src.width+(src.width-1-x)];auto mf=msf::cropFingerprints(mirrored);if(!mf.a4x3||!mf.a1x1||!mf.a9x16||!mf.mirrorA4x3||!mf.mirrorA1x1||!mf.mirrorA9x16)return 6;auto unrelated=makePattern(160,90);std::fill(unrelated.pixels.begin(),unrelated.pixels.end(),240);if(sim(c1,msf::centerCropResize(unrelated,1.0))>=99.0)return 7;std::cout<<"crop_fingerprint=ok\n";return 0;}
