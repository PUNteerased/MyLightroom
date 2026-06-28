#pragma once

#include <QListWidget>
#include <QStyledItemDelegate>

namespace mylr {

enum LibraryRoles {
    IndexRole = Qt::UserRole + 1,
    ThumbnailRole,
    RatingRole,
    FlagRole,       // -1 reject, 0 none, 1 pick
    ColorLabelRole, // "", "red", "yellow", "green", "blue", "purple"
    ReferenceRole   // true when this image is the AI reference photo
};

class LibraryItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit LibraryItemDelegate(QObject* parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    void setCellSize(int size) { m_cellSize = size; }

private:
    int m_cellSize = 160;
};

class LibraryGridView : public QListWidget {
    Q_OBJECT
public:
    explicit LibraryGridView(QWidget* parent = nullptr);

    void setImages(const QStringList& paths);
    void setThumbnail(int index, const QImage& image);
    void setRating(int index, int rating);
    void setFlag(int index, int flag);
    void setColorLabel(int index, const QString& label);
    void setCellSize(int size);
    int cellSize() const { return m_cellSize; }

    void setCurrentImageIndex(int index);
    // Mark which image is the AI reference photo (draws a REF badge). -1 clears.
    void setReferenceIndex(int index);
    QList<int> selectedImageIndices() const;

signals:
    void imageActivated(int index);
    void imageClicked(int index);
    void ratingChanged(int index, int rating);
    void flagChanged(int index, int flag);
    void colorLabelChanged(int index, const QString& label);
    void setAsReferenceRequested(int index);
    void createVirtualCopyRequested(int index);
    void showInExplorerRequested(int index);
    void exportRequested(int index);
    void addToCollectionRequested();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    LibraryItemDelegate* m_delegate = nullptr;
    int m_cellSize = 160;
    int m_referenceIndex = -1;
};

} // namespace mylr
