#include "fingerprint.h"
#include <algorithm>
#include <array>
#include <cmath>
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

namespace {
constexpr int N=32;  // logical grid the DCT runs on
constexpr int K=8;   // low-frequency block kept (K*K coefficients, DC excluded)

// Same table formulas as host_tables() in cuda_backend.cu, built once
// (thread-safe function-local static). The old code recomputed 2*64*1024
// std::cos() calls per hash.
struct DctTables {
    double cos[K][N];
    double norm[K];
    DctTables() {
        for(int u=0;u<K;++u){
            norm[u]=(u==0)?std::sqrt(1.0/N):std::sqrt(2.0/N);
            for(int x=0;x<N;++x) cos[u][x]=std::cos((2*x+1)*u*std::numbers::pi/(2*N));
        }
    }
};
const DctTables& tables(){ static const DctTables t; return t; }

// Separable 2-D DCT (rows first, then columns), same evaluation order as the
// CUDA kernel: d[u][v], u = horizontal frequency, v = vertical frequency.
void dct8x8(const std::uint8_t* px,double d[K][K]){
    const DctTables& t=tables();
    double tmp[K][N];
    for(int u=0;u<K;++u) for(int y=0;y<N;++y){
        double s=0.0;
        for(int x=0;x<N;++x) s+=static_cast<double>(px[y*N+x])*t.cos[u][x];
        tmp[u][y]=s*t.norm[u];
    }
    for(int u=0;u<K;++u) for(int v=0;v<K;++v){
        double s=0.0;
        for(int y=0;y<N;++y) s+=tmp[u][y]*t.cos[v][y];
        d[u][v]=s*t.norm[v];
    }
}

// 63 AC coefficients vs their median, bit order u-major/v-minor (bit 63 unused).
// flipOdd=true evaluates the horizontally mirrored image: cos((2(N-1-x)+1)uπ/2N)
// = (-1)^u * cos((2x+1)uπ/2N), so odd u flips sign.
std::uint64_t bits_from(const double d[K][K],bool flipOdd){
    double vals[K*K-1]; int n=0;
    for(int u=0;u<K;++u) for(int v=0;v<K;++v) if(u||v) { double c=(flipOdd&&(u&1))?-d[u][v]:d[u][v]; vals[n++]=(std::abs(c)<1e-7)?0.0:c; }
    double sorted[K*K-1]; std::copy(vals,vals+n,sorted);
    std::nth_element(sorted,sorted+n/2,sorted+n);
    const double median=sorted[n/2];
    std::uint64_t h=0;
    for(int i=0;i<n;++i) if(vals[i]>=median) h|=1ULL<<i;
    return h;
}

// Nearest-neighbour sampling of an arbitrary WxH image onto the 32x32 grid
// (identical mapping to the previous implementation).
void sample32(const std::vector<std::uint8_t>& px,int width,int height,std::uint8_t* out){
    for(int y=0;y<N;++y){
        const int sy=std::min(height-1,y*height/N);
        for(int x=0;x<N;++x){
            const int sx=std::min(width-1,x*width/N);
            out[y*N+x]=px[static_cast<std::size_t>(sy)*width+sx];
        }
    }
}
bool valid(const std::vector<std::uint8_t>& px,int w,int h){
    return w>0&&h>0&&px.size()>=static_cast<std::size_t>(w)*h;
}
}

std::uint64_t perceptual_hash(const std::vector<std::uint8_t>& pixels,int width,int height){
    if(!valid(pixels,width,height)) return 0;
    double d[K][K];
    if(width==N&&height==N){ dct8x8(pixels.data(),d); }
    else { std::array<std::uint8_t,N*N> g; sample32(pixels,width,height,g.data()); dct8x8(g.data(),d); }
    return bits_from(d,false);
}

std::uint64_t perceptual_hash_mirrored(const std::vector<std::uint8_t>& pixels,int width,int height){
    if(!valid(pixels,width,height)) return 0;
    if(width==N&&height==N){ double d[K][K]; dct8x8(pixels.data(),d); return bits_from(d,true); }
    // Non-32 sources: the nearest-neighbour grid is not mirror-symmetric, so
    // keep the exact old semantics (mirror the source, then hash it).
    std::vector<std::uint8_t> m(static_cast<std::size_t>(width)*height);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
        m[static_cast<std::size_t>(y)*width+x]=pixels[static_cast<std::size_t>(y)*width+(width-1-x)];
    return perceptual_hash(m,width,height);
}

PerceptualHashPair perceptual_hash_pair_32(const std::uint8_t* px){
    PerceptualHashPair r; if(!px) return r;
    double d[K][K]; dct8x8(px,d);
    r.normal=bits_from(d,false); r.mirrored=bits_from(d,true);
    return r;
}

PerceptualHashPair perceptual_hash_pair(const std::vector<std::uint8_t>& pixels,int width,int height){
    if(!valid(pixels,width,height)) return {};
    if(width==N&&height==N) return perceptual_hash_pair_32(pixels.data());
    return {perceptual_hash(pixels,width,height),perceptual_hash_mirrored(pixels,width,height)};
}
}
