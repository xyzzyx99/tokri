#include "filedetailsmodel.h"
#include "sortfilterproxy.h"

#include <QDateTime>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QLocale>
#include <QMimeDatabase>
#include <QSet>

namespace {
QDateTime creationTime(const QFileInfo &fileInfo)
{
    QDateTime value = fileInfo.birthTime();
    if (!value.isValid())
        value = fileInfo.metadataChangeTime();
    return value;
}

QString displayDateTime(const QDateTime &value)
{
    if (!value.isValid())
        return QString();
    return QLocale().toString(value.toLocalTime(), QLocale::ShortFormat);
}

QString displaySize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString displayType(const QFileInfo &fileInfo)
{
    if (fileInfo.isDir())
        return FileDetailsModel::tr("File folder");

    QMimeDatabase database;
    const QString comment = database.mimeTypeForFile(
        fileInfo, QMimeDatabase::MatchExtension).comment();
    if (!comment.isEmpty())
        return comment;

    const QString suffix = fileInfo.suffix();
    return suffix.isEmpty()
               ? FileDetailsModel::tr("File")
               : FileDetailsModel::tr("%1 file").arg(suffix.toUpper());
}
}

FileDetailsModel::FileDetailsModel(QObject *parent)
    : QAbstractProxyModel(parent)
{}

FSSortFilterProxy *FileDetailsModel::fileProxy() const
{
    return qobject_cast<FSSortFilterProxy *>(sourceModel());
}

void FileDetailsModel::setSourceModel(QAbstractItemModel *model)
{
    if (sourceModel())
        disconnect(sourceModel(), nullptr, this, nullptr);

    mSourceRoot = QModelIndex();
    QAbstractProxyModel::setSourceModel(model);
    if (!model)
        return;

    connect(model, &QAbstractItemModel::modelAboutToBeReset,
            this, [this] { beginResetModel(); });
    connect(model, &QAbstractItemModel::modelReset,
            this, [this] { endResetModel(); });
    connect(model, &QAbstractItemModel::layoutAboutToBeChanged,
            this, [this] { beginResetModel(); });
    connect(model, &QAbstractItemModel::layoutChanged,
            this, [this] { endResetModel(); });

    connect(model, &QAbstractItemModel::rowsAboutToBeInserted,
            this, [this](const QModelIndex &parent, int first, int last) {
        if (parent == mSourceRoot)
            beginInsertRows(QModelIndex(), first, last);
    });
    connect(model, &QAbstractItemModel::rowsInserted,
            this, [this](const QModelIndex &parent, int, int) {
        if (parent == mSourceRoot)
            endInsertRows();
    });
    connect(model, &QAbstractItemModel::rowsAboutToBeRemoved,
            this, [this](const QModelIndex &parent, int first, int last) {
        if (parent == mSourceRoot)
            beginRemoveRows(QModelIndex(), first, last);
    });
    connect(model, &QAbstractItemModel::rowsRemoved,
            this, [this](const QModelIndex &parent, int, int) {
        if (parent == mSourceRoot)
            endRemoveRows();
    });
    connect(model, &QAbstractItemModel::rowsAboutToBeMoved,
            this, [this] { beginResetModel(); });
    connect(model, &QAbstractItemModel::rowsMoved,
            this, [this] { endResetModel(); });
    connect(model, &QAbstractItemModel::dataChanged,
            this, [this](const QModelIndex &topLeft,
                         const QModelIndex &bottomRight,
                         const QList<int> &roles) {
        if (topLeft.parent() != mSourceRoot)
            return;
        emit dataChanged(index(topLeft.row(), 0),
                         index(bottomRight.row(), columnCount() - 1),
                         roles);
    });
}

void FileDetailsModel::setSourceRoot(const QModelIndex &sourceRoot)
{
    beginResetModel();
    mSourceRoot = sourceRoot;
    endResetModel();
}


int FileDetailsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !sourceModel() || !mSourceRoot.isValid())
        return 0;
    return sourceModel()->rowCount(mSourceRoot);
}

int FileDetailsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : FSSortFilterProxy::DetailColumnCount;
}

QModelIndex FileDetailsModel::index(int row, int column,
                                    const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount()
        || column < 0 || column >= columnCount())
        return QModelIndex();
    return createIndex(row, column);
}

QModelIndex FileDetailsModel::parent(const QModelIndex &) const
{
    return QModelIndex();
}

QModelIndex FileDetailsModel::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || !sourceModel() || !mSourceRoot.isValid())
        return QModelIndex();
    return sourceModel()->index(proxyIndex.row(), 0, mSourceRoot);
}

QModelIndex FileDetailsModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.parent() != mSourceRoot)
        return QModelIndex();
    return index(sourceIndex.row(), 0);
}

