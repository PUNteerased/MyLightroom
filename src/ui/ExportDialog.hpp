#pragma once

#include "../export/ExportEngine.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace mylr {

// Modal export settings dialog (format/quality/resize/output/naming/profile +
// full watermark controls). No Q_OBJECT/custom signals, so no moc pass needed.
class ExportDialog : public QDialog {
public:
    explicit ExportDialog(const ExportSettings& initial, bool batch, QWidget* parent = nullptr)
        : QDialog(parent) {
        setWindowTitle(batch ? QStringLiteral("Export Batch") : QStringLiteral("Export"));
        setMinimumWidth(440);
        auto* root = new QVBoxLayout(this);

        auto* fileGroup = new QGroupBox(QStringLiteral("File Settings"));
        auto* form = new QFormLayout(fileGroup);

        m_format = new QComboBox;
        m_format->addItems({QStringLiteral("JPEG"), QStringLiteral("PNG"), QStringLiteral("TIFF")});
        m_format->setCurrentText(initial.format.toUpper() == QStringLiteral("JPG")
                                     ? QStringLiteral("JPEG")
                                     : initial.format.toUpper());
        form->addRow(QStringLiteral("Format"), m_format);

        m_quality = new QSpinBox;
        m_quality->setRange(1, 100);
        m_quality->setValue(initial.quality);
        form->addRow(QStringLiteral("Quality"), m_quality);

        auto* resizeRow = new QWidget;
        auto* rh = new QHBoxLayout(resizeRow);
        rh->setContentsMargins(0, 0, 0, 0);
        m_resize = new QCheckBox(QStringLiteral("Resize to long edge"));
        m_resize->setChecked(initial.maxLongEdge > 0);
        m_longEdge = new QSpinBox;
        m_longEdge->setRange(64, 20000);
        m_longEdge->setValue(initial.maxLongEdge > 0 ? initial.maxLongEdge : 2048);
        m_longEdge->setSuffix(QStringLiteral(" px"));
        rh->addWidget(m_resize);
        rh->addWidget(m_longEdge, 1);
        form->addRow(QStringLiteral("Resize"), resizeRow);

        auto* dirRow = new QWidget;
        auto* dh = new QHBoxLayout(dirRow);
        dh->setContentsMargins(0, 0, 0, 0);
        m_outputDir = new QLineEdit(initial.outputDir);
        auto* browse = new QPushButton(QStringLiteral("..."));
        browse->setMaximumWidth(32);
        connect(browse, &QPushButton::clicked, this, [this]() {
            const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Output Folder"),
                                                                m_outputDir->text());
            if (!d.isEmpty()) m_outputDir->setText(d);
        });
        dh->addWidget(m_outputDir, 1);
        dh->addWidget(browse);
        form->addRow(QStringLiteral("Output folder"), dirRow);

        m_naming = new QLineEdit(initial.fileNameTemplate);
        m_naming->setToolTip(QStringLiteral("Tokens: {name} original name, {seq} sequence number"));
        form->addRow(QStringLiteral("File naming"), m_naming);

        m_embed = new QCheckBox(QStringLiteral("Embed color profile"));
        m_embed->setChecked(initial.embedProfile);
        form->addRow(QString(), m_embed);
        root->addWidget(fileGroup);

        auto* wmGroup = new QGroupBox(QStringLiteral("Watermark"));
        auto* wmForm = new QFormLayout(wmGroup);
        m_wmEnable = new QCheckBox(QStringLiteral("Enable watermark"));
        m_wmEnable->setChecked(initial.watermark.enabled);
        wmForm->addRow(QString(), m_wmEnable);

        auto* wmPathRow = new QWidget;
        auto* wph = new QHBoxLayout(wmPathRow);
        wph->setContentsMargins(0, 0, 0, 0);
        m_wmPath = new QLineEdit(initial.watermark.imagePath);
        auto* wmBrowse = new QPushButton(QStringLiteral("..."));
        wmBrowse->setMaximumWidth(32);
        connect(wmBrowse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Watermark Image"),
                                                           {}, QStringLiteral("Images (*.png *.jpg)"));
            if (!f.isEmpty()) m_wmPath->setText(f);
        });
        wph->addWidget(m_wmPath, 1);
        wph->addWidget(wmBrowse);
        wmForm->addRow(QStringLiteral("Image"), wmPathRow);

        m_wmPos = new QComboBox;
        m_wmPos->addItems({QStringLiteral("top-left"), QStringLiteral("top-right"),
                           QStringLiteral("center"), QStringLiteral("bottom-left"),
                           QStringLiteral("bottom-right")});
        m_wmPos->setCurrentText(initial.watermark.position);
        wmForm->addRow(QStringLiteral("Position"), m_wmPos);

        m_wmScale = new QSpinBox;
        m_wmScale->setRange(1, 100);
        m_wmScale->setValue(static_cast<int>(initial.watermark.scale * 100));
        m_wmScale->setSuffix(QStringLiteral(" %"));
        wmForm->addRow(QStringLiteral("Scale"), m_wmScale);

        m_wmOpacity = new QSpinBox;
        m_wmOpacity->setRange(0, 100);
        m_wmOpacity->setValue(static_cast<int>(initial.watermark.opacity * 100));
        m_wmOpacity->setSuffix(QStringLiteral(" %"));
        wmForm->addRow(QStringLiteral("Opacity"), m_wmOpacity);

        m_wmMargin = new QSpinBox;
        m_wmMargin->setRange(0, 500);
        m_wmMargin->setValue(initial.watermark.marginPx);
        m_wmMargin->setSuffix(QStringLiteral(" px"));
        wmForm->addRow(QStringLiteral("Margin"), m_wmMargin);
        root->addWidget(wmGroup);

        if (!batch) m_naming->setEnabled(false);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        root->addWidget(buttons);
    }

    ExportSettings settings() const {
        ExportSettings s;
        s.format = m_format->currentText().toLower();
        s.quality = m_quality->value();
        s.maxLongEdge = m_resize->isChecked() ? m_longEdge->value() : 0;
        s.outputDir = m_outputDir->text();
        s.embedProfile = m_embed->isChecked();
        s.fileNameTemplate = m_naming->text().isEmpty() ? QStringLiteral("{name}") : m_naming->text();
        s.watermark.enabled = m_wmEnable->isChecked();
        s.watermark.imagePath = m_wmPath->text();
        s.watermark.position = m_wmPos->currentText();
        s.watermark.scale = m_wmScale->value() / 100.f;
        s.watermark.opacity = m_wmOpacity->value() / 100.f;
        s.watermark.marginPx = m_wmMargin->value();
        return s;
    }

private:
    QComboBox* m_format;
    QSpinBox* m_quality;
    QCheckBox* m_resize;
    QSpinBox* m_longEdge;
    QLineEdit* m_outputDir;
    QCheckBox* m_embed;
    QLineEdit* m_naming;
    QCheckBox* m_wmEnable;
    QLineEdit* m_wmPath;
    QComboBox* m_wmPos;
    QSpinBox* m_wmScale;
    QSpinBox* m_wmOpacity;
    QSpinBox* m_wmMargin;
};

} // namespace mylr
