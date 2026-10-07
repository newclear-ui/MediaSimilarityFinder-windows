// Backend thumbnail encoding (P3b: 0.9.4.69). The IPC channel carries
// bounded derived payloads only (brief §7/§29.1: max 256px, 200KB encoded),
// so engine art is JPEG-encoded here with libjpeg-turbo — no QtGui, keeping
// the G13 Qt6::Core-only backend contract. The GUI decodes with QImage (its
// own QtGui, presentation side).
#pragma once
#include <cstddef>
#include <vector>

namespace msf {

struct JpegThumb {
    std::vector<unsigned char> bytes;
    int width = 0;
    int height = 0;
    bool ok = false;
};

// quality 1-100 (70 matches the former GUI disk-cache setting).
JpegThumb encodeJpegThumb(const unsigned char* bgra, int w, int h, int quality = 70);
// Single-channel 8-bit input (video fingerprint thumbs).
JpegThumb encodeJpegGray(const unsigned char* gray, int w, int h, int quality = 70);

} // namespace msf
