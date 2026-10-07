// Backend thumbnail encoding (P3b: 0.9.4.69). The IPC channel carries
// bounded derived payloads only (brief §7/§29.1: max 256px, 200KB encoded),
// so engine art is JPEG-encoded here with libjpeg-turbo — no QtGui, keeping
// the G13 Qt6::Core-only backend contract. The GUI decodes with the same
// libjpeg-turbo (turbojpeg.dll, already shipped for the backend) rather than
// Qt's JPEG plugin: the plugin needs a separate jpeg62.dll that is not part
// of the deployed set, so QtGui-only decode silently fails (proven by
// ui_scroll_regression_test/view_mode_probe: qjpeg absent from
// QImageReader::supportedImageFormats).
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

// Decoded 32-bit pixels in ARGB32 memory order (BGRA little-endian), the exact
// layout QImage::Format_ARGB32 expects, so the GUI can wrap the buffer without
// a per-pixel conversion.
struct Argb32Image {
    std::vector<unsigned char> bytes;
    int width = 0;
    int height = 0;
    bool ok = false;
};
Argb32Image decodeJpegArgb32(const unsigned char* jpeg, std::size_t len);

} // namespace msf
