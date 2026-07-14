#ifndef LISTITEMDELEGATE_H
#define LISTITEMDELEGATE_H

#include <QObject>
#include <QStyledItemDelegate>

class ListItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ListItemDelegate(QObject *parent = nullptr);

    void setIconExtent(int pixels);
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override;
    void paint(QPainter *p,
               const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override;

private:
    int mIconExtent = 64;
};

#endif // LISTITEMDELEGATE_H
