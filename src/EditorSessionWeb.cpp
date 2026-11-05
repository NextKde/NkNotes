#include "EditorSession.h"
#include <QRegularExpression>

void EditorSession::enableWeb() {
    m_web = true;
    m_rich = true;
    m_webFormat = QStringLiteral("正文");
    emit stateChanged();
}

QVariantMap EditorSession::webDocument() const {
    return {{"epoch", m_webEpoch}, {"source", m_webBody}, {"prefix", m_prefix}, {"sourceMode", m_sourceMode}};
}

void EditorSession::webConnected() {
    // A renderer reconnect starts a fresh sequence but restores the last
    // committed cache, including changes not yet acknowledged by persistence.
    m_webBody = m_webSource; m_prefix.clear();
    const auto front = QRegularExpression(QStringLiteral("\\A---\\r?\\n[\\s\\S]*?\\r?\\n(?:---|\\.\\.\\.)[^\\n]*\\n")).match(m_webSource);
    if (front.hasMatch()) { m_prefix=front.captured();m_webBody.remove(0,m_prefix.size()); }
    m_webConnected = true;
    m_webSequence = -1;
    m_webLoaded = false;
    emit webLoadRequested(webDocument());
}

void EditorSession::webDisconnected() {
    m_webConnected = false;
    m_webLoaded = false;
    m_webFlushPending = 0;
    m_composing = false;
    emit stateChanged();
}

void EditorSession::loadWeb(const QString &source) {
    if (m_composing) return;
    m_sourceEdited = false;
    ++m_webEpoch;
    m_webSequence = -1;
    m_webLoaded = false;
    m_original = m_webSource = source;
    m_webBody = source;
    m_prefix.clear();
    const auto front = QRegularExpression(QStringLiteral("\\A---\\r?\\n[\\s\\S]*?\\r?\\n(?:---|\\.\\.\\.)[^\\n]*\\n")).match(source);
    if (front.hasMatch()) {
        m_prefix = front.captured();
        m_webBody.remove(0, m_prefix.size());
    }
    m_webUndo = m_webRedo = false;
    m_webHeading = 0;
    m_webFormat = QStringLiteral("正文");
    if (m_protected) { m_protected = false; emit protectionChanged(); }
    if (m_webConnected) emit webLoadRequested(webDocument());
    emit stateChanged();
}

bool EditorSession::commandWeb(const QString &command, const QString &argument) {
    if(command.startsWith("insertNoteLink:")){return commandWeb("insertNoteLink",command.mid(15));}
    if(command.startsWith("replaceSource:")){if(!m_webLoaded||m_composing||m_webFlushPending)return false;emit webCommandRequested({{"epoch",m_webEpoch},{"command","replaceSource"},{"argument",command.mid(14)}});return true;}
    if (!m_webLoaded || m_composing || (m_protected && command != "sourceMode") || m_webFlushPending) return false;
    emit webCommandRequested({{"epoch", m_webEpoch}, {"command", command}, {"argument", argument}});
    return true;
}

bool EditorSession::flushWeb() {
    if (!m_web || !m_webConnected) return false;
    m_webFlushPending = ++m_webFlushSerial;
    emit webFlushRequested(m_webFlushPending);
    return true;
}

void EditorSession::acceptWebState(const QVariantMap &snapshot) {
    if (!m_web || snapshot.value("epoch", -1).toInt() != m_webEpoch) return;
    const int sequence = snapshot.value("sequence", -1).toInt();
    if (sequence <= m_webSequence) return;
    m_webSequence = sequence;
    m_webLoaded = snapshot.value("loaded").toBool();
    m_sourceMode = snapshot.value("sourceMode").toBool();
    const bool protectedContent = snapshot.value("protected").toBool();
    if (protectedContent != m_protected) { m_protected = protectedContent; emit protectionChanged(); }
    m_composing = snapshot.value("composing").toBool();
    m_webUndo = snapshot.value("canUndo").toBool();
    m_webRedo = snapshot.value("canRedo").toBool();
    m_webHeading = snapshot.value("heading").toInt();
    m_webFormat = snapshot.value("format", QStringLiteral("正文")).toString();
    bool changed = false;
    // Preedit snapshots never replace the committed cache used by persistence.
    if (m_webLoaded && !m_composing && !m_protected && snapshot.contains("source")) {
        const QString source = (snapshot.value("fullSource").toBool() ? QString() : m_prefix) + snapshot.value("source").toString();
        changed = source != m_webSource;
        m_webSource = source;
        m_sourceEdited = m_sourceEdited || snapshot.value("sourceEdited").toBool();
    }
    if (m_webLoaded && !m_composing && m_webFlushPending
        && snapshot.value("flush").toInt() == m_webFlushPending) m_webFlushPending = 0;
    if (changed) emit sourceChanged();
    emit stateChanged();
}
