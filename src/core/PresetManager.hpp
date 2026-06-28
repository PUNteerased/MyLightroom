#pragma once

#include "../core/DevelopSettings.hpp"
#include "../core/XmpParser.hpp"
#include "../lut/LutImporter.hpp"
#include <QObject>
#include <QString>
#include <QVector>

namespace mylr {

struct Preset {
    QString id;
    QString name;
    QString category;
    DevelopSettings settings;
    QString lutPath;
    bool hasMatchProfile = false;
    XmpImportReport importReport;
};

struct PresetImportResult {
    bool success = false;
    Preset preset;
    XmpImportReport xmpReport;
    LutImportResult lutResult;
    QString message;
};

class PresetManager : public QObject {
    Q_OBJECT
public:
    explicit PresetManager(QObject* parent = nullptr);

    bool loadDirectory(const QString& path);
    const QVector<Preset>& presets() const { return m_presets; }
    bool applyPreset(const QString& id, DevelopSettings& target) const;
    bool savePreset(const Preset& preset, const QString& dir);

    PresetImportResult importXmp(const QString& xmpPath, const QString& lutSearchDir = {});
    PresetImportResult importLutPreset(const QString& lutPath, float intensity = 1.f,
                                       const QString& name = {});
    PresetImportResult importBundle(const QString& folderPath);
    QVector<PresetImportResult> importXmpBatch(const QStringList& paths,
                                               const QString& lutSearchDir = {});

    QString lutLibraryPath() const { return m_lutLibrary; }

signals:
    void presetsChanged();

private:
    QString findCompanionLut(const QString& xmpPath, const QString& lookHint,
                             const QString& searchDir) const;
    void registerPreset(Preset preset);

    QVector<Preset> m_presets;
    QString m_lutLibrary = QStringLiteral("presets/luts");
    LutImporter m_lutImporter;
    void loadBuiltIn();
};

} // namespace mylr
