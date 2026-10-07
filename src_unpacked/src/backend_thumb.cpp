// See backend_thumb.h.
#include "backend_thumb.h"

#include <turbojpeg.h>

namespace msf {
namespace {
JpegThumb compress(int w, int h, const unsigned char* src, int pixelFormat,
                   int pitch, int quality) {
    JpegThumb out;
    out.width = w;
    out.height = h;
    if (w <= 0 || h <= 0 || !src) return out;
    tjhandle hnd = tjInitCompress();
    if (!hnd) return out;
    unsigned char* buf = nullptr;
    unsigned long size = 0;
    const int rc = tjCompress2(hnd, src, w, pitch, h, pixelFormat, &buf, &size,
                               TJSAMP_420, quality, TJFLAG_FASTDCT);
    tjDestroy(hnd);
    if (rc != 0 || !buf || size == 0) {
        if (buf) tjFree(buf);
        return out;
    }
    out.bytes.assign(buf, buf + size);
    tjFree(buf);
    out.ok = true;
    return out;
}
} // namespace

JpegThumb encodeJpegThumb(const unsigned char* bgra, int w, int h, int quality) {
    return compress(w, h, bgra, TJPF_BGRA, w * 4, quality);
}

JpegThumb encodeJpegGray(const unsigned char* gray, int w, int h, int quality) {
    return compress(w, h, gray, TJPF_GRAY, w, quality);
}

} // namespace msf
