// Included beside RepositoryWorker so persistence and path checks stay centralized.
QVariantMap RepositoryWorker::relocate(const QString &from, const QString &to, const QString &base)
{
    const QString source=path(from), target=path(to);
    const bool folder=QFileInfo(source).isDir(); QString error;
    if(source.isEmpty() || target.isEmpty() || from==to || to.startsWith(from+'/')
       || !QFileInfo(source).exists() || !QFileInfo(QFileInfo(target).absolutePath()).isDir()
       || (!folder && QFileInfo(to).suffix().compare("md",Qt::CaseInsensitive))) return failure("unsafePath");
    if(QFileInfo::exists(target)) return failure("alreadyExists");
    const QString journal=path(".nextnotes/move.json",true);
    if(QFileInfo::exists(journal)) return failure("pendingRelocation");
    auto mapped=[&](const QString &p){return p==from?to:p.startsWith(from+'/')?to+p.mid(from.size()):p;};
    const auto listing=scan(false); if(!listing.value("ok").toBool())return listing;
    QStringList notes; for(auto v:listing.value("notes").toList())notes.append(v.toMap().value("path").toString());
    if(folder) {
        QDirIterator it(source,QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);
        while(it.hasNext()){it.next(); if(it.fileInfo().isSymLink() || it.fileName()==".nextnotes" || it.fileName()=="assets")return failure("unsafePath");}
    }
    struct Edit {QString name; QByteArray before,after;}; QList<Edit> edits;
    bool ambiguous=false;
    for(const QString &name:notes) {
        QByteArray bytes; if(!readBytes(path(name),bytes,error))return failure("readFailed",error);
        if(name==from && (base.isEmpty() || MarkdownFile::digest(bytes)!=base))return failure("conflict");
        const auto doc=MarkdownFile::decode(bytes); QString text=doc.text;
        const bool parsed=LinkPaths::rewrite(text,[&](const QString &raw,bool wiki){
            QString dest;
            if(wiki) {
                QString decoded=QUrl::fromPercentEncoding(raw.toUtf8());
                if(decoded.contains('/')) {dest=LinkPaths::resolve(name,decoded);if(!dest.endsWith(".md",Qt::CaseInsensitive))dest+=".md";}
                else {for(const auto &n:notes)if(QFileInfo(n).completeBaseName()==decoded || QFileInfo(n).fileName()==decoded){if(!dest.isEmpty()){ambiguous=true;return raw;}dest=n;}}
            } else dest=LinkPaths::resolve(name,raw);
            if(dest.isEmpty() || dest.startsWith("../") || (mapped(dest)==dest && mapped(name)==name))return raw;
            if(wiki && mapped(dest)==dest && !raw.contains('/'))return raw;
            QUrl original(raw); QString relative=QDir("/" + QFileInfo(mapped(name)).path()).relativeFilePath("/" + mapped(dest));
            if(wiki)return relative.contains('/')?relative:"./"+relative;
            QUrl result; result.setPath(relative); result.setQuery(original.query());result.setFragment(original.fragment());
            return result.toString(QUrl::FullyEncoded);
        });
        if(!parsed || ambiguous)return failure("unsupportedLinks",name);
        if(text!=doc.text || mapped(name)!=name) {
            if(memoryDrafts.contains(name)||QFileInfo::exists(draftPath(name)))return failure("pendingDrafts",name);
            if(text!=doc.text && !doc.readOnlyReason.isEmpty())return failure("unsupportedFormat",name);
            edits.append({name,bytes,text==doc.text?bytes:MarkdownFile::encode(doc,text)});
        }
    }
    QJsonArray records;
    for(const auto &e:edits)records.append(QJsonObject{{"path",e.name},{"before",QString::fromLatin1(e.before.toBase64())},{"after",QString::fromLatin1(e.after.toBase64())}});
    QJsonObject intent{{"from",from},{"to",to},{"entries",records},{"metadata",metadata}};
    if(!atomicWrite(journal,QJsonDocument(intent).toJson(),error))return failure("writeFailed",error);
#ifdef Q_OS_LINUX
    { QFile f(journal); if(!f.open(QIODevice::ReadOnly)||::fsync(f.handle())!=0)return failure("writeFailed");
      int fd=::open(QFile::encodeName(QFileInfo(journal).absolutePath()).constData(),O_RDONLY|O_DIRECTORY);
      if(fd<0)return failure("writeFailed");bool ok=::fsync(fd)==0;::close(fd);if(!ok)return failure("writeFailed"); }
#endif
    // Journal remains until all edits, rename and metadata are durable. On interruption,
    // explicit recovery only restores files still equal to a recorded before/after image.
    auto resultFailure=[&](const QString &code){auto result=failure("pendingRelocation","操作记录保留在 .nextnotes/move.json；请恢复移动操作后重试。");result.insert("cause",code);return result;};
    for(const auto &e:edits) {
        QByteArray current;if(!readBytes(path(e.name),current,error)||current!=e.before)return resultFailure("conflict");
        if(e.before==e.after)continue;
        QString retained;
        QJsonObject record{{"path",e.name},{"baseHash",MarkdownFile::digest(e.before)},{"newHash",MarkdownFile::digest(e.after)}};
        if(!SafeCommit::commit(path(e.name),e.after,path(".nextnotes/commits/"+uuid()+".intent",true),QJsonDocument(record).toJson(QJsonDocument::Compact),true,retained,error))return resultFailure("writeFailed");
        QByteArray displaced;if(!readBytes(retained,displaced,error)||displaced!=e.before) {
#ifdef Q_OS_LINUX
            ::syscall(SYS_renameat2,AT_FDCWD,QFile::encodeName(retained).constData(),AT_FDCWD,QFile::encodeName(path(e.name)).constData(),2U);
#endif
            return resultFailure("conflict");
        }
    }
#ifdef Q_OS_LINUX
    if(::syscall(SYS_renameat2,AT_FDCWD,QFile::encodeName(source).constData(),AT_FDCWD,QFile::encodeName(target).constData(),1U)!=0)return resultFailure("moveFailed");
    for(const auto &p:{source,target}){int fd=::open(QFile::encodeName(QFileInfo(p).absolutePath()).constData(),O_RDONLY|O_DIRECTORY);if(fd<0)return resultFailure("writeFailed");bool ok=::fsync(fd)==0;::close(fd);if(!ok)return resultFailure("writeFailed");}
#else
    return resultFailure("moveFailed");
#endif
    auto entries=metadata.value("entries").toObject(); const auto oldEntries=entries;
    for(auto it=oldEntries.begin();it!=oldEntries.end();++it)if(mapped(it.key())!=it.key()){entries.remove(it.key());entries.insert(mapped(it.key()),it.value());}
    metadata.insert("entries",entries);
    if(!persist(error))return resultFailure("metadataWriteFailed");
    if(!QFile::remove(journal))return resultFailure("writeFailed");
    opened.clear(); fingerprints.clear();
    QVariantMap rewritten;
    for (const auto &e:edits) rewritten.insert(mapped(e.name),QVariantMap{{"before",MarkdownFile::digest(e.before)},{"after",MarkdownFile::digest(e.after)}});
    auto result=scan(false);result.insert("rewrittenHashes",rewritten);result.insert("oldPath",from);result.insert("newPath",to);return result;
}
