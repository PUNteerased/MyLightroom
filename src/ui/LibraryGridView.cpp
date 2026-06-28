#include "LibraryGridView.hpp"

#include <QContextMenuEvent>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>

namespace mylr {

namespace {
QColor colorForLabel(const QString& label) {
    if (label == QStringLiteral("red")) return QColor(220, 70, 70);
    if (label == QStringLiteral("yellow")) return QColor(220, 200, 70);
    if (label == QStringLiteral("green")) return QColor(80, 200, 90);
    if (label == QStringLiteral("blue")) return QColor(80, 140, 230);
    if (label == QStringLiteral("purple")) return QColor(170, 100, 220);
    return QColor();
}
} // namespace

void LibraryItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const QRect cell = option.rect.adjusted(4, 4, -4, -4);
    const bool selected = option.state & QStyle::State_Selected;

    painter->fillRect(option.rect, selected ? QColor(64, 64, 64) : QColor(42, 42, 42));

    const QImage thumb = index.data(ThumbnailRole).value<QImage>();
    QRect imgRect = cell.adjusted(0, 0, 0, -22);
    if (!thumb.isNull()) {
        QImage scaled = thumb.scaled(imgRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const int ox = imgRect.x() + (imgRect.width() - scaled.width()) / 2;
        const int oy = imgRect.y() + (imgRect.height() - scaled.height()) / 2;
        painter->drawImage(QPoint(ox, oy), scaled);
    } else {
        painter->fillRect(imgRect, QColor(55, 55, 55));
    }

    if (selected) {
        painter->setPen(QPen(QColor(110, 170, 255), 2));
        painter->drawRect(option.rect.adjusted(1, 1, -1, -1));
    }

    // Index number (top-left).
    const int num = index.data(IndexRole).toInt() + 1;
    painter->setPen(QColor(180, 180, 180));
    QFont f = painter->font();
    f.setPointSize(8);
    painter->setFont(f);
    painter->drawText(cell.adjusted(2, 1, 0, 0), Qt::AlignTop | Qt::AlignLeft, QString::number(num));

    // AI reference badge (top-left, below the index number).
    if (index.data(ReferenceRole).toBool()) {
        const QRect badge(cell.left() + 2, cell.top() + 16, 32, 14);
        painter->setBrush(QColor(70, 120, 200, 230));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(badge, 2, 2);
        painter->setPen(Qt::white);
        QFont bf = painter->font();
        bf.setPointSize(7);
        bf.setBold(true);
        painter->setFont(bf);
        painter->drawText(badge, Qt::AlignCenter, QStringLiteral("REF"));
    }

    // Flag (top-right).
    const int flag = index.data(FlagRole).toInt();
    if (flag == 1) {
        painter->setPen(QColor(230, 230, 230));
        painter->drawText(cell.adjusted(0, 1, -2, 0), Qt::AlignTop | Qt::AlignRight,
                          QStringLiteral("\u2691"));
    } else if (flag == -1) {
        painter->setPen(QColor(220, 90, 90));
        painter->drawText(cell.adjusted(0, 1, -2, 0), Qt::AlignTop | Qt::AlignRight,
                          QStringLiteral("\u2715"));
    }

    // Footer: filename + rating + color label.
    QRect footer(cell.left(), imgRect.bottom() + 2, cell.width(), 18);
    const QString name = index.data(Qt::DisplayRole).toString();
    painter->setPen(QColor(190, 190, 190));
    painter->drawText(footer.adjusted(2, 0, -40, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      painter->fontMetrics().elidedText(name, Qt::ElideMiddle, footer.width() - 44));

    const int rating = index.data(RatingRole).toInt();
    QString stars;
    for (int i = 0; i < 5; ++i) stars += (i < rating) ? QStringLiteral("\u2605") : QStringLiteral("\u00b7");
    painter->setPen(rating > 0 ? QColor(230, 200, 90) : QColor(90, 90, 90));
    painter->drawText(footer.adjusted(0, 0, -2, 0), Qt::AlignVCenter | Qt::AlignRight, stars);

    const QColor label = colorForLabel(index.data(ColorLabelRole).toString());
    if (label.isValid()) {
        painter->setBrush(label);
        painter->setPen(Qt::NoPen);
        painter->drawRect(QRect(cell.left(), cell.bottom() - 3, cell.width(), 3));
    }

    painter->restore();
}

QSize LibraryItemDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return {m_cellSize, m_cellSize};
}

LibraryGridView::LibraryGridView(QWidget* parent) : QListWidget(parent) {
    setViewMode(QListView::IconMode);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setSpacing(2);
    setStyleSheet(QStringLiteral("QListWidget { background-color: #1f1f1f; border: none; }"));

    m_delegate = new LibraryItemDelegate(this);
    m_delegate->setCellSize(m_cellSize);
    setItemDelegate(m_delegate);
    setGridSize(QSize(m_cellSize, m_cellSize));

    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        emit imageActivated(item->data(IndexRole).toInt());
    });
    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        emit imageClicked(item->data(IndexRole).toInt());
    });
}

