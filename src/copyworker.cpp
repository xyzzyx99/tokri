#include "copyworker.h"

#include "filepathprovider.h"
#include "standardpaths.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMimeDatabase>
#include <QUuid>
#include <QImageWriter>
#include <QColorSpace>


namespace {
QString comparablePath(const QString &path)
{
    const QFileInfo fileInfo(path);
    QString result = fileInfo.canonicalFilePath();
    if (result.isEmpty())
        result = QDir::cleanPath(fileInfo.absoluteFilePath());

#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool isSamePath(const QString &source, const QString &destination)
{
    return comparablePath(source) == comparablePath(destination);
}
}

CopyWorker::CopyWorker(QObject *parent)
    : QObject{parent}
{}

void CopyWorker::copyDirectory(const QString &src) {
    const QString dstFinal = FilePathProvider::nameFromPath(src);
    if (isSamePath(src, dstFinal))
        return;

#if defined(Q_OS_MACOS)
    QString dst = dstFinal + ".progress";
#else
    QString dst = dstFinal;
#endif
    if (!QDir().mkpath(dst)) {
        emit makePathFailed(dst);
        return;
    }
    QDirIterator it(src, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QString rel = QDir(src).relativeFilePath(it.filePath());
        QString out = dst + "/" + rel;
        if (it.fileInfo().isDir()) {
            if (!QDir().mkpath(out)) {
                emit makePathFailed(out);
                return;
            }
        } else {
            QDir().mkpath(QFileInfo(out).path());
            if (!QFile::copy(it.filePath(), out)) {
                emit copyFailed(out);
                return;
            }
        }
    }
#if defined(Q_OS_MACOS)
    if (QFileInfo::exists(dstFinal))
        QDir(dstFinal).removeRecursively();
    QString tmpDirName = QFileInfo(dst).fileName();
    QDir tmpDir = QFileInfo(dstFinal).dir();
    tmpDir.rename(tmpDirName, QFileInfo(dstFinal).fileName());
#endif
    emit copySuccess(dstFinal);
}

void CopyWorker::copyFile(const QString &filePath)
{
    const QString destination = FilePathProvider::nameFromPath(filePath);
    if (isSamePath(filePath, destination))
        return;

#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
    QFile file(filePath);
    bool copied = file.copy(destination);
    if (copied == false){
        emit copyFailed(filePath);
        return;
    }
#endif

#ifdef Q_OS_MAC
    QFile in(filePath);
    QFile out(destination);

    if (!in.open(QIODevice::ReadOnly) ||
        !out.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        out.write(in.readAll()) == -1) {
        emit copyFailed(filePath);
        return;
    }
#endif
    emit copySuccess(destination);
}

void CopyWorker::saveImage(const QImage &image)
{
    QString fileName = FilePathProvider::nameWithPrefix("image") + ".png";

    QString rootPath = StandardPaths::getPath(StandardPaths::TokriDir);
    QString fullPath = QDir(rootPath).filePath(fileName);

    QImage out = image;

#ifdef Q_OS_MACOS
    out.setColorSpace(QColorSpace());
    out = out.convertToFormat(QImage::Format_ARGB32);
#endif

    QImageWriter w(fullPath, "png");
    if (!w.write(out)) {
        emit copyFailed(fullPath);
    } else {
        emit copySuccess(fullPath);
    }
}

static QByteArray mimeToFormat(const QString &mime){
    QMimeDatabase db;
    QMimeType mt = db.mimeTypeForName(mime);
    const QStringList exts = mt.suffixes();
    if (!exts.isEmpty()){
        return exts.first().toLatin1();
    }
    return QByteArray();
}

void CopyWorker::saveImageBytes(const QByteArray bytes,
                                const QString format)
{
    QBuffer buf;
    buf.setData(bytes);
    buf.open(QIODevice::ReadOnly);

    QImageReader reader(&buf, format.toLatin1());
    QImage img = reader.read();
    if (img.isNull()) {
        return;
    }

#ifdef Q_OS_MACOS
    // macOS: strip broken ICC profile before saving
    img.setColorSpace(QColorSpace());
    img = img.convertToFormat(QImage::Format_ARGB32);
#endif

    const QString ext = "." + mimeToFormat(format);
    const QString fileName = FilePathProvider::nameWithPrefix("image") + ext;

    const QString rootPath = StandardPaths::getPath(StandardPaths::TokriDir);
    const QString fullPath = QDir(rootPath).filePath(fileName);

    QImageWriter writer(fullPath, ext.mid(1).toLatin1());
    if (!writer.write(img)) {
        emit copyFailed(fullPath);
    } else {
        emit copySuccess(fullPath);
    }
}
