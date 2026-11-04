#include "EditorSession.h"
#include <QTextCursor>
#include <algorithm>

EditorSession::EditorSession(QObject *parent) : QObject(parent) {}
QString EditorSession::source() const {
    if (!m_document) return {};
    // Unlike toPlainText(), toRawText() preserves non-breaking spaces.
    QString text = m_document->toRawText();
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return text;
}
bool EditorSession::modified() const { return m_document && m_document->isModified(); }
bool EditorSession::canUndo() const { return m_document && m_document->isUndoAvailable(); }
bool EditorSession::canRedo() const { return m_document && m_document->isRedoAvailable(); }
void EditorSession::setComposing(bool value) {
    if (m_composing == value) return;
    m_composing = value;
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
        connect(document, &QTextDocument::contentsChanged, this, &EditorSession::sourceChanged);
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
}
void EditorSession::loadPrototype(const QString &source) {
    if (!m_document || m_composing) return;
    m_document->setPlainText(source);
    m_document->setModified(false);
    select(0, 0);
    emit selectionRequested(0, 0);
    emit stateChanged();
}
bool EditorSession::execute(const QString &command) {
    if (!m_document || m_composing) return false;
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
    if (!canUndo() || m_composing) return;
    QTextCursor cursor(m_document);
    m_document->undo(&cursor);
    select(cursor.anchor(), cursor.position());
    emit selectionRequested(m_anchor, m_position);
}
void EditorSession::redo() {
    if (!canRedo() || m_composing) return;
    QTextCursor cursor(m_document);
    m_document->redo(&cursor);
    select(cursor.anchor(), cursor.position());
    emit selectionRequested(m_anchor, m_position);
}
