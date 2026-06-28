#include "DevelopPanel.hpp"

#include "../lut/LutImporter.hpp"
#include "CollapsiblePanel.hpp"
#include "ColorWheelWidget.hpp"
#include "ToneCurveEditor.hpp"

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>

namespace mylr {

namespace {
// Vertical stack of slider rows: [label | gradient slider | value field].
// Each row is an HBoxLayout so the value column cannot be clipped by a narrow grid.
QVBoxLayout* makeForm(QWidget* host, int labelWidth = 38) {
    host->setMinimumWidth(292);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(4);
    col->setProperty("mylrLabelWidth", labelWidth);
    return col;
}

// Slider that resets to its default value on double-click (Lightroom behaviour).
// No Q_OBJECT/new signals, so it needs no moc pass.
class ResettableSlider : public QSlider {
public:
    explicit ResettableSlider(Qt::Orientation o, QWidget* parent = nullptr)
        : QSlider(o, parent) {}
    std::function<void()> onReset;

protected:
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (onReset) { onReset(); e->accept(); return; }
        QSlider::mouseDoubleClickEvent(e);
    }
};

// Lightroom-style numeric field beside each slider: plain text until focused,
// supports signed display (+10 / -10) and direct typing.
class DevelopValueSpin : public QDoubleSpinBox {
public:
    explicit DevelopValueSpin(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setLocale(QLocale::c());
        setGroupSeparatorShown(false);
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        setStyleSheet(QStringLiteral(
            "QDoubleSpinBox{color:#d8d8d8;background:#2b2b2b;border:none;"
            "padding:0 2px;font-size:11px;selection-background-color:#4a6fa5;}"
            "QDoubleSpinBox:focus{background:#3a3a3a;border:1px solid #5a5a5a;border-radius:2px;}"
            "QDoubleSpinBox:disabled{color:#888888;}"));
    }

    void setSignedDisplay(bool on) { m_signed = on; }

    void setFixedDisplayWidth(float min, float max, int decimals) {
        QString sample;
        if (decimals > 0) {
            sample = (min < 0.f) ? QStringLiteral("+0.") : QStringLiteral("0.");
            sample += QString(decimals, QChar('0'));
        } else if (m_signed && min < 0.f) {
            const int digits = QString::number(static_cast<int>(qMax(qAbs(min), qAbs(max)))).size();
            sample = QStringLiteral("+") + QString(digits, QChar('9'));
        } else {
            sample = QString::number(static_cast<int>(qMax(qAbs(min), qAbs(max))));
        }
        const QFontMetrics fm(font());
        setFixedWidth(qMax(56, fm.horizontalAdvance(sample) + 14));
    }

protected:
    QString textFromValue(double v) const override {
        if (m_signed && decimals() == 0) {
            const int iv = static_cast<int>(std::lround(v));
            if (iv > 0) return QStringLiteral("+%1").arg(iv);
            return QString::number(iv);
        }
        if (decimals() > 0 && minimum() < 0.0 && v > 0.0)
            return QStringLiteral("+%1").arg(QDoubleSpinBox::textFromValue(v));
        return QDoubleSpinBox::textFromValue(v);
    }

