#include "NotesRepository.h"
#include "ManagedStore.h"
#include "MarkdownFile.h"
#include "SafeCommit.h"
#include "LinkPaths.h"
#include "TrashFiles.h"
#include "KnowledgeIndex.h"
#include "RepositoryTestHooks.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QMimeDatabase>
#include <QUrl>
#include <QLockFile>
#include <QSet>
#include <QScopedValueRollback>
#include <memory>
#include <algorithm>

namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString summary(QString text) {
    if (text.startsWith("---\n")) {
        const int end = text.indexOf("\n---", 3);
        if (end >= 0) text = text.mid(end + 4);
    }
    text = text.trimmed();
    if (text.startsWith("# ")) { const int newline = text.indexOf('\n'); if (newline >= 0) text = text.mid(newline + 1); }
    return text.simplified().left(150);
}
QVariantMap failure(const QString &code, const QString &detail = {})
{ return {{"ok", false}, {"error", code}, {"detail", detail}}; }
bool readBytes(const QString &path, QByteArray &bytes, QString &error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { error = f.errorString(); return false; }
    bytes = f.readAll();
    if (f.error() != QFileDevice::NoError) { error = f.errorString(); return false; }
    return true;
}
bool atomicWrite(const QString &path, const QByteArray &bytes, QString &error)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit()) {
        error = f.errorString(); return false;
    }
    return true;
}
}

class RepositoryWorker final : public QObject {
    Q_OBJECT
public:
    explicit RepositoryWorker(QObject *parent = nullptr) : QObject(parent) {}
    void execute(quint64 id, const QString &op, const QVariantMap &args, std::shared_ptr<std::atomic_bool> cancelled) {
        currentCancellation = std::move(cancelled);
        currentProgress = [this,id,op](const QVariantMap &value) { emit operationProgress(id,op,value); };
        QVariantMap result = dispatch(op, args);
        currentProgress = {}; currentCancellation.reset();
        result.insert("library", root);
        result.insert("generation", generation);
        if (managed) result.insert("entryRoot", managed->entryRoot());
        else if (failedManaged) result.insert("entryRoot", failedManaged->entryRoot());
        emit finished(id, op, result);
    }
signals:
    void finished(quint64, const QString &, const QVariantMap &);
    void operationProgress(quint64, const QString &, const QVariantMap &);
    void changed(const QVariantMap &);
private:
    std::unique_ptr<ManagedStore> managed;
    std::unique_ptr<ManagedStore> failedManaged;
    std::function<void(const QVariantMap &)> currentProgress;
    std::shared_ptr<std::atomic_bool> currentCancellation;
    bool editingTags = false;
    Knowledge::Index index;
    QVariantMap cachedListing;
    QString indexRoot;
    QString root;
    quint64 generation = 0;
    QJsonObject metadata;
    QHash<QString, MarkdownFile::Document> opened;
    QHash<QString, QVariantMap> memoryDrafts;
    QHash<QString, QString> fingerprints;
    QFileSystemWatcher *watcher = nullptr;
    QTimer *debounce = nullptr;
    std::unique_ptr<QLockFile> lock;

    // Validate every existing component, including internal paths. Never follow links.
    QString path(const QString &relative, bool internal = false) const {
        if (root.isEmpty() || relative.isEmpty() || QDir::isAbsolutePath(relative)) return {};
        const auto parts = relative.split(u'/');
        if (parts.contains(QStringLiteral("..")) || parts.contains(QStringLiteral(".")) || parts.contains(QString())) return {};
        if (!internal && (parts.contains(QStringLiteral(".nextnotes")) || parts.contains(QStringLiteral("assets")))) return {};
        QString current = root;
        if (QFileInfo(current).isSymLink() || QFileInfo(current).canonicalFilePath() != root) return {};
        for (const QString &part : parts) {
            current += u'/' + part;
            QFileInfo fi(current);
            if (fi.isSymLink()) return {};
            if (fi.exists() && fi.canonicalFilePath() != current) return {};
        }
        return current;
    }
    bool persist(QString &error) {
        const QString target = path(QStringLiteral(".nextnotes/state.json"), true);
        if (target.isEmpty()) { error = "Unsafe metadata path"; return false; }
        return atomicWrite(target, QJsonDocument(metadata).toJson(), error);
    }
    QString draftPath(const QString &relative) const {
        return path(".nextnotes/drafts/" + MarkdownFile::digest(relative.toUtf8()) + ".json", true);
    }
    bool checkpoint(const QString &relative, const QString &text, const QString &base, QString &error) {
        QVariantMap draft{{"path", relative}, {"text", text}, {"baseHash", base}, {"updatedUtc", now()}};
        memoryDrafts.insert(relative, draft);
        QString target = draftPath(relative);
        if (target.isEmpty()) { error = "Unsafe recovery path"; return false; }
        return atomicWrite(target, QJsonDocument::fromVariant(draft).toJson(), error);
    }
    bool clearDraft(const QString &relative) {
        const QString target = draftPath(relative);
        if (target.isEmpty() || (QFileInfo::exists(target) && !QFile::remove(target))) return false;
        memoryDrafts.remove(relative);
        return true;
    }
    QVariantMap documentResult(const QString &relative, const MarkdownFile::Document &d) {
        return {{"ok", true}, {"path", relative}, {"text", d.text}, {"baseHash", d.hash},
            {"id", metadata.value("entries").toObject().value(relative).toObject().value("id").toString()}, {"favorite", metadata.value("entries").toObject().value(relative).toObject().value("favorite").toBool()}, {"readOnly", !d.readOnlyReason.isEmpty()}, {"readOnlyReason", d.readOnlyReason}};
    }
    QVariantMap scan(bool notify) {
        const bool initial = !watcher;
        if(indexRoot!=root){index=Knowledge::Index();cachedListing.clear();indexRoot=root;}
        if (!notify && RepositoryTestHooks::beforeScan) RepositoryTestHooks::beforeScan(false);
        QVariantList notes, folders, duplicates;
        QHash<QString, QString> hashes;
        QHash<QString, QString> ids;
        QStringList watchPaths{root};
        QJsonObject entries = metadata.value("entries").toObject();
        bool dirty = false;
        QString error;
        // Manual recursion excludes reserved directories before descent, not after scanning them.
        QStringList pending{QString()};
        while (!pending.isEmpty()) {
            const QString folder = pending.takeLast();
            const QString folderPath = folder.isEmpty() ? root : path(folder);
            if (folderPath.isEmpty()) return failure("unsafePath", folder);
            const QDir dir(folderPath);
            for (const QFileInfo &fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
                if (fi.isSymLink() || fi.fileName() == ".nextnotes" || fi.fileName() == ".git" || fi.fileName() == "assets") continue;
                const QString rel = folder.isEmpty() ? fi.fileName() : folder + u'/' + fi.fileName();
                const QString absolute = path(rel);
                if (absolute.isEmpty()) continue;
                if (fi.isDir()) { folders.append(rel); pending.append(rel); watchPaths.append(absolute); continue; }
                if (fi.suffix().compare("md", Qt::CaseInsensitive) != 0) continue;
                QByteArray bytes;
                if (!readBytes(absolute, bytes, error)) return failure("readFailed", rel + ": " + error);
                auto doc = MarkdownFile::decode(bytes);
                const QString title=MarkdownFile::title(doc.text,fi.completeBaseName());
                index.put(rel,doc.text,doc.hash,title);
                hashes.insert(rel, doc.hash);
                watchPaths.append(absolute);
                QJsonObject entry = entries.value(rel).toObject();
                QString stableId = MarkdownFile::field(doc.text, "nextnotes_id");
                if (stableId.isEmpty()) stableId = entry.value("id").toString();
                if (stableId.isEmpty()) stableId = uuid();
                if (ids.contains(stableId)) duplicates.append(QVariantMap{{"id", stableId}, {"first", ids.value(stableId)}, {"second", rel}});
                else ids.insert(stableId, rel);
                if (entry.value("id").toString() != stableId) { entry.insert("id", stableId); dirty = true; }
                if (!entry.contains("createdUtc")) { entry.insert("createdUtc", now()); dirty = true; }
                entries.insert(rel, entry);
                notes.append(QVariantMap{{"path", rel}, {"id", stableId}, {"title", title},
                    {"summary", summary(doc.text)}, {"baseHash", doc.hash}, {"pinned", entry.value("pinned").toBool()}, {"favorite", entry.value("favorite").toBool()}, {"createdUtc", entry.value("createdUtc").toString()},
                    {"modifiedUtc", entry.value("modifiedUtc").toString()}, {"displayModifiedUtc", entry.value("savedHash").toString() == doc.hash ? entry.value("modifiedUtc").toString() : fi.lastModified().toUTC().toString(Qt::ISODateWithMs)}, {"timeSource", entry.value("savedHash").toString() == doc.hash ? "应用保存" : "文件时间"}, {"fileModifiedUtc", fi.lastModified().toUTC().toString(Qt::ISODateWithMs)}, {"readOnlyReason", doc.readOnlyReason}});
            }
        }
        for(const auto &key:index.documents.keys())if(!hashes.contains(key))index.remove(key);
        for (const QString &key : entries.keys()) {
            if (!hashes.contains(key)) { entries.remove(key); dirty = true; }
        }
        if (dirty) {
            metadata.insert("entries", entries);
            if (!persist(error)) return failure("metadataWriteFailed", error);
        }
        if (!watcher) {
            watcher = new QFileSystemWatcher(this);
            debounce = new QTimer(this); debounce->setSingleShot(true); debounce->setInterval(150);
            connect(watcher, &QFileSystemWatcher::directoryChanged, debounce, [this] { debounce->start(); });
            connect(watcher, &QFileSystemWatcher::fileChanged, debounce, [this] { debounce->start(); });
            connect(debounce, &QTimer::timeout, this, [this] {
                auto result = scan(true);
                if (!result.value("ok").toBool()) emit changed({{"library", root}, {"generation", generation}, {"scanError", result}});
            });
        }
        const auto oldPaths = watcher->directories() + watcher->files();
        const QSet<QString> previous(oldPaths.begin(),oldPaths.end()), wanted(watchPaths.begin(),watchPaths.end());
        const auto removed=(previous-wanted).values(), added=(wanted-previous).values();
        if (!removed.isEmpty()) watcher->removePaths(removed);
        const QStringList unwatchable = added.isEmpty()?QStringList():watcher->addPaths(added);
        QVariantList changes;
        for (auto it = hashes.cbegin(); it != hashes.cend(); ++it)
            if (fingerprints.value(it.key()) != it.value()) changes.append(QVariantMap{{"path", it.key()}, {"hash", it.value()}, {"removed", false}});
        for (auto it = fingerprints.cbegin(); it != fingerprints.cend(); ++it)
            if (!hashes.contains(it.key())) changes.append(QVariantMap{{"path", it.key()}, {"removed", true}});
        // Silent scans must not consume changes awaiting the watcher debounce.
        if (notify || initial) fingerprints = hashes;
        else if (!changes.isEmpty() && !debounce->isActive()) debounce->start();
        QVariantMap result{{"ok", true}, {"notes", notes}, {"folders", folders}, {"duplicateIds", duplicates}, {"unwatchedPaths", unwatchable},{"smartFolders",metadata.value("smartFolders").toObject().toVariantMap()}};
        cachedListing=result;
        if (notify && (!changes.isEmpty() || !unwatchable.isEmpty())) emit changed({{"library", root}, {"generation", generation}, {"changes", changes}, {"listing", result}});
        return result;
    }
    bool rememberVersion(const QString &,const QString &,const QByteArray &,QString &);
    QVariantMap knowledge(const QString &,const QVariantMap &);
    QVariantMap relocate(const QString &, const QString &, const QString & = {});
    QVariantMap dispatch(const QString &op, const QVariantMap &a);
};

