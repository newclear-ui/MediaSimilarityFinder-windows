#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace msf {
struct GrayImage { int width=0,height=0; std::vector<std::uint8_t> pixels; };
class ImageDecoder {
public:
    bool decode(const std::string& path,int width,int height,GrayImage& out) const;
    bool decodePreserveAspect(const std::string& path,int maxDimension,GrayImage& out) const;
};
}