    double valueFromText(const QString& text) const override {
        QString t = text.trimmed();
        if (t.startsWith('+')) t = t.mid(1);
        bool ok = false;
        const double v = locale().toDouble(t, &ok);
        return ok ? v : value();
    }

private:
    bool m_signed = false;
};

// Build a stylesheet that paints a horizontal colored groove (Lightroom-style
// Temp/Tint/HSL bars) while keeping the native draggable handle.
QString grooveStyle(const QGradientStops& stops) {
    if (stops.isEmpty()) return QString();
    QStringList parts;
    for (const auto& s : stops)
        parts << QStringLiteral("stop:%1 %2").arg(s.first).arg(s.second.name());
    return QStringLiteral(
               "QSlider::groove:horizontal{height:6px;border-radius:3px;border:1px solid "
               "#2b2b2b;background:qlineargradient(x1:0,y1:0,x2:1,y2:0,%1);}"
               "QSlider::handle:horizontal{width:10px;height:14px;margin:-5px 0;"
               "border-radius:3px;background:#e8e8e8;border:1px solid #4a4a4a;}")
        .arg(parts.join(QStringLiteral(",")));
}

QColor hsv(int hueDeg, float sat, float val) {
    const int h = ((hueDeg % 360) + 360) % 360;
    return QColor::fromHsvF(h / 360.f, qBound(0.f, sat, 1.f), qBound(0.f, val, 1.f));
}

// White-balance Temperature: cool blue (left) -> neutral -> warm amber (right).
QGradientStops tempStops() {
    return {{0.0, QColor(54, 110, 210)}, {0.5, QColor(150, 150, 150)}, {1.0, QColor(235, 200, 90)}};
}
// White-balance Tint: green (left) -> neutral -> magenta (right).
QGradientStops tintStops() {
    return {{0.0, QColor(90, 195, 110)}, {0.5, QColor(150, 150, 150)}, {1.0, QColor(205, 100, 205)}};
}
// Saturation / Vibrance: full-spectrum rainbow.
QGradientStops rainbowStops() {
    return {{0.00, hsv(0, 0.75f, 0.85f)},   {0.17, hsv(45, 0.75f, 0.85f)},
            {0.33, hsv(90, 0.65f, 0.80f)},  {0.50, hsv(160, 0.65f, 0.80f)},
            {0.66, hsv(225, 0.70f, 0.85f)}, {0.83, hsv(285, 0.65f, 0.85f)},
            {1.00, hsv(330, 0.75f, 0.85f)}};
}
// HSL Hue band: shows neighbouring hues around the band's centre hue.
QGradientStops hueBandStops(int centerHue) {
    return {{0.0, hsv(centerHue - 40, 0.75f, 0.85f)},
            {0.5, hsv(centerHue, 0.75f, 0.85f)},
            {1.0, hsv(centerHue + 40, 0.75f, 0.85f)}};
}
// HSL Saturation band: desaturated (left) -> fully saturated band colour (right).
QGradientStops satBandStops(int centerHue) {
    return {{0.0, hsv(centerHue, 0.05f, 0.7f)}, {1.0, hsv(centerHue, 0.85f, 0.85f)}};
}
// HSL Luminance band: dark (left) -> band colour -> light (right).
QGradientStops lumBandStops(int centerHue) {
    return {{0.0, hsv(centerHue, 0.5f, 0.25f)},
            {0.5, hsv(centerHue, 0.6f, 0.65f)},
            {1.0, hsv(centerHue, 0.25f, 0.95f)}};
}
} // namespace

DevelopPanel::DevelopPanel(EditGraph* graph, QWidget* parent)
    : QWidget(parent), m_graph(graph) {
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* container = new QWidget;
    container->setMinimumWidth(296);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    auto addSection = [&](const QString& title, QWidget* content, bool expanded) {
        auto* panel = new CollapsiblePanel(title);
        panel->setContentWidget(content);
        panel->setExpanded(expanded, false);
        layout->addWidget(panel);
    };

    addSection(QStringLiteral("Basic"), createBasicSection(), true);
    addSection(QStringLiteral("Tone Curve"), createToneSection(), false);
    addSection(QStringLiteral("HSL / Color"), createHslSection(), false);
    addSection(QStringLiteral("Color Grading"), createGradingSection(), false);
    addSection(QStringLiteral("Detail"), createDetailSection(), false);
    addSection(QStringLiteral("Effects"), createEffectsSection(), false);
    addSection(QStringLiteral("Calibration"), createCalibrationSection(), false);
    addSection(QStringLiteral("Transform"), createTransformSection(), false);
    addSection(QStringLiteral("LUT"), createLutSection(), false);
    layout->addStretch();

    scroll->setWidget(container);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);

    connect(m_graph, &EditGraph::settingsChanged, this, [this]() { refreshFromGraph(); });
    refreshFromGraph();
}

