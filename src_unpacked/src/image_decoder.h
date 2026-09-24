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
    // Header-only dimensions: PNG IHDR + JPEG SOF parsed from the first bytes,
    // no decode, no process spawn. Lets the GUI show resolutions without
    // paying an ffprobe child per file (console flash + ~100ms on the UI
    // thread). Returns false for anything else (caller falls back).
    bool dimensionsFast(const std::string& path,int& w,int& h) const;
};
}
