#include "textfile.h"
#include "filepathprovider.h"
#include <QFileInfo>

TextFile::TextFile(QObject *parent)
    : QObject{parent}
{}

void TextFile::setName(QString name)
{
    mName = name;
}

void TextFile::setContent(QString content)
{
    mContents = content;
}

QString TextFile::save()
{
    QFile file;
    if (mName.length() > 0){
        file.setFileName(mName);
    } else {
        file.setFileName(FilePathProvider::nameFromText(mContents));
    }
    const bool opened = file.open(
        QIODevice::NewOnly | QIODevice::WriteOnly | QIODevice::Text);
    if (opened) {
        QTextStream out(&file);
        out << mContents;
    }
    const QString savedPath = file.fileName();
    file.close();
    return opened && QFileInfo::exists(savedPath) ? savedPath : QString();
}
