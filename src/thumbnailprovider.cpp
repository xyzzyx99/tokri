#include "thumbnailprovider.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPalette>
#include <QTextStream>
#include <QtGui/qimagereader.h>
#include <QtGui/qpainterpath.h>

namespace {
constexpr int outerPad = 4;
constexpr int innerPad = 8;
constexpr int radius = 8;
constexpr int maxDim = 256;
}

ThumbnailProvider::ThumbnailProvider(QObject *parent)
    : QObject{parent}
{}

QString ThumbnailProvider::makeKey(const QFileInfo &fi,
                                   const QSize &size) const
{
    // A thumbnail rendered for Small view must not be reused in Medium or
    // Large view. The previous cache key omitted the requested size, so the
    // first mode used for a file determined its icon size for the rest of the
    // process.
    return fi.filePath() + "|"
           + QString::number(fi.lastModified().toMSecsSinceEpoch()) + "|"
           + QString::number(size.width()) + "x"
           + QString::number(size.height());
}

QPixmap ThumbnailProvider::pixmapForFile(const QFileInfo &fi,
                                         const QSize &size) const
{
    if (!size.isValid() || size.isEmpty())
        return {};

    const QString key = makeKey(fi, size);
    if (auto *cached = cache.object(key))
        return *cached;

    QPixmap pixmap;
    if (fi.exists() && fi.size() != 0) {
        const QMimeType mime =
            db.mimeTypeForFile(fi, QMimeDatabase::MatchContent);

        if (mime.name().startsWith("image/"))
            pixmap = imagePreviewPixmap(fi, size);
        else if (mime.name().startsWith("text/"))
            pixmap = textPreviewPixmap(fi, size);
    }

    if (pixmap.isNull())
        pixmap = fallbackPixmap(fi, size);

    if (!pixmap.isNull())
        cache.insert(key, new QPixmap(pixmap), 1);
    return pixmap;
}

QPixmap ThumbnailProvider::fallbackPixmap(const QFileInfo &fi,
                                           const QSize &size) const
{
    const QIcon icon = iconProvider.icon(fi);
    if (icon.isNull())
        return {};

    QSize sourceSize = icon.actualSize(size);
    if (!sourceSize.isValid() || sourceSize.isEmpty())
        sourceSize = size;

    const QPixmap source = icon.pixmap(sourceSize);
    if (source.isNull())
        return {};

    // QIcon deliberately avoids enlarging a small native icon. For the icon
    // modes that leaves a 16/32-pixel image inside a much larger item block.
    // Scale it explicitly onto an exact-size transparent canvas instead.
    const QPixmap scaled = source.scaled(
        size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap canvas(size);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    const QPoint topLeft((canvas.width() - scaled.width()) / 2,
                         (canvas.height() - scaled.height()) / 2);
    painter.drawPixmap(topLeft, scaled);
    return canvas;
}

QPixmap ThumbnailProvider::textPreviewPixmap(const QFileInfo &fi,
                                              const QSize &size) const
{
    QFile f(fi.filePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream ts(&f);
    const QString text = ts.read(60).simplified();
    if (text.isEmpty())
        return {};

    QPixmap pm(size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    const QPalette pal = QGuiApplication::palette();
    const QRect bgRect = pm.rect().adjusted(outerPad, outerPad,
                                             -outerPad, -outerPad);

    p.setPen(pal.color(QPalette::Mid));
    p.setBrush(pal.color(QPalette::Button));
    p.drawRoundedRect(bgRect, radius, radius);

    const QRect textRect = bgRect.adjusted(innerPad, innerPad,
                                            -innerPad, -innerPad);

    QFont font = p.font();
    font.setPointSizeF(font.pointSizeF() * 0.9);
    p.setFont(font);
    p.setPen(pal.color(QPalette::Text));
    p.drawText(textRect,
               Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
               text);

    return pm;
}

QPixmap ThumbnailProvider::imagePreviewPixmap(const QFileInfo &fi,
                                               const QSize &size) const
{
    QPixmap src(fi.filePath());
    if (src.isNull())
        return {};

    const QSize target = size.boundedTo(QSize(maxDim, maxDim));
    QPixmap pm(target);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRect r = pm.rect();
    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.setClipPath(clip);

    const QPixmap scaled = src.scaled(
        r.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft(r.center().x() - scaled.width() / 2,
                         r.center().y() - scaled.height() / 2);
    p.drawPixmap(topLeft, scaled);

    return pm;
}
