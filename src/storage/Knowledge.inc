bool RepositoryWorker::rememberVersion(const QString &id,const QString &relative,const QByteArray &bytes,QString &error) {
    if(id.isEmpty()){error="Missing note identity";return false;}
    const QString folder=path(".nextnotes/history/"+MarkdownFile::digest(id.toUtf8()),true);
    if(folder.isEmpty()||!QDir().mkpath(folder))return false;
    const QString hash=MarkdownFile::digest(bytes), data=folder+'/'+hash+".md", info=folder+'/'+hash+".json";
    if(!QFileInfo::exists(data)&&!atomicWrite(data,bytes,error))return false;
    if(!QFileInfo::exists(info)&&!atomicWrite(info,QJsonDocument(QJsonObject{{"hash",hash},{"path",relative},{"time",now()}}).toJson(),error))return false;
    // Keep a bounded independent history. Crash-recovery journals are unaffected.
    const auto versions=QDir(folder).entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Time);
    for(int i=50;i<versions.size();++i){const QString old=versions[i].completeBaseName();if(QRegularExpression("^[a-f0-9]{64}$").match(old).hasMatch()){QFile::remove(folder+'/'+old+".md");QFile::remove(versions[i].absoluteFilePath());}}
    return true;
}
QVariantMap RepositoryWorker::knowledge(const QString &op,const QVariantMap &a) {
    QString error;const QString relative=a.value("path").toString();
    if(op=="templates")return {{"ok",true},{"templates",metadata.value("templates").toObject().toVariantMap()}};
    if(op=="templateSave"||op=="templateDelete") {
        const QString name=a.value("name").toString().trimmed();if(name.isEmpty()||name.size()>100)return failure("invalidName");
        const auto old=metadata;auto templates=metadata.value("templates").toObject();
        if(op=="templateDelete")templates.remove(name);else templates.insert(name,a.value("text").toString());
        metadata.insert("templates",templates);if(!persist(error)){metadata=old;return failure("metadataWriteFailed");}
        return {{"ok",true},{"templates",templates.toVariantMap()}};
    }
    if(op=="fromTemplate"||op=="dailyNote") {
        const auto templates=metadata.value("templates").toObject();const bool daily=op=="dailyNote";
        const QString name=a.value("name").toString(),date=QDate::currentDate().toString(Qt::ISODate);
        if(!daily&&!templates.contains(name))return failure("notFound");
        QString target;
        if(daily){const QString directory=path("日记");if(directory.isEmpty()||!QDir().mkpath(directory))return failure("createFolderFailed");target="日记/"+date+".md";
            if(QFileInfo::exists(path(target)))return {{"ok",true},{"path",target},{"existing",true}};
        }else {const QString folder=a.value("folder").toString();if(!folder.isEmpty()&&(path(folder).isEmpty()||!QFileInfo(path(folder)).isDir()))return failure("unsafePath");target=(folder.isEmpty()?QString():folder+'/')+"笔记-"+uuid().left(8)+".md";}
        QString text=daily?templates.value("每日笔记").toString("# {{date}}\n\n"):templates.value(name).toString();
        text.replace("{{date}}",date);text.replace("{{time}}",QTime::currentTime().toString("HH:mm"));text.replace("{{title}}",a.value("title",daily?date:name).toString());
        return dispatch("saveAs",{{"path",target},{"text",text},{"newIdentity",true}});
    }
    if(op=="reindex") {index=Knowledge::Index();cachedListing.clear();return scan(false);}
    if(op=="smartSave"||op=="smartDelete") {
        const QString name=a.value("name").toString().trimmed();if(name.isEmpty()||name.size()>100)return failure("invalidName");
        const auto old=metadata;auto saved=metadata.value("smartFolders").toObject();
        if(op=="smartDelete")saved.remove(name);else saved.insert(name,QJsonObject{{"query",a.value("query").toString()},{"filter",a.value("filter","all").toString()}});
        metadata.insert("smartFolders",saved);if(!persist(error)){metadata=old;return failure("metadataWriteFailed");}
        cachedListing.insert("smartFolders",saved.toVariantMap());
        return {{"ok",true},{"smartFolders",saved.toVariantMap()}};
    }
    if(op=="linkCandidates") {
        QVariantList items;const QString query=a.value("query").toString();
        auto paths=index.documents.keys();paths.sort();
        for(const auto &p:paths){const auto d=index.documents.value(p);if(!p.contains(query,Qt::CaseInsensitive)&&!d.title.contains(query,Qt::CaseInsensitive))continue;
            items.append(QVariantMap{{"path",p},{"title",d.title},{"headings",d.headings}});if(items.size()==100)break;}
        return {{"ok",true},{"items",items},{"request",a.value("request")}};
    }
    if(op=="resolveWiki"||op=="createWiki") {
        // Resolve again on creation: an external editor may have created the target
        // while the confirmation was open. Never overwrite that file.
        if(op=="createWiki") {auto listing=scan(false);if(!listing.value("ok").toBool())return listing;}
        const auto candidates=index.resolve(relative,a.value("target").toString(),true);
        if(candidates.size()>1)return failure("ambiguousLink");
        if(candidates.size()==1)return {{"ok",true},{"path",candidates.first()},{"fragment",a.value("fragment")}};
        QString name=QUrl::fromPercentEncoding(a.value("target").toString().toUtf8()).trimmed();
        if(name.isEmpty()||name.size()>240||name.contains(QRegularExpression("[\\x{0000}-\\x{001f}\\\\:*?\"<>|#]"))||QDir::isAbsolutePath(name)||path(relative).isEmpty())return failure("unsafePath");
        if(!name.endsWith(".md",Qt::CaseInsensitive))name+=".md";
        const QString target=QDir::cleanPath(QFileInfo(relative).path()+'/'+name);
        const QString absolute=path(target);
        if(absolute.isEmpty()||target.split('/').contains(".git"))return failure("unsafePath");
        if(op=="resolveWiki")return {{"ok",true},{"missing",true},{"path",relative},{"targetPath",target},{"target",a.value("target")},{"fragment",a.value("fragment")}};
        if(QFileInfo::exists(absolute))return failure("alreadyExists");
        if(!QDir().mkpath(QFileInfo(absolute).absolutePath()))return failure("createFolderFailed");
        auto result=dispatch("create",{{"path",target},{"text","# "+QFileInfo(target).completeBaseName()+"\n\n"}});
        result.insert("fragment",a.value("fragment"));return result;
    }
    if(op=="tagPlan"||op=="tagBatch") {
        const QString action=a.value("action").toString();
        QStringList tags=a.value("tags").toStringList();tags.removeDuplicates();
        const QString replacement=a.value("replacement").toString();
        const auto valid=[](const QString &tag){return !tag.trimmed().isEmpty()&&tag.size()<=100&&!tag.contains(QRegularExpression("[\\r\\n\\x{0000}]"));};
        if(!QStringList{"add","remove","rename"}.contains(action)||tags.isEmpty()||tags.size()>100)return failure("invalidProperty");
        for(const auto &tag:tags)if(!valid(tag))return failure("invalidProperty");
        if(action=="rename"&&(tags.size()!=1||!valid(replacement)))return failure("invalidProperty");
        if(op=="tagPlan"&&action!="rename")return failure("invalidBatch");
        const auto listing=scan(false);if(!listing.value("ok").toBool())return listing;
        if(op=="tagPlan") {
            QVariantList items;auto paths=index.documents.keys();paths.sort();
            for(const auto &p:paths)if(index.documents[p].props.value("tags").toStringList().contains(tags.first()))items.append(QVariantMap{{"path",p},{"baseHash",index.documents[p].hash}});
            return {{"ok",true},{"action",action},{"tags",tags},{"replacement",replacement},{"items",items},{"count",items.size()}};
        }
        const auto items=a.value("items").toList();
        if(items.isEmpty()||(action!="rename"&&items.size()>200)||items.size()>100000)return failure("invalidBatch");
        QVariantList results;QSet<QString> seen;int succeeded=0,failed=0;
        {
            QScopedValueRollback<bool> guard(editingTags,true);
            for(const auto &v:items) {
                const auto item=v.toMap();const QString p=item.value("path").toString();QVariantMap result;
                QByteArray bytes;const QString absolute=path(p);
                if(seen.contains(p))result=failure("duplicateRequest");
                else if(absolute.isEmpty())result=failure("unsafePath");
                else if(!readBytes(absolute,bytes,error))result=failure("readFailed");
                else if(MarkdownFile::digest(bytes)!=item.value("baseHash").toString())result=failure("conflict");
                else if(memoryDrafts.contains(p)||QFileInfo::exists(draftPath(p)))result=failure("pendingDrafts");
                else {
                    auto current=Knowledge::properties(MarkdownFile::decode(bytes).text).value("tags").toStringList();const auto before=current;
                    if(action=="add")for(const auto &tag:tags){if(!current.contains(tag))current.append(tag);}
                    if(action=="remove")for(const auto &tag:tags)current.removeAll(tag);
                    if(action=="rename")for(QString &tag:current)if(tag==tags.first())tag=replacement;
                    current.removeDuplicates();
                    if(current==before)result={{"ok",true},{"unchanged",true}};
                    else result=knowledge("setProperty",{{"path",p},{"baseHash",item.value("baseHash")},{"key","tags"},{"value",current}});
                }
                seen.insert(p);result.insert("path",p);
                // Do not transfer complete note texts back to the UI for a bulk report.
                result.remove("text");result.remove("listing");
                if(result.value("ok").toBool())++succeeded;else ++failed;results.append(result);
            }
        }
        auto refreshed=scan(false);
        return {{"ok",true},{"action","tags"},{"results",results},{"succeeded",succeeded},{"failed",failed},{"listing",refreshed}};
    }
    if(path(relative).isEmpty())return failure("unsafePath");
    if(op=="knowledge") {
        QVariantList backlinks,outgoing;QSet<QString> allTags;
        for(auto it=index.documents.begin();it!=index.documents.end();++it){for(const auto &tag:it->props.value("tags").toStringList())allTags.insert(tag);bool linked=false;
            for(const auto &v:it->links){const auto link=v.toMap();const auto resolved=index.resolve(it.key(),link.value("target").toString(),link.value("wiki").toBool());
                if(it.key()==relative)outgoing.append(QVariantMap{{"target",link.value("target")},{"paths",resolved},{"resolved",resolved.size()==1}});
                if(!linked&&resolved.size()==1&&resolved.first()==relative){backlinks.append(QVariantMap{{"path",it.key()},{"title",it->title},{"summary",link.value("context")},{"position",link.value("position")}});linked=true;}
            }
        }
        auto tags=allTags.values();tags.sort();
        return {{"ok",true},{"path",relative},{"properties",index.documents.value(relative).props},{"backlinks",backlinks},{"outgoing",outgoing},{"tags",tags},{"smartFolders",metadata.value("smartFolders").toObject().toVariantMap()}};
    }
    if(op=="setProperty") {
        const QString key=a.value("key").toString();if(!QRegularExpression("^[A-Za-z_][A-Za-z0-9_-]*$").match(key).hasMatch()||key=="nextnotes_id")return failure("invalidProperty");
        QByteArray bytes;if(!readBytes(path(relative),bytes,error))return failure("readFailed");
        if(MarkdownFile::digest(bytes)!=a.value("baseHash").toString())return failure("conflict");
        if(key=="tags") {for(const auto &tag:a.value("value").toStringList())if(tag.size()>100||tag.contains('\n')||tag.contains('\r')||tag.contains(QChar(0)))return failure("invalidProperty");}
        else if(a.value("value").toString().contains(QRegularExpression("[\\r\\n\\x{0000}]")))return failure("invalidProperty");
        const auto doc=MarkdownFile::decode(bytes);if(!doc.readOnlyReason.isEmpty())return failure("unsupportedFormat");
        const QString text=Knowledge::update(doc.text,key,a.value("value"),a.value("remove").toBool());
        if(text.isEmpty())return failure("unsupportedFormat");opened.insert(relative,doc);
        return dispatch("save",{{"path",relative},{"text",text},{"baseHash",doc.hash},{"sourceNewlines",true}});
    }
    const QString id=metadata.value("entries").toObject().value(relative).toObject().value("id").toString();
    if(id.isEmpty())return failure("notFound");
    const QString folder=path(".nextnotes/history/"+MarkdownFile::digest(id.toUtf8()),true);
    if(folder.isEmpty())return failure("unsafePath");
    if(op=="historyVersion") {
        const QString hash=a.value("hash").toString();if(!QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch())return failure("unsafePath");
        QByteArray bytes;if(!readBytes(path(".nextnotes/history/"+MarkdownFile::digest(id.toUtf8())+'/'+hash+".md",true),bytes,error)||MarkdownFile::digest(bytes)!=hash)return failure("recoveryCorrupt");
        return {{"ok",true},{"path",relative},{"hash",hash},{"text",MarkdownFile::decode(bytes).text},{"restore",a.value("restore")}};
    }
    if(op=="history") {
        QByteArray current;if(!readBytes(path(relative),current,error))return failure("readFailed");
        if(!rememberVersion(id,relative,current,error))return failure("historyWriteFailed");
        QVariantList versions;
        for(const auto &fi:QDir(folder).entryInfoList({"*.json"},QDir::Files|QDir::NoSymLinks,QDir::Time)) {
            QByteArray bytes;if(!readBytes(fi.absoluteFilePath(),bytes,error))continue;auto entry=QJsonDocument::fromJson(bytes).object().toVariantMap();entry.insert("current",entry.value("hash").toString()==MarkdownFile::digest(current));versions.append(entry);
        }
        return {{"ok",true},{"path",relative},{"versions",versions}};
    }
    return failure("unknownOperation");
}