void DevelopPanel::commit(const DevelopSettings& s, const QString& label) {
    m_graph->setSettings(s, label);
}

void DevelopPanel::bindFloat(QVBoxLayout* col, const QString& label, float min, float max,
                             int decimals, float scale, Getter getter, Setter setter,
                             const QString& historyLabel, const QGradientStops& gradient,
                             float center) {
    (void)scale; // The track always uses a normalized 0..1000 range now.

    // The value that sits at the visual middle of the track. Defaults to the
    // arithmetic midpoint (so symmetric ranges behave linearly), but can be
    // overridden so e.g. Temperature's neutral (~6500K) is centred.
    const float mid = std::isnan(center) ? (min + max) * 0.5f : qBound(min, center, max);

    auto toValue = [min, max, mid](int pos) -> float {
        const float t = pos / 1000.f;
        if (t <= 0.5f) return min + (t / 0.5f) * (mid - min);
        return mid + ((t - 0.5f) / 0.5f) * (max - mid);
    };
    auto toSlider = [min, max, mid](float v) -> int {
        v = qBound(min, v, max);
        float t;
        if (v <= mid) t = (mid > min) ? 0.5f * (v - min) / (mid - min) : 0.f;
        else t = 0.5f + ((max > mid) ? 0.5f * (v - mid) / (max - mid) : 0.f);
        return qBound(0, static_cast<int>(t * 1000.f + 0.5f), 1000);
    };

    auto* slider = new ResettableSlider(Qt::Horizontal);
    slider->setRange(0, 1000);
    if (!gradient.isEmpty()) slider->setStyleSheet(grooveStyle(gradient));

    const bool signedDisplay = (decimals == 0 && min < 0.f && max > 0.f);
    auto* spin = new DevelopValueSpin;
    spin->setSignedDisplay(signedDisplay);
    spin->setRange(min, max);
    spin->setDecimals(decimals);
    spin->setSingleStep(decimals > 0 ? std::pow(10.0, -decimals) : 1.0);
    spin->setFixedDisplayWidth(min, max, decimals);
    spin->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    spin->setToolTip(QStringLiteral("Click to type a value"));

    // Live update (no history) while dragging; commit one history entry when the
    // value settles (release / typed / discrete change) for responsive editing.
    auto liveValue = [this, setter](float v) {
        auto s = m_graph->current();
        setter(s, v);
        m_graph->setSettingsLive(s);
        emit developChanged();
    };
    auto commitValue = [this, setter, historyLabel](float v) {
        auto s = m_graph->current();
        setter(s, v);
        commit(s, historyLabel);
        emit developChanged();
    };

    connect(slider, &QSlider::valueChanged, this,
            [this, slider, spin, toValue, liveValue, commitValue](int pos) {
                if (m_updating) return;
                const float v = toValue(pos);
                m_updating = true;
                spin->setValue(v);
                m_updating = false;
                if (slider->isSliderDown())
                    liveValue(v);     // dragging: no history churn
                else
                    commitValue(v);   // groove click / keyboard: discrete commit
            });
    connect(slider, &QSlider::sliderReleased, this, [this, slider, toValue, commitValue]() {
        commitValue(toValue(slider->value()));  // finalize drag with one history entry
    });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, slider, toSlider, commitValue](double dv) {
                if (m_updating) return;
                m_updating = true;
                slider->setValue(toSlider(static_cast<float>(dv)));
                m_updating = false;
                commitValue(static_cast<float>(dv));
            });

    m_refreshers.append([this, slider, spin, toSlider, getter]() {
        const float v = getter(m_graph->current());
        spin->setValue(v);
        slider->setValue(toSlider(v));
    });

    const float defVal = getter(DevelopSettings::defaults());
    slider->onReset = [this, slider, spin, toSlider, commitValue, defVal]() {
        m_updating = true;
        spin->setValue(defVal);
        slider->setValue(toSlider(defVal));
        m_updating = false;
        commitValue(defVal);
    };

    const int labelWidth = col->property("mylrLabelWidth").toInt();
    auto* lbl = new QLabel(label);
    lbl->setStyleSheet(QStringLiteral("color:#cfcfcf;"));
    lbl->setFixedWidth(labelWidth > 0 ? labelWidth : 40);

    auto* row = new QWidget(col->parentWidget());
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(4);
    h->addWidget(lbl);
    h->addWidget(slider, 1);
    h->addWidget(spin);
    col->addWidget(row);
}

