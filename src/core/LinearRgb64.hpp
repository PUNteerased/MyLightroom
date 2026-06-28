#pragma once

#include <QImage>
#include <QtMath>

namespace mylr {

// Box-downsample scene-linear RGBX64 without Qt's generic scaler (which corrupts
// Format_RGBX64 and causes horizontal banding / color streaks).
inline QImage scaleLinearRgb64(const QImage& src, int maxEdge) {
    if (src.isNull() || maxEdge <= 0 || src.format() != QImage::Format_RGBX64)
        return src;

    const int sw = src.width();
    const int sh = src.height();
    if (qMax(sw, sh) <= maxEdge)
        return src;

    const float scale = static_cast<float>(maxEdge) / static_cast<float>(qMax(sw, sh));
    const int dw = qMax(1, static_cast<int>(sw * scale));
    const int dh = qMax(1, static_cast<int>(sh * scale));
    QImage dst(dw, dh, QImage::Format_RGBX64);

    for (int y = 0; y < dh; ++y) {
        const int y0 = y * sh / dh;
        const int y1 = qMin(sh, (y + 1) * sh / dh);
        auto* drow = reinterpret_cast<quint16*>(dst.scanLine(y));
        for (int x = 0; x < dw; ++x) {
            const int x0 = x * sw / dw;
            const int x1 = qMin(sw, (x + 1) * sw / dw);
            float sr = 0.f, sg = 0.f, sb = 0.f;
            int n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const auto* srow = reinterpret_cast<const quint16*>(src.constScanLine(sy));
                for (int sx = x0; sx < x1; ++sx) {
                    sr += srow[sx * 4 + 0];
                    sg += srow[sx * 4 + 1];
                    sb += srow[sx * 4 + 2];
                    ++n;
                }
            }
            if (n > 0) {
                drow[x * 4 + 0] = static_cast<quint16>(sr / n + 0.5f);
                drow[x * 4 + 1] = static_cast<quint16>(sg / n + 0.5f);
                drow[x * 4 + 2] = static_cast<quint16>(sb / n + 0.5f);
            }
            drow[x * 4 + 3] = 0xffff;
        }
    }
    return dst;
}

} // namespace mylr
