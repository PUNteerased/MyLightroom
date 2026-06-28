#include "PresetManager.hpp"
#include "SidecarIO.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

namespace mylr {

PresetManager::PresetManager(QObject* parent) : QObject(parent), m_lutImporter(m_lutLibrary) {
    loadBuiltIn();
}

void PresetManager::loadBuiltIn() {
    m_presets.clear();

    auto add = [&](const QString& name, const QString& cat, auto fn) {
        Preset p;
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        p.name = name;
        p.category = cat;
        p.settings = DevelopSettings::defaults();
        fn(p.settings);
        m_presets.append(p);
    };

    add(QStringLiteral("Neutral"), QStringLiteral("Built-in"), [](DevelopSettings&) {});
    add(QStringLiteral("Portrait"), QStringLiteral("Built-in"), [](DevelopSettings& s) {
        s.basic.temp = 5800; s.basic.shadows = 20; s.basic.clarity = -10; s.basic.vibrance = 15;
    });
    add(QStringLiteral("Landscape"), QStringLiteral("Built-in"), [](DevelopSettings& s) {
        s.basic.dehaze = 15; s.basic.clarity = 15; s.basic.vibrance = 20; s.basic.saturation = 5;
    });
    add(QStringLiteral("Vivid"), QStringLiteral("Built-in"), [](DevelopSettings& s) {
        s.basic.contrast = 20; s.basic.saturation = 25; s.basic.vibrance = 30;
    });
    add(QStringLiteral("Matte"), QStringLiteral("Built-in"), [](DevelopSettings& s) {
        s.basic.contrast = -15; s.basic.blacks = 15; s.basic.saturation = -10;
    });
}

void PresetManager::registerPreset(Preset preset) {
    if (preset.id.isEmpty())
        preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_presets.append(preset);
    emit presetsChanged();
}

bool PresetManager::loadDirectory(const QString& path) {
    QDir dir(path);
    if (!dir.exists()) return false;
    for (const auto& fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        Preset p;
        p.id = o.value(QStringLiteral("id")).toString();
        p.name = o.value(QStringLiteral("name")).toString();
        p.category = o.value(QStringLiteral("category")).toString(QStringLiteral("User"));
        p.settings = SidecarIO::settingsFromJson(o.value(QStringLiteral("settings")).toObject());
        p.lutPath = o.value(QStringLiteral("lut")).toString();
        const QJsonObject lutObj = o.value(QStringLiteral("lut_settings")).toObject();
        if (!lutObj.isEmpty()) {
            p.settings.lut.path = lutObj.value(QStringLiteral("path")).toString(p.lutPath);
            p.settings.lut.intensity = static_cast<float>(lutObj.value(QStringLiteral("intensity")).toDouble(1));
            p.settings.lut.enabled = lutObj.value(QStringLiteral("enabled")).toBool(true);
        } else if (!p.lutPath.isEmpty()) {
            p.settings.lut.path = p.lutPath;
            p.settings.lut.enabled = true;
        }
        m_presets.append(p);
    }
    emit presetsChanged();
    return true;
}

bool PresetManager::applyPreset(const QString& id, DevelopSettings& target) const {
    for (const auto& p : m_presets) {
        if (p.id == id || p.name == id) {
            target = p.settings.clone();
            if (!p.lutPath.isEmpty() && target.lut.path.isEmpty()) {
                target.lut.path = p.lutPath;
                target.lut.enabled = true;
            }
            return true;
        }
    }
    return false;
}

bool PresetManager::savePreset(const Preset& preset, const QString& dir) {
    QJsonObject o;
    o[QStringLiteral("id")] = preset.id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : preset.id;
    o[QStringLiteral("name")] = preset.name;
    o[QStringLiteral("category")] = preset.category;
    o[QStringLiteral("settings")] = SidecarIO::settingsToJson(preset.settings);
    if (!preset.lutPath.isEmpty()) o[QStringLiteral("lut")] = preset.lutPath;
    QJsonObject lutObj;
    lutObj[QStringLiteral("path")] = preset.settings.lut.path;
    lutObj[QStringLiteral("intensity")] = preset.settings.lut.intensity;
    lutObj[QStringLiteral("enabled")] = preset.settings.lut.enabled;
    o[QStringLiteral("lut_settings")] = lutObj;

    QDir d(dir);
    if (!d.exists()) d.mkpath(QStringLiteral("."));
    const QString safeName =
        QString(preset.name).replace(QRegularExpression(R"([\\/:*?"<>|])"), QStringLiteral("_"));
    QFile f(QDir(dir).filePath(safeName + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));

    bool found = false;
    for (auto& p : m_presets) {
        if (p.id == preset.id || p.name == preset.name) {
            p = preset;
            found = true;
            break;
        }
    }
    if (!found)
        m_presets.append(preset);

    emit presetsChanged();
    return true;
}

QString PresetManager::findCompanionLut(const QString& xmpPath, const QString& lookHint,
                                      const QString& searchDir) const {
    const QFileInfo xmpFi(xmpPath);
    const QDir xmpDir = xmpFi.absoluteDir();

    const auto tryFind = [&](const QDir& dir, const QString& baseName) -> QString {
        for (const QString& ext : LutEngine::supportedExtensions()) {
            const QString candidate = dir.filePath(baseName + QLatin1Char('.') + ext);
            if (QFileInfo::exists(candidate)) return candidate;
        }
        return {};
    };

    QString found = tryFind(xmpDir, xmpFi.completeBaseName());
    if (!found.isEmpty()) return found;

    if (!lookHint.isEmpty()) {
        found = tryFind(xmpDir, lookHint);
        if (!found.isEmpty()) return found;
        QString sanitized = lookHint;
        sanitized.replace(QLatin1Char(' '), QLatin1Char('_'));
        found = tryFind(xmpDir, sanitized);
        if (!found.isEmpty()) return found;
    }

    if (!searchDir.isEmpty()) {
        QDir sd(searchDir);
        found = tryFind(sd, xmpFi.completeBaseName());
        if (!found.isEmpty()) return found;
        if (!lookHint.isEmpty())
            return tryFind(sd, lookHint);
    }

    for (const QString& ext : LutEngine::supportedExtensions()) {
        const auto files = xmpDir.entryList({QStringLiteral("*.") + ext}, QDir::Files);
        if (files.size() == 1)
            return xmpDir.filePath(files.first());
    }
    return {};
}

PresetImportResult PresetManager::importXmp(const QString& xmpPath, const QString& lutSearchDir) {
    PresetImportResult result;
    Preset p;
    p.settings = DevelopSettings::defaults();
    p.importReport = XmpParser::parseFile(xmpPath, p.settings);
    result.xmpReport = p.importReport;

    if (!p.importReport.success) {
        result.message = p.importReport.warnings.join(QLatin1Char('\n'));
        return result;
    }

    p.name = p.importReport.presetName.isEmpty() ? QFileInfo(xmpPath).baseName() : p.importReport.presetName;
    p.category = QStringLiteral("Imported XMP");
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    const QString companionLut =
        findCompanionLut(xmpPath, p.importReport.externalLutHint, lutSearchDir);
    if (!companionLut.isEmpty()) {
        result.lutResult = m_lutImporter.importFile(companionLut, true);
        if (result.lutResult.success) {
            p.lutPath = result.lutResult.storedPath;
            p.settings.lut.path = result.lutResult.storedPath;
            p.settings.lut.enabled = true;
            p.settings.lut.intensity = p.importReport.lookAmount > 0.f ? p.importReport.lookAmount : 1.f;
        }
    } else if (!p.importReport.externalLutHint.isEmpty()) {
        p.importReport.warnings.append(
            QStringLiteral("No companion .cube/.3dl found for '%1'")
                .arg(p.importReport.externalLutHint));
    }

    result.preset = p;
    result.success = true;
    result.message = QStringLiteral("Imported %1 fields from XMP").arg(p.importReport.mappedFields.size());
    return result;
}

PresetImportResult PresetManager::importLutPreset(const QString& lutPath, float intensity,
                                                  const QString& name) {
    PresetImportResult result;
    result.lutResult = m_lutImporter.importFile(lutPath, true);
    if (!result.lutResult.success) {
        result.message = result.lutResult.error;
        return result;
    }

    Preset p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    p.name = name.isEmpty() ? QFileInfo(lutPath).completeBaseName() : name;
    p.category = QStringLiteral("Imported LUT");
    p.lutPath = result.lutResult.storedPath;
    p.settings = DevelopSettings::defaults();
    p.settings.lut.path = result.lutResult.storedPath;
    p.settings.lut.enabled = true;
    p.settings.lut.intensity = qBound(0.f, intensity, 1.f);

    result.preset = p;
    result.success = true;
    result.message = QStringLiteral("Imported %1 LUT (%2³)")
                         .arg(result.lutResult.format.toUpper())
                         .arg(result.lutResult.size);
    return result;
}

PresetImportResult PresetManager::importBundle(const QString& folderPath) {
    PresetImportResult aggregate;
    aggregate.success = false;

    QDir dir(folderPath);
    if (!dir.exists()) {
        aggregate.message = QStringLiteral("Folder not found");
        return aggregate;
    }

    int count = 0;
    for (const auto& fi : dir.entryInfoList({QStringLiteral("*.xmp")}, QDir::Files)) {
        const PresetImportResult r = importXmp(fi.absoluteFilePath(), folderPath);
        if (r.success) {
            registerPreset(r.preset);
            savePreset(r.preset, QStringLiteral("presets"));
            ++count;
        }
    }

    for (const QString& ext : LutEngine::supportedExtensions()) {
        for (const auto& fi : dir.entryInfoList({QStringLiteral("*.") + ext}, QDir::Files)) {
            const PresetImportResult r = importLutPreset(fi.absoluteFilePath());
            if (r.success) {
                registerPreset(r.preset);
                savePreset(r.preset, QStringLiteral("presets"));
                ++count;
            }
        }
    }

    aggregate.success = count > 0;
    aggregate.message = QStringLiteral("Imported %1 preset(s)/LUT(s) from bundle").arg(count);
    return aggregate;
}

QVector<PresetImportResult> PresetManager::importXmpBatch(const QStringList& paths,
                                                          const QString& lutSearchDir) {
    QVector<PresetImportResult> results;
    for (const QString& path : paths)
        results.append(importXmp(path, lutSearchDir));
    return results;
}

} // namespace mylr
