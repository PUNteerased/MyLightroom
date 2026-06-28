#include "EditGraph.hpp"
#include <QJsonObject>

namespace mylr {

EditGraph::EditGraph(QObject* parent) : QObject(parent) {
    reset();
}

void EditGraph::reset() {
    m_current = DevelopSettings::defaults();
    m_history.clear();
    m_labels.clear();
    m_undoIndex = -1;
    pushHistory(QStringLiteral("Import"));
}

void EditGraph::setSettings(const DevelopSettings& s, const QString& label) {
    m_current = s;
    pushHistory(label.isEmpty() ? QStringLiteral("Edit") : label);
    emit settingsChanged(m_current);
}

void EditGraph::pushHistory(const QString& label) {
    if (m_undoIndex >= 0 && m_undoIndex < static_cast<int>(m_history.size()) - 1) {
        m_history.resize(m_undoIndex + 1);
        m_labels.resize(m_undoIndex + 1);
    }
    m_history.append(m_current);
    m_labels.append(label);
    m_undoIndex = static_cast<int>(m_history.size()) - 1;
    emit historyChanged();
}

void EditGraph::undo() {
    if (!canUndo()) return;
    --m_undoIndex;
    m_current = m_history[m_undoIndex];
    emit settingsChanged(m_current);
    emit historyChanged();
}

void EditGraph::redo() {
    if (!canRedo()) return;
    ++m_undoIndex;
    m_current = m_history[m_undoIndex];
    emit settingsChanged(m_current);
    emit historyChanged();
}

void EditGraph::jumpTo(int index) {
    if (index < 0 || index >= static_cast<int>(m_history.size()) || index == m_undoIndex)
        return;
    m_undoIndex = index;
    m_current = m_history[m_undoIndex];
    emit settingsChanged(m_current);
    emit historyChanged();
}

void EditGraph::saveSnapshot(const QString& name) {
    Snapshot snap;
    snap.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    snap.name = name;
    snap.settings = m_current.clone();
    snap.createdAt = QDateTime::currentDateTime();
    m_snapshots.append(snap);
}

void EditGraph::restoreSnapshot(const QString& id) {
    for (const auto& s : m_snapshots) {
        if (s.id == id) {
            setSettings(s.settings, QStringLiteral("Snapshot: %1").arg(s.name));
            return;
        }
    }
}

MatchProfile EditGraph::buildMatchProfile(const QString& imageId, const SceneFingerprint& fp,
                                          const SceneContext& ctx) const {
    MatchProfile p;
    p.version = 1;
    p.referenceImageId = imageId;
    p.fingerprint = fp;
    p.developState = m_current.clone();
    p.sceneContext = ctx;
    return p;
}

void EditGraph::applyMatchProfile(const MatchProfile& profile) {
    setSettings(profile.developState, QStringLiteral("Apply Match Profile"));
}

} // namespace mylr