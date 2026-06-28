#pragma once

#include <QString>
#include <QImage>
#include <QSize>
#include <memory>

namespace mylr {

struct RawMetadata {
    QString cameraModel;
    QString lens;
    int iso = 0;
    float aperture = 0.f;
    float shutterSec = 0.f;
    float focalLength = 0.f;
    float evBaseline = 0.f;
    float wbCoeffs[4] = {1.f, 1.f, 1.f, 1.f};
    int width = 0;
    int height = 0;
};

struct RawImage {
    QImage preview;
    QImage linearRgb;
    RawMetadata metadata;
    QString filePath;
    bool valid = false;
};

class RawDecoder {
public:
    RawDecoder();
    ~RawDecoder();

    RawImage decode(const QString& path, int maxEdge = 2048) const;
    QImage decodeQuickPreview(const QString& path, int maxEdge = 128) const;
    static bool isRawFile(const QString& path);
    static QStringList supportedExtensions();

private:
    RawImage decodeWithLibRaw(const QString& path, int maxEdge) const;
    RawImage decodeFallback(const QString& path, int maxEdge) const;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mylr
