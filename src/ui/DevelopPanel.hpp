#pragma once

#include "../core/DevelopSettings.hpp"
#include "../core/EditGraph.hpp"
#include <QGradient>
#include <QVector>
#include <QWidget>
#include <functional>
#include <limits>

class QVBoxLayout;

namespace mylr {

class ToneCurveEditor;
class ColorWheelWidget;

class DevelopPanel : public QWidget {
    Q_OBJECT
public:
    explicit DevelopPanel(EditGraph* graph, QWidget* parent = nullptr);

    // Re-read every control's value from the edit graph (after load/undo/preset/AI).
    void refreshFromGraph();

signals:
    void developChanged();

private:
    using Getter = std::function<float(const DevelopSettings&)>;
    using Setter = std::function<void(DevelopSettings&, float)>;

    // Labeled gradient slider + editable spinbox bound to a settings member.
    // `gradient` (optional) paints a colored groove; `center` (optional) sets the
    // value that sits at the visual middle of the track (e.g. Temp ~6500K), with
    // a piecewise-linear mapping on each side.
    void bindFloat(QVBoxLayout* col, const QString& label, float min, float max, int decimals,
                   float scale, Getter getter, Setter setter, const QString& historyLabel,
                   const QGradientStops& gradient = QGradientStops(),
                   float center = std::numeric_limits<float>::quiet_NaN());
    // Compact variant (0-decimal) that still shows the editable number; used for
    // dense sections like HSL and Calibration.
    void bindSlider(QVBoxLayout* col, const QString& label, float min, float max, float scale,
                    Getter getter, Setter setter, const QString& historyLabel,
                    const QGradientStops& gradient = QGradientStops());

    QWidget* createBasicSection();
    QWidget* createToneSection();
    QWidget* createHslSection();
    QWidget* createGradingSection();
    QWidget* createCalibrationSection();
    QWidget* createEffectsSection();
    QWidget* createLutSection();
    QWidget* createTransformSection();
    QWidget* createDetailSection();

    void commit(const DevelopSettings& s, const QString& label);

    EditGraph* m_graph;
    bool m_updating = false;
    QVector<std::function<void()>> m_refreshers;
};

} // namespace mylr
