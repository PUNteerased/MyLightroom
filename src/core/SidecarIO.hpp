#pragma once

#include "DevelopSettings.hpp"
#include "EditGraph.hpp"
#include <QJsonObject>
#include <QString>

namespace mylr {

class SidecarIO {
public:
    static QString sidecarPathForRaw(const QString& rawPath);
    static bool save(const QString& rawPath, const DevelopSettings& settings,
                     const MatchProfile* matchProfile = nullptr);
    static bool load(const QString& rawPath, DevelopSettings& settings,
                     MatchProfile* matchProfile = nullptr);

    static QJsonObject settingsToJson(const DevelopSettings& s);
    static DevelopSettings settingsFromJson(const QJsonObject& o);
    static QJsonObject fingerprintToJson(const SceneFingerprint& fp);
    static SceneFingerprint fingerprintFromJson(const QJsonObject& o);
    static QJsonObject matchProfileToJson(const MatchProfile& p);
    static MatchProfile matchProfileFromJson(const QJsonObject& o);
};

} // namespace mylr
