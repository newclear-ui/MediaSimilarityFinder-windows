#include "fingerprint.h"
#include <cmath>
#include <algorithm>
#include <numbers>
namespace msf {
std::uint64_t average_hash(const std::vector<std::uint8_t>& p) {
    if(p.empty()) return 0;
    std::uint64_t sum=0; for(auto x:p) sum+=x;
    const auto avg=sum/p.size(); std::uint64_t h=0;
    const auto n=std::min<std::size_t>(64,p.size());
    for(std::size_t i=0;i<n;i++) if(p[i]>=avg) h|=(1ULL<<i);
    return h;
}

std::uint64_t perceptual_hash(const std::vector<std::uint8_t>& pixels, int width, int height) {
    if(width<=0 || height<=0 || pixels.size()<static_cast<std::size_t>(width)*height) return 0;
    // Resize conceptually to an 8x8 DCT block by bilinear-nearest sampling from
    // the normalized input. We compute the low-frequency 8x8 DCT coefficients
    // over a 32x32 logical grid, then compare the 8x8 coefficients (excluding DC)
    // against their median. This is a compact pHash suitable for fast indexing.
    constexpr int N=32, K=8;
    double d[K][K]{};
    for(int u=0;u<K;u++) for(int v=0;v<K;v++) {
        double sum=0;
        for(int y=0;y<N;y++) {
            int sy=std::min(height-1, y*height/N);
            for(int x=0;x<N;x++) {
                int sx=std::min(width-1, x*width/N);
                double px=pixels[static_cast<std::size_t>(sy)*width+sx];
                sum += px*std::cos((2*x+1)*u*std::numbers::pi/(2*N))*std::cos((2*y+1)*v*std::numbers::pi/(2*N));
            }
        }
        double au=(u==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
        double av=(v==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
        d[u][v]=sum*au*av;
    }
    double vals[63]; int n=0;
    for(int u=0;u<K;u++) for(int v=0;v<K;v++) if(u||v) vals[n++]=d[u][v];
    std::sort(vals,vals+n); double median=vals[n/2];
    std::uint64_t h=0; int bit=0;
    for(int u=0;u<K;u++) for(int v=0;v<K;v++) if(u||v) { if(d[u][v]>=median) h|=1ULL<<bit; ++bit; }
    return h;
}

std::uint64_t perceptual_hash_mirrored(const std::vector<std::uint8_t>& pixels, int width, int height) {
    if(width<=0 || height<=0 || pixels.size()<static_cast<std::size_t>(width)*height) return 0;
    std::vector<std::uint8_t> mirrored(static_cast<std::size_t>(width)*height);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        mirrored[static_cast<std::size_t>(y)*width+x]=pixels[static_cast<std::size_t>(y)*width+(width-1-x)];
    return perceptual_hash(mirrored,width,height);
}
}
