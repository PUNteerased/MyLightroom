#pragma once

#include "DevelopSettings.hpp"
#include <QObject>
#include <QUuid>
#include <QDateTime>
#include <QJsonObject>

namespace mylr {

struct SceneFingerprint {
    QVector<float> luminanceCdf;
    QVector<float> redCdf;
    QVector<float> greenCdf;
    QVector<float> blueCdf;
    float labLMean = 0.f;
    float labAMean = 0.f;
    float labBMean = 0.f;
    float labLStd = 0.f;
    float labAStd = 0.f;
    float labBStd = 0.f;
    QVector<float> zoneDistribution;
    QVector<float> dominantHues;
    float highlightClipPct = 0.f;
    float shadowClipPct = 0.f;
    QJsonObject percentiles;
};

struct SceneContext {
    QString camera;
    int iso = 0;
    float evBaseline = 0.f;
    QString sceneType;
};

struct MatchProfile {
    int version = 1;
    QString referenceImageId;
    SceneFingerprint fingerprint;
    DevelopSettings developState;
    SceneContext sceneContext;
};

struct Snapshot {
    QString id;
    QString name;
    DevelopSettings settings;
    QDateTime createdAt;
};

class EditGraph : public QObject {
    Q_OBJECT
public:
    explicit EditGraph(QObject* parent = nullptr);

    const DevelopSettings& current() const { return m_current; }
    DevelopSettings& mutableCurrent() { return m_current; }

    void setSettings(const DevelopSettings& s, const QString& label = QString());
    // Update the working settings WITHOUT pushing a history entry or emitting
    // settingsChanged. Used for live slider dragging so the panel is not fully
    // refreshed on every tick; a single setSettings() commits history on release.
    void setSettingsLive(const DevelopSettings& s) { m_current = s; }
    void reset();

    bool canUndo() const { return m_undoIndex > 0; }
    bool canRedo() const { return m_undoIndex < static_cast<int>(m_history.size()) - 1; }
    void undo();
    void redo();
    // Jump directly to a history state (click-to-jump in the History panel).
    void jumpTo(int index);

    const QVector<QString>& historyLabels() const { return m_labels; }
    int historyIndex() const { return m_undoIndex; }

    void saveSnapshot(const QString& name);
    const QVector<Snapshot>& snapshots() const { return m_snapshots; }
    void restoreSnapshot(const QString& id);

    MatchProfile buildMatchProfile(const QString& imageId, const SceneFingerprint& fp,
                                   const SceneContext& ctx) const;

    void applyMatchProfile(const MatchProfile& profile);

signals:
    void settingsChanged(const DevelopSettings& settings);
    void historyChanged();

private:
    void pushHistory(const QString& label);

    DevelopSettings m_current;
    QVector<DevelopSettings> m_history;
    QVector<QString> m_labels;
    int m_undoIndex = -1;
    QVector<Snapshot> m_snapshots;
};

} // namespace mylr