#include "Relocate.inc"
#include "Knowledge.inc"

QVariantMap RepositoryWorker::dispatch(const QString &op, const QVariantMap &a)
{
    QString error;
    if (op == "selectLibrary" && a.value("managed").toBool()) {
        if ((managed && managed->hasPendingMemoryDrafts()) || !memoryDrafts.isEmpty())
            return failure("pendingDrafts", "Persist or discard in-memory drafts before switching libraries.");
        const auto selected=QDir(a.value("path").toString()).absolutePath();
        if (managed && (selected==managed->entryRoot() || selected==managed->directory()))
            return managed->dispatch("list",{},currentProgress,currentCancellation);
        // Retrying a failed selection releases only that failed context's leases.
        failedManaged.reset();
        try {
            auto next = std::make_unique<ManagedStore>(a.value("path").toString(),true);
            auto result = next->dispatch("list", {});
            if (!result.value("ok").toBool()) {
                // A failed attempt to switch never replaces an already open
                // library or arms a recovery operation against that library.
                if (!root.isEmpty()) result.insert("recoveryAvailable",false);
                else failedManaged=std::move(next);
                return result;
            }
            managed = std::move(next); root = managed->directory(); ++generation;
            delete watcher; watcher = nullptr; delete debounce; debounce = nullptr; lock.reset();
            opened.clear(); fingerprints.clear(); metadata={};
            return result;
        } catch (const ManagedStoreError &error) {
            auto result=failure(error.code,error.detail);result.insert("recoveryAvailable",false);return result;
        }
    }
    if (a.contains("generation") && op != "selectLibrary" && a.value("generation").toULongLong() != generation) return failure("staleGeneration");
    if (op == "restoreBackup" && failedManaged && !managed) {
        auto result=failedManaged->dispatch(op,a,currentProgress,currentCancellation);
        if(result.value("ok").toBool()) {
            managed=std::move(failedManaged); root=managed->directory(); ++generation;
            delete watcher; watcher=nullptr; delete debounce; debounce=nullptr; lock.reset();
        }
        return result;
    }
    if (managed && op != "selectLibrary") {
        auto result=managed->dispatch(op,a,currentProgress,currentCancellation);
        if(op=="restoreBackup" && result.value("ok").toBool()) {root=managed->directory();++generation;}
        return result;
    }
    if (op == "selectLibrary") {
        if (!memoryDrafts.isEmpty() || (managed && managed->hasPendingMemoryDrafts())) return failure("pendingDrafts", "Save or explicitly discard in-memory drafts before switching libraries.");
        QFileInfo selected(a.value("path").toString());
        if (!selected.isDir() || selected.isSymLink()) return failure("invalidLibrary");
        QString selectedRoot = selected.canonicalFilePath();
        if (selectedRoot == root) return scan(false);
        // Reject symlink ancestors as well as a symlink selected directory.
        if (QDir::cleanPath(selected.absoluteFilePath()) != selectedRoot) return failure("unsafePath");
        const QString oldRoot = root;
        root = selectedRoot;
        for (const auto &dir : {QString(".nextnotes"), QString(".nextnotes/drafts"), QString(".nextnotes/trash"), QString(".nextnotes/commits")}) {
            QString absolute = path(dir, true);
            if (absolute.isEmpty() || !QDir().mkpath(absolute)) { root = oldRoot; return failure("metadataUnavailable", dir); }
        }
        const QString lockPath = path(".nextnotes/repository.lock", true);
        if (lockPath.isEmpty()) { root = oldRoot; return failure("unsafePath"); }
        auto newLock = std::make_unique<QLockFile>(lockPath);
        newLock->setStaleLockTime(0);
        if (!newLock->tryLock()) { root = oldRoot; return failure("libraryLocked"); }
        QString statePath = path(".nextnotes/state.json", true);
        if (statePath.isEmpty()) { root = oldRoot; return failure("unsafePath"); }
        QJsonObject state;
        if (QFileInfo::exists(statePath)) {
            QByteArray bytes;
            if (!readBytes(statePath, bytes, error)) { root = oldRoot; return failure("metadataReadFailed", error); }
            QJsonParseError parseError;
            auto json = QJsonDocument::fromJson(bytes, &parseError);
            if (parseError.error != QJsonParseError::NoError || !json.isObject() || json.object().value("version").toInt() != 1
                || !json.object().value("entries").isObject() || !json.object().value("trash").isObject()) {
                root = oldRoot; return failure("metadataCorrupt", "Metadata was not overwritten; repair or restore state.json before reopening.");
            }
            state = json.object();
        } else state = {{"version", 1}, {"entries", QJsonObject()}, {"trash", QJsonObject()}};
        const auto oldMetadata = metadata;
        const auto oldOpened = opened;
        const auto oldFingerprints = fingerprints;
        auto *oldWatcher = watcher;
        auto *oldDebounce = debounce;
        watcher = nullptr; debounce = nullptr;
        metadata = state; opened.clear(); fingerprints.clear();
        QVariantMap prepared = persist(error) ? scan(false) : failure("metadataWriteFailed", error);
        if (!prepared.value("ok").toBool()) {
            delete watcher; delete debounce;
            watcher = oldWatcher; debounce = oldDebounce;
            root = oldRoot; metadata = oldMetadata; opened = oldOpened; fingerprints = oldFingerprints;
            return prepared;
        }
        delete oldWatcher; delete oldDebounce;
        lock = std::move(newLock);
        managed.reset(); failedManaged.reset();
        ++generation;
        return prepared;
    }
    if (root.isEmpty()) return failure("noLibrary");
    if (a.contains("generation") && a.value("generation").toULongLong() != generation) return failure("staleGeneration");
    if (QFileInfo(root).isSymLink() || QFileInfo(root).canonicalFilePath() != root) return failure("unsafePath");
    if (op == "recoverMove") {
        const QString journal=path(".nextnotes/move.json",true); QByteArray bytes;
        if(!readBytes(journal,bytes,error))return failure("readFailed");
        const auto record=QJsonDocument::fromJson(bytes).object();
        const QString from=record.value("from").toString(), to=record.value("to").toString();
        if(path(from).isEmpty()||path(to).isEmpty()||!record.value("entries").isArray()||!record.value("metadata").isObject())return failure("recoveryCorrupt");
        const bool moved=!QFileInfo::exists(path(from))&&QFileInfo::exists(path(to));
        if(!moved && (!QFileInfo::exists(path(from))||QFileInfo::exists(path(to))))return failure("conflict");
        auto currentPath=[&](QString p){return moved?(p==from?to:p.startsWith(from+'/')?to+p.mid(from.size()):p):p;};
        for(auto v:record.value("entries").toArray()) {
            auto e=v.toObject(); QString p=currentPath(e.value("path").toString()); QByteArray disk;
            if(path(p).isEmpty()||!readBytes(path(p),disk,error))return failure("recoveryCorrupt");
            if(disk!=QByteArray::fromBase64(e.value("before").toString().toLatin1())&&disk!=QByteArray::fromBase64(e.value("after").toString().toLatin1()))return failure("conflict");
        }
        for(auto v:record.value("entries").toArray()) {
            auto e=v.toObject();QString p=currentPath(e.value("path").toString());QByteArray before=QByteArray::fromBase64(e.value("before").toString().toLatin1()),disk;
            if(!readBytes(path(p),disk,error))return failure("readFailed");
            if(disk==before)continue;
            QString retained; QJsonObject rec{{"path",p},{"baseHash",MarkdownFile::digest(disk)},{"newHash",MarkdownFile::digest(before)}};
            if(!SafeCommit::commit(path(p),before,path(".nextnotes/commits/"+uuid()+".intent",true),QJsonDocument(rec).toJson(QJsonDocument::Compact),true,retained,error))return failure("writeFailed");
            QByteArray displaced;if(!readBytes(retained,displaced,error)||displaced!=disk){
#ifdef Q_OS_LINUX
                ::syscall(SYS_renameat2,AT_FDCWD,QFile::encodeName(retained).constData(),AT_FDCWD,QFile::encodeName(path(p)).constData(),2U);
#endif
                return failure("conflict");
            }
        }
#ifdef Q_OS_LINUX
        if(moved && ::syscall(SYS_renameat2,AT_FDCWD,QFile::encodeName(path(to)).constData(),AT_FDCWD,QFile::encodeName(path(from)).constData(),1U)!=0)return failure("moveFailed");
#else
        if(moved)return failure("moveFailed");
#endif
        metadata=record.value("metadata").toObject();if(!persist(error))return failure("metadataWriteFailed");
        if(!QFile::remove(journal))return failure("writeFailed");
        opened.clear();fingerprints.clear();return scan(false);
    }
    if(QFileInfo::exists(path(".nextnotes/move.json",true)) && !QStringList{"read","list","search","recovery","recoveryVersion","checkpoint"}.contains(op))return failure("pendingRelocation");
    if(QStringList{"knowledge","linkCandidates","resolveWiki","history","historyVersion","setProperty","smartSave","smartDelete","reindex","templates","templateSave","templateDelete","fromTemplate","dailyNote","createWiki","tagPlan","tagBatch"}.contains(op))return knowledge(op,a);
    if (op == "recoveryVersion" || op == "recoverCommit") {
        const QString token = a.value("token").toString();
        if (token.contains('/') || !token.endsWith(".intent")) return failure("recoveryCorrupt", "恢复记录名称无效");
        const QString journal = path(".nextnotes/commits/" + token, true);
        QByteArray content;
        if (journal.isEmpty() || !readBytes(journal, content, error)) return failure("recoveryCorrupt", "恢复记录无法读取");
        const int split = content.indexOf('\n');
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(content.left(split), &parse);
        if (split < 0 || parse.error != QJsonParseError::NoError || !json.isObject()) return failure("recoveryCorrupt", "恢复记录损坏");
        const auto record = json.object();
        const QString relative = record.value("path").toString();
        const QString target = path(relative);
        const QString staging = QString::fromUtf8(content.mid(split + 1));
        const QString stagingRelative = QDir(root).relativeFilePath(staging);
        const QString checked = path(stagingRelative, true);
        const QString base = record.value("baseHash").toString(), next = record.value("newHash").toString();
        const QRegularExpression hashPattern("^[0-9a-f]{64}$");
        if (target.isEmpty() || checked.isEmpty() || checked != staging || QFileInfo(staging).absolutePath() != QFileInfo(target).absolutePath()
            || !QFileInfo(staging).fileName().startsWith(".nextnotes-") || !staging.endsWith(".swap")
            || !hashPattern.match(next).hasMatch() || (!base.isEmpty() && !hashPattern.match(base).hasMatch()))
            return failure("recoveryCorrupt", "恢复路径或版本校验失败");
        const QString version = a.value("version").toString();
        if (version != "before" && version != "pending") return failure("recoveryCorrupt", "恢复版本无效");
        const QString expected = version == "before" ? base : next;
        QByteArray bytes; bool found = false;
        for (const auto &candidate : {target, staging}) {
            QByteArray b;
            if (readBytes(candidate, b, error) && !expected.isEmpty() && MarkdownFile::digest(b) == expected) { bytes = b; found = true; break; }
        }
        if (!found) return failure("recoveryCorrupt", "该版本无法确认或已被外部修改；原记录保留");
        const auto document = MarkdownFile::decode(bytes);
        if (op == "recoverCommit") {
            if (!document.readOnlyReason.isEmpty()) return failure("recoveryCorrupt", "此版本格式不能安全生成副本；原始版本保留");
            return dispatch("saveAs", {{"path", "恢复-" + uuid() + ".md"}, {"text", document.text}, {"newIdentity", true}});
        }
        return {{"ok", true}, {"text", document.text}, {"bytes", bytes}, {"version", version}, {"hash", expected}};
    }
    if (op == "batch") {
        const QString action = a.value("action").toString(), folder = a.value("folder").toString();
        auto items = a.value("items").toList();
        if (!QStringList{"trash", "move", "duplicate", "pin", "unpin", "favorite", "unfavorite"}.contains(action) || items.isEmpty() || items.size() > 200) return failure("invalidBatch");
        if (action == "move" && !folder.isEmpty() && (path(folder).isEmpty() || !QFileInfo(path(folder)).isDir())) return failure("unsafePath");
        QVariantList results; QSet<QString> seen; int succeeded = 0;
        for (int itemIndex=0;itemIndex<items.size();++itemIndex) {
            const auto value=items[itemIndex];
            const auto item = value.toMap(); const QString rel = item.value("path").toString(), absolute = path(rel);
            QVariantMap result; QByteArray bytes;
            if (seen.contains(rel)) result = failure("duplicateRequest");
            else if (absolute.isEmpty() || QFileInfo(rel).suffix().compare("md", Qt::CaseInsensitive)) result = failure("unsafePath");
            else if (!readBytes(absolute, bytes, error)) result = failure("readFailed", error);
            else if (item.value("baseHash").toString().isEmpty() || item.value("baseHash").toString() != MarkdownFile::digest(bytes)) result = failure("conflict");
            else if (action == "move") {
                const QString target = (folder.isEmpty() ? QString() : folder + '/') + QFileInfo(rel).fileName();
                if (target == rel) result = {{"ok", true}, {"newPath", rel}, {"unchanged", true}};
                else result = dispatch("rename", {{"path", rel}, {"target", target}, {"baseHash", item.value("baseHash")}});
            } else if (action == "duplicate") {
                const QFileInfo source(rel); QString target;
                for (int n = 1; n <= 10000; ++n) {
                    const QString name = source.completeBaseName() + " 副本" + (n == 1 ? QString() : " " + QString::number(n)) + ".md";
                    target = source.path() == "." ? name : source.path() + '/' + name;
                    if (!QFileInfo::exists(path(target))) break;
                }
                result = dispatch("saveAs", {{"path", target}, {"text", MarkdownFile::decode(bytes).text}, {"newIdentity", true}});
            } else if (action == "trash") result = dispatch("trash", {{"path", rel}, {"baseHash", item.value("baseHash")}});
            else if (action == "pin" || action == "unpin") result = dispatch("pin", {{"path", rel}, {"pinned", action == "pin"}});
            else result = dispatch("favorite", {{"path", rel}, {"favorite", action == "favorite"}});
            for (int next=itemIndex+1;next<items.size();++next) {
                auto pending=items[next].toMap();const auto updated=result.value("rewrittenHashes").toMap().value(pending.value("path").toString()).toMap();
                if(!updated.isEmpty() && pending.value("baseHash")==updated.value("before")){pending.insert("baseHash",updated.value("after"));items[next]=pending;}
            }
            seen.insert(rel);
            // Return only per-item outcomes, never entire scan listings/documents.
            QVariantMap outcome{{"path", rel}, {"ok", result.value("ok").toBool()}};
            for (const auto &key : {"error", "detail", "newPath", "trashToken", "unchanged"}) if (result.contains(key)) outcome.insert(key, result.value(key));
            if (action == "duplicate" && result.value("ok").toBool()) outcome.insert("newPath", result.value("path"));
            if (outcome.value("ok").toBool()) ++succeeded;
            results.append(outcome);
        }
        return {{"ok", true}, {"action", action}, {"results", results}, {"succeeded", succeeded}, {"failed", results.size() - succeeded}};
    }
    if (op == "trashFolder" || op == "restoreFolder") {
        const bool restore=op=="restoreFolder";const QString token=restore?a.value("token").toString():uuid();
        auto trash=metadata.value("folderTrash").toObject();auto record=trash.value(token).toObject();
        if(restore&&record.value("purging").toBool())return failure("purgeIncomplete");
        const QString relative=restore?record.value("source").toString():a.value("path").toString();
        const QString stored=".nextnotes/folder-trash/"+token;
        if(token.contains('/')||token.contains("..")||path(relative).isEmpty())return failure("unsafePath");
        const QString source=path(restore?stored:relative,restore),target=path(restore?relative:stored,!restore);
        if(source.isEmpty()||target.isEmpty()||!QFileInfo(source).isDir()||QFileInfo::exists(target))return failure("alreadyExists");
        if(!restore) {
            if(!QDir().mkpath(path(".nextnotes/folder-trash",true)))return failure("writeFailed");
            QJsonObject savedEntries;
            QDirIterator it(source,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
            while(it.hasNext()){it.next();if(it.fileInfo().isSymLink())return failure("unsafePath");}
            const auto entries=metadata.value("entries").toObject();
            for(auto it=entries.begin();it!=entries.end();++it)if(it.key().startsWith(relative+'/')) {
                if(memoryDrafts.contains(it.key())||QFileInfo::exists(draftPath(it.key())))return failure("pendingDrafts");
                savedEntries.insert(it.key(),it.value());
            }
            record={{"source",relative},{"entries",savedEntries},{"deletedUtc",now()}};
            trash.insert(token,record);metadata.insert("folderTrash",trash);if(!persist(error))return failure("metadataWriteFailed");
        }
#ifdef Q_OS_LINUX
        if(::syscall(SYS_renameat2,AT_FDCWD,QFile::encodeName(source).constData(),AT_FDCWD,QFile::encodeName(target).constData(),1U)!=0)return failure("moveFailed");
#else
        return failure("moveFailed");
#endif
        if(restore) {
            auto entries=metadata.value("entries").toObject();const auto saved=record.value("entries").toObject();
            for(auto it=saved.begin();it!=saved.end();++it)entries.insert(it.key(),it.value());
            metadata.insert("entries",entries);trash.remove(token);metadata.insert("folderTrash",trash);
            if(!persist(error))return failure("metadataWriteFailed");
        }
        opened.clear();fingerprints.clear();auto result=scan(false);result.insert("trashToken",token);return result;
    }
    if (op == "renameFolder" || op == "removeFolder") {
        const QString relative = a.value("path").toString(), source = path(relative);
        if (source.isEmpty() || !QFileInfo(source).isDir()) return failure("unsafePath");
        if (op == "renameFolder") return relocate(relative, a.value("target").toString());
        if (!QDir(source).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) return failure("folderNotEmpty");
        return QDir().rmdir(source) ? QVariantMap{{"ok", true}} : failure("moveFailed");
    }

    if (op == "list") return scan(false);
    if (op == "search") {
        auto listing = a.value("indexed").toBool()&&indexRoot==root&&!cachedListing.isEmpty()?cachedListing:scan(false);
        if (!listing.value("ok").toBool()) return listing;
        QString query = a.value("query").toString();const QString filter = a.value("filter").toString();
        QStringList requiredTags;QVariantMap requiredProps;QString folder;bool favorite=false;
        QRegularExpression terms(QString::fromUtf8(R"rx((?:^|\s)(tag|folder|is|prop):("(?:[^"\\]|\\.)*"|'[^']*'|[^\s]+))rx"));auto termsIt=terms.globalMatch(query);QList<QPair<int,int>> spans;
        while(termsIt.hasNext()){auto m=termsIt.next();QString key=m.captured(1),value=Knowledge::scalar(m.captured(2));if(key=="tag")requiredTags.append(value);if(key=="folder")folder=value;if(key=="is"&&value=="favorite")favorite=true;if(key=="prop"&&value.contains('='))requiredProps.insert(value.section('=',0,0),value.section('=',1));spans.append({int(m.capturedStart()),int(m.capturedLength())});}
        for(auto it=spans.crbegin();it!=spans.crend();++it)query.remove(it->first,it->second);query=query.trimmed();
        const auto candidates=index.candidates(query);
        QVariantList matches;
        for (const auto &value : listing.value("notes").toList()) {
            auto note = value.toMap();
            const QString relative = note.value("path").toString();
            if (filter == "favorite" && !note.value("favorite").toBool()) continue;
            if (filter.startsWith("folder:") && !relative.startsWith(filter.mid(7) + '/')) continue;
            if(!candidates.contains(relative))continue;
            const auto doc=index.documents.value(relative);const QString text=doc.text;
            const auto tags=doc.props.value("tags").toStringList();bool accepted=true;for(const auto &tag:requiredTags)if(!tags.contains(tag))accepted=false;
            for(auto it=requiredProps.begin();it!=requiredProps.end();++it)if(doc.props.value(it.key()).toString()!=it.value().toString())accepted=false;
            if(!accepted||(!folder.isEmpty()&&!relative.startsWith(folder+'/'))||(favorite&&!note.value("favorite").toBool()))continue;
            note.insert("tags",tags);note.insert("properties",doc.props);
            const int position = query.isEmpty() ? -1 : text.indexOf(query, 0, Qt::CaseInsensitive);
            if (!query.isEmpty() && position < 0 && !note.value("title").toString().contains(query, Qt::CaseInsensitive)) continue;
            note.insert("match", position);note.insert("matchText",query);
            if (position >= 0) note.insert("summary", text.mid(qMax(0, position - 32), 150).simplified());
            matches.append(note);
        }
        const QString sort = a.value("sort", "modified").toString();
        const bool ascending = a.value("ascending", false).toBool();
        std::stable_sort(matches.begin(), matches.end(), [sort, ascending](const QVariant &left, const QVariant &right) {
            const auto l = left.toMap(), r = right.toMap();
            if (l.value("pinned").toBool() != r.value("pinned").toBool()) return l.value("pinned").toBool();
            const QString field = sort == "title" ? "title" : sort == "created" ? "createdUtc" : "displayModifiedUtc";
            const int compared = sort == "title" ? QString::localeAwareCompare(l.value(field).toString(), r.value(field).toString()) : QString::compare(l.value(field).toString(), r.value(field).toString());
            if (compared == 0) return l.value("path").toString() < r.value("path").toString();
            return ascending ? compared < 0 : compared > 0;
        });
        listing.insert("smartFolders",metadata.value("smartFolders").toObject().toVariantMap());
        listing.insert("allNotes", listing.value("notes"));
        listing.insert("notes", matches);listing.insert("indexDocuments",index.documents.size());listing.insert("indexUpdates",index.updates);
        return listing;
    }
    if (op == "importAttachment") {
        const QString note = a.value("path").toString();
        if (path(note).isEmpty()) return failure("unsafePath");
        QByteArray bytes;
        QString name = a.value("name").toString();
        const QUrl source(a.value("source").toString());
        if (!source.isEmpty()) {
            if (!source.isLocalFile() || !QFileInfo(source.toLocalFile()).isFile() || QFileInfo(source.toLocalFile()).size() > 32 * 1024 * 1024) return failure("attachmentTooLarge");
            name = QFileInfo(source.toLocalFile()).fileName();
            if (!readBytes(source.toLocalFile(), bytes, error)) return failure("readFailed");
        } else {
            const QByteArray encoded = a.value("data").toByteArray();
            if (encoded.size() > 45 * 1024 * 1024) return failure("attachmentTooLarge");
            const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
            if (!decoded) return failure("invalidAttachment");
            bytes = decoded.decoded;
        }
        if (bytes.isEmpty() || bytes.size() > 32 * 1024 * 1024) return failure("attachmentTooLarge");
        QMimeDatabase database;
        const auto mime = database.mimeTypeForData(bytes);
        QString extension = QFileInfo(name).suffix().toLower();
        if (!QRegularExpression("^[a-z0-9]{1,8}$").match(extension).hasMatch()) extension = mime.preferredSuffix();
        if (extension.isEmpty()) extension = "bin";
        const QString relative = "assets/" + MarkdownFile::digest(bytes).left(32) + '.' + extension;
        const QString folder = path("assets", true), target = path(relative, true);
        if (folder.isEmpty() || target.isEmpty() || !QDir().mkpath(folder)) return failure("unsafePath");
        if (QFileInfo::exists(target)) {
            QByteArray existing;
            if (!readBytes(target, existing, error) || existing != bytes) return failure("alreadyExists");
        } else if (!atomicWrite(target, bytes, error)) return failure("attachmentWriteFailed");
        auto names = metadata.value("attachments").toObject();
        names.insert(relative, QFileInfo(name).fileName()); metadata.insert("attachments", names);
        if (!persist(error)) return failure("metadataWriteFailed");
        const QString from = QFileInfo(path(note)).absolutePath();
        return {{"ok", true}, {"relative", relative}, {"url", QDir(from).relativeFilePath(target)}, {"name", name}, {"image", mime.name().startsWith("image/")}, {"bytes", bytes.size()}};
    }
    if (op == "attachments" || op == "quarantineAttachment" || op == "restoreAttachment") {
        const QString folder = path("assets", true), trash = path(".nextnotes/attachment-trash", true);
        if (folder.isEmpty() || trash.isEmpty()) return failure("unsafePath");
        if (op == "restoreAttachment") {
            const QString token = a.value("token").toString();
            if (!QRegularExpression("^[a-f0-9-]{36}$").match(token).hasMatch()) return failure("unsafePath");
            QByteArray recordBytes;
            const QString manifest = path(".nextnotes/attachment-trash/" + token + ".json", true);
            if (manifest.isEmpty() || !readBytes(manifest, recordBytes, error)) return failure("notFound");
            const auto record = QJsonDocument::fromJson(recordBytes).object();
            const QString relative = record.value("relative").toString();
            if (!relative.startsWith("assets/") || relative.mid(7).contains('/')) return failure("unsafePath");
            const QString target = path(relative, true), stored = path(".nextnotes/attachment-trash/" + token + ".data", true);
            QByteArray data;
            if (target.isEmpty() || stored.isEmpty() || !readBytes(stored, data, error) || MarkdownFile::digest(data) != record.value("hash").toString()) return failure("attachmentChanged");
            if (QFileInfo::exists(target)) return failure("alreadyExists");
            if (!QDir().mkpath(folder) || !QFile::rename(stored, target)) return failure("restoreFailed");
            QFile::remove(manifest);
            return {{"ok", true}};
        }
        const auto listing = scan(false);
        if (!listing.value("ok").toBool()) return listing;
        QStringList texts;
        for (const auto &entry : listing.value("notes").toList()) {
            QByteArray bytes;
            if (!readBytes(path(entry.toMap().value("path").toString()), bytes, error)) return failure("readFailed");
            texts.append(MarkdownFile::decode(bytes).text);
        }
        QVariantList items, removed;
        for (const auto &file : QDir(folder).entryInfoList(QDir::Files | QDir::NoSymLinks, QDir::Name)) {
            const QString relative = "assets/" + file.fileName();
            if (path(relative, true).isEmpty()) continue;
            int references = 0;
            for (const QString &text : texts) if (text.contains(file.fileName(), Qt::CaseSensitive)) ++references;
            if (op == "quarantineAttachment" && relative == a.value("relative").toString()) {
                if (references) return failure("attachmentInUse");
                const QString token = uuid();
                if (!QDir().mkpath(trash)) return failure("attachmentWriteFailed");
                const QString stored = path(".nextnotes/attachment-trash/" + token + ".data", true);
                const QString manifest = path(".nextnotes/attachment-trash/" + token + ".json", true);
                QByteArray data;
                if (stored.isEmpty() || manifest.isEmpty() || !readBytes(file.absoluteFilePath(), data, error)) return failure("unsafePath");
                const QJsonObject record{{"relative", relative}, {"hash", MarkdownFile::digest(data)}, {"name", file.fileName()}, {"removedUtc", now()}};
                if (!atomicWrite(manifest, QJsonDocument(record).toJson(), error)) return failure("attachmentWriteFailed");
                if (!QFile::rename(file.absoluteFilePath(), stored)) { QFile::remove(manifest); return failure("moveFailed"); }
                return {{"ok", true}, {"token", token}};
            }
            items.append(QVariantMap{{"relative", relative}, {"name", metadata.value("attachments").toObject().value(relative).toString(file.fileName())}, {"bytes", file.size()}, {"references", references}});
        }
        if (op == "quarantineAttachment") return failure("notFound");
        for (const auto &file : QDir(trash).entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name)) {
            const QString manifest = path(".nextnotes/attachment-trash/" + file.fileName(), true);
            QByteArray data; if (manifest.isEmpty() || !readBytes(manifest, data, error)) continue;
            auto item = QJsonDocument::fromJson(data).object().toVariantMap();
            item.insert("token", file.completeBaseName()); removed.append(item);
        }
        return {{"ok", true}, {"items", items}, {"removed", removed}};
    }
    if (op == "createFolder") {
        const QString target = path(a.value("path").toString());
        if (target.isEmpty()) return failure("unsafePath");
        if (QFileInfo::exists(target)) return failure("alreadyExists");
        if (!QDir().mkdir(target)) return failure("createFolderFailed");
        return {{"ok", true}, {"listing", scan(false)}};
    }
    if (op == "trashBatch") {
        const QString action=a.value("action").toString();const auto items=a.value("items").toList();
        if(!QStringList{"restore","purge"}.contains(action)||items.isEmpty())return failure("invalidBatch");
        QVariantList results;QSet<QString> seen;int succeeded=0;
        for(auto value:items) {
            auto item=value.toMap();const QString kind=item.value("kind").toString(),token=item.value("token").toString();
            const QString key=kind+":"+token, bucket=kind=="folder"?"folderTrash":"trash";
            auto records=metadata.value(bucket).toObject();auto record=records.value(token).toObject();
            QVariantMap result;const bool folder=kind=="folder";
            const QString relative=folder?".nextnotes/folder-trash/"+token:".nextnotes/trash/"+token+".md", stored=path(relative,true);
            if(!QStringList{"note","folder"}.contains(kind)||!QRegularExpression("^[a-f0-9-]{36}$").match(token).hasMatch()||stored.isEmpty()||seen.contains(key))result=failure("unsafePath");
            else if(record.isEmpty()||(!folder&&record.value("stored").toString()!=relative))result=failure("notFound");
            else if(action=="restore"&&record.value("purging").toBool())result=failure("purgeIncomplete");
            else if(record.value("purging").toBool()&&!QFileInfo::exists(stored)&&action=="purge") {
                records.remove(token);metadata.insert(bucket,records);if(persist(error))result={{"ok",true}};else{records.insert(token,record);metadata.insert(bucket,records);result=failure("metadataWriteFailed");}
            } else if(item.value("fingerprint").toString().isEmpty()||item.value("fingerprint").toString()!=TrashFiles::fingerprint(stored))result=failure("trashChanged");
            else if(action=="restore")result=dispatch(folder?"restoreFolder":"restore",{{"token",token}});
            else {
                record.insert("purging",true);records.insert(token,record);metadata.insert(bucket,records);
                if(!persist(error))result=failure("metadataWriteFailed");
                else if(!TrashFiles::remove(stored,folder,item.value("fingerprint").toString()))result=failure("purgeIncomplete");
                else {records.remove(token);metadata.insert(bucket,records);if(persist(error))result={{"ok",true}};else{records.insert(token,record);metadata.insert(bucket,records);result=failure("metadataWriteFailed");}}
            }
            seen.insert(key);if(result.value("ok").toBool())++succeeded;
            QVariantMap outcome{{"ok",result.value("ok")},{"kind",kind},{"token",token},{"path",record.value("source").toString()}};if(result.contains("error"))outcome.insert("error",result.value("error"));results.append(outcome);
        }
        return {{"ok",true},{"action",action},{"results",results},{"succeeded",succeeded},{"failed",results.size()-succeeded}};
    }
    if (op == "restore") {
        const QString token = a.value("token").toString();
        auto trash = metadata.value("trash").toObject();
        if (!trash.contains(token)) return failure("notFound");
        const auto record = trash.value(token).toObject();
        if(record.value("purging").toBool())return failure("purgeIncomplete");
        const QString rel = a.value("target", record.value("source").toString()).toString();
        const QString target = path(rel), source = path(record.value("stored").toString(), true);
        if (target.isEmpty() || source.isEmpty() || QFileInfo(target).suffix().compare("md", Qt::CaseInsensitive) != 0) return failure("unsafePath");
        if (QFileInfo::exists(target)) return failure("alreadyExists");
        QByteArray bytes;
        if (!readBytes(source, bytes, error)) return failure("readFailed", error);
        if (MarkdownFile::digest(bytes) != record.value("hash").toString()) return failure("trashChanged");
        auto listing = scan(false);
        if (!listing.value("ok").toBool()) return listing;
        const QString restoreId = record.value("metadata").toObject().value("id").toString();
        for (const auto &item : listing.value("notes").toList())
            if (!restoreId.isEmpty() && item.toMap().value("id").toString() == restoreId) return failure("duplicateId");
        auto entries = metadata.value("entries").toObject();
        entries.insert(rel, record.value("metadata")); metadata.insert("entries", entries);
        if (!persist(error)) return failure("metadataWriteFailed", error);
        if (!QFile::rename(source, target)) return failure("restoreFailed", "Recycle file and recovery record retained.");
        fingerprints.insert(rel, MarkdownFile::digest(bytes));
        trash.remove(token); metadata.insert("trash", trash);
        if (!persist(error)) return failure("metadataWriteFailed", "File restored; stale recycle record retained on disk.");
        return {{"ok", true}, {"path", rel}, {"listing", scan(false)}};
    }
    if (op == "recovery") {
        QVariantList drafts;
        const QString directory = path(".nextnotes/drafts", true);
        if (directory.isEmpty()) return failure("unsafePath");
        for (const auto &fi : QDir(directory).entryInfoList({"*.json"}, QDir::Files)) {
            if (fi.isSymLink()) continue;
            QByteArray bytes;
            if (!readBytes(fi.absoluteFilePath(), bytes, error)) return failure("recoveryReadFailed", error);
            QJsonParseError parse;
            auto json = QJsonDocument::fromJson(bytes, &parse);
            if (parse.error != QJsonParseError::NoError || !json.isObject()) return failure("recoveryCorrupt", fi.fileName());
            const auto object = json.object();
            if (!object.value("path").isString() || path(object.value("path").toString()).isEmpty()
                || !object.value("text").isString() || !object.value("baseHash").isString()
                || !object.value("updatedUtc").isString()) return failure("recoveryCorrupt", fi.fileName());
            auto draft = object.toVariantMap();
            const QString current = path(draft.value("path").toString());
            QByteArray disk;
            draft.insert("diskHash", !current.isEmpty() && readBytes(current, disk, error) ? MarkdownFile::digest(disk) : QString());
            draft.insert("matchesBase", draft.value("diskHash") == draft.value("baseHash"));
            drafts.append(draft);
        }
        QVariantList commits;
        const QString commitDir = path(".nextnotes/commits", true);
        for (const auto &fi : QDir(commitDir).entryInfoList({"*.intent"}, QDir::Files)) {
            if (fi.isSymLink()) continue;
            QByteArray content;
            if (!readBytes(fi.absoluteFilePath(), content, error)) return failure("recoveryReadFailed", error);
            const int separator = content.indexOf('\n');
            QJsonParseError parse;
            const auto record = QJsonDocument::fromJson(content.left(separator), &parse);
            if (separator < 0 || parse.error != QJsonParseError::NoError || !record.isObject()
                || !record.object().value("path").isString()
                || !record.object().value("baseHash").isString()
                || !record.object().value("newHash").isString()
                || content.mid(separator + 1).isEmpty()) return failure("recoveryCorrupt", fi.fileName());
            QVariantList versions;
            if (a.value("includeCommitText", true).toBool()) for (const auto &version : {QString("before"), QString("pending")}) {
                const auto result = dispatch("recoveryVersion", {{"token", fi.fileName()}, {"version", version}});
                versions.append(QVariantMap{{"version", version}, {"available", result.value("ok").toBool()}, {"text", result.value("text")}});
            }
            commits.append(QVariantMap{{"token", fi.fileName()}, {"name", QFileInfo(record.object().value("path").toString()).fileName()}, {"time", fi.lastModified().toString(Qt::ISODate)}, {"versions", versions}});
        }
        QVariantList trashItems;
        for(const QString kind:{QString("note"),QString("folder")}) {
            const auto records=metadata.value(kind=="folder"?"folderTrash":"trash").toObject();
            for(auto it=records.begin();it!=records.end();++it){
                const QString relative=kind=="folder"?".nextnotes/folder-trash/"+it.key():".nextnotes/trash/"+it.key()+".md";
                const QString stored=path(relative,true);auto item=it.value().toObject().toVariantMap();
                item.insert("kind",kind);item.insert("token",it.key());item.insert("fingerprint",stored.isEmpty()?QString():TrashFiles::fingerprint(stored));trashItems.append(item);
            }
        }
        QVariantList inMemory;
        for (const auto &draft : memoryDrafts) inMemory.append(draft);
        return {{"ok", true}, {"trashItems",trashItems}, {"drafts", drafts}, {"commits", commits}, {"memoryDrafts", inMemory}, {"folderTrash",metadata.value("folderTrash").toObject().toVariantMap()}, {"trash", metadata.value("trash").toObject().toVariantMap()}};
    }
    const QString rel = a.value("path").toString();
    const QString absolute = path(rel);
    if (absolute.isEmpty() || QFileInfo(rel).suffix().compare("md", Qt::CaseInsensitive) != 0) return failure("unsafePath");
    if (op == "discardDraft") { return clearDraft(rel) ? QVariantMap{{"ok", true}} : failure("discardFailed", "Recovery record retained."); }
    if (op == "checkpoint") {
        if (!checkpoint(rel, a.value("text").toString(), a.value("baseHash").toString(), error))
            return {{"ok", false}, {"error", "recoveryWriteFailed"}, {"detail", error}, {"draftInMemory", true}, {"recoveryDurable", false}};
        return {{"ok", true}, {"recoveryDurable", true}};
    }
    if (op == "read") {
        QByteArray bytes;
        if (!readBytes(absolute, bytes, error)) return failure("readFailed", error);
        auto d = MarkdownFile::decode(bytes);
        if(QFileInfo::exists(path(".nextnotes/move.json",true)))d.readOnlyReason="pendingRelocation";
        opened.insert(rel, d);
        return documentResult(rel, d);
    }
    if (op == "create" || op == "saveAs") {
        if (QFileInfo::exists(absolute)) return failure("alreadyExists");
        if (!QFileInfo(QFileInfo(absolute).absolutePath()).isDir()) return failure("missingFolder");
        const QString id = uuid();
        QString text = op == "create" ? "---\nnextnotes_id: " + id + "\n---\n\n" + a.value("text").toString() : a.value("text").toString();
        if (op == "saveAs" && !MarkdownFile::formatError(text).isEmpty()) return failure("unsupportedFormat");
        if (op == "saveAs" && a.value("newIdentity").toBool()) {
            // Explicit caller choice: only replace the scalar ID line, never serialize front matter.
            const QString oldId = MarkdownFile::field(text, "nextnotes_id");
            if (!oldId.isEmpty()) {
                auto lines = text.split(u'\n');
                int replacements = 0;
                for (int i = 1; i < lines.size(); ++i) {
                    if (lines[i] == "---" || lines[i] == "...") break;
                    if (lines[i].startsWith("nextnotes_id:")) { lines[i] = "nextnotes_id: " + id; ++replacements; }
                }
                if (replacements != 1) return failure("ambiguousId");
                text = lines.join(u'\n');
            }
        }
        if (op == "create") text.replace("\r\n", "\n");
        const auto proposed = MarkdownFile::decode(text.toUtf8());
        if (!proposed.readOnlyReason.isEmpty()) return failure("unsupportedFormat", proposed.readOnlyReason);
        if (op == "create" && !text.endsWith(u'\n')) text += u'\n';
        auto listing = scan(false);
        if (!listing.value("ok").toBool()) return listing;
        const QString suppliedId = MarkdownFile::field(text, "nextnotes_id");
        for (const auto &item : listing.value("notes").toList())
            if (!suppliedId.isEmpty() && item.toMap().value("id").toString() == suppliedId)
                return failure("duplicateId", "An explicit unique nextnotes_id (or no ID field) is required for Save As; original draft is retained.");
        if (!checkpoint(rel, text, QString(), error)) return failure("recoveryWriteFailed", error);
        QString retained;
        const QString journal = path(".nextnotes/commits/" + uuid() + ".intent", true);
        const QByteArray record = QJsonDocument(QJsonObject{{"path", rel}, {"newHash", MarkdownFile::digest(text.toUtf8())}, {"baseHash", QString()}}).toJson(QJsonDocument::Compact);
        if (journal.isEmpty() || !SafeCommit::commit(absolute, text.toUtf8(), journal, record, false, retained, error))
            return failure("writeFailed", error);
        fingerprints.insert(rel, MarkdownFile::digest(text.toUtf8()));
        auto entries = metadata.value("entries").toObject();
        entries.insert(rel, QJsonObject{{"id", suppliedId.isEmpty() ? id : suppliedId}, {"createdUtc", now()}, {"modifiedUtc", now()}});
        metadata.insert("entries", entries);
        if (!persist(error)) return failure("metadataWriteFailed", error);
        clearDraft(rel);
        auto d = MarkdownFile::decode(text.toUtf8()); opened.insert(rel, d);
        auto result = documentResult(rel, d);
        result.insert("listing", scan(false));
        return result;
    }
    if (op == "save") {
        const QString text = a.value("text").toString(), base = a.value("baseHash").toString();
        bool durable = checkpoint(rel, text, base, error);
        auto failSave = [&](const QString &code, const QString &detail) {
            auto result = failure(code, detail); result.insert("draftInMemory", true); result.insert("recoveryDurable", durable); return result;
        };
        if (!durable) return failSave("recoveryWriteFailed", error);
        if (!opened.contains(rel) || opened.value(rel).hash != base) return failSave("unknownBase", "Read this exact base before saving; recovery never overwrites automatically.");
        const auto d = opened.value(rel);
        if (!d.readOnlyReason.isEmpty()) return failSave("readOnly", d.readOnlyReason);
        if (text.contains(u'\r') || text.contains(QChar(0x2028)) || text.contains(QChar(0x2029)) || text.contains(QChar(0)))
            return failSave("unsupportedSeparator", "Keep the draft; unsupported separators cannot be encoded safely.");
        if (!MarkdownFile::formatError(text).isEmpty()) return failSave("unsupportedFormat", MarkdownFile::formatError(text));
        auto listing = editingTags ? cachedListing : scan(false);
        if (!listing.value("ok").toBool()) return failSave("scanFailed", listing.value("detail").toString());
        if (!listing.value("duplicateIds").toList().isEmpty()) return failSave("duplicateId", "Resolve duplicated IDs before saving.");
        const QString proposedId = MarkdownFile::field(text, "nextnotes_id");
        for (const auto &item : listing.value("notes").toList()) {
            const auto note = item.toMap();
            if (note.value("path").toString() != rel && !proposedId.isEmpty() && note.value("id").toString() == proposedId)
                return failSave("duplicateId", proposedId);
        }
        QByteArray disk;
        if (!readBytes(absolute, disk, error)) return failSave("readFailed", error);
        if (MarkdownFile::digest(disk) != base) {
            auto result = failSave("conflict", "External file and local recovery draft are both retained.");
            result.insert("diskHash", MarkdownFile::digest(disk)); result.insert("diskText", MarkdownFile::decode(disk).text); return result;
        }
        auto encoding = d; if(a.value("sourceNewlines").toBool())encoding.trailingNewline=text.endsWith(u'\n');
        const QByteArray bytes = MarkdownFile::encode(encoding, text);
        const QString historyId=metadata.value("entries").toObject().value(rel).toObject().value("id").toString();
        if(bytes!=disk&&!rememberVersion(historyId,rel,disk,error))return failSave("historyWriteFailed",error);
        if (bytes != disk) {
            QString retained;
            const QString journal = path(".nextnotes/commits/" + uuid() + ".intent", true);
            const QByteArray record = QJsonDocument(QJsonObject{{"path", rel}, {"baseHash", base}, {"newHash", MarkdownFile::digest(bytes)}}).toJson(QJsonDocument::Compact);
            if (journal.isEmpty() || !SafeCommit::commit(absolute, bytes, journal, record, true, retained, error))
                return failSave("writeFailed", error);
            QByteArray displaced;
            if (!readBytes(retained, displaced, error)) return failSave("writeFailed", error);
            if (MarkdownFile::digest(displaced) != base) {
                auto result = failSave("conflict", "Actual displaced inode retained; local draft retained. No automatic rollback.");
                result.insert("retainedPath", retained); result.insert("diskText", MarkdownFile::decode(displaced).text);
                return result;
            }
        }
        fingerprints.insert(rel, MarkdownFile::digest(bytes));
        auto entries = metadata.value("entries").toObject(); auto entry = entries.value(rel).toObject();
        entry.insert("savedHash", MarkdownFile::digest(bytes)); entry.insert("modifiedUtc", now()); entries.insert(rel, entry); metadata.insert("entries", entries);
        if (!persist(error)) return failSave("metadataWriteFailed", error);
        auto saved = MarkdownFile::decode(bytes); opened.insert(rel, saved); clearDraft(rel);
        auto result = documentResult(rel, saved);
        if (!editingTags) result.insert("listing", scan(false));
        return result;
    }
    if (op == "favorite" || op == "pin") {
        if (!QFileInfo(absolute).isFile()) return failure("notFound");
        auto entries = metadata.value("entries").toObject(); auto entry = entries.value(rel).toObject();
        entry.insert(op == "pin" ? "pinned" : "favorite", a.value(op == "pin" ? "pinned" : "favorite").toBool()); entries.insert(rel, entry);
        const auto previous = metadata; metadata.insert("entries", entries);
        if (!persist(error)) { metadata = previous; return failure("metadataWriteFailed", error); }
        return {{"ok", true}};
    }
    if (op == "rename") return relocate(rel, a.value("target").toString(), a.value("baseHash").toString());
    if (op == "rename" || op == "trash") {
        if (memoryDrafts.contains(rel) || QFileInfo::exists(draftPath(rel))) return failure("pendingDrafts");
        QByteArray disk;
        if (!readBytes(absolute, disk, error)) return failure("readFailed", error);
        if (a.value("baseHash").toString() != MarkdownFile::digest(disk)) return failure("conflict");
        const QString token = uuid();
        const QString targetRel = op == "trash" ? ".nextnotes/trash/" + token + ".md" : a.value("target").toString();
        const QString target = path(targetRel, op == "trash");
        if (target.isEmpty() || QFileInfo(target).suffix().compare("md", Qt::CaseInsensitive) != 0) return failure("unsafePath");
        if (QFileInfo::exists(target)) return failure("alreadyExists");
        if (op == "rename" && QFileInfo(absolute).absolutePath() != QFileInfo(target).absolutePath()) {
            // Until reference rewriting is available, never silently break relative references.
            const QString text = MarkdownFile::decode(disk).text;
            if (text.contains(QRegularExpression(QStringLiteral("\\]\\s*[(:]|\\[\\[|<(?:a|img|source)\\b"), QRegularExpression::CaseInsensitiveOption))) return failure("relativeLinks");
        }
        auto entries = metadata.value("entries").toObject();
        auto entry = entries.value(rel).toObject();
        if (op == "trash") {
            auto trash = metadata.value("trash").toObject();
            trash.insert(token, QJsonObject{{"source", rel}, {"stored", targetRel}, {"metadata", entry}, {"hash", MarkdownFile::digest(disk)}, {"deletedUtc", now()}});
            metadata.insert("trash", trash);
        } else {
            // Keep source mapping until a later scan; new mapping is durable before moving.
            entries.insert(targetRel, entry); metadata.insert("entries", entries);
        }
        if (!persist(error)) return failure("metadataWriteFailed", error);
        if (!QFile::rename(absolute, target)) return failure("moveFailed", "Original retained; recovery metadata may contain a pending move.");
        fingerprints.remove(rel);
        if (op == "rename") fingerprints.insert(targetRel, MarkdownFile::digest(disk));
        opened.remove(rel);
        return {{"ok", true}, {"oldPath", rel}, {"newPath", targetRel}, {"trashToken", op == "trash" ? token : QString()}, {"listing", scan(false)}};
    }
    return failure("unknownOperation", op);
}

NotesRepository::NotesRepository(QObject *parent) : QObject(parent), m_worker(new RepositoryWorker)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &RepositoryWorker::finished, this, &NotesRepository::finished);
    connect(m_worker, &RepositoryWorker::operationProgress, this, &NotesRepository::operationProgress);
    connect(m_worker, &RepositoryWorker::changed, this, &NotesRepository::libraryChanged);
    m_thread.start();
}
NotesRepository::~NotesRepository()
{
    // Drain all previously accepted requests before stopping; controller should wait for saves before exit.
    QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
    m_thread.quit(); m_thread.wait();
}
quint64 NotesRepository::request(const QString &operation, const QVariantMap &arguments)
{
    const quint64 id = ++m_nextRequest;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    if (operation == "importPreview" || operation == "importNotes") m_cancelled = cancelled;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, operation, arguments, cancelled] { worker->execute(id, operation, arguments, cancelled); }, Qt::QueuedConnection);
    return id;
}
void NotesRepository::cancelOperation() { if (m_cancelled) m_cancelled->store(true); }
#include "NotesRepository.moc"
