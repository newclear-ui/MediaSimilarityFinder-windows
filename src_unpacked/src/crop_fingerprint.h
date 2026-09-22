#pragma once
#include "image_decoder.h"
#include <cstdint>
namespace msf {
enum class CropAspect { A4x3, A1x1, A9x16 };
struct CropFingerprints { std::uint64_t a4x3=0,a1x1=0,a9x16=0; std::uint64_t mirrorA4x3=0,mirrorA1x1=0,mirrorA9x16=0; };
GrayImage centerCropResize(const GrayImage& src, double targetAspect, int outSize=32);
CropFingerprints cropFingerprints(const GrayImage& src);
}
