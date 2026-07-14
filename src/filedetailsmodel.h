#ifndef FILEDETAILSMODEL_H
#define FILEDETAILSMODEL_H

#include <QAbstractProxyModel>
#include <QPersistentModelIndex>

class FSSortFilterProxy;

class FileDetailsModel : public QAbstractProxyModel
{
    Q_OBJECT
public:
    explicit FileDetailsModel(QObject *parent = nullptr);

    bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                         int row, int column,
                         const QModelIndex &parent) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index,
                  int role = Qt::DisplayRole) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                      int row, int column,
                      const QModelIndex &parent) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    void setSourceModel(QAbstractItemModel *sourceModel) override;
    void setSourceRoot(const QModelIndex &sourceRoot);
    void sort(int column,
              Qt::SortOrder order = Qt::AscendingOrder) override;
    Qt::DropActions supportedDragActions() const override;
    Qt::DropActions supportedDropActions() const override;

private:
    FSSortFilterProxy *fileProxy() const;

    QPersistentModelIndex mSourceRoot;
};

#endif // FILEDETAILSMODEL_H
