#include "RawDecoder.hpp"
#include <QFileInfo>
#include <QTransform>
#include <QtMath>
#include <cmath>

#if defined(MYLR_HAS_LIBRAW) && MYLR_HAS_LIBRAW
#include <libraw/libraw.h>
#endif

namespace mylr {

struct RawDecoder::Impl {};

RawDecoder::RawDecoder() : m_impl(std::make_unique<Impl>()) {}
RawDecoder::~RawDecoder() = default;

QStringList RawDecoder::supportedExtensions() {
    return {QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("nef"),
            QStringLiteral("arw"), QStringLiteral("dng"), QStringLiteral("raf"),
            QStringLiteral("orf"), QStringLiteral("rw2")};
}

bool RawDecoder::isRawFile(const QString& path) {
    return supportedExtensions().contains(QFileInfo(path).suffix().toLower());
}

namespace {

QImage scaleToMaxEdge(const QImage& img, int maxEdge) {
    if (img.isNull() || maxEdge <= 0) return img;
    if (img.width() <= maxEdge && img.height() <= maxEdge)
        return img.convertToFormat(QImage::Format_RGB888);
    const float scale = static_cast<float>(maxEdge) /
                        static_cast<float>(qMax(img.width(), img.height()));
    return img.scaled(static_cast<int>(img.width() * scale),
                      static_cast<int>(img.height() * scale),
                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .convertToFormat(QImage::Format_RGB888);
}

#if defined(MYLR_HAS_LIBRAW) && MYLR_HAS_LIBRAW
void fillMetadataFromLibRaw(RawMetadata& metadata, LibRaw& raw) {
    metadata.width = raw.imgdata.sizes.iwidth;
    metadata.height = raw.imgdata.sizes.iheight;
    if (raw.imgdata.idata.make[0]) {
        metadata.cameraModel = QString::fromUtf8(raw.imgdata.idata.make) + QLatin1Char(' ') +
                               QString::fromUtf8(raw.imgdata.idata.model);
    }
    metadata.iso = raw.imgdata.other.iso_speed;
    metadata.aperture = raw.imgdata.other.aperture;
    metadata.shutterSec = raw.imgdata.other.shutter;
    metadata.focalLength = raw.imgdata.other.focal_len;
    if (metadata.aperture > 0 && metadata.shutterSec > 0) {
        metadata.evBaseline = std::log2((metadata.aperture * metadata.aperture) /
                                        metadata.shutterSec * 100.f / qMax(1, metadata.iso));
    }
    // LibRaw applies the as-shot WB (cam_mul) during decode, so the linear buffer
    // is already neutral; the pipeline WB stage must therefore stay at unity
    // (Temp/Tint then act as relative offsets from the as-shot baseline).
    metadata.wbCoeffs[0] = 1.f;
    metadata.wbCoeffs[1] = 1.f;
    metadata.wbCoeffs[2] = 1.f;
    metadata.wbCoeffs[3] = 1.f;
}

int openLibRawFile(LibRaw& raw, const QString& path) {
#ifdef _WIN32
    return raw.open_file(reinterpret_cast<const wchar_t*>(path.utf16()));
#else
    return raw.open_file(path.toUtf8().constData());
#endif
}

void configureLibRawOutput(LibRaw& raw) {
    raw.imgdata.params.output_color = 1;  // sRGB primaries
    raw.imgdata.params.output_bps = 16;   // high-bit so the float pipeline has headroom
    // Decode to LINEAR light (gamma 1.0, no auto-brighten) but let LibRaw apply
    // the as-shot camera white balance (cam_mul) in camera-native space. This is
    // both correct and robust: the linear buffer is already neutral, so the
    // pipeline's WB stage stays at unity and Temp/Tint act as relative offsets.
    raw.imgdata.params.gamm[0] = 1.0;     // linear gamma (power)
    raw.imgdata.params.gamm[1] = 1.0;     // linear toe slope
    raw.imgdata.params.no_auto_bright = 1;  // never auto-stretch; keep exposure predictable
    raw.imgdata.params.use_camera_wb = 1;   // as-shot WB from cam_mul
    raw.imgdata.params.use_auto_wb = 0;
    raw.imgdata.params.no_auto_scale = 0;
    raw.imgdata.params.highlight = 0;     // keep highlights (clip to white level)
    raw.imgdata.params.user_qual = 3;     // AHD demosaic
}

// sRGB transfer functions (kept local so the decoder does not depend on the
// render module). Used to build the 8-bit display preview from linear data.
inline float linToSrgb(float c) {
    c = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f;
}
inline float srgbToLin(float c) {
    c = c < 0.f ? 0.f : (c > 1.f ? 1.f : c);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Build a 16-bit-per-channel LINEAR image (Format_RGBX64) from LibRaw's processed
// buffer. Values are scene-referred linear with 1.0 (=65535) at sensor white.
QImage linear64FromLibRawImage(const libraw_processed_image_t* img) {
    if (!img || img->type != LIBRAW_IMAGE_BITMAP || img->colors < 3)
        return {};

    const int w = img->width;
    const int h = img->height;
    const int colors = img->colors;
    QImage full(w, h, QImage::Format_RGBX64);

    if (img->bits == 16) {
        const unsigned short* src = reinterpret_cast<const unsigned short*>(img->data);
        for (int y = 0; y < h; ++y) {
            auto* dst = reinterpret_cast<quint16*>(full.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int si = (y * w + x) * colors;
                dst[x * 4 + 0] = src[si + 0];
                dst[x * 4 + 1] = src[si + 1];
                dst[x * 4 + 2] = src[si + 2];
                dst[x * 4 + 3] = 0xffff;
            }
        }
    } else if (img->bits == 8) {
        const unsigned char* src = img->data;
        for (int y = 0; y < h; ++y) {
            auto* dst = reinterpret_cast<quint16*>(full.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const int si = (y * w + x) * colors;
                dst[x * 4 + 0] = static_cast<quint16>(src[si + 0]) << 8;
                dst[x * 4 + 1] = static_cast<quint16>(src[si + 1]) << 8;
                dst[x * 4 + 2] = static_cast<quint16>(src[si + 2]) << 8;
                dst[x * 4 + 3] = 0xffff;
            }
        }
    } else {
        return {};
    }
    return full;
}

// Scale a linear RGBX64 image to a maximum edge, preserving the 16-bit format.
QImage scaleLinear64(const QImage& img, int maxEdge) {
    if (img.isNull() || maxEdge <= 0) return img;
    if (img.width() <= maxEdge && img.height() <= maxEdge) return img;
    const float scale = static_cast<float>(maxEdge) /
                        static_cast<float>(qMax(img.width(), img.height()));
    return img.scaled(static_cast<int>(img.width() * scale),
                      static_cast<int>(img.height() * scale),
                      Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// Produce an 8-bit sRGB display image from linear RGBX64 for UI thumbnails and
// the AI feature/baseline path: apply as-shot WB (normalized to green) then sRGB.
QImage previewFromLinear64(const QImage& lin, const float wb[4]) {
    if (lin.isNull()) return {};
    const int w = lin.width(), h = lin.height();
    QImage out(w, h, QImage::Format_RGB888);
    const float wr = wb[0] > 0.f ? wb[0] : 1.f;
    const float wg = wb[1] > 0.f ? wb[1] : 1.f;
    const float wbb = wb[2] > 0.f ? wb[2] : 1.f;
    for (int y = 0; y < h; ++y) {
        const auto* src = reinterpret_cast<const quint16*>(lin.constScanLine(y));
        auto* dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float r = src[x * 4 + 0] / 65535.f * wr;
            const float g = src[x * 4 + 1] / 65535.f * wg;
            const float b = src[x * 4 + 2] / 65535.f * wbb;
            dst[x * 3 + 0] = static_cast<unsigned char>(linToSrgb(r) * 255.f + 0.5f);
            dst[x * 3 + 1] = static_cast<unsigned char>(linToSrgb(g) * 255.f + 0.5f);
            dst[x * 3 + 2] = static_cast<unsigned char>(linToSrgb(b) * 255.f + 0.5f);
        }
    }
    return out;
}

// Convert an 8-bit sRGB image (embedded thumbnail / non-RAW fallback) into a
// linear RGBX64 buffer so the rest of the pipeline can treat it uniformly.
QImage srgb8ToLinear64(const QImage& srgbIn) {
    if (srgbIn.isNull()) return {};
    const QImage srgb = srgbIn.convertToFormat(QImage::Format_RGB888);
    const int w = srgb.width(), h = srgb.height();
    QImage out(w, h, QImage::Format_RGBX64);
    for (int y = 0; y < h; ++y) {
        const auto* s = srgb.constScanLine(y);
        auto* d = reinterpret_cast<quint16*>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            d[x * 4 + 0] = static_cast<quint16>(srgbToLin(s[x * 3 + 0] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 1] = static_cast<quint16>(srgbToLin(s[x * 3 + 1] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 2] = static_cast<quint16>(srgbToLin(s[x * 3 + 2] / 255.f) * 65535.f + 0.5f);
            d[x * 4 + 3] = 0xffff;
        }
    }
    return out;
}

// Apply LibRaw's `sizes.flip` orientation code to an image so embedded thumbnails
// match the orientation that dcraw_process() bakes into the full decode. The
// code is a dcraw bitmask: 1 = vertical mirror, 2 = horizontal mirror,
// 4 = transpose (swap x/y). The common camera values are 0, 3 (180),
// 5 (90 CCW) and 6 (90 CW).
QImage applyLibRawFlip(const QImage& img, int flip) {
    if (img.isNull() || flip <= 0) return img;
    switch (flip) {
        case 1: return img.mirrored(false, true);
        case 2: return img.mirrored(true, false);
        case 3: return img.transformed(QTransform().rotate(180), Qt::SmoothTransformation);
        case 5: return img.transformed(QTransform().rotate(270), Qt::SmoothTransformation);
        case 6: return img.transformed(QTransform().rotate(90), Qt::SmoothTransformation);
        case 4:  // transpose
            return img.transformed(QTransform().rotate(90), Qt::SmoothTransformation)
                .mirrored(true, false);
        case 7:  // transpose + both mirrors
            return img.transformed(QTransform().rotate(270), Qt::SmoothTransformation)
                .mirrored(true, false);
        default: return img;
    }
}

QImage embeddedLibRawThumbnail(LibRaw& raw, int maxEdge) {
    if (raw.unpack_thumb() != LIBRAW_SUCCESS)
        return {};

    const int flip = raw.imgdata.sizes.flip;
    const libraw_thumbnail_t& thumb = raw.imgdata.thumbnail;
    if (thumb.tformat == LIBRAW_THUMBNAIL_JPEG && thumb.thumb && thumb.tlength > 0) {
        QImage img;
        if (img.loadFromData(reinterpret_cast<const unsigned char*>(thumb.thumb),
                             static_cast<int>(thumb.tlength), "JPEG"))
            return scaleToMaxEdge(applyLibRawFlip(img, flip), maxEdge);
    }

    if (thumb.tformat == LIBRAW_THUMBNAIL_BITMAP && thumb.thumb && thumb.twidth > 0 &&
        thumb.theight > 0) {
        QImage img(thumb.twidth, thumb.theight, QImage::Format_RGB888);
        const unsigned char* src = reinterpret_cast<const unsigned char*>(thumb.thumb);
        for (int y = 0; y < thumb.theight; ++y) {
            auto* dst = img.scanLine(y);
            for (int x = 0; x < thumb.twidth; ++x) {
                const int si = (y * thumb.twidth + x) * 3;
                dst[x * 3 + 0] = src[si + 0];
                dst[x * 3 + 1] = src[si + 1];
                dst[x * 3 + 2] = src[si + 2];
            }
        }
        return scaleToMaxEdge(applyLibRawFlip(img, flip), maxEdge);
    }
    return {};
}
#endif

} // namespace

#if defined(MYLR_HAS_LIBRAW) && MYLR_HAS_LIBRAW
RawImage RawDecoder::decodeWithLibRaw(const QString& path, int maxEdge) const {
    RawImage result;
    result.filePath = path;

    LibRaw raw;
    configureLibRawOutput(raw);
    if (openLibRawFile(raw, path) != LIBRAW_SUCCESS)
        return result;

    fillMetadataFromLibRaw(result.metadata, raw);

    QImage linearFull;  // Format_RGBX64, scene-referred linear
    if (raw.unpack() == LIBRAW_SUCCESS && raw.dcraw_process() == LIBRAW_SUCCESS) {
        libraw_processed_image_t* img = raw.dcraw_make_mem_image();
        if (img) {
            linearFull = linear64FromLibRawImage(img);
            LibRaw::dcraw_clear_mem(img);
        }
    }

    // Fallback: embedded JPEG thumbnail (already sRGB-encoded) -> linearize it so
    // the rest of the engine has a uniform linear source.
    if (linearFull.isNull()) {
        const QImage thumb = embeddedLibRawThumbnail(raw, maxEdge);
        if (!thumb.isNull())
            linearFull = srgb8ToLinear64(thumb);
    }

    if (linearFull.isNull())
        return result;

    result.linearRgb = scaleLinear64(linearFull, maxEdge);
    result.metadata.width = result.linearRgb.width();
    result.metadata.height = result.linearRgb.height();
    result.preview = previewFromLinear64(result.linearRgb, result.metadata.wbCoeffs);
    result.valid = true;
    return result;
}
#else
RawImage RawDecoder::decodeWithLibRaw(const QString&, int) { return {}; }
#endif

RawImage RawDecoder::decodeFallback(const QString& path, int maxEdge) const {
    RawImage result;
    result.filePath = path;
    QImage img(path);
    if (img.isNull()) return result;

    result.metadata.width = img.width();
    result.metadata.height = img.height();
    result.metadata.cameraModel = QStringLiteral("Unknown (preview fallback)");
    result.metadata.iso = 400;
    result.metadata.evBaseline = 8.f;

    // Non-RAW files arrive already display-encoded (sRGB); linearize into RGBX64.
    const QImage scaled = scaleToMaxEdge(img, maxEdge);
    result.preview = scaled;
    result.linearRgb = srgb8ToLinear64(scaled);
    result.valid = true;
    return result;
}

RawImage RawDecoder::decode(const QString& path, int maxEdge) const {
#if defined(MYLR_HAS_LIBRAW) && MYLR_HAS_LIBRAW
    if (isRawFile(path)) {
        auto r = decodeWithLibRaw(path, maxEdge);
        if (r.valid) return r;
    }
#endif
    return decodeFallback(path, maxEdge);
}

QImage RawDecoder::decodeQuickPreview(const QString& path, int maxEdge) const {
#if defined(MYLR_HAS_LIBRAW) && MYLR_HAS_LIBRAW
    if (isRawFile(path)) {
        LibRaw raw;
        if (openLibRawFile(raw, path) == LIBRAW_SUCCESS) {
            QImage thumb = embeddedLibRawThumbnail(raw, maxEdge);
            if (!thumb.isNull()) return thumb;
        }
        const RawImage full = decodeWithLibRaw(path, maxEdge);
        if (full.valid) return full.preview;
    }
#endif
    return decodeFallback(path, maxEdge).preview;
}

} // namespace mylr
