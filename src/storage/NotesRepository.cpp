#include "NotesRepository.h"
#include "MarkdownFile.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <QDateTime>
#include <QRegularExpression>
#include <QLockFile>
#include <memory>

namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString now() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
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
    void execute(quint64 id, const QString &op, const QVariantMap &args) {
        QVariantMap result = dispatch(op, args);
        result.insert("library", root);
        result.insert("generation", generation);
        emit finished(id, op, result);
    }
signals:
    void finished(quint64, const QString &, const QVariantMap &);
    void changed(const QVariantMap &);
private:
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
            {"readOnly", !d.readOnlyReason.isEmpty()}, {"readOnlyReason", d.readOnlyReason}};
    }
    QVariantMap scan(bool notify) {
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
                if (fi.isSymLink() || fi.fileName() == ".nextnotes" || fi.fileName() == "assets") continue;
                const QString rel = folder.isEmpty() ? fi.fileName() : folder + u'/' + fi.fileName();
                const QString absolute = path(rel);
                if (absolute.isEmpty()) continue;
                if (fi.isDir()) { folders.append(rel); pending.append(rel); watchPaths.append(absolute); continue; }
                if (fi.suffix().compare("md", Qt::CaseInsensitive) != 0) continue;
                QByteArray bytes;
                if (!readBytes(absolute, bytes, error)) return failure("readFailed", rel + ": " + error);
                auto doc = MarkdownFile::decode(bytes);
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
                notes.append(QVariantMap{{"path", rel}, {"id", stableId}, {"title", MarkdownFile::title(doc.text, fi.completeBaseName())},
                    {"baseHash", doc.hash}, {"favorite", entry.value("favorite").toBool()}, {"createdUtc", entry.value("createdUtc").toString()},
                    {"fileModifiedUtc", fi.lastModified().toUTC().toString(Qt::ISODateWithMs)}, {"readOnlyReason", doc.readOnlyReason}});
            }
        }
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
        if (!oldPaths.isEmpty()) watcher->removePaths(oldPaths);
        const QStringList unwatchable = watcher->addPaths(watchPaths);
        QVariantList changes;
        for (auto it = hashes.cbegin(); it != hashes.cend(); ++it)
            if (fingerprints.value(it.key()) != it.value()) changes.append(QVariantMap{{"path", it.key()}, {"hash", it.value()}, {"removed", false}});
        for (auto it = fingerprints.cbegin(); it != fingerprints.cend(); ++it)
            if (!hashes.contains(it.key())) changes.append(QVariantMap{{"path", it.key()}, {"removed", true}});
        fingerprints = hashes;
        QVariantMap result{{"ok", true}, {"notes", notes}, {"folders", folders}, {"duplicateIds", duplicates}, {"unwatchedPaths", unwatchable}};
        if (notify && (!changes.isEmpty() || !unwatchable.isEmpty())) emit changed({{"library", root}, {"generation", generation}, {"changes", changes}, {"listing", result}});
        return result;
    }
    QVariantMap dispatch(const QString &op, const QVariantMap &a);
};

