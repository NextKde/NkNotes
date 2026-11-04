#include "MarkdownFile.h"
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QStringDecoder>

namespace MarkdownFile {
QString digest(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
Document decode(const QByteArray &bytes)
{
    Document d;
    d.bytes = bytes;
    d.hash = digest(bytes);
    d.bom = bytes.startsWith("\xEF\xBB\xBF");
    QStringDecoder decoder(QStringDecoder::Utf8);
    d.text = decoder(d.bom ? bytes.mid(3) : bytes);
    if (decoder.hasError()) d.readOnlyReason = QStringLiteral("invalidUtf8");
    const int pairs = d.text.count(QStringLiteral("\r\n"));
    const int lf = d.text.count(u'\n');
    const int cr = d.text.count(u'\r');
    d.crlf = pairs > 0;
    if (cr != pairs || (pairs && lf != pairs)) d.readOnlyReason = QStringLiteral("mixedNewlines");
    if (d.text.contains(QChar(0x2028)) || d.text.contains(QChar(0x2029)) || d.text.contains(QChar(0)))
        d.readOnlyReason = QStringLiteral("unsupportedSeparator");
    d.trailingNewline = d.text.endsWith(u'\n');
    d.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return d;
}
QByteArray encode(const Document &base, const QString &text)
{
    if (text == base.text) return base.bytes;
    QString normalized = text;
    // Preserve the original terminal-newline state while preserving interior whitespace.
    if (base.trailingNewline && !normalized.endsWith(u'\n')) normalized += u'\n';
    if (!base.trailingNewline) while (normalized.endsWith(u'\n')) normalized.chop(1);
    if (base.crlf) normalized.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    return (base.bom ? QByteArray("\xEF\xBB\xBF") : QByteArray()) + normalized.toUtf8();
}
QString field(const QString &text, const QString &key)
{
    // Deliberately conservative scalar subset, never a YAML serialization round trip.
    const auto lines = text.split(u'\n');
    if (lines.isEmpty() || lines.first() != QStringLiteral("---")) return {};
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i] == QStringLiteral("---") || lines[i] == QStringLiteral("...")) break;
        if (!lines[i].startsWith(key + u':')) continue;
        QString value = lines[i].mid(key.size() + 1).trimmed();
        if (value.size() >= 2 && ((value.front() == u'"' && value.back() == u'"') || (value.front() == u'\'' && value.back() == u'\'')))
            value = value.mid(1, value.size() - 2);
        return value;
    }
    return {};
}
QString title(const QString &text, const QString &fileName)
{
    const QString value = field(text, QStringLiteral("title"));
    if (!value.isEmpty()) return value;
    const auto match = QRegularExpression(QStringLiteral("^# +([^\n]+)"), QRegularExpression::MultilineOption).match(text);
    return match.hasMatch() ? match.captured(1).trimmed() : fileName;
}
}
