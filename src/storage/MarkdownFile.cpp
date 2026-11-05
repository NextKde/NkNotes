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
    if (d.readOnlyReason.isEmpty()) d.readOnlyReason = formatError(d.text);
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
static QString scalar(QString v, bool &ok)
{
    v = v.trimmed();
    if (v.startsWith(u'\'') || v.startsWith(u'"')) {
        const QChar quote = v.front(); QString out; int i = 1;
        for (; i < v.size(); ++i) {
            if (v[i] == quote) {
                if (quote == u'\'' && i + 1 < v.size() && v[i+1] == quote) { out += quote; ++i; continue; }
                const QString tail = v.mid(i+1).trimmed();
                ok = tail.isEmpty() || tail.startsWith(u'#'); return out;
            }
            if (v[i] == u'\\' && quote == u'"') { ok = false; return {}; }
            out += v[i];
        }
        ok = false; return {};
    }
    v.remove(QRegularExpression(QStringLiteral("\\s+#.*$")));
    if (v.startsWith(u'#')) v.clear();
    ok = !v.contains(QRegularExpression(QStringLiteral("[\\[\\]{}&*!|>\\\"']|:\\s")));
    return v.trimmed();
}
static QString parseField(const QString &text, const QString &key, bool &ok)
{
    if (!text.startsWith("---\n") && text != "---") return {};
    const auto lines = text.split(u'\n');
    if (lines.first() != "---") return {};
    static const QRegularExpression idKey("^nextnotes_id\\s*:"), titleKey("^title\\s*:");
    const QRegularExpression pattern=key=="nextnotes_id"?idKey:key=="title"?titleKey:QRegularExpression("^"+QRegularExpression::escape(key)+"\\s*:");
    QString result; bool seen = false, ended = false;
    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i] == "---" || lines[i] == "...") { ended = true; break; }
        const QString line = lines[i].trimmed();
        if (!pattern.match(line).hasMatch()) continue;
        if (!lines[i].startsWith(key + u':')) { ok = false; continue; }
        if (seen) ok = false;
        seen = true; bool valid = true;
        result = scalar(lines[i].mid(key.size()+1), valid); ok = ok && valid;
    }
    ok = ok && ended;
    return result;
}
QString field(const QString &text, const QString &key)
{
    bool ok = true; const auto value = parseField(text, key, ok); return ok ? value : QString();
}
QString formatError(const QString &text)
{
    bool ok = true;
    parseField(text, "nextnotes_id", ok); parseField(text, "title", ok);
    return ok ? QString() : QStringLiteral("unsupportedFrontMatter");
}
QString title(const QString &text, const QString &fileName)
{
    const auto value = field(text, "title"); if (!value.isEmpty()) return value;
    static const QRegularExpression fencePattern("^ {0,3}(`{3,}|~{3,})(.*)$");
    static const QRegularExpression setextPattern("^ {0,3}=+[ \\t]*$");
    static const QRegularExpression headingPattern("^ {0,3}#(?:[ \\t]+(.*)|$)");
    static const QRegularExpression closingPattern("[ \\t]+#+[ \\t]*$");
    static const QRegularExpression blockPattern("^(?: {4}|\\t| {0,3}(?:#{1,6}(?:[ \\t]|$)|>|[-+*][ \\t]|[0-9]+[.)][ \\t]|(?:[-*_][ \\t]*){3,}$|[-=]+[ \\t]*$))");
    QString paragraph;
    QChar fence; int length = 0; bool front = text.startsWith("---\n"); int n = 0;
    for (const QString &line : text.split(u'\n')) {
        if (front) { if (n++ && (line == "---" || line == "...")) front = false; continue; }
        const auto m = fencePattern.match(line);
        if (m.hasMatch()) {
            paragraph.clear();
            const auto run = m.captured(1);
            if (fence.isNull()) { fence = run.front(); length = run.size(); }
            else if (run.front() == fence && run.size() >= length && m.captured(2).trimmed().isEmpty()) fence = QChar();
            continue;
        }
        if (!fence.isNull()) continue;
        if (!paragraph.isEmpty() && setextPattern.match(line).hasMatch())
            return paragraph;
        auto h = headingPattern.match(line);
        if (h.hasMatch()) { QString t = h.captured(1); t.remove(closingPattern); return t.trimmed(); }
        // A Setext underline belongs to the preceding paragraph, not an indented
        // code block, list, quote, thematic break, or another heading.
        if (line.trimmed().isEmpty() || blockPattern.match(line).hasMatch())
            paragraph.clear();
        else {
            if (!paragraph.isEmpty()) paragraph += u' ' ;
            paragraph += line.trimmed();
        }
    }
    return fileName;
}
}
