#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "video_sampling.h"
namespace msf { struct AnalysisResult{std::uint64_t fingerprint=0; std::size_t samples=0; double intervalSec=0;}; AnalysisResult analyze_image_bytes(const std::vector<std::uint8_t>&); AnalysisResult analyze_video_duration(const std::string&,double); }
