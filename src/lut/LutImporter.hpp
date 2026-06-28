#pragma once

#include "LutEngine.hpp"
#include <QString>

namespace mylr {

struct LutImportResult {
    bool success = false;
    QString sourcePath;
    QString storedPath;
    QString format;
    int size = 0;
    QString error;
};

class LutImporter {
public:
    explicit LutImporter(const QString& libraryDir = QStringLiteral("presets/luts"));

    LutImportResult importFile(const QString& sourcePath, bool copyToLibrary = true);
    LutImportResult importAndValidate(const QString& sourcePath);
    QString libraryDirectory() const { return m_libraryDir; }

private:
    QString m_libraryDir;
    LutEngine m_engine;
};

} // namespace mylr
