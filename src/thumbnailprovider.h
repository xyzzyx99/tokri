#ifndef THUMBNAILPROVIDER_H
#define THUMBNAILPROVIDER_H

#include <QObject>
#include <QPixmap>
#include <QSize>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QFileIconProvider>
#include <QCache>

class ThumbnailProvider : public QObject
{
    Q_OBJECT
public:
    explicit ThumbnailProvider(QObject *parent = nullptr);
    QPixmap pixmapForFile(const QFileInfo &fi, const QSize &size) const;

private:
    QString makeKey(const QFileInfo &fi, const QSize &size) const;
    QPixmap fallbackPixmap(const QFileInfo &fi, const QSize &size) const;
    QPixmap textPreviewPixmap(const QFileInfo &fi,
                              const QSize &size) const;
    QPixmap imagePreviewPixmap(const QFileInfo &fi,
                               const QSize &size) const;

    mutable QCache<QString, QPixmap> cache{256};
    QMimeDatabase db;
    QFileIconProvider iconProvider;
};

#endif // THUMBNAILPROVIDER_H
