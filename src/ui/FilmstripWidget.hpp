#pragma once

#include <QHash>
#include <QImage>
#include <QStringList>
#include <QWidget>

namespace mylr {

class FilmstripWidget : public QWidget {
    Q_OBJECT
public:
    explicit FilmstripWidget(QWidget* parent = nullptr);

    void setImages(const QStringList& paths);
    void setThumbnail(int index, const QImage& image);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }

    // Mark which image is the AI reference photo (draws a badge). -1 clears it.
    void setReferenceIndex(int index);

public slots:
    // Scroll by a number of thumbnails (negative = left). Used by nav buttons.
    void scrollByThumbs(int count);

signals:
    void imageSelected(int index);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    int cellStride() const { return ThumbSize + Spacing; }
    int contentWidth() const;
    int maxScroll() const;
    void clampScroll();
    void ensureVisible(int index);

    QStringList m_paths;
    QHash<int, QImage> m_thumbs;
    int m_current = -1;
    int m_referenceIndex = -1;
    int m_scrollOffset = 0;
    static constexpr int Margin = 8;
    static constexpr int ThumbSize = 96;
    static constexpr int Spacing = 4;
};

} // namespace mylr