void DevelopPanel::bindSlider(QVBoxLayout* col, const QString& label, float min, float max,
                              float scale, Getter getter, Setter setter,
                              const QString& historyLabel, const QGradientStops& gradient) {
    // Now identical to bindFloat but with 0 decimals, so dense sections (HSL,
    // Calibration) also get an editable number beside the colored bar.
    bindFloat(col, label, min, max, 0, scale, std::move(getter), std::move(setter), historyLabel,
              gradient);
}

QWidget* DevelopPanel::createBasicSection() {
    auto* w = new QWidget;
    auto* form = makeForm(w);
    bindFloat(form, QStringLiteral("Temp"), 2000, 50000, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.temp; },
              [](DevelopSettings& s, float v) { s.basic.temp = v; }, QStringLiteral("Temp"),
              tempStops(), 6500.f);
    bindFloat(form, QStringLiteral("Tint"), -150, 150, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.tint; },
              [](DevelopSettings& s, float v) { s.basic.tint = v; }, QStringLiteral("Tint"),
              tintStops());
    bindFloat(form, QStringLiteral("Exposure"), -5, 5, 2, 100.f,
              [](const DevelopSettings& s) { return s.basic.exposure; },
              [](DevelopSettings& s, float v) { s.basic.exposure = v; }, QStringLiteral("Exposure"));
    bindFloat(form, QStringLiteral("Contrast"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.contrast; },
              [](DevelopSettings& s, float v) { s.basic.contrast = v; }, QStringLiteral("Contrast"));
    bindFloat(form, QStringLiteral("Highlights"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.highlights; },
              [](DevelopSettings& s, float v) { s.basic.highlights = v; }, QStringLiteral("Highlights"));
    bindFloat(form, QStringLiteral("Shadows"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.shadows; },
              [](DevelopSettings& s, float v) { s.basic.shadows = v; }, QStringLiteral("Shadows"));
    bindFloat(form, QStringLiteral("Whites"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.whites; },
              [](DevelopSettings& s, float v) { s.basic.whites = v; }, QStringLiteral("Whites"));
    bindFloat(form, QStringLiteral("Blacks"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.blacks; },
              [](DevelopSettings& s, float v) { s.basic.blacks = v; }, QStringLiteral("Blacks"));
    bindFloat(form, QStringLiteral("Texture"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.texture; },
              [](DevelopSettings& s, float v) { s.basic.texture = v; }, QStringLiteral("Texture"));
    bindFloat(form, QStringLiteral("Clarity"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.clarity; },
              [](DevelopSettings& s, float v) { s.basic.clarity = v; }, QStringLiteral("Clarity"));
    bindFloat(form, QStringLiteral("Dehaze"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.dehaze; },
              [](DevelopSettings& s, float v) { s.basic.dehaze = v; }, QStringLiteral("Dehaze"));
    bindFloat(form, QStringLiteral("Vibrance"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.vibrance; },
              [](DevelopSettings& s, float v) { s.basic.vibrance = v; }, QStringLiteral("Vibrance"),
              rainbowStops());
    bindFloat(form, QStringLiteral("Saturation"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.basic.saturation; },
              [](DevelopSettings& s, float v) { s.basic.saturation = v; },
              QStringLiteral("Saturation"), rainbowStops());
    return w;
}