void LibraryGridView::setImages(const QStringList& paths) {
    clear();
    for (int i = 0; i < paths.size(); ++i) {
        auto* item = new QListWidgetItem(QFileInfo(paths[i]).fileName());
        item->setData(IndexRole, i);
        item->setData(RatingRole, 0);
        item->setData(FlagRole, 0);
        item->setData(ColorLabelRole, QString());
        item->setData(ReferenceRole, false);
        addItem(item);
    }
    m_referenceIndex = -1;
}

void LibraryGridView::setThumbnail(int index, const QImage& image) {
    if (index < 0 || index >= count() || image.isNull()) return;
    item(index)->setData(ThumbnailRole, image);
    viewport()->update();
}

void LibraryGridView::setRating(int index, int rating) {
    if (index < 0 || index >= count()) return;
    rating = qBound(0, rating, 5);
    item(index)->setData(RatingRole, rating);
    viewport()->update();
    emit ratingChanged(index, rating);
}

void LibraryGridView::setFlag(int index, int flag) {
    if (index < 0 || index >= count()) return;
    item(index)->setData(FlagRole, qBound(-1, flag, 1));
    viewport()->update();
    emit flagChanged(index, flag);
}

void LibraryGridView::setColorLabel(int index, const QString& label) {
    if (index < 0 || index >= count()) return;
    item(index)->setData(ColorLabelRole, label);
    viewport()->update();
    emit colorLabelChanged(index, label);
}

void LibraryGridView::contextMenuEvent(QContextMenuEvent* e) {
    QListWidgetItem* it = itemAt(e->pos());
    if (!it) { QListWidget::contextMenuEvent(e); return; }
    const int index = it->data(IndexRole).toInt();
    if (!it->isSelected()) setCurrentRow(index);

    QMenu menu(this);
    menu.addAction(QStringLiteral("Set as Reference Photo"), this,
                   [this, index]() { emit setAsReferenceRequested(index); });
    menu.addAction(QStringLiteral("Create Virtual Copy"), this,
                   [this, index]() { emit createVirtualCopyRequested(index); });
    menu.addSeparator();

    QMenu* ratingMenu = menu.addMenu(QStringLiteral("Set Rating"));
    for (int r = 0; r <= 5; ++r)
        ratingMenu->addAction(r == 0 ? QStringLiteral("None") : QString(r, QChar(0x2605)),
                              this, [this, index, r]() { setRating(index, r); });

    QMenu* labelMenu = menu.addMenu(QStringLiteral("Color Label"));
    const QStringList labels = {QStringLiteral("red"), QStringLiteral("yellow"),
                                QStringLiteral("green"), QStringLiteral("blue"),
                                QStringLiteral("purple")};
    labelMenu->addAction(QStringLiteral("None"), this,
                         [this, index]() { setColorLabel(index, QString()); });
    for (const QString& lbl : labels)
        labelMenu->addAction(lbl, this, [this, index, lbl]() { setColorLabel(index, lbl); });

    menu.addAction(QStringLiteral("Add to Collection..."), this,
                   [this]() { emit addToCollectionRequested(); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Show in Explorer"), this,
                   [this, index]() { emit showInExplorerRequested(index); });
    menu.addAction(QStringLiteral("Export..."), this,
                   [this, index]() { emit exportRequested(index); });
    menu.exec(e->globalPos());
}

void LibraryGridView::setCellSize(int size) {
    m_cellSize = qBound(80, size, 360);
    m_delegate->setCellSize(m_cellSize);
    setGridSize(QSize(m_cellSize, m_cellSize));
    viewport()->update();
}

void LibraryGridView::setCurrentImageIndex(int index) {
    if (index < 0 || index >= count()) return;
    setCurrentRow(index);
    scrollToItem(item(index));
}

void LibraryGridView::setReferenceIndex(int index) {
    if (m_referenceIndex >= 0 && m_referenceIndex < count())
        item(m_referenceIndex)->setData(ReferenceRole, false);
    m_referenceIndex = index;
    if (index >= 0 && index < count())
        item(index)->setData(ReferenceRole, true);
    viewport()->update();
}

QList<int> LibraryGridView::selectedImageIndices() const {
    QList<int> indices;
    for (QListWidgetItem* it : selectedItems())
        indices << it->data(IndexRole).toInt();
    return indices;
}

void LibraryGridView::keyPressEvent(QKeyEvent* e) {
    const int row = currentRow();
    if (row >= 0) {
        // Number keys set rating; P/X/U set flags (Lightroom shortcuts).
        if (e->key() >= Qt::Key_0 && e->key() <= Qt::Key_5) {
            setRating(row, e->key() - Qt::Key_0);
            return;
        }
        if (e->key() == Qt::Key_P) { setFlag(row, 1); return; }
        if (e->key() == Qt::Key_X) { setFlag(row, -1); return; }
        if (e->key() == Qt::Key_U) { setFlag(row, 0); return; }
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            emit imageActivated(row);
            return;
        }
    }
    QListWidget::keyPressEvent(e);
}

} // namespace mylr