QVariant FileDetailsModel::data(const QModelIndex &index, int role) const
{
    const QModelIndex sourceIndex = mapToSource(index);
    if (!sourceIndex.isValid())
        return QVariant();

    const QFileInfo fileInfo = sourceIndex.data(
        QFileSystemModel::FileInfoRole).value<QFileInfo>();

    if (role == QFileSystemModel::FileInfoRole)
        return QVariant::fromValue(fileInfo);

    if (role == Qt::DecorationRole)
        return index.column() == FSSortFilterProxy::NameColumn
                   ? sourceIndex.data(role)
                   : QVariant();

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case FSSortFilterProxy::NameColumn:
            return fileInfo.fileName();
        case FSSortFilterProxy::AddedColumn:
            return fileProxy()
                       ? displayDateTime(fileProxy()->addedTime(fileInfo))
                       : QString();
        case FSSortFilterProxy::CreatedColumn:
            return displayDateTime(creationTime(fileInfo));
        case FSSortFilterProxy::ModifiedColumn:
            return displayDateTime(fileInfo.lastModified());
        case FSSortFilterProxy::TypeColumn:
            return displayType(fileInfo);
        case FSSortFilterProxy::SizeColumn:
            return fileInfo.isDir() ? QString() : displaySize(fileInfo.size());
        }
    }

    if (role == Qt::EditRole
        && index.column() == FSSortFilterProxy::NameColumn)
        return fileInfo.fileName();

    if (role == Qt::TextAlignmentRole
        && index.column() == FSSortFilterProxy::SizeColumn)
        return Qt::AlignRight | Qt::AlignVCenter;

    if (role == Qt::ToolTipRole)
        return fileInfo.absoluteFilePath();

    return sourceIndex.data(role);
}

QVariant FileDetailsModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractProxyModel::headerData(section, orientation, role);

    switch (section) {
    case FSSortFilterProxy::NameColumn: return tr("Name");
    case FSSortFilterProxy::AddedColumn: return tr("Adding time");
    case FSSortFilterProxy::CreatedColumn: return tr("Creation time");
    case FSSortFilterProxy::ModifiedColumn: return tr("Modification time");
    case FSSortFilterProxy::TypeColumn: return tr("Type");
    case FSSortFilterProxy::SizeColumn: return tr("Size");
    default: return QVariant();
    }
}

Qt::ItemFlags FileDetailsModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return sourceModel() && mSourceRoot.isValid()
                   ? sourceModel()->flags(mSourceRoot)
                   : Qt::NoItemFlags;

    Qt::ItemFlags result = sourceModel()->flags(mapToSource(index));
    if (index.column() != FSSortFilterProxy::NameColumn)
        result &= ~Qt::ItemIsEditable;
    return result;
}

bool FileDetailsModel::setData(const QModelIndex &index,
                               const QVariant &value, int role)
{
    if (index.column() != FSSortFilterProxy::NameColumn)
        return false;
    return sourceModel()->setData(mapToSource(index), value, role);
}

QMimeData *FileDetailsModel::mimeData(const QModelIndexList &indexes) const
{
    QModelIndexList sourceIndexes;
    QSet<int> rows;
    for (const QModelIndex &index : indexes) {
        if (!index.isValid() || rows.contains(index.row()))
            continue;
        rows.insert(index.row());
        sourceIndexes << mapToSource(index);
    }
    return sourceModel()->mimeData(sourceIndexes);
}

QStringList FileDetailsModel::mimeTypes() const
{
    return sourceModel() ? sourceModel()->mimeTypes() : QStringList();
}

bool FileDetailsModel::canDropMimeData(const QMimeData *data,
                                       Qt::DropAction action,
                                       int row, int column,
                                       const QModelIndex &) const
{
    return sourceModel()
           && sourceModel()->canDropMimeData(
               data, action, row, column, mSourceRoot);
}

bool FileDetailsModel::dropMimeData(const QMimeData *data,
                                    Qt::DropAction action,
                                    int row, int column,
                                    const QModelIndex &)
{
    return sourceModel()
           && sourceModel()->dropMimeData(
               data, action, row, column, mSourceRoot);
}

Qt::DropActions FileDetailsModel::supportedDragActions() const
{
    return sourceModel() ? sourceModel()->supportedDragActions()
                         : Qt::IgnoreAction;
}

Qt::DropActions FileDetailsModel::supportedDropActions() const
{
    return sourceModel() ? sourceModel()->supportedDropActions()
                         : Qt::IgnoreAction;
}

void FileDetailsModel::sort(int column, Qt::SortOrder order)
{
    if (auto *proxy = fileProxy())
        proxy->sort(column, order);
}
