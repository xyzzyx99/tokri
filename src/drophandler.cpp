#include "drophandler.h"
#include "filepathprovider.h"
#include "textfile.h"

#include <QRegularExpression>
#include <QNetworkReply>
#include <QNetworkRequest>

TextDropHandler::TextDropHandler(QObject *parent)
    : QObject{parent}
{}

bool TextDropHandler::handleTextDrop(const QString &text)
{
    TextFile file;
    file.setContent(text);
    const QString path = file.save();
    if (!path.isEmpty())
        emit itemCreated(path);
    return !path.isEmpty();
}

bool TextDropHandler::handleUrlDrop(const QString &urlStr)
{
    thread_local QNetworkAccessManager manager;
    QNetworkRequest request((QUrl(urlStr)));

    auto headReply = manager.head(request);

    QObject::connect(headReply, &QNetworkReply::finished,
        [this, headReply, urlStr, request]() {

        if (headReply->error() != QNetworkReply::NoError) {
            headReply->deleteLater();
            TextFile file;
            file.setName(FilePathProvider::nameFromUrl(urlStr) + ".url.txt");
            file.setContent(urlStr);
            const QString path = file.save();
            if (!path.isEmpty())
                emit itemCreated(path);
            return;
        }

        const auto contentType = headReply->header(QNetworkRequest::ContentTypeHeader).toString();
        headReply->deleteLater();

        if (contentType.startsWith("image/")) {
            auto getReply = manager.get(request);

            QObject::connect(getReply, &QNetworkReply::finished,
                [this, getReply, urlStr]() {
                if (getReply->error() != QNetworkReply::NoError) {
                    getReply->deleteLater();
                    return;
                }
                QByteArray data = getReply->readAll();
                getReply->deleteLater();

                QFile file(FilePathProvider::nameFromUrl(urlStr));
                if (!file.open(QIODevice::WriteOnly)) return;

                file.write(data);
                const QString path = file.fileName();
                file.close();
                emit itemCreated(path);
            });

        } else {
            TextFile file;
            file.setName(FilePathProvider::nameFromUrl(urlStr) + ".url.txt");
            file.setContent(urlStr);
            const QString path = file.save();
            if (!path.isEmpty())
                emit itemCreated(path);
        }
    });

    return true;
}
