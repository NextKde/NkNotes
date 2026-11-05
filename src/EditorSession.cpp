#include "EditorSession.h"
#include <QTextCursor>
#include <algorithm>
#include <QTextBlock>
#include <QTextList>
#include <QTextTable>
#include <QImage>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QRegularExpression>
#include <memory>

namespace {
QString editableMarkdown(const QTextDocument *document) {
    if (!document->toRawText().contains(QChar::LineSeparator))
        return document->toMarkdown(QTextDocument::MarkdownDialectGitHub);
    // Rich text can contain explicit U+2028 breaks. Qt serializes them as a soft
    // Markdown newline, which becomes a space on reload. Preserve hard breaks
    // without changing the live document, its undo history or IME state.
    std::unique_ptr<QTextDocument> copy(document->clone());
    QString token = QStringLiteral("NKNOTESHARDBREAK7b628e");
    while (document->toRawText().contains(token)) token += QLatin1Char('X');
    for (auto block = copy->begin(); block.isValid(); block = block.next()) {
        if (block.blockFormat().hasProperty(QTextFormat::BlockCodeFence)
            || block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) continue;
        for (int i = block.text().size() - 1; i >= 0; --i) {
            if (block.text().at(i) != QChar::LineSeparator) continue;
            QTextCursor cursor(copy.get()); cursor.setPosition(block.position() + i);
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
            cursor.insertText(token);
        }
    }
    QString markdown = copy->toMarkdown(QTextDocument::MarkdownDialectGitHub);
    markdown.replace(token, QStringLiteral("  \n"));
    return markdown;
}
QStringList markdownUnits(const QString &text) {
    QStringList units;
    QString unit, fence;
    const auto lines = text.split(QLatin1Char('\n'));
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = lines[i];
        const bool thematicBreak = fence.isEmpty() && unit.trimmed().isEmpty()
            && QRegularExpression(QStringLiteral("^ {0,3}(?:(?:\\* *){3,}|(?:- *){3,}|(?:_ *){3,})$")).match(line).hasMatch();
        if (i == lines.size() - 1 && line.isEmpty()) break;
        const auto marker = QRegularExpression(QStringLiteral("^ {0,3}(`{3,}|~{3,})")).match(line);
        if (marker.hasMatch()) {
            if (fence.isEmpty()) fence = marker.captured(1);
            else if (marker.captured(1).at(0) == fence.at(0) && marker.captured(1).size() >= fence.size()) fence.clear();
        }
        unit += line;
        if (i + 1 < lines.size()) unit += QLatin1Char('\n');
        if ((line.trimmed().isEmpty() && fence.isEmpty()) || thematicBreak) {
            if (unit.trimmed().isEmpty() && !units.isEmpty()) units.last() += unit;
            else units.append(unit);
            unit.clear();
        }
    }
    if (!unit.isEmpty()) units.append(unit);
    return units;
}
}

EditorSession::EditorSession(QObject *parent) : QObject(parent) {}
QString EditorSession::source() const {
    if (m_web) return m_webSource;
    if (!m_document) return {};
    if (m_rich) {
        if (m_loading || m_protected) return m_original;
        QString markdown = editableMarkdown(m_document);
        // Compare content, including rich formatting, rather than undo-stack state.
        if (m_document->toHtml() == m_loadedRich) return m_original;
        // Restore complete untouched blocks in source order. Changed blocks alone
        // use Qt's serializer. Complex cross-block syntax may still normalize.
        QString restored;
        qsizetype after = 0;
        const auto units = markdownUnits(markdown);
        for (qsizetype index = 0; index < units.size(); ++index) {
            const auto &unit = units[index];
            qsizetype match = -1;
            for (qsizetype i = after; i < m_originalBlocks.size(); ++i) {
                if (m_originalBlocks[i].first == unit) { match = i; break; }
            }
            if (match < 0) restored += unit;
            else {
                QString original = m_originalBlocks[match].second;
                // A former EOF paragraph may now have a paragraph after it.
                // Retain the serializer's required separator at that boundary.
                if (index + 1 < units.size()) {
                    auto trailingLines = [](const QString &text) {
                        int n = 0;
                        for (qsizetype i = text.size(); i > 0 && text.at(i - 1) == QLatin1Char('\n'); --i) ++n;
                        return n;
                    };
                    original += QString(qMax(0, trailingLines(unit) - trailingLines(original)), QLatin1Char('\n'));
                }
                restored += original; after = match + 1;
            }
        }
        return m_prefix + restored;
    }
    // Unlike toPlainText(), toRawText() preserves non-breaking spaces.
    QString text = m_document->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}