QWidget* DevelopPanel::createToneSection() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto* editor = new ToneCurveEditor;
    connect(editor, &ToneCurveEditor::curveChanged, this, [this](const ToneCurveSettings& c) {
        if (m_updating) return;
        auto s = m_graph->mutableCurrent();
        s.toneCurve = c;
        commit(s, QStringLiteral("Tone Curve"));
        emit developChanged();
    });
    m_refreshers.append([this, editor]() { editor->setCurve(m_graph->current().toneCurve); });
    v->addWidget(editor);

    // Parametric region sliders complement the point curve.
    auto* form = makeForm(new QWidget);
    auto* formHost = form->parentWidget();
    bindSlider(form, QStringLiteral("Highlights"), -100, 100, 1.f,
               [](const DevelopSettings& s) { return s.toneCurve.highlights; },
               [](DevelopSettings& s, float v) { s.toneCurve.highlights = v; },
               QStringLiteral("Curve Highlights"));
    bindSlider(form, QStringLiteral("Lights"), -100, 100, 1.f,
               [](const DevelopSettings& s) { return s.toneCurve.lights; },
               [](DevelopSettings& s, float v) { s.toneCurve.lights = v; },
               QStringLiteral("Curve Lights"));
    bindSlider(form, QStringLiteral("Darks"), -100, 100, 1.f,
               [](const DevelopSettings& s) { return s.toneCurve.darks; },
               [](DevelopSettings& s, float v) { s.toneCurve.darks = v; },
               QStringLiteral("Curve Darks"));
    bindSlider(form, QStringLiteral("Shadows"), -100, 100, 1.f,
               [](const DevelopSettings& s) { return s.toneCurve.shadows; },
               [](DevelopSettings& s, float v) { s.toneCurve.shadows = v; },
               QStringLiteral("Curve Shadows"));
    v->addWidget(formHost);
    return w;
}

QWidget* DevelopPanel::createHslSection() {
    static const char* names[] = {"Red", "Orange", "Yellow", "Green",
                                   "Aqua", "Blue", "Purple", "Magenta"};
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(6);

    // Centre hue (degrees) for each of the 8 HSL bands.
    static const int bandHue[] = {0, 30, 60, 120, 180, 240, 270, 300};

    enum ChannelKind { Hue, Saturation, Luminance };
    struct Channel {
        QString title;
        float HslBand::* member;
        QString hist;
        ChannelKind kind;
    };
    const Channel channels[] = {
        {QStringLiteral("Hue"), &HslBand::hue, QStringLiteral("HSL Hue"), Hue},
        {QStringLiteral("Saturation"), &HslBand::saturation, QStringLiteral("HSL Saturation"),
         Saturation},
        {QStringLiteral("Luminance"), &HslBand::luminance, QStringLiteral("HSL Luminance"),
         Luminance},
    };
    for (const auto& ch : channels) {
        auto* label = new QLabel(ch.title);
        label->setStyleSheet(QStringLiteral("color:#9a9a9a; font-weight:bold;"));
        v->addWidget(label);
        auto* sub = new QWidget;
        auto* form = makeForm(sub);
        for (int i = 0; i < 8; ++i) {
            const int band = i;
            float HslBand::* member = ch.member;
            QGradientStops stops;
            switch (ch.kind) {
                case Hue: stops = hueBandStops(bandHue[i]); break;
                case Saturation: stops = satBandStops(bandHue[i]); break;
                case Luminance: stops = lumBandStops(bandHue[i]); break;
            }
            bindSlider(form, QString::fromUtf8(names[i]), -100, 100, 1.f,
                       [band, member](const DevelopSettings& s) {
                           return s.hsl[static_cast<size_t>(band)].*member;
                       },
                       [band, member](DevelopSettings& s, float val) {
                           s.hsl[static_cast<size_t>(band)].*member = val;
                       },
                       ch.hist, stops);
        }
        v->addWidget(sub);
    }
    return w;
}

