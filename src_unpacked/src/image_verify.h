#pragma once
#include <string>
namespace msf {
// Second-stage structural verification for IMAGE pairs that passed L1 Hamming.
//
// Rationale: pHash keeps 8x8 low-frequency DCT only, and bestMatch() takes the
// max over dozens of full/mirror/crop variant pairs. Two dark, smooth, warm
// photos can land within Hamming <= 8 (87.5%) by low-frequency coincidence.
// Like the video L3 gate: Hamming failures are untouched; near-identical pairs
// (>=97) return immediately with no decode; grey-zone pairs are re-scored
// 0.5*Hamming + 0.5*SSIM over the squashed full frame, the aspect-preserved
// center-crop regions (same centerCropResize geometry as the crop
// fingerprints), and crop-vs-full pairings, mirror covered by flipped
// comparison. Only pairs whose blend still reaches threshold pass.
// Decodes are shared through a small stat-validated LRU so repeated pairs
// sharing a file decode once. Decode failures fall back to the Hamming score
// (legacy behavior).
// Decode failures fall back to the Hamming score (legacy behavior).
// Non-image pairs pass through unchanged (videos keep their temporal L2/L3).
double verifyImagePair(const std::string& pathA, const std::string& pathB,
                       bool isImage, double hammingSim, double threshold);
}