QVariantMap RepositoryWorker::dispatch(const QString &op, const QVariantMap &a)
{
    QString error;
    if (op == "selectLibrary") {
        if (!memoryDrafts.isEmpty()) return failure("pendingDrafts", "Save or explicitly discard in-memory drafts before switching libraries.");
        QFileInfo selected(a.value("path").toString());
        if (!selected.isDir() || selected.isSymLink()) return failure("invalidLibrary");
        QString selectedRoot = selected.canonicalFilePath();
        if (selectedRoot == root) return scan(false);
        // Reject symlink ancestors as well as a symlink selected directory.
        if (QDir::cleanPath(selected.absoluteFilePath()) != selectedRoot) return failure("unsafePath");
        const QString oldRoot = root;
        root = selectedRoot;
        for (const auto &dir : {QString(".nextnotes"), QString(".nextnotes/drafts"), QString(".nextnotes/trash")}) {
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
        if (watcher) { delete watcher; watcher = nullptr; delete debounce; debounce = nullptr; }
        lock = std::move(newLock);
        metadata = state;
        opened.clear(); fingerprints.clear();
        ++generation;
        if (!persist(error)) return failure("metadataWriteFailed", error);
        return scan(false);
    }
    if (root.isEmpty()) return failure("noLibrary");
    if (a.contains("generation") && a.value("generation").toULongLong() != generation) return failure("staleGeneration");
    if (QFileInfo(root).isSymLink() || QFileInfo(root).canonicalFilePath() != root) return failure("unsafePath");
    if (op == "list") return scan(false);
    if (op == "createFolder") {
        const QString target = path(a.value("path").toString());
        if (target.isEmpty()) return failure("unsafePath");
        if (QFileInfo::exists(target)) return failure("alreadyExists");
        if (!QDir().mkdir(target)) return failure("createFolderFailed");
        return {{"ok", true}, {"listing", scan(false)}};
    }
    if (op == "restore") {
        const QString token = a.value("token").toString();
        auto trash = metadata.value("trash").toObject();
        if (!trash.contains(token)) return failure("notFound");
        const auto record = trash.value(token).toObject();
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
            auto draft = json.object().toVariantMap();
            const QString current = path(draft.value("path").toString());
            QByteArray disk;
            draft.insert("diskHash", !current.isEmpty() && readBytes(current, disk, error) ? MarkdownFile::digest(disk) : QString());
            draft.insert("matchesBase", draft.value("diskHash") == draft.value("baseHash"));
            drafts.append(draft);
        }
        QVariantList inMemory;
        for (const auto &draft : memoryDrafts) inMemory.append(draft);
        return {{"ok", true}, {"drafts", drafts}, {"memoryDrafts", inMemory}, {"trash", metadata.value("trash").toObject().toVariantMap()}};
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
        opened.insert(rel, d);
        return documentResult(rel, d);
    }
    if (op == "create" || op == "saveAs") {
        if (QFileInfo::exists(absolute)) return failure("alreadyExists");
        if (!QFileInfo(QFileInfo(absolute).absolutePath()).isDir()) return failure("missingFolder");
        const QString id = uuid();
        QString text = op == "create" ? "---\nnextnotes_id: " + id + "\n---\n\n" + a.value("text").toString() : a.value("text").toString();
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
        if (op == "create" && !text.endsWith(u'\n')) text += u'\n';
        auto listing = scan(false);
        if (!listing.value("ok").toBool()) return listing;
        const QString suppliedId = MarkdownFile::field(text, "nextnotes_id");
        for (const auto &item : listing.value("notes").toList())
            if (!suppliedId.isEmpty() && item.toMap().value("id").toString() == suppliedId)
                return failure("duplicateId", "An explicit unique nextnotes_id (or no ID field) is required for Save As; original draft is retained.");
        if (!checkpoint(rel, text, QString(), error)) return failure("recoveryWriteFailed", error);
        // Reserve destination atomically; QSaveFile alone may overwrite a concurrently-created file.
        QFile reservation(absolute);
        if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return failure("createFailed", reservation.errorString());
        reservation.close();
        if (!atomicWrite(absolute, text.toUtf8(), error)) {
            // Keep the reserved empty file: never remove an external writer's replacement.
            return failure("writeFailed", error);
        }
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
        auto listing = scan(false);
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
        const QByteArray bytes = MarkdownFile::encode(d, text);
        if (bytes != disk && !atomicWrite(absolute, bytes, error)) return failSave("writeFailed", error);
        auto entries = metadata.value("entries").toObject(); auto entry = entries.value(rel).toObject();
        entry.insert("modifiedUtc", now()); entries.insert(rel, entry); metadata.insert("entries", entries);
        if (!persist(error)) return failSave("metadataWriteFailed", error);
        auto saved = MarkdownFile::decode(bytes); opened.insert(rel, saved); clearDraft(rel);
        auto result = documentResult(rel, saved); result.insert("listing", scan(false)); return result;
    }
    if (op == "favorite") {
        if (!QFileInfo(absolute).isFile()) return failure("notFound");
        auto entries = metadata.value("entries").toObject(); auto entry = entries.value(rel).toObject();
        entry.insert("favorite", a.value("favorite").toBool()); entries.insert(rel, entry);
        const auto previous = metadata; metadata.insert("entries", entries);
        if (!persist(error)) { metadata = previous; return failure("metadataWriteFailed", error); }
        return {{"ok", true}};
    }
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
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, id, operation, arguments] { worker->execute(id, operation, arguments); }, Qt::QueuedConnection);
    return id;
}
#include "NotesRepository.moc"
