#pragma once
#include <QByteArray>
#include <QString>

namespace MarkdownFile {
struct Document {
    QByteArray bytes;
    QString text;
    QString hash;
    QString readOnlyReason;
    bool bom = false;
    bool crlf = false;
    bool trailingNewline = false;
};
QString digest(const QByteArray &bytes);
Document decode(const QByteArray &bytes);
QByteArray encode(const Document &base, const QString &text);
QString formatError(const QString &text);
QString field(const QString &text, const QString &key);
QString title(const QString &text, const QString &fileName);
}
