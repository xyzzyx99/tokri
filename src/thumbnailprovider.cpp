#include "thumbnailprovider.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
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

    // A native Windows icon can have a device-pixel ratio greater than 1.
    // Scaling that pixmap directly preserves the ratio, so a 96-pixel result
    // with DPR 2 is painted as only 48 logical pixels. It also places that
    // smaller image at the top-left of the destination canvas. Convert to a
    // DPR-1 image first, then remove transparent shell-icon padding before
    // performing the final centered scale.
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    image.setDevicePixelRatio(1.0);

    QRect visibleBounds;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(
            image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) <= 4)
                continue;
            const QRect pixelRect(x, y, 1, 1);
            visibleBounds = visibleBounds.isNull()
                                ? pixelRect
                                : visibleBounds.united(pixelRect);
        }
    }

    if (!visibleBounds.isNull())
        image = image.copy(visibleBounds);

    const QImage scaled = image.scaled(
        size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap canvas(size);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    const QRect target(
        (canvas.width() - scaled.width()) / 2,
        (canvas.height() - scaled.height()) / 2,
        scaled.width(), scaled.height());
    painter.drawImage(target, scaled);
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
