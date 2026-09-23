#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct GrayImage { int width=0,height=0; std::vector<std::uint8_t> pixels; };
struct ColorImage { int width=0,height=0; std::vector<std::uint8_t> bgra; }; // 4 bytes/px, B,G,R,A order
class ImageDecoder {
public:
    bool decode(const std::string& path,int width,int height,GrayImage& out) const;
    bool decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const;
    // Display path (previews): color via WIC BGRA. Fingerprint paths stay gray.
    bool decodeColorAspect(const std::string& path,int maxDimension,ColorImage& out) const;
};
}
