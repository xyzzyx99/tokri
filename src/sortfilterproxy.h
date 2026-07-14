#ifndef SORTFILTERPROXY_H
#define SORTFILTERPROXY_H

#include <QDateTime>
#include <QHash>
#include <QSettings>
#include <QSortFilterProxyModel>

class QFileInfo;

class FSSortFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum DetailColumn {
        NameColumn = 0,
        AddedColumn,
        CreatedColumn,
        ModifiedColumn,
        TypeColumn,
        SizeColumn,
        DetailColumnCount
    };

    explicit FSSortFilterProxy(QObject *parent = nullptr);

    QDateTime addedTime(const QFileInfo &fileInfo) const;
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    void recordAddedPath(const QString &path);
    void renameAddedPath(const QString &oldPath, const QString &newPath);
    void setSearch(const QString &string);
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

private:
    QString addedTimeKey(const QString &path) const;
    QDateTime creationTime(const QFileInfo &fileInfo) const;
    QString typeName(const QFileInfo &fileInfo) const;
    double score(const QString &candidate) const;

    mutable QHash<QString, QDateTime> mAddedTimes;
    mutable QSettings mSettings;
    QString mSearch;
    int mSortColumn = AddedColumn;
};

#endif // SORTFILTERPROXY_H
