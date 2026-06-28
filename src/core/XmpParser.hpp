#pragma once

#include "DevelopSettings.hpp"
#include <QHash>
#include <QString>
#include <QStringList>

namespace mylr {

struct XmpImportReport {
    bool success = false;
    QString presetName;
    QStringList mappedFields;
    QStringList skippedFields;
    QStringList warnings;
    QString externalLutHint;
    float lookAmount = 1.f;
};

class XmpParser {
public:
    static XmpImportReport parseFile(const QString& xmpPath, DevelopSettings& out);
    static XmpImportReport parseContent(const QString& content, DevelopSettings& out,
                                        const QString& suggestedName = {});

private:
    static QHash<QString, QString> extractAttributes(const QString& content);
    static float parseNumber(const QString& raw);
    static QVector<QPointF> parseCurvePoints(const QString& raw);
    static void applyAttribute(const QString& key, const QString& value, DevelopSettings& s,
                               XmpImportReport& report);
    static QString localName(const QString& key);
};

} // namespace mylr
