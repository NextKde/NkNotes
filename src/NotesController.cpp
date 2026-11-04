#include "NotesController.h"
#include "EditorSession.h"
#include "storage/NotesRepository.h"
#include <QSettings>
#include <QUrl>
#include <QDesktopServices>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>

NotesController::NotesController(EditorSession *session, NotesRepository *repository, QObject *parent)
    : QObject(parent), m_session(session), m_repository(repository)
{
    m_autosave.setSingleShot(true); m_autosave.setInterval(700);
    m_search.setSingleShot(true); m_search.setInterval(180);
    connect(&m_autosave, &QTimer::timeout, this, &NotesController::save);
    connect(&m_search, &QTimer::timeout, this, &NotesController::refresh);
    connect(session, &EditorSession::sourceChanged, this, [this] {
        if (m_applying || m_path.isEmpty()) return;
        ++m_revision;
        m_status = m_conflict ? "冲突 · 本地输入仍保留" : "已修改";
        if (!m_session->composing() && !m_conflict) m_autosave.start();
        emit changed();
    });
    connect(session, &EditorSession::stateChanged, this, [this] {
        if (m_session->composing()) m_autosave.stop();
        else if (dirty() && !m_saving && !m_conflict) {
            if (m_transition) save(); else m_autosave.start();
        } else if (m_transition && !m_session->composing()) advance();
    });
    if (repository) {
        connect(repository, &NotesRepository::finished, this, &NotesController::receive);
        connect(repository, &NotesRepository::libraryChanged, this, &NotesController::externalChange);
    }
}
bool NotesController::readOnly() const { return m_readOnly || m_path.isEmpty() || (busy() && !m_session->composing()); }
quint64 NotesController::send(const QString &op, QVariantMap args) {
    if (op != "selectLibrary") { args.insert("generation", m_generation); args.insert("library", m_library); }
    const quint64 id = m_repository ? m_repository->request(op, args) : ++m_next;
    m_pending.insert(id, {op, args.value("path").toString(), m_library, m_generation, m_epoch, m_revision, args});
    emit requestSent(id, op, args);
    return id;
}
void NotesController::drain(std::function<void()> action) {
    if (busy()) return;
    m_afterSave = std::move(action); m_transition = true; emit changed(); advance();
}
void NotesController::advance() {
    if (!m_transition || m_loading || m_saving || m_session->composing()) return;
    if (dirty()) {
        if (m_conflict || !m_error.isEmpty()) {
            m_transition = false; m_afterSave = {}; emit changed(); return;
        }
        save(); return;
    }
    auto action = std::move(m_afterSave); m_afterSave = {}; m_transition = false;
    if (action) action();
    emit changed();
}
void NotesController::selectLibrary(const QUrl &url) {
    if (!url.isLocalFile()) return;
    drain([this, url] { m_loading = true; send("selectLibrary", {{"path", url.toLocalFile()}}); });
}
void NotesController::load(const QString &path, int match) {
    m_loading = true;
    send("read", {{"path", path}, {"match", match}}); emit changed();
}
void NotesController::openNote(const QString &path, int match) {
    if (path == m_path && match >= 0) { emit documentLoaded(match, -1, m_query.size()); return; }
    if (path == m_path) return;
    drain([this, path, match] { load(path, match); });
}
void NotesController::createNote() {
    drain([this] {
        const QString folder = m_filter.startsWith("folder:") ? m_filter.mid(7) + '/' : QString();
        const QString path = folder + "笔记-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + ".md";
        m_loading = true; send("create", {{"path", path}, {"text", "# 未命名笔记\n\n"}});
    });
}
void NotesController::createFolder(const QString &path) { if (!busy()) send("createFolder", {{"path", path}}); }
void NotesController::save() {
    if (!dirty() || m_saving || m_path.isEmpty() || m_session->composing() || m_loading) return;
    if (m_conflict) { m_error = "文件已在外部改变。本地输入仍保留；请另存副本，或明确放弃本地输入后重新读取。"; emit changed(); return; }
    m_autosave.stop(); m_error.clear(); m_status = "保存中…";
    m_saving = send("save", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}}); emit changed();
}
void NotesController::requestClose() { drain([this] { emit closeReady(); }); }
void NotesController::setQuery(const QString &v) { if (v == m_query) return; m_query = v; ++m_searchSerial; m_search.start(); emit changed(); }
void NotesController::setFilter(const QString &v) { if (v == m_filter) return; m_filter = v; ++m_searchSerial; refresh(); emit changed(); }
void NotesController::refresh() {
    if (!m_library.isEmpty()) send("search", {{"query", m_query}, {"filter", m_filter}, {"serial", m_searchSerial}});
}
void NotesController::favorite() {
    if (m_path.isEmpty() || busy()) return;
    send("favorite", {{"path", m_path}, {"favorite", !m_favorite}});
}
void NotesController::trash() {
    drain([this] { m_loading = true; send("trash", {{"path", m_path}, {"baseHash", m_base}}); });
}
void NotesController::move(const QString &target) {
    drain([this, target] { m_loading = true; send("rename", {{"path", m_path}, {"target", target}, {"baseHash", m_base}}); });
}
void NotesController::restore(const QString &token, const QString &target) {
    QVariantMap a{{"token", token}}; if (!target.isEmpty()) a.insert("target", target); send("restore", a);
}
void NotesController::inspectRecovery() { if (!m_library.isEmpty()) send("recovery"); }
void NotesController::recoverDraft(const QString &path, const QString &text, const QString &base) {
    drain([this, path, text, base] {
        m_loading = true; send("read", {{"path", path}, {"recoverText", text}, {"recoverBase", base}});
    });
}
void NotesController::saveCopy(const QString &target) {
    if (m_session->composing() || busy() || m_path.isEmpty() || m_saving) return;
    m_loading = true;
    send("saveAs", {{"path", target}, {"text", m_session->source()}, {"newIdentity", true}}); emit changed();
}
void NotesController::copyRecovery(const QString &text) {
    if (busy()) return;
    send("saveAs", {{"path", "恢复-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8) + ".md"}, {"text", text}, {"newIdentity", true}, {"recoveryCopy", true}});
}
void NotesController::discardRecovery(const QString &path) { send("discardDraft", {{"path", path}}); }
void NotesController::openRecoveryDirectory() { if (!m_library.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(m_library + "/.nextnotes")); }
void NotesController::discardAndReload() {
    if (m_saving || busy() || m_session->composing()) return;
    // Read first. If it fails the current buffer and draft remain intact.
    m_loading = true; send("read", {{"path", m_path}, {"discardAfterRead", true}}); emit changed();
}
void NotesController::dismissError() { m_error.clear(); emit changed(); }
QString NotesController::positionKey() const {
    return "positions/" + QString::fromLatin1(QCryptographicHash::hash((m_library + '/' + (m_selectedId.isEmpty() || m_duplicateIds.contains(m_selectedId) ? m_selectedId + "/" + m_path : m_selectedId)).toUtf8(), QCryptographicHash::Sha256).toHex());
}
void NotesController::rememberPosition(int cursor, double scroll) {
    if (m_path.isEmpty() || m_loading || m_applying) return;
    QSettings s; s.setValue(positionKey(), QVariantMap{{"cursor", cursor}, {"scroll", scroll}});
}
QVariant NotesController::preference(const QString &key, const QVariant &fallback) const { return QSettings().value("workspace/" + key, fallback); }
void NotesController::setPreference(const QString &key, const QVariant &value) { QSettings().setValue("workspace/" + key, value); }
void NotesController::applyListing(const QVariantMap &r) {
    if (r.contains("duplicateIds")) {
        m_duplicateIds.clear();
        for (const auto &v : r.value("duplicateIds").toList()) m_duplicateIds.append(v.toMap().value("id").toString());
    }
    if (r.contains("folders")) m_folders = r.value("folders").toList();
    if (r.contains("notes")) m_notes = r.value("notes").toList();
    if (!r.value("duplicateIds").toList().isEmpty())
        m_error = "库内存在重复笔记 ID，保存将被阻止。列表按路径区分；可将重复笔记另存为新身份副本，再整理原文件。";
    if (!r.value("unwatchedPaths").toStringList().isEmpty())
        m_error = "部分文件无法自动监听。请使用刷新检查外部变化；保存仍校验文件摘要。";
}
void NotesController::report(const QVariantMap &r) {
    const QString code = r.value("error").toString();
    m_conflict = m_conflict || code == "conflict" || code == "unknownBase";
    m_status = m_conflict ? "冲突 · 未覆盖本地输入" : "操作失败";
    m_error = "操作未完成（" + code + "）。当前编辑内容仍在窗口中。";
    if (r.contains("recoveryDurable")) m_error += r.value("recoveryDurable").toBool()
        ? "恢复草稿已写入磁盘。" : "恢复草稿未能写入磁盘，请保持窗口打开并另存或重试。";
    m_error += "可重试、另存副本或打开恢复中心。\n" + r.value("detail").toString();
}
void NotesController::receive(quint64 id, const QString &op, const QVariantMap &r) {
    if (!m_pending.contains(id)) return;
    const Pending p = m_pending.take(id);
    if (p.op != op) return;
    if (op != "selectLibrary" && (r.value("library").toString() != m_library || r.value("generation").toULongLong() != m_generation || p.generation != m_generation)) return;
    if (op == "search" && p.args.value("serial").toULongLong() != m_searchSerial) return;
    const bool documentOp = op == "read" || op == "save" || op == "saveAs" || op == "create" || op == "trash" || op == "rename";
    if (documentOp && !p.args.value("recoveryCopy").toBool() && p.epoch != m_epoch) return;
    if (op == "save") m_saving = 0;
    if ((documentOp && !p.args.value("recoveryCopy").toBool()) || op == "selectLibrary") m_loading = false;
    if (!r.value("ok").toBool()) {
        report(r); m_transition = false; m_afterSave = {}; emit changed(); return;
    }
    if (op == "saveAs" && p.args.value("recoveryCopy").toBool()) {
        m_status = "恢复副本已创建：" + r.value("path").toString(); refresh(); inspectRecovery(); emit changed(); return;
    }
    if (op == "selectLibrary") {
        m_recovery.clear();
        m_library = r.value("library").toString(); m_generation = r.value("generation").toULongLong();
        ++m_epoch; m_path.clear(); m_selectedId.clear(); m_base.clear(); m_revision = m_savedRevision = 0;
        m_error.clear(); m_conflict = false; m_query.clear(); m_filter = "all";
        m_applying = true; m_session->loadPrototype({}); m_applying = false;
        applyListing(r); m_status = "请选择笔记"; setPreference("lastLibrary", m_library); inspectRecovery(); refresh();
    } else if (op == "search" || op == "list") {
        applyListing(r);
        // Manual refresh also detects deletion when watcher resources are exhausted.
        if (r.contains("allNotes") && !m_path.isEmpty() && !m_saving && !busy()) {
            bool exists = false;
            for (const auto &v : r.value("allNotes").toList()) if (v.toMap().value("path").toString() == m_path) exists = true;
            if (!exists && !m_conflict) {
                if (!dirty()) ++m_revision;
                m_conflict = true; m_autosave.stop(); m_status = "文件已被外部移除 · 本地版本保留";
                send("checkpoint", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}});
            }
        }
        // Manual refresh also detects a changed current baseline.
        for (const auto &v : r.value("allNotes", r.value("notes")).toList()) {
            auto n = v.toMap();
            if (n.value("path").toString() == m_path) m_favorite = n.value("favorite").toBool();
            if (n.value("path").toString() == m_path && n.value("baseHash").toString() != m_base && !m_saving && !m_loading) {
                if (dirty() || m_session->composing()) { m_conflict = true; m_status = "外部修改冲突"; send("checkpoint", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}}); }
                else load(m_path);
                break;
            }
        }
    } else if (op == "save") {
        m_base = r.value("baseHash").toString();
        m_savedRevision = p.revision; // Never setPlainText here: preserve IME, cursor and undo.
        m_status = dirty() ? "已修改 · 正在保存后续输入" : "已保存"; m_error.clear();
        if (dirty()) { if (!m_session->composing()) save(); } else { refresh(); inspectRecovery(); advance(); }
    } else if (op == "read" || op == "create") {
        // A clean reload can race with typing while the response is in flight.
        if (op == "read" && p.revision != m_revision) { m_conflict = true; m_status = "外部修改冲突"; emit changed(); return; }
        m_path = r.value("path").toString(); m_base = r.value("baseHash").toString(); ++m_epoch;
        if (r.contains("listing")) applyListing(r.value("listing").toMap());
        m_selectedId = r.value("id").toString(); m_favorite = r.value("favorite").toBool();
        for (const auto &v : m_notes) if (v.toMap().value("path").toString() == m_path) m_selectedId = v.toMap().value("id").toString();
        m_readOnly = r.value("readOnly").toBool(); m_conflict = false; m_error.clear();
        m_applying = true; m_session->loadPrototype(p.args.contains("recoverText") ? p.args.value("recoverText").toString() : r.value("text").toString()); m_applying = false;
        m_revision = m_savedRevision = 0;
        if (p.args.contains("recoverText")) { ++m_revision; m_conflict = p.args.value("recoverBase").toString() != m_base; }
        m_status = m_readOnly ? "只读 · " + r.value("readOnlyReason").toString() : m_conflict ? "恢复草稿与磁盘不同 · 请另存" : dirty() ? "已恢复草稿 · 待保存" : "已保存";
        if (!p.args.contains("recoverText") && !p.args.value("discardAfterRead").toBool()) {
            for (const auto &draft : m_recovery.value("drafts").toList()) {
                if (draft.toMap().value("path").toString() == m_path) {
                    m_readOnly = true;
                    m_status = "存在未处理恢复草稿";
                    m_error = "此笔记有上次会话的恢复草稿。为保留双方版本，正文暂时只读。请在恢复中心载入或复制草稿；确认不要草稿时，从更多菜单放弃本地修改并读取磁盘。";
                    break;
                }
            }
        }
        if (p.args.value("discardAfterRead").toBool()) send("discardDraft", {{"path", m_path}});
        const auto position = QSettings().value(positionKey()).toMap();
        const int match = p.args.value("match", -1).toInt();
        emit documentLoaded(match >= 0 ? match : position.value("cursor", op == "create" ? m_session->source().size() : 0).toInt(), match >= 0 ? -1 : position.value("scroll", 0).toDouble(), match >= 0 ? m_query.size() : 0);
        if (dirty() && !m_conflict) m_autosave.start();
        refresh();
    } else if (op == "saveAs") {
        // The original recovery remains available. Explicit Save Copy resolves this
        // buffer by explicitly opening the new identity; original draft is retained.
        m_path = r.value("path").toString(); m_base = r.value("baseHash").toString(); ++m_epoch;
        m_applying = true; m_session->loadPrototype(r.value("text").toString()); m_applying = false;
        m_revision = m_savedRevision = 0; m_conflict = false; m_readOnly = false; m_error.clear();
        applyListing(r.value("listing").toMap());
        for (const auto &v : m_notes) if (v.toMap().value("path").toString() == m_path) m_selectedId = v.toMap().value("id").toString();
        m_status = "副本已保存 · 原恢复记录仍保留"; inspectRecovery(); refresh();
    } else if (op == "trash") {
        ++m_epoch; m_path.clear(); m_selectedId.clear(); m_revision = m_savedRevision = 0;
        m_applying = true; m_session->loadPrototype({}); m_applying = false;
        m_status = "已移到最近删除"; inspectRecovery(); refresh();
    } else if (op == "rename") { load(r.value("newPath").toString()); refresh(); }
    else if (op == "recovery") {
        m_recovery = r;
        QVariantList drafts;
        for (const auto &v : r.value("drafts").toList()) { auto d = v.toMap(); d.insert("durable", true); drafts.append(d); }
        for (const auto &v : r.value("memoryDrafts").toList()) {
            auto d = v.toMap(); bool found = false;
            for (auto &stored : drafts) {
                auto existing = stored.toMap();
                if (existing.value("path") != d.value("path")) continue;
                d.insert("durable", existing.value("text") == d.value("text") && existing.value("baseHash") == d.value("baseHash"));
                d.insert("matchesBase", existing.value("matchesBase")); stored = d; found = true; break;
            }
            if (!found) { d.insert("durable", false); drafts.append(d); }
        }
        m_recovery.insert("drafts", drafts);
    }
    else if (op == "checkpoint") { if (m_conflict) m_error = "外部版本已改变；本地恢复草稿已保存。请另存副本，或在恢复中心保留双方版本。"; }
    else if (op == "favorite" || op == "createFolder" || op == "restore" || op == "discardDraft") { if (op == "favorite" && p.path == m_path) m_favorite = p.args.value("favorite").toBool(); refresh(); inspectRecovery(); }
    emit changed();
}
void NotesController::externalChange(const QVariantMap &r) {
    if (r.value("library").toString() != m_library || r.value("generation").toULongLong() != m_generation) return;
    if (r.contains("scanError")) { report(r.value("scanError").toMap()); emit changed(); return; }
    refresh();
    for (const auto &v : r.value("changes").toList()) {
        const auto change = v.toMap();
        if (change.value("path").toString() != m_path || change.value("hash").toString() == m_base) continue;
        if (m_saving) continue; // save itself performs the authoritative baseline comparison.
        if (dirty() || m_session->composing() || change.value("removed").toBool()) {
            if (change.value("removed").toBool() && !dirty()) ++m_revision;
            m_conflict = true; m_autosave.stop(); m_status = "外部修改冲突";
            send("checkpoint", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}});
        } else if (!busy()) load(m_path);
    }
    emit changed();
}
