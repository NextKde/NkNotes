#include "ManagedStore.h"
#include "ManagedLibrary.h"
#include "ManagedAssets.h"
#include "RepositoryTestHooks.h"
#include "MarkdownFile.h"
#include "KnowledgeIndex.h"
#include <sqlite3.h>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>
#include <QMimeDatabase>
#include <algorithm>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QSet>
#include <QMap>

namespace {
using StoreError = ManagedStoreError;
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
Database openDatabase(const QString &path, int flags);
void copyDatabase(sqlite3 *destination, sqlite3 *source);
void streamCopy(const QString &source, const QString &destination);
void failAt(const QString &stage) {
    if (RepositoryTestHooks::managedFailure && RepositoryTestHooks::managedFailure(stage))
        throw StoreError{"writeFailed", stage};
}
void recoveryStage(const QString &stage) {
    if (RepositoryTestHooks::managedRecoveryStage) RepositoryTestHooks::managedRecoveryStage(stage);
}
void externalStage(const QString &stage) {
    if (RepositoryTestHooks::externalSaveStage) RepositoryTestHooks::externalSaveStage(stage);
}
QString uid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString utc() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
QString hashText(const QString &text) { return MarkdownFile::digest(text.toUtf8()); }
QVariantMap ok() { return {{"ok", true}}; }
void require(bool value, const QString &code, const QString &detail = {}) { if (!value) throw StoreError{code, detail}; }
QString parentFolder(const QString &path) { const auto folder = QFileInfo(path).path(); return folder == "." ? QString() : folder; }
QString relativeManagedPath(const QString &source, const QString &target) {
    return QDir("/" + parentFolder(source)).relativeFilePath("/" + target);
}
QString relativeManagedUrl(const QString &source, const QString &target, const QUrl &original) {
    QString result=QString::fromUtf8(QUrl::toPercentEncoding(relativeManagedPath(source,target),"/"));
    if(original.hasQuery())result+='?'+original.query(QUrl::FullyEncoded);
    if(original.hasFragment())result+='#'+original.fragment(QUrl::FullyEncoded);
    return result;
}
// Export supports the same Markdown spans as LinkPaths. Hide complete raw HTML
// opening tags while rewriting those spans, then restore the tags byte-for-byte.
// HTML resources remain unsupported and are explicitly reported to the caller.
bool rewriteExportLinks(QString &text, const std::function<QString(const QString &,bool)> &map) {
    static const QRegularExpression htmlTag(R"rx(<(?:a|img|source)\b(?:[^<>"']|"[^"]*"|'[^']*')*>)rx",QRegularExpression::CaseInsensitiveOption);
    QString prefix;do {prefix="NKNOTES_HTML_"+uid()+'_';}while(text.contains(prefix));
    QList<QPair<QString,QString>> hidden;
    QList<QRegularExpressionMatch> matches;
    auto found=htmlTag.globalMatch(LinkPaths::masked(text));while(found.hasNext())matches.append(found.next());
    for(auto it=matches.crbegin();it!=matches.crend();++it) {
        const auto marker=prefix+QString::number(hidden.size())+'_';
        hidden.append({marker,text.mid(it->capturedStart(),it->capturedLength())});
        text.replace(it->capturedStart(),it->capturedLength(),marker);
    }
    const bool supported=LinkPaths::rewrite(text,map);
    for(const auto &entry:hidden)text.replace(entry.first,entry.second);
    return supported&&hidden.isEmpty();
}
bool validPath(const QString &path, bool empty = false) {
    if (path.isEmpty()) return empty;
    if (QDir::isAbsolutePath(path) || path.startsWith("external:") || path.contains('\\') || path.contains(QChar::Null)) return false;
    for (const auto &part : path.split('/')) if (part.isEmpty() || part == "." || part == ".." || part == ".nextnotes" || part == ".git") return false;
    return true;
}
bool validFolderIcon(const QString &icon) {
    return QStringList{"folder", "editRounded", "album", "home", "flag", "heart", "calendar", "music", "grid", "sparkle", "artist", "reminder",
            "archive", "envelope", "wallpaper", "keyboard", "settings", "queueList", "radio", "theme", "sliders", "circle", "waves", "info",
            "work", "study", "sport", "running", "cycling", "swimming", "fitness", "basketball", "football", "badminton", "tennis",
            "shopping", "travel", "food", "coffee", "health", "finance", "gift", "pets", "garden", "car", "code", "camera", "ideas"}.contains(icon);
}
QByteArray bytesAt(const QString &path, qint64 max = 64 * 1024 * 1024) {
    QFile f(path); require(QFileInfo(path).isFile() && QFileInfo(path).size() <= max, "readFailed", path);
    require(f.open(QIODevice::ReadOnly), "readFailed", path); auto bytes = f.read(max + 1); require(f.error() == QFileDevice::NoError && bytes.size() <= max && f.atEnd(), "readFailed", path); return bytes;
}
void writeBytes(const QString &path, const QByteArray &bytes) {
    require(QDir().mkpath(QFileInfo(path).absolutePath()), "writeFailed", path);
    QSaveFile f(path); f.setDirectWriteFallback(false);
    require(f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit(), "writeFailed", path);
}
QString noteSummary(QString text) {
    if (text.startsWith("---\n")) { int end = text.indexOf("\n---", 3); if (end >= 0) text = text.mid(end + 4); }
    text = text.trimmed(); if (text.startsWith("# ")) { int end = text.indexOf('\n'); if (end >= 0) text = text.mid(end + 1); }
    return text.simplified().left(150);
}
}

struct ManagedStore::Impl {
    sqlite3 *db = nullptr;
    struct Change { QString table, id; QVariantMap before, after; };
    struct Command { QString label; QList<Change> changes; QString primaryId; };
    QList<Command> undoStack, redoStack;
    QString initializationNotice;
    int cacheFailures = 0;
    void initializeFolderMetadata() {
        QSet<QString> columns;
        for (const auto &v : query("PRAGMA table_info(folders)"))
            columns.insert(v.toMap().value("name").toString());
        // Additive metadata: old libraries and backups retain their folder IDs.
        if (!columns.contains("icon")) query("ALTER TABLE folders ADD COLUMN icon TEXT NOT NULL DEFAULT 'folder'");
        // Equal initial ranks retain the old alphabetical order until rearranged.
        if (!columns.contains("sort_order")) query("ALTER TABLE folders ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0");
        // Legacy folders have no reliable creation date; leave it unknown.
        if (!columns.contains("created")) query("ALTER TABLE folders ADD COLUMN created TEXT NOT NULL DEFAULT ''");
    }
    void initializeFolders();
    void initializeUndo();
    void beginCapture();
    Command finishCapture(const QString &label);
    QVariantMap historyState() const;
    QVariantMap applyHistory(bool redo);
    QString commandLabel(const QString &op, const QVariantMap &a) const;

