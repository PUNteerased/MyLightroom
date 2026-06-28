#include "LutImporter.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace mylr {

LutImporter::LutImporter(const QString& libraryDir) : m_libraryDir(libraryDir) {}

LutImportResult LutImporter::importAndValidate(const QString& sourcePath) {
    LutImportResult result;
    result.sourcePath = sourcePath;

    Lut3D lut;
    if (!m_engine.loadFromPath(sourcePath, lut)) {
        result.error = QStringLiteral("Unsupported or invalid LUT file");
        return result;
    }

    result.success = true;
    result.format = QFileInfo(sourcePath).suffix().toLower();
    result.size = lut.size;
    result.storedPath = sourcePath;
    return result;
}

LutImportResult LutImporter::importFile(const QString& sourcePath, bool copyToLibrary) {
    LutImportResult result = importAndValidate(sourcePath);
    if (!result.success)
        return result;

    if (!copyToLibrary)
        return result;

    QDir dir(m_libraryDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    const QFileInfo fi(sourcePath);
    const QString dest = dir.filePath(fi.fileName());
    if (QFileInfo::exists(dest) && QFileInfo(dest).absoluteFilePath() == fi.absoluteFilePath()) {
        result.storedPath = dest;
        return result;
    }

    if (QFile::exists(dest))
        QFile::remove(dest);

    if (!QFile::copy(sourcePath, dest)) {
        result.error = QStringLiteral("Failed to copy LUT to library");
        result.success = false;
        return result;
    }

    result.storedPath = dest;
    return result;
}

} // namespace mylr