bool EditorSession::modified() const { return m_web ? m_webSource != m_original : m_document && m_document->isModified(); }
bool EditorSession::canUndo() const { return m_web ? m_webUndo : m_document && m_document->isUndoAvailable(); }
bool EditorSession::canRedo() const { return m_web ? m_webRedo : m_document && m_document->isRedoAvailable(); }
void EditorSession::setComposing(bool value) {
    if (m_composing == value) return;
    m_composing = value;
    // Register the committed IME text before observers may drain/close.
    if (!value) emit sourceChanged();
    emit stateChanged();
}
void EditorSession::attach(QQuickTextDocument *document) {
    attachDocument(document ? document->textDocument() : nullptr);
}
void EditorSession::attachDocument(QTextDocument *document) {
    if (m_document == document) return;
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        document->setUndoRedoEnabled(true);
        document->setResourceProvider([](const QUrl &url) -> QVariant {
            if (url.isLocalFile()) return QImage(url.toLocalFile());
            return QImage();
        });
        connect(document, &QTextDocument::contentsChanged, this, [this] { if (!m_loading && !m_composing) emit sourceChanged(); });
        connect(document, &QTextDocument::contentsChanged, this, &EditorSession::stateChanged);
        connect(document, &QTextDocument::undoAvailable, this, &EditorSession::stateChanged);
        connect(document, &QTextDocument::redoAvailable, this, &EditorSession::stateChanged);
        connect(document, &QTextDocument::modificationChanged, this, &EditorSession::stateChanged);
    }
    emit sourceChanged();
    emit stateChanged();
}
void EditorSession::select(int anchor, int position) {
    const int length = m_document ? m_document->characterCount() - 1 : 0;
    m_anchor = std::clamp(anchor, 0, length);
    m_position = std::clamp(position, 0, length);
    emit stateChanged();
}
void EditorSession::loadPrototype(const QString &source) {
    if (m_web) { loadWeb(source); return; }
    if (!m_document || m_composing) return;
    const bool wasProtected = m_protected;
    m_loading = true;
    m_original = source;
    m_prefix.clear();
    m_originalBlocks.clear();
    m_imageNames.clear();
    m_protected = false;
    QString body = source;
    if (m_rich) {
        QRegularExpression front(QStringLiteral("\\A---\\r?\\n[\\s\\S]*?\\r?\\n(?:---|\\.\\.\\.)[^\\n]*\\n"));
        const auto match = front.match(body);
        if (match.hasMatch()) { m_prefix = match.captured(); body.remove(0, m_prefix.size()); }
        // Extensions and embedded markup cannot be round-tripped by Qt safely.
        m_protected = body.contains(QRegularExpression(QStringLiteral("<!--|<[/A-Za-z]|\\[\\[|\\$\\$|\\$[^\\s$][^$\\n]*[^\\s$]\\$|^:::|^\\[\\^"), QRegularExpression::MultilineOption));
        // Parse in a plain document first, which cannot start QQuick's network
        // image loader. Seed every resource before the live document sees it.
        QTextDocument parsed;
        parsed.setMarkdown(body, QTextDocument::MarkdownDialectGitHub);
        for (auto b = parsed.begin(); b.isValid(); b = b.next())
            for (auto it = b.begin(); !it.atEnd(); ++it)
                if (it.fragment().charFormat().isImageFormat())
                    m_imageNames.append(it.fragment().charFormat().toImageFormat().name());
        cacheImages();
        m_document->setMarkdown(body, QTextDocument::MarkdownDialectGitHub);
        m_loadedRich = m_document->toHtml();
        cacheImages();
        // Keep blank-separated source blocks together, including fenced code
        // with internal blank lines. Only independently round-trippable units
        // are candidates for exact spelling restoration.
        for (const auto &unit : markdownUnits(body)) {
            QTextDocument fragment;
            // Qt drops a standalone final rule from toMarkdown(). A trailing
            // paragraph makes it serialize just as it does in the full document.
            const QString sentinel = QStringLiteral("NKNOTES_CANONICAL_END_7b628e");
            fragment.setMarkdown(unit + QStringLiteral("\n\n") + sentinel,
                                 QTextDocument::MarkdownDialectGitHub);
            const QString serialized = fragment.toMarkdown(QTextDocument::MarkdownDialectGitHub);
            const QString canonical = serialized.left(serialized.lastIndexOf(sentinel));
            if (!canonical.trimmed().isEmpty()) m_originalBlocks.append({canonical, unit});
        }
    } else m_document->setPlainText(source);
    m_loading = false;
    m_document->setModified(false);
    if (m_protected != wasProtected) emit protectionChanged();
    select(0, 0);
    emit selectionRequested(0, 0);
    emit stateChanged();
}
bool EditorSession::markdownHeading(int anchor, int position) {
    if (!m_rich || !m_document || m_loading || m_composing || m_protected || anchor != position
        || position < 0 || position >= m_document->characterCount()) return false;
    QTextCursor cursor(m_document); cursor.setPosition(position);
    const auto block = cursor.block();
    if (block.textList() || block.blockFormat().headingLevel()
        || block.blockFormat().hasProperty(QTextFormat::BlockCodeFence)
        || block.blockFormat().hasProperty(QTextFormat::BlockCodeLanguage)) return false;
    const QString prefix = block.text().left(position - block.position());
    if (!QRegularExpression(QStringLiteral("^#{1,6}$")).match(prefix).hasMatch()) return false;
    cursor.beginEditBlock();
    cursor.setPosition(block.position(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    auto format = cursor.blockFormat(); format.setHeadingLevel(prefix.size()); cursor.setBlockFormat(format);
    QTextCharFormat text; text.setFontWeight(QFont::Bold);
    text.setProperty(QTextFormat::FontSizeAdjustment, 4 - prefix.size());
    cursor.setBlockCharFormat(text);
    QTextCursor contents(cursor); contents.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    contents.mergeCharFormat(text);
    cursor.setCharFormat(text);
    cursor.endEditBlock();
    select(cursor.position(), cursor.position()); emit selectionRequested(m_anchor, m_position);
    emit stateChanged(); return true;
}
bool EditorSession::endHeading(int anchor, int position) {
    if (!m_rich || !m_document || m_loading || m_composing || m_protected
        || anchor < 0 || position < 0 || qMax(anchor, position) >= m_document->characterCount()) return false;
    QTextCursor cursor(m_document); cursor.setPosition(anchor); cursor.setPosition(position, QTextCursor::KeepAnchor);
    if (!cursor.blockFormat().headingLevel()) return false;
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.endEditBlock();
    select(cursor.position(), cursor.position()); emit selectionRequested(m_anchor, m_position);
    emit stateChanged(); return true;
}
bool EditorSession::execute(const QString &command) {
    if (m_web) return commandWeb(command);
    if (!m_document || m_composing) return false;
    if (m_rich) {
        if (m_protected) return false;
        if (m_anchor == m_position && (command == "bold" || command == "italic" || command == "strike" || command == "code")) { emit typingFormatRequested(command); return true; }
        QTextCursor c(m_document);
        c.setPosition(m_anchor); c.setPosition(m_position, QTextCursor::KeepAnchor);
        c.beginEditBlock();
        QTextCharFormat f;
        if (command == "bold") { f.setFontWeight(c.charFormat().fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold); c.mergeCharFormat(f); }
        else if (command == "italic") { f.setFontItalic(!c.charFormat().fontItalic()); c.mergeCharFormat(f); }
        else if (command == "strike") { f.setFontStrikeOut(!c.charFormat().fontStrikeOut()); c.mergeCharFormat(f); }
        else if (command == "code") {
            const bool enabled = !c.charFormat().fontFixedPitch();
            f.setFontFixedPitch(enabled);
            f.setFontFamilies(enabled ? QStringList{"monospace"} : m_document->defaultFont().families());
            c.mergeCharFormat(f);
        }
        else if (command == "paragraph" || command.startsWith("heading")) {
            bool valid = command == "paragraph";
            const int level = valid ? 0 : command.mid(7).toInt(&valid);
            if (!valid || level < 0 || level > 6) { c.endEditBlock(); return false; }
            // A paragraph style applies to complete blocks, including when only
            // part of their text is selected. Keep the user's original selection.
            QTextCursor blocks(c);
            const int end = c.hasSelection() ? c.selectionEnd() - 1 : c.position();
            blocks.setPosition(c.selectionStart()); blocks.movePosition(QTextCursor::StartOfBlock);
            blocks.setPosition(end, QTextCursor::KeepAnchor); blocks.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            QTextBlockFormat b; b.setHeadingLevel(level); blocks.mergeBlockFormat(b);
            f.setFontWeight(level ? QFont::Bold : QFont::Normal);
            f.setProperty(QTextFormat::FontSizeAdjustment, level ? 4 - level : 0);
            blocks.mergeCharFormat(f);
        } else if (command == "bullet" || command == "ordered" || command == "task") {
            const auto style = command == "ordered" ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
            const auto list = c.currentList();
            const bool remove = list && list->format().style() == style
                && (command == "task") == (c.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker);
            if (remove) {
                const int end = c.hasSelection() ? c.selectionEnd() - 1 : c.position();
                for (auto block = m_document->findBlock(c.selectionStart()); block.isValid() && block.position() <= end; block = block.next()) {
                    if (auto current = block.textList()) current->remove(block);
                    QTextCursor row(block); auto b = row.blockFormat(); b.setIndent(0); b.setMarker(QTextBlockFormat::MarkerType::NoMarker); row.setBlockFormat(b);
                }
            } else {
                QTextListFormat l; l.setStyle(style); c.createList(l);
                QTextBlockFormat b;
                b.setMarker(command == "task" ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::NoMarker);
                c.mergeBlockFormat(b);
            }
        } else if (command == "check") { auto b=c.blockFormat(); b.setMarker(b.marker()==QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::Checked); c.mergeBlockFormat(b); }
        else if (command == "table") c.insertTable(3, 3);
        else { c.endEditBlock(); return false; }
        c.endEditBlock();
        select(c.anchor(), c.position()); emit selectionRequested(m_anchor, m_position); emit stateChanged(); return true;
    }
    QString marker;
    if (command == "bold") marker = "**";
    else if (command == "italic") marker = "*";
    else if (command == "strike") marker = "~~";
    else if (command == "code") marker = "`";
    else return false;
    const int start = std::min(m_anchor, m_position);
    const int end = std::max(m_anchor, m_position);
    const int originalAnchor = m_anchor;
    const int originalPosition = m_position;
    const QString current = source();
    // A source transformation, deliberately not a parser: only exact adjacent
    // delimiters are toggled. Mixed / nested Markdown state is deferred to M2.
    const int size = marker.size();
    const bool unwrap = start >= size && current.mid(start-size, size) == marker
        && current.mid(end, size) == marker;
    QTextCursor cursor(m_document);
    cursor.beginEditBlock();
    if (unwrap) {
        cursor.setPosition(end);
        cursor.setPosition(end+size, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        cursor.setPosition(start-size);
        cursor.setPosition(start, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        cursor.setPosition(end);
        cursor.insertText(marker);
        cursor.setPosition(start);
        cursor.insertText(marker);
    }
    cursor.endEditBlock();
    const int delta = unwrap ? -size : size;
    select(originalAnchor + delta, originalPosition + delta);
    emit selectionRequested(m_anchor, m_position);
    return true;
}
void EditorSession::undo() {
    if (m_web) { commandWeb("undo"); return; }
    if (!canUndo() || m_composing) return;
    QTextCursor cursor(m_document);
    m_document->undo(&cursor);
    select(cursor.anchor(), cursor.position());
    emit selectionRequested(m_anchor, m_position);
}
void EditorSession::redo() {
    if (m_web) { commandWeb("redo"); return; }
    if (!canRedo() || m_composing) return;
    QTextCursor cursor(m_document);
    m_document->redo(&cursor);
    select(cursor.anchor(), cursor.position());
    emit selectionRequested(m_anchor, m_position);
}

int EditorSession::headingLevel() const {
    if (m_web) return m_webHeading;
    if (!m_document) return 0;
    QTextCursor c(m_document); c.setPosition(std::min(m_position, m_document->characterCount() - 1));
    return c.blockFormat().headingLevel();
}

void EditorSession::setImageWidth(qreal width) {
    if (width <= 0 || qFuzzyCompare(m_imageWidth, width)) return;
    m_imageWidth = width;
    cacheImages();
    if (m_document) m_document->markContentsDirty(0, m_document->characterCount());
}

void EditorSession::cacheImages() {
    if (!m_document) return;
    for (const auto &name : m_imageNames) {
        const QUrl key(name);
        const QUrl resolved = m_document->baseUrl().resolved(key);
        QImage image;
        if (resolved.isLocalFile()) image.load(resolved.toLocalFile());
        // A valid transparent resource also suppresses QQuick's remote loader.
        if (image.isNull()) { image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent); }
        else if (image.width() > m_imageWidth) image = image.scaledToWidth(qMax(1, int(m_imageWidth)), Qt::SmoothTransformation);
        m_document->addResource(QTextDocument::ImageResource, key, image);
        m_document->addResource(QTextDocument::ImageResource, resolved, image);
    }
}

QString EditorSession::formatState() const {
    if (m_web) return m_webFormat;
    if (!m_document) return QStringLiteral("正文");
    QTextCursor c(m_document); c.setPosition(m_position);
    if (m_anchor != m_position) {
        QTextCursor scan(m_document); scan.setPosition(std::min(m_anchor,m_position));
        const auto first = scan.charFormat(); const int heading = scan.blockFormat().headingLevel();
        for (int i=std::min(m_anchor,m_position)+1;i<=std::max(m_anchor,m_position);++i) {
            scan.setPosition(i);
            if (scan.charFormat() != first || scan.blockFormat().headingLevel()!=heading) return QStringLiteral("混合格式选区");
        }
    }
    QStringList parts; const int h=c.blockFormat().headingLevel();
    parts << (h ? QStringLiteral("标题 %1").arg(h) : QStringLiteral("正文"));
    if (c.charFormat().fontWeight() >= QFont::Bold) parts << QStringLiteral("粗体");
    if (c.charFormat().fontItalic()) parts << QStringLiteral("斜体");
    if (c.charFormat().fontStrikeOut()) parts << QStringLiteral("删除线");
    if (c.currentList()) parts << QStringLiteral("列表");
    return parts.join(QStringLiteral(" · "));
}
QString EditorSession::fidelityNotice() const {
    if (m_web) return m_protected ? QStringLiteral("包含不支持的扩展内容，原文保留并保护为只读") : QStringLiteral("Markdown 编辑由 Milkdown 提供；修改后按标准 Markdown 保存");
    return m_protected ? QStringLiteral("包含尚不支持的扩展或 HTML：保护为只读，原文保留") : QStringLiteral("保留原文与未修改段落；复杂跨段落语法仍可能规范化");
}

bool EditorSession::link(const QString &url) {
    if (m_web) {
        const QUrl target(url);
        if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http" && target.scheme() != "mailto")) return false;
        return commandWeb("link", url);
    }
    if (!m_rich || !m_document || m_composing || m_protected) return false;
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != "https" && target.scheme() != "http" && target.scheme() != "mailto")) return false;
    QTextCursor c(m_document); c.setPosition(m_anchor); c.setPosition(m_position, QTextCursor::KeepAnchor);
    c.beginEditBlock();
    QTextCharFormat f; f.setAnchor(true); f.setAnchorHref(url); f.setFontUnderline(true);
    if (c.hasSelection()) c.mergeCharFormat(f); else c.insertText(url, f);
    c.endEditBlock(); select(c.anchor(), c.position()); emit selectionRequested(m_anchor,m_position); return true;
}
