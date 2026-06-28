#include "FilmstripWidget.hpp"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace mylr {

FilmstripWidget::FilmstripWidget(QWidget* parent) : QWidget(parent) {
    setFixedHeight(ThumbSize + 16);
    setStyleSheet(QStringLiteral("background-color: #1e1e1e;"));
}

int FilmstripWidget::contentWidth() const {
    if (m_paths.isEmpty()) return 0;
    return Margin * 2 + m_paths.size() * cellStride() - Spacing;
}

int FilmstripWidget::maxScroll() const {
    return qMax(0, contentWidth() - width());
}

void FilmstripWidget::clampScroll() {
    m_scrollOffset = qBound(0, m_scrollOffset, maxScroll());
}

void FilmstripWidget::ensureVisible(int index) {
    if (index < 0 || index >= m_paths.size()) return;
    const int cellLeft = Margin + index * cellStride();
    const int cellRight = cellLeft + ThumbSize;
    if (cellLeft - m_scrollOffset < Margin)
        m_scrollOffset = cellLeft - Margin;
    else if (cellRight - m_scrollOffset > width() - Margin)
        m_scrollOffset = cellRight - width() + Margin;
    clampScroll();
}

void FilmstripWidget::setImages(const QStringList& paths) {
    m_paths = paths;
    m_thumbs.clear();
    m_current = paths.isEmpty() ? -1 : 0;
    m_referenceIndex = -1;
    m_scrollOffset = 0;
    update();
}

void FilmstripWidget::setThumbnail(int index, const QImage& image) {
    if (index < 0 || index >= m_paths.size() || image.isNull()) return;
    m_thumbs[index] = image.scaled(ThumbSize, ThumbSize, Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
    update();
}

void FilmstripWidget::setCurrentIndex(int index) {
    if (index >= 0 && index < m_paths.size()) {
        m_current = index;
        ensureVisible(index);
        update();
    }
}

void FilmstripWidget::setReferenceIndex(int index) {
    m_referenceIndex = index;
    update();
}

void FilmstripWidget::scrollByThumbs(int count) {
    m_scrollOffset += count * cellStride();
    clampScroll();
    update();
}

void FilmstripWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(30, 30, 30));

    int x = Margin - m_scrollOffset;
    for (int i = 0; i < m_paths.size(); ++i) {
        const QRect cell(x, 8, ThumbSize, ThumbSize);
        // Skip cells entirely outside the viewport for performance.
        if (cell.right() >= 0 && cell.left() <= width()) {
            p.fillRect(cell, QColor(50, 50, 50));
            if (m_thumbs.contains(i)) {
                // Letterbox: draw the aspect-preserved thumbnail centered in the
                // square cell instead of stretching it to fill.
                const QImage& thumb = m_thumbs[i];
                const int ox = cell.x() + (cell.width() - thumb.width()) / 2;
                const int oy = cell.y() + (cell.height() - thumb.height()) / 2;
                p.drawImage(QPoint(ox, oy), thumb);
            }
            p.setPen(i == m_current ? QPen(QColor(100, 160, 255), 2) : QPen(QColor(60, 60, 60)));
            p.drawRect(cell);

            if (i == m_referenceIndex) {
                // Small "REF" badge in the top-left corner.
                const QRect badge(cell.x() + 2, cell.y() + 2, 30, 14);
                p.fillRect(badge, QColor(70, 120, 200, 220));
                p.setPen(Qt::white);
                p.drawText(badge, Qt::AlignCenter, QStringLiteral("REF"));
            }

            const QString name =
                m_paths[i].section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
            p.setPen(Qt::white);
            p.drawText(cell.adjusted(2, 2, -2, -2), Qt::AlignBottom | Qt::TextWordWrap,
                       name.left(12));
        }
        x += cellStride();
    }
}

void FilmstripWidget::mousePressEvent(QMouseEvent* e) {
    int x = Margin - m_scrollOffset;
    for (int i = 0; i < m_paths.size(); ++i) {
        const QRect cell(x, 8, ThumbSize, ThumbSize);
        if (cell.contains(e->pos())) {
            m_current = i;
            ensureVisible(i);
            update();
            emit imageSelected(i);
            return;
        }
        x += cellStride();
    }
}

void FilmstripWidget::wheelEvent(QWheelEvent* e) {
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    m_scrollOffset -= delta / 2;
    clampScroll();
    update();
}

} // namespace mylr