QWidget* DevelopPanel::createGradingSection() {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);

    auto* wheelRow = new QWidget;
    auto* grid = new QHBoxLayout(wheelRow);
    grid->setContentsMargins(0, 0, 0, 0);

    struct WheelDef {
        QString title;
        ColorWheel ColorGradingSettings::* member;
        QString hist;
    };
    const WheelDef defs[] = {
        {QStringLiteral("Shadows"), &ColorGradingSettings::shadows, QStringLiteral("Grade Shadows")},
        {QStringLiteral("Midtones"), &ColorGradingSettings::midtones, QStringLiteral("Grade Midtones")},
        {QStringLiteral("Highlights"), &ColorGradingSettings::highlights,
         QStringLiteral("Grade Highlights")},
    };
    for (const auto& d : defs) {
        auto* wheel = new ColorWheelWidget(d.title);
        ColorWheel ColorGradingSettings::* member = d.member;
        const QString hist = d.hist;
        connect(wheel, &ColorWheelWidget::wheelChanged, this,
                [this, member, hist](const ColorWheel& cw) {
                    if (m_updating) return;
                    auto s = m_graph->mutableCurrent();
                    s.colorGrading.*member = cw;
                    commit(s, hist);
                    emit developChanged();
                });
        m_refreshers.append([this, wheel, member]() {
            wheel->setWheel(m_graph->current().colorGrading.*member);
        });
        grid->addWidget(wheel);
    }
    v->addWidget(wheelRow);

    auto* form = makeForm(new QWidget);
    auto* host = form->parentWidget();
    bindFloat(form, QStringLiteral("Balance"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.colorGrading.balance; },
              [](DevelopSettings& s, float val) { s.colorGrading.balance = val; },
              QStringLiteral("Grade Balance"));
    bindFloat(form, QStringLiteral("Blending"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.colorGrading.blending; },
              [](DevelopSettings& s, float val) { s.colorGrading.blending = val; },
              QStringLiteral("Grade Blending"));
    v->addWidget(host);
    return w;
}

QWidget* DevelopPanel::createCalibrationSection() {
    auto* w = new QWidget;
    auto* form = makeForm(w);
    bindFloat(form, QStringLiteral("Shadow Tint"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.calibration.shadowTint; },
              [](DevelopSettings& s, float v) { s.calibration.shadowTint = v; },
              QStringLiteral("Shadow Tint"), tintStops());

    struct Primary {
        QString name;
        HslBand CalibrationSettings::* member;
        int hue;
    };
    const Primary primaries[] = {
        {QStringLiteral("Red"), &CalibrationSettings::redPrimary, 0},
        {QStringLiteral("Green"), &CalibrationSettings::greenPrimary, 120},
        {QStringLiteral("Blue"), &CalibrationSettings::bluePrimary, 240},
    };
    for (const auto& pr : primaries) {
        HslBand CalibrationSettings::* member = pr.member;
        bindSlider(form, pr.name + QStringLiteral(" Hue"), -100, 100, 1.f,
                   [member](const DevelopSettings& s) { return (s.calibration.*member).hue; },
                   [member](DevelopSettings& s, float v) { (s.calibration.*member).hue = v; },
                   QStringLiteral("Calibration Hue"), hueBandStops(pr.hue));
        bindSlider(form, pr.name + QStringLiteral(" Sat"), -100, 100, 1.f,
                   [member](const DevelopSettings& s) { return (s.calibration.*member).saturation; },
                   [member](DevelopSettings& s, float v) { (s.calibration.*member).saturation = v; },
                   QStringLiteral("Calibration Sat"), satBandStops(pr.hue));
    }
    return w;
}

