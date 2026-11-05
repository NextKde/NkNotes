#include "NotesController.h"
#include "EditorSession.h"
#include "storage/NotesRepository.h"
#include <QSettings>
#include <QDir>
#include <QRegularExpression>
#include <QUrl>
#include <QDesktopServices>
#include <QUuid>
#include <QCryptographicHash>
#include <algorithm>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QDir>

NotesController::NotesController(EditorSession *session, NotesRepository *repository, QObject *parent)
    : QObject(parent), m_session(session), m_repository(repository)
{
    m_autosave.setSingleShot(true); m_autosave.setInterval(700);
    m_search.setSingleShot(true); m_search.setInterval(180);
    connect(&m_autosave, &QTimer::timeout, this, &NotesController::save);
    connect(&m_search, &QTimer::timeout, this, [this]{ refresh(true); });
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
    m_afterSave = std::move(action); m_transition = true;
    m_session->flushWeb();
    emit changed(); advance();
}
void NotesController::advance() {
    if (!m_transition || m_loading || m_saving || m_session->composing() || m_session->awaitingWebFlush()) return;
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
void NotesController::restoreWorkspace() {
    if (!m_library.isEmpty() || busy()) return;
    const QString last = preference("lastLibrary").toString();
    if (last.isEmpty()) return;
    m_restoreWorkspace = true;
    selectLibrary(QUrl::fromLocalFile(last));
}
void NotesController::selectLibrary(const QUrl &url) {
    if (!url.isLocalFile()) return;
    drain([this, url] { m_loading = true; send("selectLibrary", {{"path", url.toLocalFile()}}); });
}
void NotesController::load(const QString &path, int match) {
    m_knowledge.clear();m_version.clear();m_versions.clear();
    m_loading = true;
    QString matchQuery=m_query;for(const auto &v:m_notes)if(v.toMap().value("path").toString()==path){matchQuery=v.toMap().value("matchText",m_query).toString();break;}
    send("read", {{"path", path}, {"match", match}, {"matchQuery",matchQuery}}); emit changed();
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
    m_saving = send("save", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}, {"sourceNewlines", m_session->sourceEdited()}}); emit changed();
}
void NotesController::requestClose() { drain([this] { emit closeReady(); }); }
void NotesController::setQuery(const QString &v) { if (v == m_query) return; m_query = v; ++m_searchSerial; m_search.start(); emit changed(); }
void NotesController::setFilter(const QString &v) { if (v == m_filter) return; m_filter = v; ++m_searchSerial; refresh(); emit changed(); }
void NotesController::refresh(bool indexed) {
    if (!m_library.isEmpty()) send("search", {{"query", m_query}, {"filter", m_filter}, {"sort", sortKey()}, {"ascending", sortAscending()}, {"serial", m_searchSerial}, {"indexed", indexed}});
}
void NotesController::favorite() {
    if (m_path.isEmpty() || busy()) return;
    send("favorite", {{"path", m_path}, {"favorite", !m_favorite}});
}
void NotesController::trash() {
    drain([this] { m_loading = true; send("trash", {{"path", m_path}, {"baseHash", m_base}}); });
}
void NotesController::move(const QString &target) {
    drain([this, target] { m_organizing = true; m_loading = true; send("rename", {{"path", m_path}, {"target", target}, {"baseHash", m_base}}); });
}
void NotesController::restore(const QString &token, const QString &target) {
    QVariantMap a{{"token", token}}; if (!target.isEmpty()) a.insert("target", target); send("restore", a);
}
void NotesController::inspectRecovery() { if (!m_library.isEmpty()) send("recovery"); }
void NotesController::setRecoveryVisible(bool visible) {
    if (m_recoveryVisible == visible) return;
    m_recoveryVisible = visible;
    if (!visible && !m_library.isEmpty()) send("recovery", {{"includeCommitText", false}});
}
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
void NotesController::importAttachment(const QString &request, int webEpoch, const QUrl &source, const QString &name, const QString &data) {
    if (readOnly() || m_path.isEmpty() || webEpoch != m_session->webDocument().value("epoch").toInt()) {
        emit attachmentReady(request, webEpoch, {{"ok", false}, {"error", "documentChanged"}}); return;
    }
    send("importAttachment", {{"path", m_path}, {"request", request}, {"webEpoch", webEpoch}, {"source", source.toString()}, {"name", name}, {"data", data}});
}
void NotesController::inspectAttachments() { drain([this] { send("attachments"); }); }
void NotesController::quarantineAttachment(const QString &relative) { drain([this, relative] { send("quarantineAttachment", {{"relative", relative}}); }); }
void NotesController::restoreAttachment(const QString &token) { if (!busy()) send("restoreAttachment", {{"token", token}}); }
void NotesController::copyText(const QString &text) { QGuiApplication::clipboard()->setText(text); }
QVariantMap NotesController::imageWidths() const { return QSettings().value(positionKey() + "/images").toMap(); }
QVariantList NotesController::tableWidths() const { return QSettings().value(positionKey() + "/tableWidths").toList(); }
void NotesController::setTableWidths(const QVariantList &widths) { if (!m_path.isEmpty() && widths != tableWidths()) QSettings().setValue(positionKey() + "/tableWidths", widths); }
void NotesController::setImageWidth(const QString &source, int width) {
    auto widths = imageWidths(); if (width <= 0) widths.remove(source); else widths.insert(source, std::clamp(width, 64, 1600));
    QSettings().setValue(positionKey() + "/images", widths); emit changed();
}
void NotesController::openLink(const QString &href) {
    if(href.startsWith("wiki:")){if(!busy())send("resolveWiki",{{"path",m_path},{"target",href.mid(5).section('#',0,0)},{"fragment",href.mid(5).section('#',1)}});return;}
    const QUrl url(href);
    if (url.scheme() == "https" || url.scheme() == "http" || url.scheme() == "mailto") { QDesktopServices::openUrl(url); return; }
    const QUrl resolved = QUrl::fromLocalFile(m_library + '/' + m_path).resolved(url);
    if (!resolved.isLocalFile()) return;
    const QString file = QFileInfo(resolved.toLocalFile()).canonicalFilePath();
    const QString root = QFileInfo(m_library).canonicalFilePath();
    if (file.isEmpty() || root.isEmpty() || !file.startsWith(root + '/')) {
        m_error = "链接目标不存在或不在当前笔记库中"; emit changed(); return;
    }
    if (QFileInfo(file).suffix().compare("md", Qt::CaseInsensitive) == 0) {
        if (busy()) return;
        const QString relative = QDir(root).relativeFilePath(file), fragment = resolved.fragment();
        drain([this, relative, fragment] { m_loading = true; send("read", {{"path", relative}, {"fragment", fragment}}); emit changed(); });
    } else QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}
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
    if(r.contains("smartFolders"))m_smartFolders=r.value("smartFolders").toMap();
    if (r.contains("duplicateIds")) {
        m_duplicateIds.clear();
        for (const auto &v : r.value("duplicateIds").toList()) m_duplicateIds.append(v.toMap().value("id").toString());
    }
    if (r.contains("folders") && m_folders != r.value("folders").toList()) { m_folders = r.value("folders").toList(); emit foldersChanged(); }
    if (r.contains("notes") && m_notes != r.value("notes").toList()) {
        m_notes = r.value("notes").toList();
        m_noteModel.replace(m_notes);
        emit notesChanged();
    }
    if (!r.value("duplicateIds").toList().isEmpty())
        m_error = "库内存在重复笔记 ID，保存将被阻止。列表按路径区分；可将重复笔记另存为新身份副本，再整理原文件。";
    if (!r.value("unwatchedPaths").toStringList().isEmpty())
        m_error = "部分文件无法自动监听。请使用刷新检查外部变化；保存仍校验文件摘要。";
}
void NotesController::report(const QVariantMap &r) {
    const QString code = r.value("error").toString();
    if (code == "pendingRelocation") m_readOnly = true;
    m_conflict = m_conflict || code == "conflict" || code == "unknownBase";
    m_status = m_conflict ? "冲突 · 未覆盖本地输入" : r.contains("recoveryDurable") ? "保存失败 · 输入仍保留" : "操作失败";
    static const QHash<QString, QString> messages{
        {"invalidLibrary", "笔记库目录不可用"}, {"libraryLocked", "笔记库正被其他应用实例使用"},
        {"metadataCorrupt", "工作区记录损坏，原记录未覆盖"}, {"metadataUnavailable", "无法创建库管理目录"},
        {"metadataReadFailed", "无法读取库管理记录"}, {"metadataWriteFailed", "无法保存库管理记录"},
        {"readFailed", "无法读取文件"}, {"writeFailed", "文件保存失败，请检查空间和写入权限"},
        {"recoveryWriteFailed", "无法写入恢复草稿"}, {"recoveryReadFailed", "无法读取恢复草稿"},
        {"recoveryCorrupt", "恢复记录损坏"}, {"conflict", "外部文件已改变，双方版本保留"},
        {"unknownBase", "无法确认原文件版本"}, {"unsafePath", "路径不安全或包含符号链接"},
        {"duplicateId", "笔记标识重复，请另存为新身份副本"}, {"ambiguousId", "笔记标识无法唯一定位"},
        {"alreadyExists", "目标文件已存在，请换一个名称"}, {"missingFolder", "目标文件夹不存在"},
        {"notFound", "文件不存在"}, {"noLibrary", "请先选择笔记库"},
        {"pendingDrafts", "请先保存或明确处理当前恢复草稿"}, {"staleGeneration", "工作区已改变，请重试"},
        {"readOnly", "此文件暂时只读"}, {"unsupportedFormat", "文件格式无法安全编辑，请转换副本后打开"},
        {"invalidUtf8", "文件不是有效的 UTF-8 编码，请转换副本后打开"},
        {"mixedNewlines", "文件混用换行格式，请在其他编辑器中统一副本的换行"},
        {"unsupportedSeparator", "文件包含不支持的分隔符，请转换副本后打开"},
        {"createFolderFailed", "创建文件夹失败"}, {"moveFailed", "移动文件失败"},
        {"discardFailed", "未能移除恢复记录"}, {"restoreFailed", "恢复文件失败"},
        {"trashChanged", "回收记录已改变，请刷新"}, {"scanFailed", "扫描笔记库失败"},
        {"folderNotEmpty", "此操作仅支持空文件夹，请先移动其中内容"},
        {"linkNotFound", "链接目标不存在"}, {"ambiguousLink", "存在多个同名笔记，请用带目录的链接"},
        {"historyWriteFailed", "历史版本写入失败，请检查笔记库空间和权限"}, {"invalidProperty", "属性名称无效或属于只读身份字段"},
        {"pendingRelocation", "有未完成的移动操作，请在更多菜单恢复移动操作"},
        {"unsupportedLinks", "存在暂不支持或歧义的引用，未移动；请先整理引用"},
        {"relativeLinks", "包含链接引用，暂不能安全跨文件夹移动；原文件保留"},
        {"invalidBatch", "一次请选择 1 至 200 篇笔记"},
        {"attachmentTooLarge", "单个附件最大 32 MiB，请检查文件大小或源文件权限"},
        {"invalidAttachment", "附件数据无效，请重新选择或粘贴"},
        {"attachmentWriteFailed", "附件写入失败，请检查笔记库空间与权限"},
        {"attachmentInUse", "附件仍被笔记引用，不能回收"},
        {"attachmentChanged", "附件回收数据发生变化，已阻止恢复"},
        {"unknownOperation", "此操作暂不受支持"}
    };
    m_error = messages.value(code, "操作未完成，请检查文件和目录权限") + "。当前编辑输入仍保留；不支持的格式不会覆盖原文。";
    if (r.contains("recoveryDurable")) m_error += r.value("recoveryDurable").toBool()
        ? "恢复草稿已写入磁盘。" : "恢复草稿未能写入磁盘，请保持窗口打开并另存或重试。";
    m_error += "可重试、另存副本或打开恢复中心。";
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
    if (op == "createWiki" || op == "tagBatch" || op == "fromTemplate" || op == "dailyNote" || op == "setProperty" || op == "historyVersion" || op == "trashBatch" || op == "batch" || op == "rename" || op == "renameFolder" || op == "removeFolder" || op == "trashFolder" || op == "restoreFolder") m_organizing = false;
    if ((documentOp && !p.args.value("recoveryCopy").toBool()) || op == "selectLibrary") m_loading = false;
    if (!r.value("ok").toBool()) {
        if (op == "batch" || op == "trashBatch" || op == "tagBatch") {
            QVariantList failed;
            for (const auto &value : p.args.value("items").toList()) failed.append(QVariantMap{{"path", value.toMap().value("path")}, {"ok", false}, {"error", r.value("error")}});
            acceptBatch({{"action", p.args.value("action")}, {"results", failed}, {"succeeded", 0}, {"failed", failed.size()}}); return;
        }
        if (op == "read") m_historyTarget = -1;
        if (op == "renameFolder" || op == "removeFolder") { if (r.value("error") == "pendingRelocation") m_readOnly = true; m_status = organizerError(r.value("error").toString()); emit changed(); return; }
        if (op == "importAttachment") emit attachmentReady(p.args.value("request").toString(), p.args.value("webEpoch").toInt(), r);
        report(r); if (m_restoreWorkspace) { m_restoreWorkspace = false; m_error.prepend("上次工作区无法恢复，请重新选择笔记库。"); } m_transition = false; m_afterSave = {}; emit changed(); return;
    }
    if(QStringList{"knowledge","history","historyVersion","resolveWiki","createWiki"}.contains(op) && (p.epoch!=m_epoch||p.path!=m_path))return;
    if(op=="templates"||op=="templateSave"||op=="templateDelete"){m_templates=r.value("templates").toMap();emit changed();return;}
    if(op=="fromTemplate"||op=="dailyNote"){load(r.value("path").toString());refresh();return;}
    if(op=="knowledge"){m_knowledge=r;m_smartFolders=r.value("smartFolders").toMap();emit changed();return;}
    if(op=="linkCandidates"){if(p.epoch!=m_epoch)return;m_linkCandidates=r.value("items").toList();emit wikiCandidatesReady(p.args.value("request").toInt(),m_linkCandidates);emit changed();return;}
    if(op=="tagPlan"){m_tagPlan=r;emit changed();emit tagRenameReady();return;}
    if(op=="resolveWiki"&&r.value("missing").toBool()){m_missingLink=r;emit changed();emit missingLinkRequested();return;}
    if(op=="createWiki"){m_missingLink.clear();m_loading=true;send("read",{{"path",r.value("path")},{"fragment",r.value("fragment")}});refresh();emit changed();return;}
    if(op=="resolveWiki"){const QString target=r.value("path").toString(),fragment=r.value("fragment").toString();drain([this,target,fragment]{m_loading=true;send("read",{{"path",target},{"fragment",fragment}});emit changed();});return;}
    if(op=="history"){m_versions=r;m_version.clear();emit changed();return;}
    if(op=="historyVersion"){m_version=r;if(p.args.value("restore").toBool()){m_session->execute("replaceSource:"+r.value("text").toString());m_status="历史内容已载入，可撤销；正在自动保存";}emit changed();return;}
    if(op=="setProperty"){load(m_path);refresh();return;}
    if(op=="smartSave"||op=="smartDelete"){m_smartFolders=r.value("smartFolders").toMap();emit changed();return;}
    if(op=="reindex"){applyListing(r);refresh();inspectKnowledge();m_status="索引已重建";emit changed();return;}
    if (op == "trashFolder" || op == "restoreFolder") {
        if (op == "trashFolder" && m_path.startsWith(p.args.value("path").toString() + '/')) {
            ++m_epoch; m_path.clear(); m_selectedId.clear(); m_base.clear(); m_revision=m_savedRevision=0;
            m_applying=true;m_session->loadPrototype({});m_applying=false;m_readOnly=false;m_conflict=false;
        }
        m_filter="all";++m_searchSerial;refresh();inspectRecovery();m_status="文件夹整理完成";emit changed();return;
    }
    if (op == "recoverMove") { m_error.clear(); m_status = "移动操作已回退"; refresh(); inspectRecovery(); if (!m_path.isEmpty()) load(m_path); emit changed(); return; }
    if (op == "batch" || op == "trashBatch" || op == "tagBatch") { acceptBatch(r); return; }
    if (op == "renameFolder" || op == "removeFolder") {
        if (m_filter == "folder:" + p.args.value("path").toString()) m_filter = op == "removeFolder" ? "all" : "folder:" + r.value("newPath").toString();
        ++m_searchSerial; m_status = op == "removeFolder" ? "空文件夹已删除" : "文件夹已重命名";
        if (op == "renameFolder") {
            const QString old = p.args.value("path").toString(), next = r.value("newPath").toString();
            relocateHistory(old, next);
            if (m_path.startsWith(old + '/')) load(next + m_path.mid(old.size()));
            else if (!m_path.isEmpty()) load(m_path);
        }
        refresh(); emit changed(); return;
    }
    if (op == "importAttachment") {
        emit attachmentReady(p.args.value("request").toString(), p.args.value("webEpoch").toInt(), r);
        if (p.epoch == m_epoch) m_status = "附件已导入笔记库";
        emit changed(); return;
    }
    if (op == "attachments") { m_attachments = r; emit changed(); return; }
    if (op == "quarantineAttachment" || op == "restoreAttachment") {
        m_status = op == "restoreAttachment" ? "附件已恢复" : "附件已移至回收区，可在附件管理中恢复";
        send("attachments"); emit changed(); return;
    }
    if (op == "recoverCommit" || (op == "saveAs" && p.args.value("recoveryCopy").toBool())) {
        m_status = "恢复副本已创建：" + r.value("path").toString(); refresh(); inspectRecovery(); emit changed(); return;
    }
    if (op == "selectLibrary") {
        m_missingLink.clear();m_tagPlan.clear();m_templates.clear();m_knowledge.clear();m_versions.clear();m_version.clear();m_smartFolders.clear();m_linkCandidates.clear();
        m_recovery.clear(); m_attachments.clear(); m_batchReport.clear();
        m_library = r.value("library").toString(); m_history.clear(); m_historyIndex = -1; m_historyTarget = -1; m_generation = r.value("generation").toULongLong();
        ++m_epoch; m_path.clear(); m_selectedId.clear(); m_base.clear(); m_revision = m_savedRevision = 0;
        m_error.clear(); m_conflict = false; m_query.clear(); m_filter = "all";
        m_applying = true; m_session->loadPrototype({}); m_applying = false;
        applyListing(r); m_status = "请选择笔记"; setPreference("lastLibrary", m_library);
        // Startup needs draft locks and recovery metadata, not every retained
        // version's full body. Opening recovery explicitly loads those previews.
        send("recovery", {{"includeCommitText", false}}); refresh();
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
            if (n.value("path").toString() == m_path && n.value("baseHash").toString() != m_base && !m_saving && !busy()) {
                if (dirty() || m_session->composing()) { m_conflict = true; m_status = "外部修改冲突"; send("checkpoint", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}}); }
                else load(m_path);
                break;
            }
        }
    } else if (op == "save") {
        const auto position = QSettings().value(positionKey());
        m_base = r.value("baseHash").toString();
        auto listing = r.value("listing").toMap();
        // A save returns a raw disk scan, not the filtered, sorted search view.
        // Publish note rows once, from the following canonical search response.
        const auto savedNotes = listing.take("notes").toList();
        applyListing(listing);
        for (const auto &v : savedNotes) if (v.toMap().value("path").toString() == m_path) m_selectedId = v.toMap().value("id").toString();
        QSettings().setValue(positionKey(), position);
        recordActiveDocument();
        m_savedRevision = p.revision; // Never setPlainText here: preserve IME, cursor and undo.
        m_status = dirty() ? "已修改 · 正在保存后续输入" : "已保存"; m_error.clear();
        if (dirty()) { if (!m_session->composing()) save(); } else { refresh(); send("recovery", {{"includeCommitText", m_recoveryVisible}}); advance(); }
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
        const QString reason = r.value("readOnlyReason").toString();
        const QHash<QString, QString> readOnlyMessages{
            {"invalidUtf8", "文件含无效 UTF-8 字节；请在其他编辑器中将副本转换为 UTF-8"},
            {"mixedNewlines", "文件混用了换行格式或含孤立回车；请统一副本的换行格式"},
            {"unsupportedSeparator", "文件含不支持的 Unicode 行或段分隔符；请将副本转换为普通换行"}
        };
        m_status = m_readOnly ? "只读 · 原文未被覆盖。" + readOnlyMessages.value(reason, "文件格式不能安全编辑；请使用支持该格式的编辑器转换副本后打开") : m_conflict ? "恢复草稿与磁盘不同 · 请另存" : dirty() ? "已恢复草稿 · 待保存" : "已保存";
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
        m_missingLink.clear();m_versions.clear();m_version.clear();inspectKnowledge();
        recordActiveDocument();
        const auto position = QSettings().value(positionKey()).toMap();
        if (m_historyTarget >= 0) { m_historyIndex = m_historyTarget; m_historyTarget = -1; }
        else if (m_historyIndex < 0 || m_history.value(m_historyIndex) != m_path) {
            while (m_history.size() > m_historyIndex + 1) m_history.removeLast();
            m_history.append(m_path); m_historyIndex = m_history.size() - 1;
        }
        int match = p.args.value("match", -1).toInt();
        m_highlightQuery=p.args.value("matchQuery",m_query).toString();
        const QString fragment = p.args.value("fragment").toString();
        if (!fragment.isEmpty()) {
            QRegularExpression heading("(?m)^ {0,3}#{1,6} +(.+?) *#* *$");
            auto hits = heading.globalMatch(m_session->source());
            while (hits.hasNext()) { auto hit = hits.next(); QString title = hit.captured(1), slug = title.toLower();
                slug.remove(QRegularExpression("[^\\p{L}\\p{N}_ -]")); slug.replace(' ', '-');
                if (title == fragment || slug == fragment) { match = hit.capturedStart(); break; }
            }
            if (match < 0) m_status = "笔记已打开，未找到链接中的标题";
        }
        emit documentLoaded(match >= 0 ? match : position.value("cursor", op == "create" ? m_session->source().size() : 0).toInt(), match >= 0 ? -1 : position.value("scroll", 0).toDouble(), match >= 0 ? m_highlightQuery.size() : 0);
        if (!fragment.isEmpty()) emit documentAnchorRequested(fragment);
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
        recordActiveDocument();
        emit documentLoaded(0, 0, 0);
        m_status = "副本已保存 · 原恢复记录仍保留"; inspectRecovery(); refresh();
    } else if (op == "trash") {
        ++m_epoch; m_path.clear(); m_selectedId.clear(); m_revision = m_savedRevision = 0;
        m_applying = true; m_session->loadPrototype({}); m_applying = false;
        m_status = "已移到最近删除"; inspectRecovery(); refresh();
    } else if (op == "rename") { relocateHistory(p.path, r.value("newPath").toString()); load(r.value("newPath").toString()); refresh(); }
    else if (op == "recovery") {
        // A background summary may have been queued just before the dialog
        // opened. It must not replace full previews in an already visible UI.
        if (m_recoveryVisible && !p.args.value("includeCommitText", true).toBool()) { inspectRecovery(); return; }
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
        if (m_restoreWorkspace) {
            m_restoreWorkspace = false;
            const auto last = preference("lastNote").toMap();
            if (last.value("library").toString() == m_library)
                for (const auto &v : m_notes) {
                    const auto n = v.toMap();
                    if (n.value("id") == last.value("id") && (!m_duplicateIds.contains(n.value("id").toString()) || n.value("path") == last.value("path"))) { load(n.value("path").toString()); break; }
                }
        }
    }
    else if (op == "checkpoint") { if (m_conflict) m_error = "外部版本已改变；本地恢复草稿已保存。请另存副本，或在恢复中心保留双方版本。"; }
    else if (op == "favorite" || op == "createFolder" || op == "restore" || op == "discardDraft") { if (op == "favorite" && p.path == m_path) m_favorite = p.args.value("favorite").toBool(); refresh(); inspectRecovery(); }
    emit changed();
}
void NotesController::externalChange(const QVariantMap &r) {
    if (r.value("library").toString() != m_library || r.value("generation").toULongLong() != m_generation) return;
    if (r.contains("scanError")) { report(r.value("scanError").toMap()); emit changed(); return; }
    refresh(true); // The watcher has already scanned and updated the index.
    for (const auto &v : r.value("changes").toList()) {
        const auto change = v.toMap();
        if (change.value("path").toString() != m_path || change.value("hash").toString() == m_base) continue;
        if (m_saving || m_organizing) continue; // save itself performs the authoritative baseline comparison.
        if (dirty() || m_session->composing() || change.value("removed").toBool()) {
            if (change.value("removed").toBool() && !dirty()) ++m_revision;
            m_conflict = true; m_autosave.stop(); m_status = "外部修改冲突";
            send("checkpoint", {{"path", m_path}, {"text", m_session->source()}, {"baseHash", m_base}});
        } else if (!busy()) load(m_path);
    }
    emit changed();
}

void NotesController::recoverCommit(const QString &token, const QString &version) { send("recoverCommit", {{"token", token}, {"version", version}}); }

void NotesController::recordActiveDocument() {
    setPreference("lastNote", QVariantMap{{"library", m_library}, {"id", m_selectedId}, {"path", m_path}});
}

void NotesController::navigateBack() {
    if (!canGoBack() || busy()) return;
    drain([this] { m_historyTarget = m_historyIndex - 1; load(m_history[m_historyTarget]); });
}
void NotesController::navigateForward() {
    if (!canGoForward() || busy()) return;
    drain([this] { m_historyTarget = m_historyIndex + 1; load(m_history[m_historyTarget]); });
}
void NotesController::relocateHistory(const QString &from, const QString &to) {
    for (QString &p : m_history) if (p == from || p.startsWith(from + '/')) p = to + p.mid(from.size());
}
void NotesController::recoverMove() { if (!busy() && !dirty()) send("recoverMove"); }

void NotesController::restoreFolder(const QString &token) { if (!busy()) drain([this,token]{m_organizing=true;send("restoreFolder",{{"token",token}});emit changed();}); }