    std::function<void(const QVariantMap &)> progress;
    std::shared_ptr<std::atomic_bool> cancelled;
    bool stopping() const { return cancelled && cancelled->load(); }
    void notify(const QString &phase, int done, int total = 0) { if (progress) progress({{"phase",phase},{"completed",done},{"total",total}}); }
    QString root, configuredRoot;
    std::unique_ptr<ManagedLibrary> library;
    QVariantMap openError;
    Knowledge::Index index;
    QHash<QString, MarkdownFile::Document> external;
    // Contains only the newest request for each path, including failed commits.
    QHash<QString, QVariantMap> memoryDrafts;
    QVariantMap assetManifest;
    bool manifestDirty = true;
    ~Impl() { if (db) sqlite3_close(db); }
    void initializeDatabase();
    void preflight();
    void validateDatabase(bool backup, bool legacy = false);
    void prepareBackup(const QString &source, const QString &snapshot);
    QVariantMap recoverInitialization(const QVariantMap &a);
    QVariantMap checkpoint(const QVariantMap &a);
    QVariantMap discardDraft(const QString &path);
    QVariantMap saveExternal(const QVariantMap &a);
    QVariantList recoveryDrafts();
    bool hasPendingMemoryDrafts() const {
        for (const auto &draft : memoryDrafts) if (!draft.value("recoveryDurable").toBool()) return true;
        return false;
    }
    QString checkedCachePath(const QString &relative) const {
        require(validPath(relative) && relative.startsWith("assets/"), "unsafePath", relative);
        QString current = root;
        require(QFileInfo(root).canonicalFilePath() == root && !QFileInfo(root).isSymLink(), "unsafePath", root);
        const auto parts = relative.split('/');
        for (qsizetype i = 0; i < parts.size(); ++i) {
            current += '/' + parts[i];
            const QFileInfo info(current);
            require(!info.isSymLink() && (!info.exists() || (info.canonicalFilePath() == current
                && (i + 1 < parts.size() ? info.isDir() : info.isFile()))), "unsafePath", current);
        }
        return current;
    }
    const QVariantMap &managedManifest() {
        if (manifestDirty) {
            QVariantMap next;
            for (const auto &v : query("SELECT path,data FROM assets WHERE deleted=''")) {
                const auto row=v.toMap();const auto path=row.value("path").toString(),digest=ManagedAssets::digestForPath(path);
                require(!digest.isEmpty() && row.value("data").metaType().id()==QMetaType::QByteArray
                    && row.value("data").toByteArray().size()<=ManagedAssets::MaxBytes
                    && MarkdownFile::digest(row.value("data").toByteArray())==digest,"databaseCorrupt","Invalid asset identity");
                next.insert(path,digest);
            }
            assetManifest = std::move(next); manifestDirty = false;
        }
        return assetManifest;
    }
    QString attachmentIdentity(const QString &source, QUrl url) {
        if(!validPath(source))return {};
        url.setQuery(QString());url.setFragment(QString());
        return ManagedAssets::resolveIdentity({root,QUrl::fromLocalFile(root+'/'+source),managedManifest(),true},url);
    }
    QStringList attachmentReferenceTexts() {
        QStringList texts;
        // Old history/drafts do not record their original document base. Digest
        // references deliberately over-protect; they never dereference a path.
        for(const auto &v:query("SELECT text FROM notes UNION ALL SELECT text FROM versions UNION ALL SELECT text FROM drafts"))
            texts.append(v.toMap().value("text").toString());
        const auto templates=settings("templates");for(const auto &value:templates)texts.append(value.toString());
        for(const auto &draft:memoryDrafts)texts.append(draft.value("text").toString());
        for(const auto *stack:{&undoStack,&redoStack})for(const auto &command:*stack)for(const auto &change:command.changes) {
            texts.append(change.before.value("text").toString());texts.append(change.after.value("text").toString());
        }
        for(auto &text:texts)if(text.contains('%'))text+='\n'+QUrl::fromPercentEncoding(text.toUtf8());
        return texts;
    }
    int attachmentReferences(const QString &path,const QStringList &texts) const {
        const auto digest=ManagedAssets::digestForPath(path);int count=0;
        for(const auto &text:texts)if(text.contains(path)||(!digest.isEmpty()&&text.contains(digest)))++count;
        return count;
    }
    void addContext(QVariantMap &result) {
        result.insert("cacheFailures", cacheFailures);
        result.insert("assetManifest", managedManifest());
        result.insert("managedAssets", !result.value("path").toString().startsWith("external:"));
        if (!result.value("managedAssets").toBool()) result.insert("assetManifest", QVariantMap());
        result.insert("library", root);
        result.insert("entryRoot", library ? library->entryRoot() : configuredRoot);
    }
    QVariantList query(const QString &sql, const QVariantList &args = {}) {
        sqlite3_stmt *raw = nullptr;
        const auto statement = sql.toUtf8();
        require(sqlite3_prepare_v2(db, statement.constData(), -1, &raw, nullptr) == SQLITE_OK, "databaseError", QString::fromUtf8(sqlite3_errmsg(db)));
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> guard(raw, sqlite3_finalize);
        for (int i = 0; i < args.size(); ++i) {
            int result;
            if (!args[i].isValid()) result = sqlite3_bind_null(raw, i + 1);
            else if (args[i].metaType().id() == QMetaType::QByteArray) { const auto b = args[i].toByteArray(); result = sqlite3_bind_blob(raw, i + 1, b.constData(), b.size(), SQLITE_TRANSIENT); }
            else if (args[i].metaType().id() == QMetaType::Bool || args[i].metaType().id() == QMetaType::Int || args[i].metaType().id() == QMetaType::LongLong) result = sqlite3_bind_int64(raw, i + 1, args[i].toLongLong());
            else { const auto b = args[i].toString().toUtf8(); result = sqlite3_bind_text(raw, i + 1, b.constData(), b.size(), SQLITE_TRANSIENT); }
            require(result == SQLITE_OK, "databaseError", QString::fromUtf8(sqlite3_errmsg(db)));
        }
        QVariantList result; int state;
        while ((state = sqlite3_step(raw)) == SQLITE_ROW) {
            QVariantMap row;
            for (int c = 0; c < sqlite3_column_count(raw); ++c) {
                const auto name = QString::fromUtf8(sqlite3_column_name(raw, c));
                switch (sqlite3_column_type(raw, c)) {
                case SQLITE_INTEGER: row.insert(name, qlonglong(sqlite3_column_int64(raw, c))); break;
                case SQLITE_BLOB: row.insert(name, QByteArray(static_cast<const char *>(sqlite3_column_blob(raw, c)), sqlite3_column_bytes(raw, c))); break;
                case SQLITE_NULL: row.insert(name, QVariant()); break;
                default: row.insert(name, QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(raw, c)), sqlite3_column_bytes(raw, c))); break;
                }
            }
            result.append(row);
        }
        require(state == SQLITE_DONE, "databaseError", QString::fromUtf8(sqlite3_errmsg(db))); return result;
    }
    QVariantMap one(const QString &sql, const QVariantList &args = {}) { const auto rows = query(sql, args); return rows.isEmpty() ? QVariantMap() : rows.first().toMap(); }
    QVariantMap note(const QString &path, bool deleted = false) {
        auto n = one("SELECT * FROM notes WHERE path=? AND (deleted='' OR ?=1)", {path, deleted}); require(!n.isEmpty(), "notFound", path); return n;
    }
    QVariantMap settings(const QString &kind) {
        QVariantMap result; for (const auto &v : query("SELECT name,value FROM settings WHERE kind=?", {kind})) { auto r = v.toMap(); result.insert(r["name"].toString(), QJsonDocument::fromJson(r["value"].toByteArray()).object().value("value").toVariant()); } return result;
    }
    void setSetting(const QString &kind, const QString &name, const QVariant &value) {
        query("INSERT INTO settings(kind,name,value) VALUES(?,?,?) ON CONFLICT(kind,name) DO UPDATE SET value=excluded.value", {kind,name,QJsonDocument(QJsonObject{{"value",QJsonValue::fromVariant(value)}}).toJson(QJsonDocument::Compact)});
    }
    void folder(const QString &path) {
        require(validPath(path, true), "invalidName", path); if (path.isEmpty()) return;
        require(one("SELECT id FROM folders WHERE path=? AND deleted<>''", {path}).isEmpty(), "alreadyExists", path);
        QString current; for (const auto &part : path.split('/')) { current += (current.isEmpty() ? "" : "/") + part; require(one("SELECT id FROM folders WHERE path=? AND deleted<>''",{current}).isEmpty(),"alreadyExists",current); query("INSERT OR IGNORE INTO folders(id,path,deleted,trash_token,sort_order,created) VALUES(?,?,'','',(SELECT COALESCE(MAX(sort_order),-1)+1 FROM folders),?)", {uid(),current,utc()}); }
    }
    QString available(QString path, const QSet<QString> &reserved = {}) {
        require(validPath(path), "invalidName", path); const auto base = path;
        for (int count = 2; reserved.contains(path) || !one("SELECT id FROM notes WHERE path=?", {path}).isEmpty(); ++count) path = base.left(base.size() - 3) + " " + QString::number(count) + ".md";
        return path;
    }
    void version(const QVariantMap &n) {
        query("INSERT OR IGNORE INTO versions(note_id,hash,text,time) VALUES(?,?,?,?)", {n["id"],n["hash"],n["text"],utc()});
        query("DELETE FROM versions WHERE note_id=? AND rowid NOT IN (SELECT rowid FROM versions WHERE note_id=? ORDER BY rowid DESC LIMIT 50)", {n["id"],n["id"]});
    }
    void updateText(const QVariantMap &n, const QString &text) {
        version(n); query("UPDATE notes SET text=?,hash=?,modified=? WHERE id=?", {text,hashText(text),utc(),n["id"]});
        version(note(n["path"].toString(), true));
    }
    QVariantMap document(const QVariantMap &n) { return {{"ok",true},{"path",n["path"]},{"text",n["text"]},{"id",n["id"]},{"baseHash",n["hash"]},{"favorite",n["favorite"].toBool()},{"readOnly",!MarkdownFile::formatError(n["text"].toString()).isEmpty()},{"readOnlyReason",MarkdownFile::formatError(n["text"].toString())}}; }
    QVariantMap insert(const QString &requested, const QString &text, const QString &source = {}, const QString &sourceHash = {}, const QString &batch = {}) {
        require(MarkdownFile::formatError(text).isEmpty(), "unsupportedFormat", source);
        const auto path = available(requested); folder(parentFolder(path)); const auto id = uid(), time = utc();
        query("INSERT INTO notes(id,path,text,hash,created,modified,source,source_hash,import_batch) VALUES(?,?,?,?,?,?,?,?,?)", {id,path,text,hashText(text),time,time,source,sourceHash,batch});
        auto n = note(path); version(n); return document(n);
    }
    void refreshIndex() {
        QSet<QString> present;
        for (const auto &v : query("SELECT path,text,hash FROM notes WHERE deleted=''")) { const auto n=v.toMap(); const auto path=n["path"].toString(); present.insert(path); index.put(path,n["text"].toString(),n["hash"].toString(),MarkdownFile::title(n["text"].toString(),QFileInfo(path).completeBaseName())); }
        const auto keys = index.documents.keys(); for (const auto &path : keys) if (!present.contains(path)) index.remove(path);
    }
    QVariantMap listing(const QVariantMap &a = {}) {
        refreshIndex(); QVariantList all, matches, folders; QVariantMap folderIcons, folderDetails;
        QHash<QString, int> folderNoteCounts;
        for (const auto &v : query("SELECT path FROM notes WHERE deleted=''"))
            ++folderNoteCounts[parentFolder(v.toMap().value("path").toString())];
        for (const auto &v : query("SELECT path,icon,created FROM folders WHERE deleted='' ORDER BY sort_order,path")) {
            const auto folder = v.toMap(); folders.append(folder["path"]);
            folderIcons.insert(folder["path"].toString(), folder["icon"]);
            folderDetails.insert(folder["path"].toString(), QVariantMap{{"createdUtc", folder["created"]}, {"noteCount", folderNoteCounts.value(folder["path"].toString())}});
        }
        QString queryText=a.value("query").toString().trimmed(); const auto filter=a.value("filter").toString();
        QStringList tags; QVariantMap props; QString scope; bool favorite = false;
        static const QRegularExpression terms(R"rx((?:^|\s)(tag|folder|is|prop):("(?:[^"\\]|\\.)*"|'[^']*'|[^\s]+))rx");
        auto hits=terms.globalMatch(queryText); QList<QPair<int,int>> spans;
        while(hits.hasNext()) { const auto m=hits.next(); auto value=Knowledge::scalar(m.captured(2)); const auto key=m.captured(1); if(key=="tag")tags.append(value); if(key=="folder")scope=value; if(key=="is"&&value=="favorite")favorite=true; if(key=="prop"&&value.contains('='))props.insert(value.section('=',0,0),value.section('=',1)); spans.append({int(m.capturedStart()),int(m.capturedLength())}); }
        for(auto it=spans.crbegin();it!=spans.crend();++it)queryText.remove(it->first,it->second);queryText=queryText.trimmed();
        const auto candidates=index.candidates(queryText);
        for(const auto &v:query("SELECT * FROM notes WHERE deleted=''")) {
            const auto n=v.toMap(); const auto path=n["path"].toString(); const auto d=index.documents.value(path);
            QVariantMap row{{"id",n["id"]},{"path",path},{"title",d.title},{"summary",noteSummary(d.text)},{"baseHash",n["hash"]},{"pinned",n["pinned"].toBool()},{"favorite",n["favorite"].toBool()},{"createdUtc",n["created"]},{"modifiedUtc",n["modified"]},{"displayModifiedUtc",n["modified"]},{"tags",d.props.value("tags")},{"properties",d.props},{"source",n["source"]},{"importBatch",n["import_batch"]}};
            all.append(row);
            if(filter=="favorite"&&!n["favorite"].toBool())continue;
            if(filter=="inbox"&&!parentFolder(path).isEmpty())continue;
            if(filter.startsWith("folder:")&&parentFolder(path)!=filter.mid(7))continue;
            if(filter.startsWith("import:")&&n["import_batch"].toString()!=filter.mid(7))continue;
            if(!candidates.contains(path)||(!queryText.isEmpty()&&!d.text.contains(queryText,Qt::CaseInsensitive)&&!d.title.contains(queryText,Qt::CaseInsensitive)))continue;
            bool accept=true;for(const auto &tag:tags)if(!d.props.value("tags").toStringList().contains(tag))accept=false;
            for(auto it=props.begin();it!=props.end();++it)if(d.props.value(it.key()).toString()!=it.value().toString())accept=false;
            if(!accept||(!scope.isEmpty()&&parentFolder(path)!=scope)||(favorite&&!n["favorite"].toBool()))continue;
            const int pos=queryText.isEmpty() ? -1 : d.text.indexOf(queryText,0,Qt::CaseInsensitive);
            row.insert("match",pos);row.insert("matchText",queryText);
            if(pos>=0) {
                int bodyStart=0;
                if(d.text.startsWith("---\n")) {
                    const auto end=QRegularExpression("(?m)^(?:---|\\.\\.\\.)[ \\t]*$").match(d.text,4);
                    if(end.hasMatch())bodyStart=end.capturedEnd();
                }
                row.insert("summary",d.text.mid(qMax(bodyStart,qMax(0,pos-32)),150).simplified());
            }
            matches.append(row);
        }
        const auto sort=a.value("sort","modified").toString();const bool ascending=a.value("ascending").toBool();
        std::stable_sort(matches.begin(),matches.end(),[&](const QVariant &l,const QVariant &r){auto x=l.toMap(),y=r.toMap();if(x["pinned"].toBool()!=y["pinned"].toBool())return x["pinned"].toBool();const auto field=sort=="title"?"title":sort=="created"?"createdUtc":"displayModifiedUtc";int c=QString::localeAwareCompare(x[field].toString(),y[field].toString());return c==0?x["id"].toString()<y["id"].toString():ascending?c<0:c>0;});
        return {{"ok",true},{"notes",matches},{"allNotes",all},{"folders",folders},{"folderIcons",folderIcons},{"folderDetails",folderDetails},{"smartFolders",settings("smart")},{"initializationNotice",initializationNotice},{"duplicateIds",QVariantList()},{"unwatchedPaths",QStringList()},{"indexDocuments",index.documents.size()},{"indexUpdates",index.updates}};
    }
    void rewriteRelocatedLinks(const QHash<QString,QString> &moved) {
        // Preserve path-based Markdown links while identities remain unchanged.
        refreshIndex();
        for(const auto &v:query("SELECT * FROM notes")) {
            auto n=v.toMap(); const auto old=n["path"].toString(),next=moved.value(old,old);QString text=n["text"].toString();
            const bool rewritable=LinkPaths::rewrite(text,[&](const QString &target,bool wiki){if(wiki){auto paths=index.resolve(old,target,true);if(paths.size()!=1)return target;const auto destination=moved.value(paths[0],paths[0]);if(next==old&&destination==paths[0])return target;return relativeManagedPath(next,destination);}
                const QUrl url(target);const auto identity=attachmentIdentity(old,url);
                if(identity.isEmpty()&&url.path(QUrl::FullyDecoded).split('/').contains("assets"))return target;
                const auto resolved=identity.isEmpty()?LinkPaths::resolve(old,target):identity;if(resolved.isEmpty())return target;
                const auto destination=moved.value(resolved,resolved);if(next==old&&destination==resolved)return target;
                return relativeManagedUrl(next,destination,url);});
            require(rewritable,"unsupportedLinks",old);
            if(text!=n["text"].toString())updateText(n,text);
        }
    }
    void relocate(const QString &oldPath,const QString &newPath,bool tree) {
        require(validPath(newPath),"invalidName");
        require(!tree||(!newPath.startsWith(oldPath+'/')&&newPath!=oldPath),"invalidName");
        QHash<QString,QString> moved;
        for(const auto &v:query("SELECT path FROM notes")){auto p=v.toMap()["path"].toString();if(p==oldPath||(tree&&p.startsWith(oldPath+'/')))moved.insert(p,newPath+p.mid(oldPath.size()));}
        for(auto it=moved.begin();it!=moved.end();++it)require(one("SELECT id FROM notes WHERE path=?",{it.value()}).isEmpty(),"alreadyExists",it.value());
        rewriteRelocatedLinks(moved);
        if(tree){
            require(one("SELECT id FROM folders WHERE path=?",{newPath}).isEmpty(),"alreadyExists",newPath);folder(parentFolder(newPath));
            const auto rows=query("SELECT id,path FROM folders");for(const auto &v:rows){auto n=v.toMap();auto p=n["path"].toString();if(p==oldPath||p.startsWith(oldPath+'/'))query("UPDATE folders SET path=? WHERE id=?",{newPath+p.mid(oldPath.size()),n["id"]});}
            if (parentFolder(oldPath) != parentFolder(newPath))
                query("UPDATE folders SET sort_order=(SELECT COALESCE(MAX(sort_order),-1)+1 FROM folders) WHERE path=?", {newPath});
        }else folder(parentFolder(newPath));
        for(auto it=moved.begin();it!=moved.end();++it){query("UPDATE drafts SET path=? WHERE path=?",{it.value(),it.key()});query("UPDATE notes SET path=? WHERE path=?",{it.value(),it.key()});}
    }
    QString asset(const QByteArray &bytes,const QString &name) {
        QString suffix=QFileInfo(name).suffix().toLower();if(!QRegularExpression("^[a-z0-9]{1,8}$").match(suffix).hasMatch())suffix="bin";
        const auto path="assets/"+MarkdownFile::digest(bytes)+'.'+suffix;
        query("INSERT INTO assets(path,data,name,deleted) VALUES(?,?,?,'') ON CONFLICT(path) DO UPDATE SET deleted=''",{path,bytes,name});
        manifestDirty=true;
        // Materialized attachment cache is disposable; the BLOB is authoritative.
        const auto target=checkedCachePath(path);
        if(!QFileInfo::exists(target))writeBytes(target,bytes);return path;
    }
    QString rebaseAttachments(QString text,const QString &source,const QString &destination) {
        if(!validPath(source))return text;
        rewriteExportLinks(text,[&](const QString &target,bool wiki){
            if(wiki)return target;const QUrl url(target);const auto owned=attachmentIdentity(source,url);
            return owned.isEmpty()?target:relativeManagedUrl(destination,owned,url);
        });
        return text;
    }
    QString rebaseText(QString text,const QString &source,const QString &destination) {
        refreshIndex();
        require(LinkPaths::rewrite(text,[&](const QString &target,bool wiki){
            QString resolved;
            if(wiki) { const auto choices=index.resolve(source,target,true);if(choices.size()!=1)return target;resolved=choices[0]; }
            else {
                const QUrl url(target);resolved=attachmentIdentity(source,url);
                if(resolved.isEmpty()&&url.path(QUrl::FullyDecoded).split('/').contains("assets"))return target;
                if(resolved.isEmpty())resolved=LinkPaths::resolve(source,target);
            }
            if(resolved.isEmpty())return target;
            if(wiki)return relativeManagedPath(destination,resolved);
            return relativeManagedUrl(destination,resolved,QUrl(target));
        }),"unsupportedLinks");
        return text;
    }
    QVariantMap run(const QString &op,const QVariantMap &a);
    QVariantMap importPreview(const QVariantMap &a);
    QVariantMap importItems(const QVariantMap &a);
    QVariantMap exportItems(const QVariantMap &a);
    QVariantMap backup(const QVariantMap &a);
    QVariantMap restoreBackup(const QVariantMap &a);
};

ManagedStore::ManagedStore(const QString &directory, bool retainInitializationFailure) : d(std::make_unique<Impl>()) {
    d->configuredRoot = QDir(directory).absolutePath();
    d->root = d->configuredRoot;
    try {
        d->library = std::make_unique<ManagedLibrary>(directory);
        d->root = d->library->activeRoot();
        d->preflight();
        d->initializeDatabase();
    } catch (const StoreError &error) {
        if (!retainInitializationFailure) throw;
        if (d->db) { sqlite3_close(d->db); d->db = nullptr; }
        d->openError = {{"ok", false}, {"error", error.code}, {"detail", error.detail},
            {"entryRoot", d->configuredRoot}, {"failedDirectory", d->root},
            {"recoveryAvailable", bool(d->library) && error.code != "libraryLocked"}};
    }
}
void ManagedStore::Impl::initializeDatabase() {
    require(sqlite3_open_v2(QFile::encodeName(root+"/notes.sqlite3").constData(),&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)==SQLITE_OK,"databaseError");
    sqlite3_busy_timeout(db,5000);
    query("PRAGMA trusted_schema=OFF");
    query("PRAGMA journal_mode=WAL");query("PRAGMA synchronous=FULL");query("PRAGMA foreign_keys=ON");
    require(one("PRAGMA user_version").value("user_version").toInt()<=1,"databaseVersion","This database was created by a newer version.");
    query("CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,path TEXT NOT NULL UNIQUE,text TEXT NOT NULL,hash TEXT NOT NULL,created TEXT NOT NULL,modified TEXT NOT NULL,pinned INTEGER NOT NULL DEFAULT 0,favorite INTEGER NOT NULL DEFAULT 0,deleted TEXT NOT NULL DEFAULT '',trash_token TEXT NOT NULL DEFAULT '',source TEXT NOT NULL DEFAULT '',source_hash TEXT NOT NULL DEFAULT '',import_batch TEXT NOT NULL DEFAULT '')");
    query("CREATE TABLE IF NOT EXISTS folders(id TEXT PRIMARY KEY,path TEXT NOT NULL UNIQUE,deleted TEXT NOT NULL DEFAULT '',trash_token TEXT NOT NULL DEFAULT '',icon TEXT NOT NULL DEFAULT 'folder')");
    initializeFolderMetadata();
    query("CREATE TABLE IF NOT EXISTS drafts(path TEXT PRIMARY KEY,text TEXT NOT NULL,base_hash TEXT NOT NULL,time TEXT NOT NULL)");
    query("CREATE TABLE IF NOT EXISTS versions(note_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,hash TEXT NOT NULL,text TEXT NOT NULL,time TEXT NOT NULL,PRIMARY KEY(note_id,hash))");
    query("CREATE TABLE IF NOT EXISTS assets(path TEXT PRIMARY KEY,data BLOB NOT NULL,name TEXT NOT NULL,deleted TEXT NOT NULL DEFAULT '')");
    query("CREATE TABLE IF NOT EXISTS imports(id TEXT PRIMARY KEY,name TEXT NOT NULL,time TEXT NOT NULL,report BLOB NOT NULL)");
    query("CREATE TABLE IF NOT EXISTS settings(kind TEXT NOT NULL,name TEXT NOT NULL,value BLOB NOT NULL,PRIMARY KEY(kind,name))");
    query("CREATE INDEX IF NOT EXISTS notes_source ON notes(source,source_hash)");query("CREATE INDEX IF NOT EXISTS notes_state ON notes(deleted,modified)");query("PRAGMA user_version=1");
    query("PRAGMA application_id=1313558065");
    initializeFolders();
    initializeUndo();
    cacheFailures=0;
    for(const auto &v:query("SELECT path,data FROM assets WHERE deleted=''")) {
        const auto row=v.toMap();
        try {
            const auto target=checkedCachePath(row["path"].toString());
            if(!QFileInfo::exists(target)) { failAt("cacheWrite"); writeBytes(target,row["data"].toByteArray()); }
        } catch(const StoreError &) { ++cacheFailures; }
    }
    if(cacheFailures>0) {
        if(!initializationNotice.isEmpty())initializationNotice+='\n';
        initializationNotice+="部分附件缓存未能生成，原件仍保留在数据库中。请检查磁盘空间与权限后重新打开。";
    }
}
void ManagedStore::Impl::validateDatabase(bool backup, bool legacy) {
    query("PRAGMA trusted_schema=OFF");
    const auto version = one("PRAGMA user_version").value("user_version").toInt();
    require(version <= 1, backup ? "invalidBackup" : "databaseVersion", "This database was created by a newer version.");
    require(version == 1 || (legacy && version == 0), backup ? "invalidBackup" : "databaseCorrupt");
    const auto application = one("PRAGMA application_id").value("application_id").toInt();
    require(application == 1313558065 || (!backup && application == 0), "databaseCorrupt", "Unrecognized database application ID");
    const auto check = query("PRAGMA quick_check");
    require(check.size() == 1 && check.first().toMap().value("quick_check").toString() == "ok", "databaseCorrupt");
    require(query("PRAGMA foreign_key_check").isEmpty(), "databaseCorrupt");
    const QMap<QString, QStringList> required{
        {"notes", QString("id,path,text,hash,created,modified,pinned,favorite,deleted,trash_token,source,source_hash,import_batch").split(',')},
        {"folders", QString("id,path,deleted,trash_token").split(',')},
        {"drafts", QString("path,text,base_hash,time").split(',')},
        {"versions", QString("note_id,hash,text,time").split(',')},
        {"assets", QString("path,data,name,deleted").split(',')},
        {"imports", QString("id,name,time,report").split(',')},
        {"settings", QString("kind,name,value").split(',')}
    };
    QSet<QString> tables;
    for (const auto &v : query("SELECT name,type,tbl_name,sql FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'")) {
        const auto row = v.toMap(); const auto name = row.value("name").toString();
        if (row.value("type") == "index") {
            require(required.contains(row.value("tbl_name").toString()), "databaseCorrupt"); continue;
        }
        require(row.value("type") == "table" && required.contains(name)
                && QRegularExpression("^CREATE\\s+TABLE\\s", QRegularExpression::CaseInsensitiveOption).match(row.value("sql").toString()).hasMatch(), "databaseCorrupt");
        tables.insert(name);
    }
    // Only the documented version-zero, folder-only legacy store is partial.
    const bool partialLegacy = legacy && version == 0 && (tables.isEmpty() || tables == QSet<QString>{"folders"});
    require(partialLegacy || tables.size() == required.size(), "databaseCorrupt", "Missing managed tables");
    const QMap<QString,QStringList> primaryKeys{{"notes",{"id"}},{"folders",{"id"}},{"drafts",{"path"}},
        {"versions",{"note_id","hash"}},{"assets",{"path"}},{"imports",{"id"}},{"settings",{"kind","name"}}};
    for (const auto &table : tables) {
        QSet<QString> columns;
        QMap<int,QString> keyColumns;
        const auto optional = table == "folders" ? QStringList{"icon", "sort_order", "created"} : QStringList();
        for (const auto &v : query("PRAGMA table_xinfo(" + table + ')')) {
            const auto column = v.toMap(); const auto name = column.value("name").toString();
            require(column.value("hidden").toInt() == 0 && (required.value(table).contains(name) || optional.contains(name)), "databaseCorrupt", table);
            columns.insert(name);
            if(column.value("pk").toInt()>0)keyColumns.insert(column.value("pk").toInt(),name);
        }
        require(keyColumns.values()==primaryKeys.value(table),"databaseCorrupt","Invalid primary key: "+table);
        for (const auto &column : required.value(table)) require(columns.contains(column), "databaseCorrupt", table + '.' + column);
        query("SELECT " + required.value(table).join(',') + " FROM " + table + " LIMIT 0");
        if(table=="notes" || table=="folders") {
            bool uniquePath=false;
            for(const auto &v:query("PRAGMA index_list("+table+')')) {
                const auto index=v.toMap();if(!index.value("unique").toBool() || index.value("partial").toBool())continue;
                const auto fields=query("SELECT name FROM pragma_index_info(?)",{index.value("name")});
                if(fields.size()==1 && fields.first().toMap().value("name")=="path")uniquePath=true;
            }
            require(uniquePath,"databaseCorrupt","Missing unique path: "+table);
        }
        if(table=="versions") {
            query("SELECT rowid FROM versions LIMIT 0");
            const auto keys=query("PRAGMA foreign_key_list(versions)");
            require(keys.size()==1 && keys.first().toMap().value("table")=="notes" && keys.first().toMap().value("from")=="note_id"
                && keys.first().toMap().value("to")=="id" && keys.first().toMap().value("on_delete")=="CASCADE","databaseCorrupt","Invalid version ownership");
        }
    }
    for (const auto &table : {QString("notes"), QString("folders"), QString("drafts")}) {
        if (!tables.contains(table)) continue;
        for (const auto &v : query("SELECT path FROM " + table)) {
            const auto path = v.toMap().value("path").toString();
            bool valid = validPath(path);
            if (table == "drafts" && path.startsWith("external:"))
                valid = QDir::isAbsolutePath(path.mid(9)) && !path.contains(QChar::Null) && !path.contains('\n') && !path.contains('\r');
            require(valid, "databaseCorrupt", "Invalid stored path");
        }
    }
    if (tables.contains("assets")) {
        static const QRegularExpression identity("^assets/([0-9a-f]{64})\\.[a-z0-9]{1,8}$");
        for (const auto &v : query("SELECT path,data FROM assets")) {
            const auto row = v.toMap(); const auto match = identity.match(row.value("path").toString());
            require(match.hasMatch() && row.value("data").metaType().id() == QMetaType::QByteArray
                    && row.value("data").toByteArray().size() <= 32 * 1024 * 1024
                    && match.captured(1) == MarkdownFile::digest(row.value("data").toByteArray()), "databaseCorrupt", "Invalid asset identity");
        }
    }
}
void ManagedStore::Impl::preflight() {
    const auto file = root + "/notes.sqlite3";
    if (!QFileInfo::exists(file) && !QFileInfo(file).isSymLink()) {
        require(!QFileInfo::exists(file + "-wal") && !QFileInfo::exists(file + "-shm")
                && !QFileInfo(file + "-wal").isSymLink() && !QFileInfo(file + "-shm").isSymLink(), "databaseCorrupt", "Database is missing but sidecars remain");
        return;
    }
    // Even SQLITE_OPEN_READONLY can create/write shared-memory sidecars for a
    // WAL database. Copy under our retained lease, and probe only the private
    // snapshot. The original database, WAL and SHM are never opened by SQLite.
    QTemporaryDir temporary; require(temporary.isValid(), "metadataUnavailable");
    const auto snapshot = temporary.filePath("notes.sqlite3");
    for (const auto &suffix : {QString(), QString("-wal"), QString("-shm"), QString("-journal")}) {
        const auto source = file + suffix; const QFileInfo info(source);
        require(!info.isSymLink() && (!info.exists() || info.isFile()), "unsafePath", source);
        if (info.exists() && suffix != "-shm") streamCopy(source, snapshot + suffix);
    }
    try {
        Impl inspection;
        inspection.db = openDatabase(snapshot, SQLITE_OPEN_READWRITE).release();
        inspection.query("PRAGMA query_only=ON");
        inspection.validateDatabase(false, true);
        inspection.query("PRAGMA query_only=OFF");
        // Exercise legacy migrations before allowing them to touch the original.
        if (inspection.one("PRAGMA user_version").value("user_version").toInt() == 1) {
            inspection.initializeFolderMetadata(); inspection.initializeFolders();
            inspection.listing();
        }
        if (!inspection.one("SELECT name FROM sqlite_master WHERE type='table' AND name='assets'").isEmpty())
            for (const auto &asset : inspection.query("SELECT path FROM assets")) checkedCachePath(asset.toMap().value("path").toString());
    } catch (const StoreError &error) {
        if (error.code == "databaseVersion" || error.code == "unsafePath") throw;
        throw StoreError{"databaseCorrupt", error.detail};
    }
}
void ManagedStore::Impl::prepareBackup(const QString &source, const QString &snapshot) {
    require(QFileInfo(source).isFile() && !QFileInfo(source).isSymLink(), "invalidBackup");
    streamCopy(source, snapshot);
    try {
        Impl inspection;
        inspection.db = openDatabase(snapshot, SQLITE_OPEN_READWRITE).release();
        inspection.validateDatabase(true);
        inspection.initializeFolderMetadata(); inspection.initializeFolders();
        inspection.listing();
        inspection.query("PRAGMA journal_mode=DELETE");
    } catch (const StoreError &error) {
        throw StoreError{"invalidBackup", error.detail.isEmpty() ? QStringLiteral("备份内容或结构无法读取") : error.detail};
    }
    recoveryStage("snapshotValidated");
}

void ManagedStore::Impl::initializeFolders() {
    const auto initialized = settings("initialization");
    if (initialized.value("defaultFolderCreated").toBool() && initialized.value("quickNotesFolderCreated").toBool()) return;
    query("BEGIN IMMEDIATE");
    try {
        // These are ordinary folders after seeding. Never recreate them after
        // a rename, move, trash, or permanent deletion.
        if (!initialized.value("defaultFolderCreated").toBool()) {
            query("INSERT OR IGNORE INTO folders(id,path,deleted,trash_token) VALUES(?,?,'','')", {uid(),QStringLiteral("默认文件夹")});
            setSetting("initialization", "defaultFolderCreated", true);
        }
        if (!initialized.value("quickNotesFolderCreated").toBool()) {
            const QString quickFolder = QStringLiteral("随手记");
            query("INSERT OR IGNORE INTO folders(id,path,deleted,trash_token) VALUES(?,?,'','')", {uid(),quickFolder});
            query("SAVEPOINT migrate_inbox");
            try {
                const auto roots = query("SELECT path FROM notes WHERE deleted='' AND instr(path,'/')=0 ORDER BY path");
                require(!one("SELECT id FROM folders WHERE path=? AND deleted=''", {quickFolder}).isEmpty(), "missingFolder");
                // Recovery text and its baseline must stay together. Leave all
                // root notes untouched if recovering drafts must happen first.
                require(roots.isEmpty() || one("SELECT path FROM drafts LIMIT 1").isEmpty(), "pendingDrafts");
                QHash<QString,QString> moved;
                QSet<QString> reserved;
                for (const auto &v : roots) {
                    const auto old = v.toMap()["path"].toString();
                    const auto next = available(quickFolder + '/' + old, reserved);
                    moved.insert(old, next); reserved.insert(next);
                }
                if (!moved.isEmpty()) rewriteRelocatedLinks(moved);
                for (auto it = moved.cbegin(); it != moved.cend(); ++it)
                    query("UPDATE notes SET path=? WHERE path=?", {it.value(),it.key()});
                const auto smartFolders = settings("smart");
                for (auto it = smartFolders.cbegin(); it != smartFolders.cend(); ++it) {
                    auto view = it.value().toMap();
                    if (view.value("filter") != "inbox") continue;
                    view.insert("filter", "folder:" + quickFolder);
                    setSetting("smart", it.key(), view);
                }
            } catch (const StoreError &error) {
                query("ROLLBACK TO migrate_inbox");
                if (error.code != "pendingDrafts" && error.code != "unsupportedLinks" && error.code != "missingFolder") throw;
                const auto reason = error.code == "pendingDrafts" ? QStringLiteral("有待恢复的草稿")
                    : error.code == "unsupportedLinks" ? QStringLiteral("包含无法自动调整的链接")
                    : QStringLiteral("同名文件夹位于最近删除中");
                initializationNotice = QStringLiteral("原有未分类笔记因%1而保留原位，可在“全部笔记”中查看和整理。").arg(reason);
            }
            query("RELEASE migrate_inbox");
            setSetting("initialization", "quickNotesFolderCreated", true);
        }
        query("COMMIT");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}
void ManagedStore::Impl::initializeUndo() {
    query("CREATE TEMP TABLE capture_keys(table_name TEXT,id TEXT,PRIMARY KEY(table_name,id))");
    query("CREATE TEMP TABLE capture_enabled(value INTEGER)");
    query("INSERT INTO capture_enabled VALUES(0)");
    for (const auto &table : {QString("notes"), QString("folders")}) {
        query("CREATE TEMP TABLE capture_" + table + " AS SELECT * FROM " + table + " WHERE 0");
        QStringList oldColumns;
        for (const auto &v : query("PRAGMA table_info(" + table + ")")) oldColumns << "OLD." + v.toMap()["name"].toString();
        const QString first = " NOT EXISTS(SELECT 1 FROM capture_keys WHERE table_name='" + table + "' AND id=OLD.id)";
        for (const auto &event : {QString("UPDATE"), QString("DELETE")})
            query("CREATE TEMP TRIGGER capture_" + table + event + " BEFORE " + event + " ON main." + table +
                  " WHEN (SELECT value FROM capture_enabled)=1 BEGIN INSERT INTO capture_" + table + " SELECT " + oldColumns.join(',') + " WHERE" + first +
                  "; INSERT OR IGNORE INTO capture_keys VALUES('" + table + "',OLD.id); END");
        query("CREATE TEMP TRIGGER capture_" + table + "INSERT AFTER INSERT ON main." + table +
              " WHEN (SELECT value FROM capture_enabled)=1 BEGIN INSERT OR IGNORE INTO capture_keys VALUES('" + table + "',NEW.id); END");
    }
}
void ManagedStore::Impl::beginCapture() {
    query("DELETE FROM capture_keys"); query("DELETE FROM capture_notes"); query("DELETE FROM capture_folders");
    query("UPDATE capture_enabled SET value=1");
}
ManagedStore::Impl::Command ManagedStore::Impl::finishCapture(const QString &label) {
    query("UPDATE capture_enabled SET value=0");
    Command command{label, {}};
    for (const auto &v : query("SELECT * FROM capture_keys")) {
        const auto key = v.toMap(); const auto table = key["table_name"].toString(), id = key["id"].toString();
        auto before = one("SELECT * FROM capture_" + table + " WHERE id=?", {id});
        auto after = one("SELECT * FROM " + table + " WHERE id=?", {id});
        if (before != after) command.changes.append({table,id,before,after});
    }
    query("DELETE FROM capture_keys"); query("DELETE FROM capture_notes"); query("DELETE FROM capture_folders");
    return command;
}
QString ManagedStore::Impl::commandLabel(const QString &op, const QVariantMap &a) const {
    if (op == "editFolder") return QStringLiteral("编辑文件夹");
    if (op == "moveFolder") return parentFolder(a.value("path").toString()) == a.value("folder").toString()
        ? QStringLiteral("调整文件夹顺序") : QStringLiteral("移动文件夹");
    if (op=="batch" || op=="tagBatch") {
        const QHash<QString,QString> labels{{"move","移动笔记"},{"trash","删除笔记"},{"duplicate","创建副本"},{"pin","置顶"},{"unpin","取消置顶"},{"favorite","收藏"},{"unfavorite","取消收藏"}};
        return op=="tagBatch" ? "修改标签" : labels.value(a.value("action").toString());
    }
    if (op=="trashBatch") return a.value("action")=="restore" ? "恢复笔记" : QString();
    const QHash<QString,QString> labels{{"createFolder","新建文件夹"},{"renameFolder","整理文件夹"},{"trashFolder","删除文件夹"},{"renameNote","修改标题"},{"setProperty","修改笔记信息"},{"favorite","收藏"},{"pin","置顶"},{"restore","恢复笔记"},{"restoreFolder","恢复文件夹"},{"create","新建笔记"},{"createWiki","新建笔记"},{"fromTemplate","从模板新建笔记"},{"dailyNote","新建每日笔记"},{"saveAs","创建副本"},{"rename","移动笔记"},{"trash","删除笔记"}};
    return labels.value(op);
}
QVariantMap ManagedStore::Impl::historyState() const {
    return {{"canUndo",!undoStack.isEmpty()},{"canRedo",!redoStack.isEmpty()},
            {"undoLabel",undoStack.isEmpty()?QString():undoStack.last().label},
            {"redoLabel",redoStack.isEmpty()?QString():redoStack.last().label}};
}
QVariantMap ManagedStore::Impl::applyHistory(bool redo) {
    auto &from = redo ? redoStack : undoStack;
    require(!from.isEmpty(),"emptyHistory");
    const auto &command = from.last();
    // Validate only the columns this command changes. Later body edits survive
    // an unrelated pin/move; a changed body touched by this command conflicts.
    for (const auto &c : command.changes) {
        const auto expected = redo ? c.before : c.after, desired = redo ? c.after : c.before;
        const auto current = one("SELECT * FROM " + c.table + " WHERE id=?",{c.id});
        require(current.isEmpty()==expected.isEmpty(),"undoConflict");
        if (expected.isEmpty()) continue;
        for (auto it=expected.begin();it!=expected.end();++it)
            if (it.key()!="modified" && (desired.isEmpty() || desired.value(it.key())!=it.value()))
                require(current.value(it.key())==it.value(),"undoConflict");
    }
    QVariantList affected, folderChanges;
    // Avoid transient UNIQUE path collisions when restoring a whole hierarchy.
    for (const auto &c : command.changes) {
        const auto expected=redo?c.before:c.after, desired=redo?c.after:c.before;
        if (!expected.isEmpty() && (desired.isEmpty() || desired.value("path")!=expected.value("path")))
            query("UPDATE " + c.table + " SET path=? WHERE id=?",{".undo-"+uid(),c.id});
    }
    for (const auto &c : command.changes) {
        const auto expected=redo?c.before:c.after, desired=redo?c.after:c.before;
        if (desired.isEmpty()) query("DELETE FROM " + c.table + " WHERE id=?",{c.id});
        else if (expected.isEmpty()) {
            QStringList fields, placeholders; QVariantList values;
            for (auto it=desired.begin();it!=desired.end();++it) { fields<<it.key();placeholders<<"?";values<<it.value(); }
            query("INSERT INTO " + c.table + "("+fields.join(',')+") VALUES("+placeholders.join(',')+")",values);
            if(c.table=="notes")version(desired);
        } else {
            QStringList fields; QVariantList values;
            for(auto it=desired.begin();it!=desired.end();++it) if(it.value()!=expected.value(it.key())){fields<<it.key()+"=?";values<<it.value();}
            if(!fields.isEmpty()){values<<c.id;query("UPDATE "+c.table+" SET "+fields.join(',')+" WHERE id=?",values);}
        }
        if(c.table=="notes") affected.append(QVariantMap{{"id",c.id},{"path",desired.value("path")},{"needsRead",expected.value("text")!=desired.value("text") || expected.value("path")!=desired.value("path") || expected.value("deleted")!=desired.value("deleted")},{"deleted",desired.isEmpty()||!desired.value("deleted").toString().isEmpty()}});
        else folderChanges.append(QVariantMap{{"from",expected.value("path")},{"to",desired.isEmpty()||!desired.value("deleted").toString().isEmpty()?QString():desired.value("path").toString()}});
    }
    // A folder created by an earlier command may now contain imported notes.
    // Do not orphan those notes by undoing its creation.
    QSet<QString> activeFolders;
    for(const auto &v:query("SELECT path FROM folders WHERE deleted=''"))activeFolders.insert(v.toMap()["path"].toString());
    for(const auto &v:query("SELECT path FROM notes WHERE deleted='' UNION SELECT path FROM folders WHERE deleted=''")) {
        const auto parent=parentFolder(v.toMap()["path"].toString());
        require(parent.isEmpty()||activeFolders.contains(parent),"undoConflict");
    }
    return {{"ok",true},{"affected",affected},{"primaryId",command.primaryId},{"folderChanges",folderChanges},{"label",command.label}};
}
ManagedStore::~ManagedStore() = default;
QString ManagedStore::directory() const { return d->root; }
QString ManagedStore::entryRoot() const { return d->library ? d->library->entryRoot() : d->configuredRoot; }
bool ManagedStore::hasPendingMemoryDrafts() const { return d->hasPendingMemoryDrafts(); }
QVariantMap ManagedStore::initializationError() const { return d->openError; }
QVariantMap ManagedStore::dispatch(const QString &op,const QVariantMap &a, std::function<void(const QVariantMap &)> progress, std::shared_ptr<std::atomic_bool> cancelled) {
    d->progress = std::move(progress); d->cancelled = std::move(cancelled);
    if (!d->openError.isEmpty()) {
        if (op == "restoreBackup" && d->openError.value("recoveryAvailable").toBool()) {
            try { return d->recoverInitialization(a); }
            catch (const StoreError &error) {
                const bool uncertain = d->library && d->library->activationUncertain();
                if (uncertain) d->openError = {{"ok",false},{"error","recoveryActivationUncertain"},{"recoveryAvailable",false},{"activationUncertain",true}};
                return {{"ok",false},{"error",uncertain ? QString("recoveryActivationUncertain") : error.code},
                    {"detail",error.detail},{"activationUncertain",uncertain},{"entryRoot",entryRoot()},
                    {"preservedDirectory",d->root},{"recoveryAvailable",!uncertain}};
            }
        }
        return d->openError;
    }
    const bool outer=sqlite3_get_autocommit(d->db);
    try {
        if (op == "restoreBackup" || op == "importNotes" || op == "rename" || op == "renameFolder" || op == "moveFolder" || op == "editFolder"
                || op == "trash" || op == "trashFolder" || op == "purgeFolder" || op == "undo" || op == "redo"
                || (op == "batch" && QStringList{"move","trash","purgeNotes"}.contains(a.value("action").toString())))
            require(!hasPendingMemoryDrafts(), "pendingDrafts", "Persist or discard in-memory drafts before replacing or reorganizing the library.");
        // These operations own independent draft transactions and must never be
        // rolled back with the later external-file operation.
        if (op == "checkpoint" || op == "discardDraft" || (op == "save" && a.value("path").toString().startsWith("external:"))) {
            auto result = op == "checkpoint" ? d->checkpoint(a) : op == "discardDraft" ? d->discardDraft(a.value("path").toString()) : d->saveExternal(a);
            result.insert("undoState", d->historyState());
            result.insert("entryRoot", entryRoot()); return result;
        }
        const bool ownTransaction = outer && op != "backup" && op != "restoreBackup" && op != "importNotes" && op != "recovery";
        if(ownTransaction)d->query("BEGIN IMMEDIATE");
        const auto label=d->commandLabel(op,a);
        if(!label.isEmpty())d->beginCapture();
        auto result = op=="undo" || op=="redo" ? d->applyHistory(op=="redo") : d->run(op,a);
        Impl::Command command;
        if(!label.isEmpty()) {
            command=d->finishCapture(label);
            QString primary=a.value("path").toString();
            if(primary.isEmpty()&&!a.value("items").toList().isEmpty())primary=a.value("items").toList().first().toMap().value("path").toString();
            if(primary.isEmpty())primary=result.value("path").toString();
            for(const auto &c:command.changes)if(c.table=="notes"&&(c.before.value("path").toString()==primary||c.after.value("path").toString()==primary)){command.primaryId=c.id;break;}
        }
        if(ownTransaction)d->query("COMMIT");
        if(op=="undo" || op=="redo") {
            auto &from=op=="redo"?d->redoStack:d->undoStack;
            auto &to=op=="redo"?d->undoStack:d->redoStack;
            to.append(from.takeLast());
        } else if(!command.changes.isEmpty()) {
            d->undoStack.append(std::move(command)); d->redoStack.clear();
            auto retainedBytes=[&] { qint64 bytes=0;for(const auto &entry:d->undoStack)for(const auto &c:entry.changes)bytes+=2*(c.before.value("text").toString().size()+c.after.value("text").toString().size());return bytes; };
            // Keep recent organization commands bounded, including rewritten links.
            while(d->undoStack.size()>50 || (d->undoStack.size()>1 && retainedBytes()>64*1024*1024))d->undoStack.removeFirst();
            result.insert("historyRecorded",true);
        } else if(op=="clearRedo") d->redoStack.clear();
        else if(op=="importNotes" || op=="restoreBackup" || op=="purgeFolder" || (op=="trashBatch"&&a.value("action")=="purge")
                || (op=="batch" && a.value("action")=="purgeNotes" && result.value("succeeded").toInt()>0)) {d->undoStack.clear();d->redoStack.clear();}
        if (result.value("ok").toBool() && (op == "save" || op == "trash")) d->memoryDrafts.remove(a.value("path").toString());
        result.insert("undoState",d->historyState());
        if (result.value("ok").toBool() && (op=="list" || op=="search" || op=="selectLibrary" || op=="reindex"
                || op=="read" || op=="restoreBackup" || op=="importNotes" || op=="importAttachment"
                || op=="quarantineAttachment" || op=="restoreAttachment" || op=="restore" || op=="restoreFolder" || op=="create" || op=="saveAs"
                || op=="fromTemplate" || op=="dailyNote" || op=="createWiki")) d->addContext(result);
        return result;
    }catch(const StoreError &e){if(outer)sqlite3_exec(d->db,"ROLLBACK",nullptr,nullptr,nullptr);d->index=Knowledge::Index();d->manifestDirty=true;return {{"ok",false},{"error",e.code},{"detail",e.detail}};}
}

QVariantMap ManagedStore::Impl::checkpoint(const QVariantMap &a) {
    const auto path = a.value("path").toString();
    require(path.startsWith("external:") ? QDir::isAbsolutePath(path.mid(9)) && !path.contains(QChar::Null) && !path.contains('\n') && !path.contains('\r') : validPath(path), "invalidName");
    QVariantMap draft{{"path",path},{"text",a.value("text").toString()},{"baseHash",a.value("baseHash").toString()},
        {"updatedUtc",utc()},{"recoveryDurable",false}};
    memoryDrafts.insert(path,draft);
    try {
        failAt("draftBegin"); query("BEGIN IMMEDIATE");
        query("INSERT INTO drafts(path,text,base_hash,time) VALUES(?,?,?,?) ON CONFLICT(path) DO UPDATE SET text=excluded.text,base_hash=excluded.base_hash,time=excluded.time",
              {path,draft.value("text"),draft.value("baseHash"),draft.value("updatedUtc")});
        failAt("draftCommit"); query("COMMIT");
        // Once committed, the database is authoritative. Keeping a duplicate
        // memory entry would become stale after note relocation or deletion.
        memoryDrafts.remove(path);
    } catch (const StoreError &error) {
        sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        return {{"ok",false},{"error","recoveryWriteFailed"},{"detail",error.detail},{"draftInMemory",true},{"recoveryDurable",false}};
    }
    return {{"ok",true},{"recoveryDurable",true},{"draftInMemory",false}};
}
QVariantMap ManagedStore::Impl::discardDraft(const QString &path) {
    try {
        query("BEGIN IMMEDIATE"); failAt("draftCleanup");
        query("DELETE FROM drafts WHERE path=?",{path});
        failAt("draftCleanupCommit"); query("COMMIT");
        memoryDrafts.remove(path);
        return ok();
    } catch (const StoreError &error) {
        sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
        return {{"ok",false},{"error","discardFailed"},{"detail",error.detail},{"draftRetained",true}};
    }
}
QVariantMap ManagedStore::Impl::saveExternal(const QVariantMap &a) {
    auto draftResult = checkpoint(a);
    if (!draftResult.value("ok").toBool()) return draftResult;
    externalStage("draftCommitted");
    const auto path=a.value("path").toString(), file=path.mid(9), text=a.value("text").toString(), base=a.value("baseHash").toString();
    try {
        require(external.contains(path) && external.value(path).hash == base,"unknownBase");
        auto document=external.value(path);
        require(document.readOnlyReason.isEmpty(),"readOnly",document.readOnlyReason);
        require(MarkdownFile::formatError(text).isEmpty(),"unsupportedFormat");
        require(QFileInfo::exists(file),"notFound",file);
        const auto current=MarkdownFile::decode(bytesAt(file));
        if (current.hash != base) return {{"ok",false},{"error","conflict"},{"diskHash",current.hash},{"diskText",current.text},{"draftInMemory",false},{"recoveryDurable",true}};
        if(a.value("sourceNewlines").toBool())document.trailingNewline=text.endsWith('\n');
        const auto bytes=MarkdownFile::encode(document,text);
        failAt("externalWrite");
        QSaveFile output(file); output.setDirectWriteFallback(false);
        require(output.open(QIODevice::WriteOnly) && output.write(bytes)==bytes.size() && output.commit(),"writeFailed",output.errorString());
        external.insert(path,MarkdownFile::decode(bytes));
        // The disk baseline changes before cleanup. Cleanup failure must never
        // tell the editor that its successful write failed or kept the old hash.
        externalStage("externalWritten"); externalStage("beforeDraftCleanup");
        const auto cleanup=discardDraft(path);
        QVariantMap result{{"ok",true},{"path",path},{"baseHash",MarkdownFile::digest(bytes)},{"recoveryDurable",true}};
        if(!cleanup.value("ok").toBool()) {
            result.insert("draftRetained",true); result.insert("recoveryCleanupFailed",true);
            result.insert("warning","recoveryCleanupFailed"); result.insert("detail",cleanup.value("detail"));
        }
        return result;
    } catch(const StoreError &error) {
        return {{"ok",false},{"error",error.code},{"detail",error.detail},{"draftInMemory",false},{"recoveryDurable",true}};
    }
}
QVariantList ManagedStore::Impl::recoveryDrafts() {
    QMap<QString,QVariantMap> merged;
    for(const auto &v:query("SELECT * FROM drafts")) {
        const auto row=v.toMap();
        merged.insert(row.value("path").toString(),{{"path",row.value("path")},{"text",row.value("text")},
            {"baseHash",row.value("base_hash")},{"updatedUtc",row.value("time")},{"recoveryDurable",true}});
    }
    for(auto it=memoryDrafts.cbegin();it!=memoryDrafts.cend();++it)merged.insert(it.key(),it.value());
    QVariantList result;
    for(auto draft:merged) {
        const auto path=draft.value("path").toString();
        draft.insert("title",MarkdownFile::title(draft.value("text").toString(),"未命名笔记"));
        if(path.startsWith("external:")) {
            // Backup-owned paths are data, never an instruction to read a file.
            const bool known=external.contains(path);
            draft.insert("baseUnverified",!known);
            draft.insert("matchesBase",known && external.value(path).hash==draft.value("baseHash").toString());
        } else {
            const auto stored=one("SELECT hash FROM notes WHERE path=?",{path});
            draft.insert("matchesBase",!stored.isEmpty() && stored.value("hash")==draft.value("baseHash"));
        }
        result.append(draft);
    }
    return result;
}

QVariantMap ManagedStore::Impl::run(const QString &op,const QVariantMap &a) {
    const auto path=a.value("path").toString();
    if(op=="list"||op=="search"||op=="selectLibrary"||op=="reindex")return listing(a);
    if(op=="clearRedo")return ok();
    if(op=="backup")return backup(a);
    if(op=="restoreBackup")return restoreBackup(a);
    if(op=="importPreview")return importPreview(a);
    if(op=="importNotes")return importItems(a);
    if(op=="exportNotes")return exportItems(a);
    if(op=="imports") {QVariantList items;for(const auto &v:query("SELECT id,name,time,report FROM imports ORDER BY time DESC")){auto r=v.toMap();r.insert("report",QJsonDocument::fromJson(r["report"].toByteArray()).toVariant());items.append(r);}return {{"ok",true},{"items",items}};}
    // External documents are deliberately absent from notes, folders and the index.
    if(path.startsWith("external:")) {
        const auto file=path.mid(9);
        if(op=="read") {require(QDir::isAbsolutePath(file)&&!file.contains(QChar::Null),"unsafePath");auto d=MarkdownFile::decode(bytesAt(file));external.insert(path,d);return {{"ok",true},{"path",path},{"id",path},{"text",d.text},{"baseHash",d.hash},{"readOnly",!d.readOnlyReason.isEmpty()},{"readOnlyReason",d.readOnlyReason}};}
        if(op!="checkpoint"&&op!="discardDraft"&&op!="saveAs")return {{"ok",false},{"error","externalDocument"}};
    }
    if(op=="read")return document(note(path));
    if(op=="create"||op=="saveAs") {
        auto text=a.value("text").toString();const auto source=a.value("sourcePath").toString();
        if(op=="saveAs")text=rebaseAttachments(text,source.isEmpty()?QStringLiteral("recovery.md"):source,path);
        auto result=insert(path,text);result.insert("listing",listing());return result;
    }
    if(op=="save") {
        auto n=note(path);require(n["hash"].toString()==a.value("baseHash").toString(),"conflict");
        require(MarkdownFile::formatError(a.value("text").toString()).isEmpty(),"unsupportedFormat");
        updateText(n,a.value("text").toString());query("DELETE FROM drafts WHERE path=?",{path});auto result=document(note(path));result.insert("listing",listing());return result;
    }
    if (op == "createFolder") {
        const auto icon = a.value("icon", QStringLiteral("folder")).toString();
        require(validPath(path), "invalidName");
        require(validFolderIcon(icon), "invalidIcon");
        require(one("SELECT id FROM folders WHERE path=?", {path}).isEmpty(), "alreadyExists");
        folder(path);
        query("UPDATE folders SET icon=? WHERE path=?", {icon, path});
        return ok();
    }
    if(op=="renameFolder") {require(!one("SELECT id FROM folders WHERE path=? AND deleted=''",{path}).isEmpty(),"notFound");auto target=a.value("target").toString();relocate(path,target,true);return {{"ok",true},{"newPath",target}};}
    if (op == "moveFolder") {
        const auto current = one("SELECT id FROM folders WHERE path=? AND deleted=''", {path});
        require(!current.isEmpty(), "notFound");
        const auto parent = a.value("folder").toString(), sibling = a.value("sibling").toString();
        require(validPath(parent, true), "invalidName");
        require(parent != path && !parent.startsWith(path + '/'), "invalidParent");
        require(parent.isEmpty() || !one("SELECT id FROM folders WHERE path=? AND deleted=''", {parent}).isEmpty(), "missingFolder");
        const auto anchor = sibling.isEmpty() ? QVariantMap() : one("SELECT id FROM folders WHERE path=? AND deleted=''", {sibling});
        require(sibling.isEmpty() || (!anchor.isEmpty() && sibling != path && parentFolder(sibling) == parent), "missingFolder");
        const auto target = (parent.isEmpty() ? QString() : parent + '/') + path.section('/', -1);
        if (target != path) {
            require(one("SELECT path FROM drafts LIMIT 1").isEmpty(), "pendingDrafts");
            relocate(path, target, true);
        }
        // Persist sibling order by folder identity, within the same undoable move.
        QStringList siblings;
        for (const auto &v : query("SELECT id,path FROM folders WHERE deleted='' ORDER BY sort_order,path")) {
            const auto row = v.toMap();
            if (row["id"] != current["id"] && parentFolder(row["path"].toString()) == parent)
                siblings.append(row["id"].toString());
        }
        const int at = sibling.isEmpty() ? siblings.size() : siblings.indexOf(anchor["id"].toString()) + (a.value("after").toBool() ? 1 : 0);
        siblings.insert(at, current["id"].toString());
        for (int i = 0; i < siblings.size(); ++i)
            query("UPDATE folders SET sort_order=? WHERE id=? AND sort_order<>?", {i, siblings[i], i});
        return {{"ok",true},{"newPath",target}};
    }
    if (op == "editFolder") {
        const auto current = one("SELECT id FROM folders WHERE path=? AND deleted=''", {path});
        require(!current.isEmpty(), "notFound");
        const auto target = a.value("target").toString(), icon = a.value("icon").toString();
        require(validPath(target), "invalidName");
        require(validFolderIcon(icon), "invalidIcon");
        const auto parent = parentFolder(target);
        require(parent != path && !parent.startsWith(path + '/'), "invalidParent");
        require(parent.isEmpty() || !one("SELECT id FROM folders WHERE path=? AND deleted=''", {parent}).isEmpty(), "missingFolder");
        if (target != path) {
            require(one("SELECT path FROM drafts LIMIT 1").isEmpty(), "pendingDrafts");
            relocate(path, target, true);
        }
        query("UPDATE folders SET icon=? WHERE id=?", {icon,current["id"]});
        return {{"ok",true},{"newPath",target}};
    }
    if(op=="removeFolder") {
        require(one("SELECT id FROM notes WHERE path LIKE ? ESCAPE '\\'",{QString(path).replace('\\',"\\\\").replace('%',"\\%").replace('_',"\\_")+"/%"}).isEmpty(),"folderNotEmpty");
        for(const auto &v:query("SELECT path FROM folders"))require(!v.toMap()["path"].toString().startsWith(path+'/'),"folderNotEmpty");query("DELETE FROM folders WHERE path=?",{path});return ok();
    }
    if(op=="rename") {note(path);auto target=a.value("target").toString();relocate(path,target,false);return {{"ok",true},{"newPath",target},{"listing",listing()}};}
    if (op == "purgeFolder") {
        require(validPath(path), "invalidName");
        require(!one("SELECT id FROM folders WHERE path=? AND deleted=''", {path}).isEmpty(), "notFound");
        const auto prefix = path + '/';
        // Do not discard unresolved recovery text, or touch previously trashed
        // entries. instr keeps subtree matching literal and case-sensitive.
        require(one("SELECT path FROM drafts WHERE instr(path,?)=1 LIMIT 1", {prefix}).isEmpty(), "pendingDrafts");
        query("DELETE FROM notes WHERE deleted='' AND instr(path,?)=1", {prefix});
        // Note versions cascade with their note; shared assets keep their
        // existing reference-aware cleanup lifecycle.
        query("DELETE FROM folders WHERE deleted='' AND (path=? OR instr(path,?)=1)", {path, prefix});
        return ok();
    }
    if(op=="trash"||op=="trashFolder") {
        const auto token=uid(),time=utc();if(op=="trash") {const auto n=note(path);require(a.value("baseHash").toString()==n["hash"].toString(),"conflict");query("UPDATE notes SET deleted=?,trash_token=? WHERE id=?",{time,token,n["id"]});query("DELETE FROM drafts WHERE path=?",{path});}
        else {require(!one("SELECT id FROM folders WHERE path=? AND deleted=''",{path}).isEmpty(),"notFound");for(const auto &v:query("SELECT path FROM folders WHERE deleted=''")){auto p=v.toMap()["path"].toString();if(p==path||p.startsWith(path+'/'))query("UPDATE folders SET deleted=?,trash_token=? WHERE path=?",{time,token,p});}for(const auto &v:query("SELECT path FROM notes WHERE deleted=''")){auto p=v.toMap()["path"].toString();if(p.startsWith(path+'/')){query("UPDATE notes SET deleted=?,trash_token=? WHERE path=?",{time,token,p});query("DELETE FROM drafts WHERE path=?",{p});}}}
        return {{"ok",true},{"trashToken",token},{"listing",listing()}};
    }
    if(op=="recovery") {
        QVariantList trash,drafts;
        for(const auto &v:query("SELECT * FROM notes WHERE deleted<>''")){auto n=v.toMap();if(!one("SELECT id FROM folders WHERE trash_token=?",{n["trash_token"]}).isEmpty())continue;trash.append(QVariantMap{{"kind","note"},{"token",n["trash_token"]},{"source",n["path"]},{"deletedUtc",n["deleted"]},{"fingerprint",n["hash"]},{"title",MarkdownFile::title(n["text"].toString(),QFileInfo(n["path"].toString()).completeBaseName())}});}
        QSet<QString> seen;
        for(const auto &v:query("SELECT * FROM folders WHERE deleted<>'' ORDER BY length(path)")){auto n=v.toMap();const auto token=n["trash_token"].toString();if(seen.contains(token))continue;seen.insert(token);trash.append(QVariantMap{{"kind","folder"},{"token",token},{"source",n["path"]},{"deletedUtc",n["deleted"]},{"fingerprint",token}});}
        drafts=recoveryDrafts();
        QVariantList memory;
        for(const auto &draft:memoryDrafts)memory.append(draft);
        return {{"ok",true},{"trashItems",trash},{"drafts",drafts},{"commits",QVariantList()},{"memoryDrafts",memory}};
    }
    if(op=="restore"||op=="restoreFolder") {
        const auto token=a.value("token").toString(),target=a.value("target").toString();auto n=one("SELECT * FROM notes WHERE trash_token=? AND deleted<>''",{token});
        if(!target.isEmpty()){require(!n.isEmpty(),"notFound");return insert(target,rebaseAttachments(n["text"].toString(),n["path"].toString(),target));}
        require(!n.isEmpty()||!one("SELECT id FROM folders WHERE trash_token=?",{token}).isEmpty(),"notFound");
        const auto rows=query("SELECT path FROM notes WHERE trash_token=?",{token});
        query("UPDATE folders SET deleted='',trash_token='' WHERE trash_token=?",{token});
        for(const auto &v:rows)folder(parentFolder(v.toMap()["path"].toString()));
        query("UPDATE notes SET deleted='',trash_token='' WHERE trash_token=?",{token});return ok();
    }
    if(op=="trashBatch") {
        QVariantList results;int succeeded=0;const auto action=a.value("action").toString();require(action=="restore"||action=="purge","invalidBatch");
        for(const auto &v:a.value("items").toList()) {
            const auto item=v.toMap();const auto token=item.value("token").toString();query("SAVEPOINT item");
            try {auto n=one("SELECT * FROM notes WHERE trash_token=? AND deleted<>''",{token});auto f=one("SELECT * FROM folders WHERE trash_token=? AND deleted<>''",{token});require(!n.isEmpty()||!f.isEmpty(),"trashChanged");require(item.value("fingerprint").toString()==(item.value("kind")=="folder"?token:n["hash"].toString()),"trashChanged");
                if(action=="restore")run("restore",{{"token",token}});else{query("DELETE FROM notes WHERE trash_token=?",{token});query("DELETE FROM folders WHERE trash_token=?",{token});}query("RELEASE item");++succeeded;results.append(QVariantMap{{"path",item.value("source")},{"ok",true}});
            }catch(const StoreError &e){query("ROLLBACK TO item");query("RELEASE item");results.append(QVariantMap{{"path",item.value("source")},{"ok",false},{"error",e.code},{"detail",e.detail}});}
        }
        return {{"ok",true},{"action",action},{"results",results},{"succeeded",succeeded},{"failed",results.size()-succeeded}};
    }
    if(op=="pin"||op=="favorite") {auto n=note(path);query(op=="pin"?"UPDATE notes SET pinned=? WHERE id=?":"UPDATE notes SET favorite=? WHERE id=?",{a.value(op=="pin"?"pinned":"favorite").toBool(),n["id"]});return ok();}
    if(op=="batch"||op=="tagBatch") {
        const auto action=a.value("action").toString();const auto items=a.value("items").toList();require(!items.isEmpty()&&items.size()<=200,"invalidBatch");
        require(op=="tagBatch"||QStringList{"pin","unpin","favorite","unfavorite","trash","purgeNotes","move","duplicate"}.contains(action),"invalidBatch");
        QVariantList results;int succeeded=0;QSet<QString> seen;QHash<QString,QString> baselines;
        for(const auto &v:items){const auto p=v.toMap().value("path").toString();baselines.insert(p,one("SELECT hash FROM notes WHERE path=? AND deleted=''",{p}).value("hash").toString());}
        for(const auto &v:items){auto item=v.toMap();auto p=item.value("path").toString();query("SAVEPOINT item");try {
            require(!seen.contains(p),"duplicateRequest");seen.insert(p);auto n=note(p);require(!baselines.value(p).isEmpty()&&item.value("baseHash").toString()==baselines.value(p),"conflict");QVariantMap result=ok();
            if(op=="tagBatch") {auto tags=Knowledge::properties(n["text"].toString()).value("tags").toStringList();for(const auto &v:a.value("tags").toStringList()){if(action=="remove"||action=="rename")tags.removeAll(v);else if(!tags.contains(v))tags.append(v);}if(action=="rename")tags.append(a.value("replacement").toString());tags.removeDuplicates();updateText(n,Knowledge::update(n["text"].toString(),"tags",tags));}
            else if(action=="trash")run("trash",{{"path",p},{"baseHash",n["hash"]}});
            else if(action=="purgeNotes") {
                require(one("SELECT path FROM drafts WHERE path=?", {p}).isEmpty(), "pendingDrafts");
                query("DELETE FROM notes WHERE id=?", {n["id"]});
            }
            else if(action=="duplicate"){auto copy=insert(parentFolder(p)+(parentFolder(p).isEmpty()?"":"/")+QFileInfo(p).completeBaseName()+" 副本.md",Knowledge::update(n["text"].toString(),"title",MarkdownFile::title(n["text"].toString(),QFileInfo(p).completeBaseName())+" 副本"));result.insert("newPath",copy["path"]);}
            else if(action=="move"){const auto dest=a.value("folder").toString();require(dest.isEmpty()||!one("SELECT id FROM folders WHERE path=? AND deleted=''",{dest}).isEmpty(),"missingFolder");const auto next=(dest.isEmpty()?QString():dest+'/')+QFileInfo(p).fileName();if(next!=p)relocate(p,next,false);result.insert("newPath",next);}
            else run(action=="pin"||action=="unpin"?"pin":"favorite",{{"path",p},{"pinned",action=="pin"},{"favorite",action=="favorite"}});
            query("RELEASE item");result.insert("path",p);results.append(result);++succeeded;
        }catch(const StoreError &e){query("ROLLBACK TO item");query("RELEASE item");results.append(QVariantMap{{"path",p},{"ok",false},{"error",e.code},{"detail",e.detail}});}}
        return {{"ok",true},{"action",op=="tagBatch"?"tags":action},{"results",results},{"succeeded",succeeded},{"failed",results.size()-succeeded}};
    }
    if(op=="renameNote") {
        auto n=note(path);require(n["hash"].toString()==a.value("baseHash").toString(),"conflict");const auto title=a.value("title").toString().trimmed();require(!title.isEmpty()&&title.size()<=300&&!title.contains('\n'),"invalidName");
        refreshIndex();
        for(const auto &v:query("SELECT * FROM notes WHERE deleted=''")) {auto linked=v.toMap();const auto source=linked["path"].toString();QString text=linked["text"].toString();LinkPaths::rewrite(text,[&](const QString &target,bool wiki){if(!wiki)return target;const auto choices=index.resolve(source,target,true);if(choices.size()!=1||choices[0]!=path)return target;return relativeManagedPath(source,path);});if(text!=linked["text"].toString())updateText(linked,text);}
        n=note(path);const QString text=n["text"].toString();
        updateText(n,Knowledge::update(text,"title",title));return ok();
    }
    if(op=="setProperty") {auto n=note(path);require(n["hash"].toString()==a.value("baseHash").toString(),"conflict");const auto key=a.value("key").toString();require(QRegularExpression("^[A-Za-z_][A-Za-z0-9_-]*$").match(key).hasMatch()&&key!="nextnotes_id","invalidProperty");updateText(n,Knowledge::update(n["text"].toString(),key,a.value("value"),a.value("remove").toBool()));return ok();}
    if(op=="tagPlan") {refreshIndex();QVariantList items;const auto tags=a.value("tags").toStringList();for(auto it=index.documents.begin();it!=index.documents.end();++it)for(const auto &tag:tags)if(it->props.value("tags").toStringList().contains(tag)){items.append(QVariantMap{{"path",it.key()},{"baseHash",it->hash}});break;}return {{"ok",true},{"action",a.value("action")},{"tags",tags},{"replacement",a.value("replacement")},{"items",items},{"count",items.size()}};}
    if(op=="history") {auto n=note(path);QVariantList versions;for(const auto &v:query("SELECT hash,time FROM versions WHERE note_id=? ORDER BY rowid DESC",{n["id"]})){auto r=v.toMap();r.insert("current",r["hash"]==n["hash"]);versions.append(r);}return {{"ok",true},{"versions",versions}};}
    if(op=="historyVersion") {auto n=note(path);auto v=one("SELECT text,hash,time FROM versions WHERE note_id=? AND hash=?",{n["id"],a.value("hash")});require(!v.isEmpty(),"notFound");v.insert("ok",true);return v;}
    if(op=="templates")return {{"ok",true},{"templates",settings("templates")}};
    if(op=="templateSave"||op=="templateDelete"||op=="smartSave"||op=="smartDelete") {
        auto name=a.value("name").toString().trimmed();require(!name.isEmpty()&&name.size()<=100,"invalidName");auto kind=op.startsWith("template")?QString("templates"):QString("smart");
        if(op.endsWith("Delete"))query("DELETE FROM settings WHERE kind=? AND name=?",{kind,name});else {
            auto value=kind=="templates"?a.value("text"):QVariant(QVariantMap{{"query",a.value("query")},{"filter",a.value("filter")}});
            if(kind=="templates"&&!path.isEmpty())value=rebaseText(value.toString(),path,"template.md");
            setSetting(kind,name,value);
        }
        return {{"ok",true},{kind=="templates"?"templates":"smartFolders",settings(kind)}};
    }
    if(op=="fromTemplate"||op=="dailyNote") {
        const bool daily=op=="dailyNote";const auto date=QDate::currentDate().toString(Qt::ISODate),title=a.value("title",date).toString();const auto templates=settings("templates");const auto name=daily?QString("每日笔记"):a.value("name").toString();require(daily||templates.contains(name),"notFound");
        auto text=templates.value(name,"# {{title}}\n\n").toString();text.replace("{{date}}",date).replace("{{time}}",QTime::currentTime().toString("HH:mm")).replace("{{title}}",title);
        const auto dest=daily?QString("日记"):a.value("folder").toString();const auto p=(dest.isEmpty()?QString():dest+'/')+(daily?date:uid())+".md";if(daily&&!one("SELECT id FROM notes WHERE path=? AND deleted=''",{p}).isEmpty())return {{"ok",true},{"path",p}};return insert(p,rebaseText(text,"template.md",p));
    }
    if(op=="knowledge"||op=="linkCandidates"||op=="resolveWiki"||op=="createWiki") {
        refreshIndex();
        if(op=="linkCandidates"){QVariantList items;const auto q=a.value("query").toString();for(auto it=index.documents.begin();it!=index.documents.end();++it){if(!it->title.contains(q,Qt::CaseInsensitive)&&!it.key().contains(q,Qt::CaseInsensitive))continue;items.append(QVariantMap{{"path",it.key()},{"title",it->title},{"headings",it->headings}});if(items.size()>=100)break;}return {{"ok",true},{"items",items}};}
        if(op=="resolveWiki"||op=="createWiki") {
            const auto target=a.value("target").toString();auto choices=index.resolve(path,target,true);require(choices.size()<=1,"ambiguousLink");
            if(choices.size()==1)return {{"ok",true},{"path",choices[0]},{"fragment",a.value("fragment")}};
            QString targetPath=target.contains('/')?QDir::cleanPath(parentFolder(path)+'/'+target):target;if(!targetPath.endsWith(".md",Qt::CaseInsensitive))targetPath+=".md";require(validPath(targetPath),"invalidName");
            if(op=="createWiki")return insert(targetPath,"# "+QFileInfo(targetPath).completeBaseName()+"\n\n");
            return {{"ok",true},{"missing",true},{"path",path},{"target",target},{"targetPath",targetPath},{"fragment",a.value("fragment")}};
        }
        const auto doc=index.documents.value(path);QVariantList outgoing,backlinks;QStringList tags;
        for(const auto &v:doc.links){
            auto l=v.toMap();const auto target=l["target"].toString();const bool wiki=l["wiki"].toBool();
            const auto resolved=index.resolve(path,target,wiki);const QUrl url(target);
            const bool remote=!wiki&&!url.isRelative()&&!url.isLocalFile();
            const bool anchor=!wiki&&target.startsWith('#');
            const auto local=wiki?QString():attachmentIdentity(path,url);
            const bool attachment=!local.isEmpty();
            l.insert("resolved",resolved.size()==1||remote||anchor||attachment);
            if(resolved.size()==1) {
                const auto &linked=index.documents[resolved[0]];
                l.insert("kind","note");l.insert("path",resolved[0]);l.insert("title",linked.title);
            }else if(attachment) {
                l.insert("kind","attachment");l.insert("title",one("SELECT name FROM assets WHERE path=?",{local})["name"]);
            }else if(remote) { l.insert("kind","web");l.insert("title",url.host().isEmpty()?url.path():url.host()); }
            else if(anchor) { l.insert("kind","anchor");l.insert("title",url.fragment().isEmpty()?QString("本篇笔记"):url.fragment()); }
            else { l.insert("kind","missing");l.insert("title",QFileInfo(QUrl::fromPercentEncoding(target.toUtf8())).completeBaseName()); }
            outgoing.append(l);
        }
        for(auto it=index.documents.begin();it!=index.documents.end();++it){tags.append(it->props.value("tags").toStringList());for(const auto &v:it->links){auto l=v.toMap();if(index.resolve(it.key(),l["target"].toString(),l["wiki"].toBool()).contains(path)){backlinks.append(QVariantMap{{"path",it.key()},{"title",it->title},{"summary",l["context"]}});break;}}}
        tags.removeDuplicates();tags.sort();return {{"ok",true},{"properties",doc.props},{"tags",tags},{"backlinks",backlinks},{"outgoing",outgoing},{"smartFolders",settings("smart")}};
    }
    if(op=="importAttachment") {
        note(path);QByteArray bytes;QString name=a.value("name").toString();const QUrl source(a.value("source").toString());if(source.isLocalFile()){name=QFileInfo(source.toLocalFile()).fileName();bytes=bytesAt(source.toLocalFile(),32*1024*1024);}else{auto decoded=QByteArray::fromBase64Encoding(a.value("data").toByteArray(),QByteArray::AbortOnBase64DecodingErrors);require(bool(decoded),"invalidAttachment");bytes=decoded.decoded;}
        require(!bytes.isEmpty()&&bytes.size()<=32*1024*1024,"attachmentTooLarge");auto rel=asset(bytes,name);return {{"ok",true},{"path",rel},{"name",name},{"url",QString::fromUtf8(QUrl::toPercentEncoding(relativeManagedPath(path,rel),"/"))},{"mime",QMimeDatabase().mimeTypeForData(bytes).name()},{"image",QMimeDatabase().mimeTypeForData(bytes).name().startsWith("image/")}};
    }
    if(op=="attachments") {
        QVariantList items,removed;
        const auto texts=attachmentReferenceTexts();
        for(const auto &v:query("SELECT path,name,length(data) AS size,deleted FROM assets")) {
            auto n=v.toMap();const auto p=n["path"].toString();const int references=attachmentReferences(p,texts);
            n.insert("references",references);n.insert("bytes",n["size"]);n.insert("relative",p);n.insert("token",p);n.insert("removedUtc",n["deleted"]);
            if(n["deleted"].toString().isEmpty())items.append(n);else removed.append(n);
        }
        return {{"ok",true},{"items",items},{"removed",removed}};
    }
    if(op=="quarantineAttachment"||op=="restoreAttachment") {
        const auto rel=op=="quarantineAttachment"?a.value("relative",a.value("path")).toString():a.value("token").toString();
        require(!one("SELECT path FROM assets WHERE path=?",{rel}).isEmpty(),"notFound");
        if(op=="quarantineAttachment")require(attachmentReferences(rel,attachmentReferenceTexts())==0,"attachmentInUse");
        query("UPDATE assets SET deleted=? WHERE path=?",{op=="quarantineAttachment"?utc():QString(),rel});manifestDirty=true;return ok();
    }
    if(op=="recoverMove")return ok();
    throw StoreError{"unknownOperation",op};
}

QVariantMap ManagedStore::Impl::importPreview(const QVariantMap &a) {
    notify("scanning",0);
    const auto sources=a.value("sources").toStringList();require(!sources.isEmpty(),"invalidImport");
    const auto target=a.value("folder").toString();require(validPath(target,true),"invalidName");
    QVariantList files,dirs,problems;int duplicate=0;bool hasFolders=false;
    const bool preserve=a.value("keepStructure",true).toBool();
    for(const auto &value:sources) {
        const QUrl url(value);QFileInfo source(url.isLocalFile()?url.toLocalFile():value);require(source.exists()&&!source.isSymLink(),"readFailed",source.absoluteFilePath());
        if(source.isDir()) {
            hasFolders=true;
            const auto base=source.canonicalFilePath();QString top=(target.isEmpty()?QString():target+'/')+source.fileName();if(source.fileName().isEmpty())top=(target.isEmpty()?QString():target+'/')+"导入的笔记";
            if(preserve)dirs.append(top);
            QStringList pending{base};
            while(!pending.isEmpty()) {
                if(stopping())return {{"ok",true},{"cancelled",true}};
                const auto current=pending.takeLast();
                for(const auto &fi:QDir(current).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) {
                    if(stopping())return {{"ok",true},{"cancelled",true}};
                    if(fi.isSymLink()||fi.fileName()==".git"||fi.fileName()==".nextnotes"||fi.fileName()==".obsidian")continue;
                    const auto relative=QDir(base).relativeFilePath(fi.absoluteFilePath());
                    if(fi.isDir()){pending.append(fi.absoluteFilePath());if(preserve)dirs.append(top+'/'+relative);continue;}
                    if(fi.suffix().compare("md",Qt::CaseInsensitive))continue;
                    files.append(QVariantMap{{"source",fi.canonicalFilePath()},{"boundary",base},{"target",preserve?top+'/'+relative:(target.isEmpty()?QString():target+'/')+fi.fileName()},{"size",fi.size()}});
                }
            }
        }else if(source.suffix().compare("md",Qt::CaseInsensitive)==0)files.append(QVariantMap{{"source",source.canonicalFilePath()},{"boundary",source.canonicalPath()},{"target",(target.isEmpty()?QString():target+'/')+source.fileName()},{"size",source.size()}});
        else problems.append(QVariantMap{{"source",source.absoluteFilePath()},{"message","仅支持 Markdown 文件"}});
    }
    int reviewed=0;
    for(auto &v:files){if(stopping())return {{"ok",true},{"cancelled",true}};if(reviewed % 25 == 0)notify("reviewing",reviewed,files.size());++reviewed;auto file=v.toMap();try{const auto bytes=bytesAt(file["source"].toString());const auto hash=MarkdownFile::digest(bytes);file.insert("sourceHash",hash);const auto existing=one("SELECT id,path FROM notes WHERE source=? AND source_hash=? AND deleted=''",{file["source"],hash});file.insert("duplicate",!existing.isEmpty());file.insert("existingPath",existing["path"]);if(!existing.isEmpty())++duplicate;auto d=MarkdownFile::decode(bytes);require(d.readOnlyReason.isEmpty(),"unsupportedFormat");}catch(const StoreError &e){file.insert("error",e.code);problems.append(QVariantMap{{"source",file["source"]},{"message",e.code=="unsupportedFormat"?"编码或换行格式不受支持，原文件保留":"无法读取文件，原文件保留"}});}v=file;}
    return {{"ok",true},{"sources",sources},{"folder",target},{"keepStructure",preserve},{"hasFolders",hasFolders},{"files",files},{"directories",dirs},{"count",files.size()},{"folderCount",dirs.size()},{"duplicates",duplicate},{"problems",problems},{"name",sources.size()==1?QFileInfo(QUrl(sources[0]).isLocalFile()?QUrl(sources[0]).toLocalFile():sources[0]).fileName():QString("%1 个文件").arg(sources.size())}};
}
QVariantMap ManagedStore::Impl::importItems(const QVariantMap &a) {
    // Revalidate reviewed files, rather than importing files added after preview.
    const auto plan=a.value("plan").toMap();const auto batch=uid();const auto duplicatePolicy=a.value("duplicates","skip").toString();
    require(!plan.isEmpty(),"invalidImport");require(duplicatePolicy=="skip"||duplicatePolicy=="copy","invalidImport");
    const int originalFolders=one("SELECT count(*) AS count FROM folders WHERE deleted=''")["count"].toInt();
    QVariantList results,prepared,warnings;QHash<QString,QString> mappings;QSet<QString> reserved;int skipped=0,failed=0,succeeded=0;
    auto reserve=[&](QString path){const auto base=path;for(int i=2;reserved.contains(path)||!one("SELECT id FROM notes WHERE path=?",{path}).isEmpty();++i)path=base.left(base.size()-3)+" "+QString::number(i)+".md";reserved.insert(path);return path;};
    const auto plannedFiles=plan.value("files").toList();
    int checked=0;
    for(const auto &value:plannedFiles) {
        if(stopping())break;
        if(checked % 25 == 0)notify("preparing",checked,plannedFiles.size());++checked;
        auto file=value.toMap();const auto source=file["source"].toString();
        try {require(file["error"].toString().isEmpty(),"unsupportedFormat");require(QFileInfo(source).canonicalFilePath()==source&&!QFileInfo(source).isSymLink(),"unsafePath");auto bytes=bytesAt(source);require(MarkdownFile::digest(bytes)==file["sourceHash"].toString(),"sourceChanged");
            const auto existing=one("SELECT path FROM notes WHERE source=? AND source_hash=? AND deleted=''",{source,file["sourceHash"]});
            if(duplicatePolicy=="skip"&&!existing.isEmpty()){++skipped;mappings.insert(source,existing["path"].toString());results.append(QVariantMap{{"source",source},{"status","skipped"},{"message","已导入相同内容"},{"path",existing["path"]}});continue;}
            auto d=MarkdownFile::decode(bytes);require(d.readOnlyReason.isEmpty(),"unsupportedFormat");auto dest=reserve(file["target"].toString());file.insert("destination",dest);prepared.append(file);mappings.insert(source,dest);
        }catch(const StoreError &e){++failed;results.append(QVariantMap{{"source",source},{"status","failed"},{"message",e.code=="sourceChanged"?"预览后文件已改变，请重新预览":e.code=="unsupportedFormat"?"编码或格式暂不支持":"无法读取文件"}});}
    }
    int processed=0,folderCount=0;
    const auto persistPartial=[&] {
        QVariantMap partial{{"id",batch},{"name",plan.value("name")},{"time",utc()},{"succeeded",succeeded},{"skipped",skipped},{"failed",failed},{"results",results},{"warnings",warnings},{"interrupted",true},{"remaining",plannedFiles.size()-succeeded-skipped-failed}};
        query("INSERT INTO imports(id,name,time,report) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET report=excluded.report",{batch,partial["name"],partial["time"],QJsonDocument::fromVariant(partial).toJson(QJsonDocument::Compact)});
    };
    query("BEGIN IMMEDIATE");
    for(const auto &v:prepared) {
        if(stopping())break;
        if(processed % 25 == 0)notify("importing",processed,prepared.size());
        auto file=v.toMap();const auto source=file["source"].toString(),dest=file["destination"].toString(),boundary=file["boundary"].toString();query("SAVEPOINT imported_note");
        try {
            const auto bytes=bytesAt(source);require(MarkdownFile::digest(bytes)==file["sourceHash"].toString(),"sourceChanged");
            QString text=MarkdownFile::decode(bytes).text;
            const bool supportedLinks=LinkPaths::rewrite(text,[&](const QString &target,bool wiki){
                if(wiki) {
                    QString candidate=QDir(QFileInfo(source).absolutePath()).absoluteFilePath(target);
                    if(!candidate.endsWith(".md",Qt::CaseInsensitive))candidate+=".md";
                    const auto mapped=mappings.value(QFileInfo(candidate).canonicalFilePath());
                    if(!mapped.isEmpty())return relativeManagedPath(dest,mapped);
                    return target;
                }
                QUrl url(target);if(url.path().isEmpty()||(!url.isRelative()&&!url.isLocalFile()))return target;
                const auto original=url.isLocalFile()?url.toLocalFile():QDir(QFileInfo(source).absolutePath()).absoluteFilePath(QUrl::fromPercentEncoding(url.path(QUrl::FullyEncoded).toUtf8()));
                const auto canonical=QFileInfo(original).canonicalFilePath();
                if(canonical.isEmpty()||!(canonical==boundary||canonical.startsWith(boundary+'/'))||QFileInfo(original).isSymLink()){warnings.append(QVariantMap{{"source",source},{"message","关联文件不存在或位于导入范围之外："+target}});return target;}
                QString managed=mappings.value(canonical);
                if(managed.isEmpty()&&QFileInfo(canonical).suffix().compare("md",Qt::CaseInsensitive)==0){warnings.append(QVariantMap{{"source",source},{"message","链接笔记未导入："+target}});return target;}
                if(managed.isEmpty()){managed=asset(bytesAt(canonical,32*1024*1024),QFileInfo(canonical).fileName());}
                const auto relative=relativeManagedPath(dest,managed);
                QString result=QString::fromUtf8(QUrl::toPercentEncoding(relative,"/"));if(!url.query().isEmpty())result+='?'+url.query(QUrl::FullyEncoded);if(!url.fragment().isEmpty())result+='#'+url.fragment(QUrl::FullyEncoded);return result;
            });
            if(!supportedLinks)warnings.append(QVariantMap{{"source",source},{"message","包含 HTML 链接或图片：正文已保留，这类关联资源暂未自动收集。"}});
            auto result=insert(dest,text,source,file["sourceHash"].toString(),batch);query("RELEASE imported_note");++succeeded;results.append(QVariantMap{{"source",source},{"status","imported"},{"path",result["path"]},{"message","已导入"}});
        }catch(const StoreError &e){query("ROLLBACK TO imported_note");query("RELEASE imported_note");++failed;results.append(QVariantMap{{"source",source},{"status","failed"},{"message","笔记或关联附件无法导入，原文件保留"},{"detail",e.detail}});}
        ++processed;
        if(processed % 50 == 0){persistPartial();query("COMMIT");query("BEGIN IMMEDIATE");}
    }
    const bool wasCancelled=stopping();
    if(!wasCancelled)for(const auto &v:plan.value("directories").toList())folder(v.toString());
    folderCount=one("SELECT count(*) AS count FROM folders WHERE deleted=''")["count"].toInt()-originalFolders;
    if(wasCancelled && succeeded > 0)warnings.append(QVariantMap{{"message","已保留完成的笔记。如有指向尚未导入笔记的链接，可能暂时不可用。再次导入并跳过重复内容可继续收集。"}});
    QVariantMap report{{"cancelled",wasCancelled},{"remaining",plannedFiles.size()-succeeded-skipped-failed},{"id",batch},{"name",plan.value("name")},{"folderCount",folderCount},{"time",utc()},{"succeeded",succeeded},{"skipped",skipped},{"failed",failed},{"results",results},{"warnings",warnings},{"folder",plan.value("folder")}};
    query("INSERT INTO imports(id,name,time,report) VALUES(?,?,?,?) ON CONFLICT(id) DO UPDATE SET report=excluded.report",{batch,report["name"],report["time"],QJsonDocument::fromVariant(report).toJson(QJsonDocument::Compact)});
    query("COMMIT");
    notify("finished",succeeded+skipped+failed,plannedFiles.size());
    report.insert("ok",true);return report;
}
QVariantMap ManagedStore::Impl::exportItems(const QVariantMap &a) {
    const QUrl url(a.value("destination").toString());const auto directory=url.isLocalFile()?url.toLocalFile():a.value("destination").toString();require(QFileInfo(directory).isDir(),"writeFailed");
    const auto chosen=a.value("paths").toStringList();const auto scope=a.value("folder").toString();QVariantList exported,warnings;
    auto top=QDir(directory).filePath("笔记导出-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+'-'+uid().left(6));require(!QFileInfo::exists(top)&&QDir().mkpath(top),"writeFailed");
    // Identity lookup is independent of the old absolute path and the disposable
    // cache. Only the current database's validated BLOBs become exported files.
    const auto manifest=managedManifest();QSet<QString> assets;
    if(chosen.isEmpty())for(const auto &v:query("SELECT path FROM folders WHERE deleted=''")){const auto p=v.toMap()["path"].toString();if(scope.isEmpty()||p==scope||p.startsWith(scope+'/')){require(validPath(p),"unsafePath");require(QDir().mkpath(top+'/'+p),"writeFailed",p);}}
    for(const auto &v:query("SELECT * FROM notes WHERE deleted=''")) {
        auto n=v.toMap();const auto path=n["path"].toString();if(!chosen.isEmpty()&&!chosen.contains(path))continue;if(!scope.isEmpty()&&!path.startsWith(scope+'/'))continue;
        require(validPath(path),"unsafePath");auto text=n["text"].toString();
        const ManagedAssets::Context context{root,QUrl::fromLocalFile(root+'/'+path),manifest,true};
        QSet<QString> warned;
        const bool supported=rewriteExportLinks(text,[&](const QString &target,bool wiki){
            if(wiki)return target;
            const QUrl original(target);auto identityUrl=original;identityUrl.setQuery(QString());identityUrl.setFragment(QString());
            const auto identity=ManagedAssets::resolveIdentity(context,identityUrl);
            if(!identity.isEmpty()) {assets.insert(identity);return relativeManagedUrl(path,identity,original);}
            // Unknown local resources remain visible in the Markdown. Never try
            // their former filesystem location, including for a missing digest.
            const auto resolved=LinkPaths::resolve(path,target);
            const bool local=!original.path().isEmpty() && (original.isRelative()||original.isLocalFile()||original.scheme()=="nkasset");
            if(local && !warned.contains(target) && (resolved.isEmpty()||one("SELECT id FROM notes WHERE path=? AND deleted=''",{resolved}).isEmpty())) {
                warned.insert(target);warnings.append(QVariantMap{{"path",path},{"target",target},{"code","missingAttachment"},
                    {"message","附件未包含在当前笔记库中，链接已原样保留："+target}});
            }
            return target;
        });
        if(!supported)warnings.append(QVariantMap{{"path",path},{"code","unsupportedHtml"},
            {"message","HTML 链接或图片已原样保留；这类关联资源未自动导出。"}});
        writeBytes(top+'/'+path,text.toUtf8());exported.append(path);
    }
    for(const auto &path:assets) {
        const auto stored=one("SELECT data FROM assets WHERE path=? AND deleted=''",{path});
        require(!stored.isEmpty() && stored.value("data").metaType().id()==QMetaType::QByteArray
            && MarkdownFile::digest(stored.value("data").toByteArray())==manifest.value(path).toString(),"databaseCorrupt","Invalid asset identity");
        writeBytes(top+'/'+path,stored.value("data").toByteArray());
    }
    return {{"ok",true},{"directory",top},{"count",exported.size()},{"warnings",warnings}};
}

namespace {
Database openDatabase(const QString &path, int flags) {
    sqlite3 *raw = nullptr;
    const int rc = sqlite3_open_v2(QFile::encodeName(path).constData(), &raw, flags, nullptr);
    Database result(raw, sqlite3_close);
    require(rc == SQLITE_OK, "invalidBackup", path);
    sqlite3_busy_timeout(raw, 5000);
    return result;
}
void copyDatabase(sqlite3 *destination, sqlite3 *source) {
    auto *copy = sqlite3_backup_init(destination, "main", source, "main");
    require(copy, "backupFailed");
    int state;
    QElapsedTimer timer; timer.start();
    do {
        state = sqlite3_backup_step(copy, 256);
        if (state == SQLITE_BUSY || state == SQLITE_LOCKED) QThread::msleep(20);
    } while (state == SQLITE_OK || ((state == SQLITE_BUSY || state == SQLITE_LOCKED) && timer.elapsed() < 5000));
    const int finish = sqlite3_backup_finish(copy);
    require(state == SQLITE_DONE && finish == SQLITE_OK, "backupFailed");
}
void streamCopy(const QString &source, const QString &destination) {
    QFile in(source); require(in.open(QIODevice::ReadOnly), "readFailed", source);
    QSaveFile out(destination); out.setDirectWriteFallback(false);
    require(out.open(QIODevice::WriteOnly), "writeFailed", destination);
    while (!in.atEnd()) {
        const auto chunk = in.read(1024 * 1024);
        require(in.error() == QFileDevice::NoError && out.write(chunk) == chunk.size(), "writeFailed", destination);
    }
    require(out.commit(), "writeFailed", destination);
}
}
QVariantMap ManagedStore::Impl::backup(const QVariantMap &a) {
    const auto destination = QUrl(a.value("destination").toString()).toLocalFile();
    require(!destination.isEmpty() && !QFileInfo(destination).isDir(), "writeFailed");
    require(!QFileInfo(destination).isSymLink(),"backupLocation");
    const auto canonicalParent = QFileInfo(destination).absoluteDir().canonicalPath();
    require(!canonicalParent.isEmpty() && !(canonicalParent == root || canonicalParent.startsWith(root + '/')),
            "backupLocation", "Choose a folder outside the managed storage.");
    QTemporaryDir temporary; require(temporary.isValid(), "writeFailed");
    const auto snapshot = temporary.filePath("snapshot.sqlite3");
    {
        auto target = openDatabase(snapshot, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
        copyDatabase(target.get(), db);
        require(sqlite3_exec(target.get(), "PRAGMA application_id=1313558065; PRAGMA journal_mode=DELETE", nullptr,nullptr,nullptr) == SQLITE_OK, "backupFailed");
    }
    streamCopy(snapshot,destination);
    return {{"ok",true},{"file",destination}};
}
QVariantMap ManagedStore::Impl::restoreBackup(const QVariantMap &a) {
    const auto source = QUrl(a.value("source").toString()).toLocalFile();
    QTemporaryDir temporary; require(temporary.isValid(), "writeFailed");
    const auto snapshot = temporary.filePath("restore.sqlite3");
    prepareBackup(source,snapshot);
    auto incoming = openDatabase(snapshot, SQLITE_OPEN_READONLY);
    // Prepare metadata and check every cache destination before the restore
    // commit; no post-commit read can misreport a completed replacement.
    Impl incomingInspection;incomingInspection.db=openDatabase(snapshot,SQLITE_OPEN_READONLY).release();
    QVariantMap incomingManifest;
    const auto incomingAssets=incomingInspection.query("SELECT path,data,deleted FROM assets");
    for(const auto &v:incomingAssets) {
        const auto asset=v.toMap();checkedCachePath(asset.value("path").toString());
        if(asset.value("deleted").toString().isEmpty())incomingManifest.insert(asset.value("path").toString(),MarkdownFile::digest(asset.value("data").toByteArray()));
    }
    // Keep an independent, user-accessible rollback copy before replacing anything.
    const auto safetyFolder=QDir::cleanPath(QDir(root).absoluteFilePath("../Backups"));
    require(QDir().mkpath(safetyFolder),"writeFailed");
    const auto safety=QDir(safetyFolder).filePath("恢复前-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+'-'+uid().left(6)+".nkbackup");
    backup({{"destination",QUrl::fromLocalFile(safety).toString()}});
    copyDatabase(db,incoming.get());
    initializationNotice.clear();
    index=Knowledge::Index();external.clear();memoryDrafts.clear();assetManifest=std::move(incomingManifest);manifestDirty=false;
    cacheFailures=0;
    // Cache refresh failures do not undo or misreport a committed database restore.
    for(const auto &v:incomingAssets) {
        const auto asset=v.toMap();if(!asset.value("deleted").toString().isEmpty())continue;
        try { writeBytes(checkedCachePath(asset["path"].toString()),asset["data"].toByteArray()); }
        catch(const StoreError &) { ++cacheFailures; }
    }
    return {{"ok",true},{"safetyCopy",safety},{"cacheFailures",cacheFailures},{"initializationNotice",initializationNotice},{"restored",true}};
}
QVariantMap ManagedStore::Impl::recoverInitialization(const QVariantMap &a) {
    require(library && !library->activationUncertain() && !db, "recoveryUnavailable");
    require(!hasPendingMemoryDrafts(), "pendingDrafts");
    QTemporaryDir temporary; require(temporary.isValid(), "writeFailed");
    const auto snapshot=temporary.filePath("validated.sqlite3");
    prepareBackup(QUrl(a.value("source").toString()).toLocalFile(),snapshot);
    const auto preserved=root, recoveryId=uid();
    const auto replacement=library->createReplacement(recoveryId);
    const auto lease=ManagedLibrary::lockDirectory(replacement);
    failAt("replacementDatabase"); streamCopy(snapshot,replacement+"/notes.sqlite3");
    Impl prepared; prepared.root=replacement; prepared.configuredRoot=configuredRoot;
    prepared.initializeDatabase();
    require(prepared.cacheFailures==0,"writeFailed","Replacement attachment cache is incomplete");
    auto listing=prepared.listing();
    prepared.addContext(listing);
    // Complete WAL checkpoint and close all preparation connections before
    // syncing the replacement; no connection ever touches the failed database.
    const auto checkpoint=prepared.one("PRAGMA wal_checkpoint(TRUNCATE)");
    require(checkpoint.value("busy").toInt()==0,"writeFailed","Replacement checkpoint failed");
    require(prepared.one("PRAGMA journal_mode=DELETE").value("journal_mode").toString()=="delete","writeFailed");
    require(sqlite3_close(prepared.db)==SQLITE_OK,"writeFailed"); prepared.db=nullptr;
    QStringList directories{replacement};
    QDirIterator files(replacement,QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden,QDirIterator::Subdirectories);
    while(files.hasNext()) {
        files.next(); const auto info=files.fileInfo();
        require(!info.isSymLink(),"unsafePath",info.absoluteFilePath());
        if(info.isDir())directories.append(info.absoluteFilePath());else ManagedLibrary::syncFile(info.absoluteFilePath());
    }
    for(auto it=directories.crbegin();it!=directories.crend();++it)ManagedLibrary::syncDirectory(*it);
    ManagedLibrary::syncDirectory(QFileInfo(replacement).absolutePath());
    // Reopen and complete the first listing before publication. DELETE mode
    // avoids creating a new unsynced WAL after the durable preparation step.
    prepared.db=openDatabase(replacement+"/notes.sqlite3",SQLITE_OPEN_READWRITE).release();
    prepared.query("PRAGMA trusted_schema=OFF"); prepared.query("PRAGMA synchronous=FULL"); prepared.query("PRAGMA foreign_keys=ON");
    prepared.initializeUndo(); listing=prepared.listing(); prepared.addContext(listing);
    recoveryStage("replacementReady");
    library->publish(replacement,recoveryId,lease);
    // Pointer publication is the sole commit point. Only infallible state moves
    // follow it; errors while syncing its directory keep this context blocked.
    db=prepared.db; prepared.db=nullptr; root=replacement;
    initializationNotice=prepared.initializationNotice; index=std::move(prepared.index);
    assetManifest=std::move(prepared.assetManifest); manifestDirty=false;
    external.clear();memoryDrafts.clear();undoStack.clear();redoStack.clear();openError.clear();
    listing.insert("ok",true);listing.insert("restored",true);listing.insert("recoveredInitialization",true);
    listing.insert("entryRoot",library->entryRoot());listing.insert("library",root);listing.insert("preservedDirectory",preserved);
    listing.insert("undoState",historyState()); return listing;
}
