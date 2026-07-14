#include "sortfilterproxy.h"
// #include <rapidfuzz/fuzz.hpp>

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMimeDatabase>
#include <QCollator>

namespace {
QString normalizedPath(const QString &path)
{
    const QFileInfo fileInfo(path);
    QString normalized = fileInfo.canonicalFilePath();
    if (normalized.isEmpty())
        normalized = QDir::cleanPath(fileInfo.absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}
}

FSSortFilterProxy::FSSortFilterProxy(QObject *parent)
    : QSortFilterProxyModel{parent}
    , mSettings(QStringLiteral("Tokri"), QStringLiteral("Tokri"))
{}

QString FSSortFilterProxy::addedTimeKey(const QString &path) const
{
    const QByteArray digest = QCryptographicHash::hash(
        normalizedPath(path).toUtf8(), QCryptographicHash::Sha256);
    return QStringLiteral("AddedTimes/") + QString::fromLatin1(digest.toHex());
}

QDateTime FSSortFilterProxy::creationTime(const QFileInfo &fileInfo) const
{
    QDateTime result = fileInfo.birthTime();
    if (!result.isValid())
        result = fileInfo.metadataChangeTime();
    return result;
}

QDateTime FSSortFilterProxy::addedTime(const QFileInfo &fileInfo) const
{
    const QString normalized = normalizedPath(fileInfo.absoluteFilePath());
    const auto cached = mAddedTimes.constFind(normalized);
    if (cached != mAddedTimes.constEnd())
        return cached.value();

    const QString key = addedTimeKey(normalized);
    QDateTime value = QDateTime::fromMSecsSinceEpoch(
        mSettings.value(key, 0).toLongLong(), Qt::UTC);
    if (!value.isValid() || value.toMSecsSinceEpoch() == 0) {
        value = creationTime(fileInfo);
        if (!value.isValid())
            value = fileInfo.lastModified();
        if (!value.isValid())
            value = QDateTime::currentDateTimeUtc();
        mSettings.setValue(key, value.toUTC().toMSecsSinceEpoch());
    }

    mAddedTimes.insert(normalized, value);
    return value;
}

void FSSortFilterProxy::recordAddedPath(const QString &path)
{
    const QString normalized = normalizedPath(path);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    mAddedTimes.insert(normalized, now);
    mSettings.setValue(addedTimeKey(normalized), now.toMSecsSinceEpoch());
    invalidate();
}

void FSSortFilterProxy::renameAddedPath(const QString &oldPath,
                                        const QString &newPath)
{
    const QString oldNormalized = normalizedPath(oldPath);
    const QString oldKey = addedTimeKey(oldNormalized);
    QDateTime value = mAddedTimes.take(oldNormalized);
    if (!value.isValid()) {
        value = QDateTime::fromMSecsSinceEpoch(
            mSettings.value(oldKey, 0).toLongLong(), Qt::UTC);
    }
    mSettings.remove(oldKey);

    if (!value.isValid() || value.toMSecsSinceEpoch() == 0)
        value = QDateTime::currentDateTimeUtc();

    const QString newNormalized = normalizedPath(newPath);
    mAddedTimes.insert(newNormalized, value);
    mSettings.setValue(addedTimeKey(newNormalized),
                       value.toUTC().toMSecsSinceEpoch());
    invalidate();
}

QString FSSortFilterProxy::typeName(const QFileInfo &fileInfo) const
{
    if (fileInfo.isDir())
        return tr("File folder");

    QMimeDatabase database;
    const QString comment = database.mimeTypeForFile(
        fileInfo, QMimeDatabase::MatchExtension).comment();
    if (!comment.isEmpty())
        return comment;

    const QString suffix = fileInfo.suffix();
    return suffix.isEmpty() ? tr("File") : tr("%1 file").arg(suffix.toUpper());
}

bool FSSortFilterProxy::lessThan(const QModelIndex &left,
                                 const QModelIndex &right) const
{
    const QFileInfo leftInfo =
        left.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();
    const QFileInfo rightInfo =
        right.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();

    switch (mSortColumn) {
    case NameColumn: {
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        collator.setNumericMode(true);
        return collator.compare(leftInfo.fileName(), rightInfo.fileName()) < 0;
    }
    case AddedColumn:
        return addedTime(leftInfo) < addedTime(rightInfo);
    case CreatedColumn:
        return creationTime(leftInfo) < creationTime(rightInfo);
    case ModifiedColumn:
        return leftInfo.lastModified() < rightInfo.lastModified();
    case TypeColumn: {
        const int byType = QString::localeAwareCompare(
            typeName(leftInfo), typeName(rightInfo));
        if (byType != 0)
            return byType < 0;
        return QString::localeAwareCompare(
            leftInfo.fileName(), rightInfo.fileName()) < 0;
    }
    case SizeColumn:
        return leftInfo.size() < rightInfo.size();
    default:
        return false;
    }
}

void FSSortFilterProxy::sort(int column, Qt::SortOrder order)
{
    if (column >= NameColumn && column < DetailColumnCount)
        mSortColumn = column;
    QSortFilterProxyModel::sort(0, order);
}

bool FSSortFilterProxy::filterAcceptsRow(int row,
                                         const QModelIndex &parent) const
{
    if (mSearch.isEmpty())
        return true;

    auto *src = sourceModel();
    if (!src)
        return false;

    const QModelIndex idx = src->index(row, 0, parent);
    if (!idx.isValid())
        return false;

    const QFileInfo info =
        idx.data(QFileSystemModel::FileInfoRole).value<QFileInfo>();

    if (!info.exists())
        return false;

    if (info.isDir())
        return true;

    const QString name = info.fileName();
    Q_UNUSED(name);
    return true;
    // return score(name) > 30;
}

// void FSSortFilterProxy::setSearch(const QString &string)
// {
//     if (mSearch != string){
//         beginFilterChange();
//         mSearch = string;
//         endFilterChange();
//     }
// }

// double FSSortFilterProxy::score(const QString &candidate) const
// {
//     const auto result = rapidfuzz::fuzz::ratio(
//         mSearch.toStdString(),
//         candidate.toLower().toStdString());
//     return result;
// }
