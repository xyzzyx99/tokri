#include "nointernaldraglistview.h"

#include <QDragEnterEvent>
#include <QItemSelectionModel>
#include <QMouseEvent>

NoInternalDragListView::NoInternalDragListView() {}

void NoInternalDragListView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->source() == this)
        e->ignore();
    else {
        emit dropping(true);
        QListView::dragEnterEvent(e);
    }
}

void NoInternalDragListView::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->source() == this)
        e->ignore();
    else
        QListView::dragMoveEvent(e);
}

void NoInternalDragListView::dragLeaveEvent(QDragLeaveEvent *e)
{
    emit dropping(false);

    QListView::dragLeaveEvent(e);
}

void NoInternalDragListView::dropEvent(QDropEvent *e)
{
    emit dropping(false);

    QListView::dropEvent(e);
}

void NoInternalDragListView::mousePressEvent(QMouseEvent *e)
{
    QItemSelectionModel *selection = selectionModel();
    const QModelIndex clicked = indexAt(e->position().toPoint());
    const bool preserveMultipleSelection =
        e->button() == Qt::RightButton
        && clicked.isValid()
        && selection
        && selection->isSelected(clicked)
        && selection->selectedIndexes().size() > 1;

    if (!preserveMultipleSelection) {
        QListView::mousePressEvent(e);
        return;
    }

    // QAbstractItemView normally collapses an extended selection to the item
    // receiving a right click. Keep the selection intact so the context menu
    // can offer actions for every selected file.
    const QItemSelection savedSelection = selection->selection();
    QListView::mousePressEvent(e);
    selection->select(savedSelection, QItemSelectionModel::ClearAndSelect);
    selection->setCurrentIndex(clicked, QItemSelectionModel::NoUpdate);
}

void NoInternalDragListView::paintEvent(QPaintEvent *e) {
    QListView::paintEvent(e);

    auto *m = model();
    if (!m || m->rowCount(rootIndex()) > 0)
        return;

    QPainter p(viewport());

    QPixmap pm(":/background.png");
    pm = pm.scaled(150, 150,
                   Qt::KeepAspectRatio,
                   Qt::SmoothTransformation);

    const QString text =
        "Drop files, folders, text, links, or images here.";

    QFontMetrics fm(font());
    int textHeight = fm.height();
    int spacing = 8;

    int totalHeight = pm.height() + spacing + textHeight;

    int startY = (height() - totalHeight) / 3;

    QPoint iconPos(width()/2 - pm.width()/2,
                   startY);
    p.drawPixmap(iconPos, pm);

    QRect textRect(0,
                   startY + pm.height() + spacing,
                   width(),
                   textHeight);

    p.setPen(palette().color(QPalette::Text));
    p.drawText(textRect, Qt::AlignHCenter, text);
}
