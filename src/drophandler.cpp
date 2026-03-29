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
    file.save();
    return true;
}

bool TextDropHandler::handleUrlDrop(const QString &urlStr)
{
    QNetworkRequest request((QUrl(urlStr)));

    auto headReply = manager.head(request);

    QObject::connect(headReply, &QNetworkReply::finished,
        [this, headReply, urlStr, request]() {

        if (headReply->error() != QNetworkReply::NoError) {
            headReply->deleteLater();
            TextFile file;
            file.setName(FilePathProvider::nameFromUrl(urlStr) + ".url.txt");
            file.setContent(urlStr);
            file.save();
            return;
        }

        const auto contentType = headReply->header(QNetworkRequest::ContentTypeHeader).toString();
        headReply->deleteLater();

        if (contentType.startsWith("image/")) {
            auto getReply = manager.get(request);

            QObject::connect(getReply, &QNetworkReply::finished,
                [getReply, urlStr]() {
                    if (getReply->error() != QNetworkReply::NoError) {
                        getReply->deleteLater();
                        return;
                    }
                QByteArray data = getReply->readAll();
                getReply->deleteLater();

                QFile file(FilePathProvider::nameFromUrl(urlStr));
                if (!file.open(QIODevice::WriteOnly)) return;

                file.write(data);
                file.close();
            });

            } else {
                TextFile file;
                file.setName(FilePathProvider::nameFromUrl(urlStr) + ".url.txt");
                file.setContent(urlStr);
                file.save();
            }
        });

    return true;
}