QWidget* DevelopPanel::createEffectsSection() {
    auto* w = new QWidget;
    auto* form = makeForm(w);
    bindFloat(form, QStringLiteral("Vignette"), -100, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.effects.vignetteAmount; },
              [](DevelopSettings& s, float v) { s.effects.vignetteAmount = v; },
              QStringLiteral("Vignette"));
    bindFloat(form, QStringLiteral("Midpoint"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.effects.vignetteMidpoint; },
              [](DevelopSettings& s, float v) { s.effects.vignetteMidpoint = v; },
              QStringLiteral("Vignette Midpoint"));
    bindFloat(form, QStringLiteral("Grain"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.effects.grainAmount; },
              [](DevelopSettings& s, float v) { s.effects.grainAmount = v; },
              QStringLiteral("Grain"));
    bindFloat(form, QStringLiteral("Grain Size"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.effects.grainSize; },
              [](DevelopSettings& s, float v) { s.effects.grainSize = v; },
              QStringLiteral("Grain Size"));
    return w;
}

QWidget* DevelopPanel::createDetailSection() {
    auto* w = new QWidget;
    auto* form = makeForm(w);
    bindFloat(form, QStringLiteral("Sharpen"), 0, 150, 0, 1.f,
              [](const DevelopSettings& s) { return s.detail.sharpenAmount; },
              [](DevelopSettings& s, float v) { s.detail.sharpenAmount = v; },
              QStringLiteral("Sharpen"));
    bindFloat(form, QStringLiteral("Radius"), 0.5f, 3.f, 1, 10.f,
              [](const DevelopSettings& s) { return s.detail.sharpenRadius; },
              [](DevelopSettings& s, float v) { s.detail.sharpenRadius = v; },
              QStringLiteral("Sharpen Radius"));
    bindFloat(form, QStringLiteral("Detail"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.detail.sharpenDetail; },
              [](DevelopSettings& s, float v) { s.detail.sharpenDetail = v; },
              QStringLiteral("Sharpen Detail"));
    bindFloat(form, QStringLiteral("Luminance NR"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.detail.noiseLuminance; },
              [](DevelopSettings& s, float v) { s.detail.noiseLuminance = v; },
              QStringLiteral("Luminance NR"));
    bindFloat(form, QStringLiteral("Color NR"), 0, 100, 0, 1.f,
              [](const DevelopSettings& s) { return s.detail.noiseColor; },
              [](DevelopSettings& s, float v) { s.detail.noiseColor = v; },
              QStringLiteral("Color NR"));
    return w;
}

QWidget* DevelopPanel::createTransformSection() {
    auto* w = new QWidget;
    auto* form = makeForm(w);
    bindFloat(form, QStringLiteral("Rotate"), -45, 45, 1, 10.f,
              [](const DevelopSettings& s) { return s.geometry.rotation; },
              [](DevelopSettings& s, float v) { s.geometry.rotation = v; },
              QStringLiteral("Rotate"));
    bindFloat(form, QStringLiteral("Straighten"), -10, 10, 1, 10.f,
              [](const DevelopSettings& s) { return s.geometry.straighten; },
              [](DevelopSettings& s, float v) { s.geometry.straighten = v; },
              QStringLiteral("Straighten"));
    return w;
}

