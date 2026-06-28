#include "CollapsiblePanel.hpp"

#include <QFrame>
#include <QPropertyAnimation>
#include <QToolButton>
#include <QVBoxLayout>

namespace mylr {

CollapsiblePanel::CollapsiblePanel(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    m_toggle = new QToolButton;
    m_toggle->setText(title);
    m_toggle->setCheckable(true);
    m_toggle->setChecked(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setArrowType(Qt::DownArrow);
    m_toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_toggle->setCursor(Qt::PointingHandCursor);
    m_toggle->setStyleSheet(QStringLiteral(
        "QToolButton { background-color: #383838; color: #cfcfcf; border: none;"
        " padding: 6px 8px; text-align: left; font-weight: bold; }"
        "QToolButton:hover { background-color: #444444; }"));
    outer->addWidget(m_toggle);

    m_content = new QFrame;
    m_content->setFrameShape(QFrame::NoFrame);
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setContentsMargins(8, 6, 8, 8);
    m_contentLayout->setSpacing(4);
    outer->addWidget(m_content);

    m_animation = new QPropertyAnimation(m_content, "maximumHeight", this);
    m_animation->setDuration(160);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);

    connect(m_toggle, &QToolButton::toggled, this, [this](bool checked) {
        setExpanded(checked, true);
    });
}

void CollapsiblePanel::setContentLayout(QLayout* layout) {
    auto* host = new QWidget;
    host->setLayout(layout);
    m_contentLayout->addWidget(host);
    recalcContentHeight();
}

void CollapsiblePanel::setContentWidget(QWidget* widget) {
    m_contentLayout->addWidget(widget);
    recalcContentHeight();
}

void CollapsiblePanel::recalcContentHeight() {
    m_content->adjustSize();
    m_contentHeight = m_content->sizeHint().height();
    if (m_expanded)
        m_content->setMaximumHeight(QWIDGETSIZE_MAX);
}

void CollapsiblePanel::setExpanded(bool expanded, bool animate) {
    m_expanded = expanded;
    m_toggle->setChecked(expanded);
    m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);

    const int target = expanded ? qMax(m_contentHeight, m_content->sizeHint().height()) : 0;
    if (!animate) {
        m_content->setMaximumHeight(expanded ? QWIDGETSIZE_MAX : 0);
        return;
    }
    m_animation->stop();
    m_animation->setStartValue(m_content->height());
    m_animation->setEndValue(target);
    QObject::disconnect(m_animation, &QPropertyAnimation::finished, nullptr, nullptr);
    if (expanded) {
        connect(m_animation, &QPropertyAnimation::finished, this, [this]() {
            if (m_expanded) m_content->setMaximumHeight(QWIDGETSIZE_MAX);
        });
    }
    m_animation->start();
}

} // namespace mylr
