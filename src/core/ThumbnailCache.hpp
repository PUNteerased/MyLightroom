#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QString>

namespace mylr {

// On-disk thumbnail cache. Files are named "{hash}_{size}.jpg" where the hash is
// derived from the source path + size + mtime, so edits to the source file
// invalidate the cached thumbnail automatically. Methods are const and only
// touch the filesystem, so they are safe to call from worker threads (distinct
// files per image/size).
class ThumbnailCache {
public:
    void setDirectory(const QString& dir) {
        m_dir = dir;
        if (!m_dir.isEmpty()) QDir().mkpath(m_dir);
    }

    QImage load(const QString& imagePath, int size) const {
        if (m_dir.isEmpty()) return {};
        const QString file = pathFor(imagePath, size);
        if (file.isEmpty() || !QFileInfo::exists(file)) return {};
        QImage img;
        if (img.load(file, "JPEG")) return img;
        return {};
    }

    void store(const QString& imagePath, int size, const QImage& image) const {
        if (m_dir.isEmpty() || image.isNull()) return;
        const QString file = pathFor(imagePath, size);
        if (!file.isEmpty()) image.save(file, "JPEG", 85);
    }

private:
    QString pathFor(const QString& imagePath, int size) const {
        const QFileInfo fi(imagePath);
        // Version tag in the seed lets us invalidate the whole cache when the
        // thumbnail pipeline changes (e.g. v2 added EXIF orientation handling).
        const QString seed = QStringLiteral("v3|") + fi.absoluteFilePath() + QLatin1Char('|') +
                             QString::number(fi.size()) + QLatin1Char('|') +
                             QString::number(fi.lastModified().toMSecsSinceEpoch());
        const QString hash = QString::fromLatin1(
            QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5).toHex());
        return QDir(m_dir).filePath(QStringLiteral("%1_%2.jpg").arg(hash).arg(size));
    }

    QString m_dir;
};

} // namespace mylr