QWidget* DevelopPanel::createLutSection() {
    auto* w = new QWidget;
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* pathLabel = new QLabel(QStringLiteral("No LUT loaded"));
    pathLabel->setWordWrap(true);
    pathLabel->setStyleSheet(QStringLiteral("color: #aaa; font-size: 11px;"));
    layout->addWidget(pathLabel);

    auto* intensityRow = new QWidget;
    auto* intensityLayout = new QHBoxLayout(intensityRow);
    intensityLayout->setContentsMargins(0, 0, 0, 0);
    intensityLayout->setSpacing(4);
    intensityLayout->addWidget(new QLabel(QStringLiteral("Intensity")));
    auto* intensitySlider = new QSlider(Qt::Horizontal);
    intensitySlider->setRange(0, 100);
    intensitySlider->setValue(100);
    auto* intensitySpin = new QSpinBox;
    intensitySpin->setRange(0, 100);
    intensitySpin->setValue(100);
    intensitySpin->setSuffix(QStringLiteral("%"));
    intensitySpin->setLocale(QLocale::c());
    intensitySpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    intensitySpin->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    intensitySpin->setFixedWidth(44);
    intensitySpin->setStyleSheet(QStringLiteral(
        "QSpinBox{color:#d8d8d8;background:transparent;border:none;padding:0 2px;font-size:11px;}"
        "QSpinBox:focus{background:#3a3a3a;border:1px solid #5a5a5a;border-radius:2px;}"));
    intensityLayout->addWidget(intensitySlider, 1);
    intensityLayout->addWidget(intensitySpin, 0, Qt::AlignRight);
    layout->addWidget(intensityRow);

    auto syncIntensity = [this, pathLabel, intensitySlider, intensitySpin](int v, bool fromSlider) {
        if (m_updating) return;
        m_updating = true;
        if (fromSlider) intensitySpin->setValue(v);
        else intensitySlider->setValue(v);
        m_updating = false;
        if (!m_graph->current().lut.enabled) return;
        auto s = m_graph->mutableCurrent();
        s.lut.intensity = v / 100.f;
        commit(s, QStringLiteral("LUT Intensity"));
        pathLabel->setText(QFileInfo(s.lut.path).fileName() + QStringLiteral(" (%1%)").arg(v));
        emit developChanged();
    };
    connect(intensitySlider, &QSlider::valueChanged, this,
            [syncIntensity](int v) { syncIntensity(v, true); });
    connect(intensitySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [syncIntensity](int v) { syncIntensity(v, false); });

    auto* loadBtn = new QPushButton(QStringLiteral("Import LUT (.cube / .3dl)..."));
    connect(loadBtn, &QPushButton::clicked, this, [this, pathLabel, intensitySlider, intensitySpin] {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import LUT"), {},
            QStringLiteral("LUT Files (*.cube *.3dl);;Cube (*.cube);;3DL (*.3dl)"));
        if (path.isEmpty()) return;
        LutImporter importer;
        const LutImportResult imported = importer.importFile(path, true);
        if (!imported.success) {
            QMessageBox::warning(this, QStringLiteral("LUT"), imported.error);
            return;
        }
        auto s = m_graph->mutableCurrent();
        s.lut.path = imported.storedPath;
        s.lut.enabled = true;
        s.lut.intensity = intensitySlider->value() / 100.f;
        commit(s, QStringLiteral("Import LUT"));
        pathLabel->setText(QFileInfo(imported.storedPath).fileName());
        intensitySpin->setValue(intensitySlider->value());
        emit developChanged();
    });
    layout->addWidget(loadBtn);

    auto* clearBtn = new QPushButton(QStringLiteral("Disable LUT"));
    connect(clearBtn, &QPushButton::clicked, this, [this, pathLabel, intensitySlider, intensitySpin] {
        auto s = m_graph->mutableCurrent();
        s.lut.enabled = false;
        commit(s, QStringLiteral("Disable LUT"));
        pathLabel->setText(QStringLiteral("No LUT loaded"));
        intensitySlider->setValue(100);
        intensitySpin->setValue(100);
        emit developChanged();
    });
    layout->addWidget(clearBtn);

    m_refreshers.append([this, pathLabel, intensitySlider, intensitySpin]() {
        const auto& lut = m_graph->current().lut;
        if (lut.enabled && !lut.path.isEmpty()) {
            pathLabel->setText(QFileInfo(lut.path).fileName() +
                               QStringLiteral(" (%1%)").arg(static_cast<int>(lut.intensity * 100)));
            const int pct = static_cast<int>(lut.intensity * 100);
            intensitySlider->setValue(pct);
            intensitySpin->setValue(pct);
        } else {
            pathLabel->setText(QStringLiteral("No LUT loaded"));
        }
    });
    return w;
}

void DevelopPanel::refreshFromGraph() {
    m_updating = true;
    for (auto& r : m_refreshers)
        r();
    m_updating = false;
}

} // namespace mylr
