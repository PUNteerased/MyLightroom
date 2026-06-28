#pragma once

#include <QWidget>

class QToolButton;
class QFrame;
class QVBoxLayout;
class QPropertyAnimation;

namespace mylr {

// Lightroom-style accordion section: a clickable header that expands/collapses
// its content with a smooth height animation.
class CollapsiblePanel : public QWidget {
    Q_OBJECT
public:
    explicit CollapsiblePanel(const QString& title, QWidget* parent = nullptr);

    void setContentLayout(QLayout* layout);
    void setContentWidget(QWidget* widget);
    void setExpanded(bool expanded, bool animate = true);
    bool isExpanded() const { return m_expanded; }

private:
    void recalcContentHeight();

    QToolButton* m_toggle = nullptr;
    QFrame* m_content = nullptr;
    QVBoxLayout* m_contentLayout = nullptr;
    QPropertyAnimation* m_animation = nullptr;
    bool m_expanded = true;
    int m_contentHeight = 0;
};

} // namespace mylr
