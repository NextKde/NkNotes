#include <QtTest>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>
#include "ManagedStore.h"
#include "ManagedAssets.h"
#include "MarkdownFile.h"
#include "RepositoryTestHooks.h"

class ManagedPortabilityTest : public QObject {
    Q_OBJECT
    static QString url(const QString &path) { return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded); }
    static QByteArray read(const QString &path) {
        QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();
    }
    static bool write(const QString &path,const QByteArray &bytes) {
        QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();
    }
    static QVariantMap create(ManagedStore &store,const QString &path,const QString &text) {
        return store.dispatch("create",{{"path",path},{"text",text}});
    }
    static QVariantMap save(ManagedStore &store,const QString &path,const QString &text) {
        const auto document=store.dispatch("read",{{"path",path}});
        return store.dispatch("save",{{"path",path},{"text",text},{"baseHash",document.value("baseHash")}});
    }
    static QString attachment(ManagedStore &store,const QByteArray &bytes) {
        const auto result=store.dispatch("importAttachment",{{"path","seed.md"},{"name","sample.bin"},{"data",bytes.toBase64()}});
        return result.value("ok").toBool()?result.value("path").toString():QString();
    }
    static QVariantMap referenceItem(ManagedStore &store,const QString &asset) {
        for(const auto &v:store.dispatch("attachments",{}).value("items").toList()) {
            const auto row=v.toMap();if(row.value("relative").toString()==asset)return row;
        }
        return {};
    }
    static QString image(const QString &target) { return "![fixture]("+target+")\n"; }
private slots:
    void cleanup() { RepositoryTestHooks::managedFailure={}; }
    void backupAcrossRootsPreservesEveryReferenceAndExportsDatabaseBytes() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());
        const auto rootA=fixture.filePath("库 A 空格"),rootB=fixture.filePath("库 B 新位置"),backup=fixture.filePath("portable.nkbackup");
        const QString current="深层/二层/current.md",historical="history.md",draftPath="draft.md",deletedPath="deleted.md";
        QMap<QString,QString> assets;QMap<QString,QByteArray> bytes;
        QString currentText,currentHash,historyText,historyHash,draftText,draftHash,templateText,deletedText,deletedHash;
        {
            ManagedStore store(rootA);QVERIFY(create(store,"seed.md","seed").value("ok").toBool());
            for(const auto &kind:{QString("current"),QString("relative"),QString("history"),QString("draft"),QString("template"),QString("deleted")}) {
                bytes.insert(kind,kind.toUtf8()+QByteArray::fromHex("000102ff80")+" synthetic bytes\n");
                assets.insert(kind,attachment(store,bytes.value(kind)));QVERIFY(!assets.value(kind).isEmpty());
            }
            currentText="# Portable\n\n"+image(url(rootA+'/'+assets.value("current"))+"?download=a%20b#page%202")
                +image("../../"+assets.value("relative")+"?raw=1#pixel");
            auto document=create(store,current,currentText);QVERIFY(document.value("ok").toBool());currentHash=document.value("baseHash").toString();
            historyText=image(url(rootA+'/'+assets.value("history")));document=create(store,historical,historyText);
            QVERIFY(document.value("ok").toBool());historyHash=document.value("baseHash").toString();QVERIFY(save(store,historical,"history retained only\n").value("ok").toBool());
            document=create(store,draftPath,"disk baseline\n");QVERIFY(document.value("ok").toBool());draftHash=document.value("baseHash").toString();
            draftText=image(url(rootA+'/'+assets.value("draft")))+"unsaved\n";
            QVERIFY(store.dispatch("checkpoint",{{"path",draftPath},{"text",draftText},{"baseHash",draftHash}}).value("ok").toBool());
            templateText="# {{title}}\n"+image(url(rootA+'/'+assets.value("template")));
            QVERIFY(store.dispatch("templateSave",{{"name","Legacy absolute"},{"text",templateText}}).value("ok").toBool());
            deletedText=image(url(rootA+'/'+assets.value("deleted")));document=create(store,deletedPath,deletedText);
            QVERIFY(document.value("ok").toBool());deletedHash=document.value("baseHash").toString();
            QVERIFY(store.dispatch("trash",{{"path",deletedPath},{"baseHash",deletedHash}}).value("ok").toBool());
            QVERIFY(store.dispatch("backup",{{"destination",url(backup)}}).value("ok").toBool());
        }
        QVERIFY(QDir().rename(rootA,rootA+" unavailable"));QVERIFY(!QFileInfo::exists(rootA));
        {
            ManagedStore store(rootB);const auto restored=store.dispatch("restoreBackup",{{"source",url(backup)}});
            QVERIFY2(restored.value("ok").toBool(),qPrintable(restored.value("detail").toString()));
            const auto manifest=restored.value("assetManifest").toMap();QCOMPARE(manifest.size(),assets.size());
            const auto document=store.dispatch("read",{{"path",current}});
            QCOMPARE(document.value("text").toString(),currentText);QCOMPARE(document.value("baseHash").toString(),currentHash);
            QCOMPARE(store.dispatch("historyVersion",{{"path",historical},{"hash",historyHash}}).value("text").toString(),historyText);
            const auto recovery=store.dispatch("recovery",{});const auto drafts=recovery.value("drafts").toList();QCOMPARE(drafts.size(),1);
            QCOMPARE(drafts.first().toMap().value("text").toString(),draftText);QCOMPARE(drafts.first().toMap().value("baseHash").toString(),draftHash);
            QCOMPARE(store.dispatch("templates",{}).value("templates").toMap().value("Legacy absolute").toString(),templateText);
            for(const auto &asset:assets) {
                QVERIFY(referenceItem(store,asset).value("references").toInt()>0);
                QCOMPARE(store.dispatch("quarantineAttachment",{{"relative",asset}}).value("error").toString(),QString("attachmentInUse"));
            }
            const auto oldIdentity=ManagedAssets::resolveIdentity({rootB,QUrl::fromLocalFile(rootB+'/'+current),manifest,true},QUrl(url(rootA+'/'+assets.value("current"))+"?download=a%20b#page%202"));
            QCOMPARE(oldIdentity,assets.value("current"));
            // Missing or changed caches must not affect an export from the BLOBs.
            QVERIFY(QFile::remove(rootB+'/'+assets.value("current")));QVERIFY(write(rootB+'/'+assets.value("relative"),"cache must not be exported"));
            auto exported=store.dispatch("exportNotes",{{"destination",url(fixture.path())},{"paths",QStringList{current}}});
            QVERIFY(exported.value("ok").toBool());QVERIFY(exported.value("warnings").toList().isEmpty());
            const auto directory=exported.value("directory").toString();const auto markdown=QString::fromUtf8(read(directory+'/'+current));
            QCOMPARE(markdown,QString("# Portable\n\n")+image("../../"+assets.value("current")+"?download=a%20b#page%202")+image("../../"+assets.value("relative")+"?raw=1#pixel"));
            QCOMPARE(read(directory+'/'+assets.value("current")),bytes.value("current"));QCOMPARE(read(directory+'/'+assets.value("relative")),bytes.value("relative"));
            QCOMPARE(store.dispatch("read",{{"path",current}}).value("baseHash").toString(),currentHash);
            auto copied=store.dispatch("saveAs",{{"path","copies/deep/current.md"},{"sourcePath",current},{"text",currentText}});
            QVERIFY(copied.value("ok").toBool());QVERIFY(!copied.value("text").toString().contains(rootA));QVERIFY(copied.value("text").toString().contains("../../"+assets.value("current")));
            auto fromTemplate=store.dispatch("fromTemplate",{{"name","Legacy absolute"},{"title","Created"},{"folder","copies/deep"}});
            QVERIFY(fromTemplate.value("ok").toBool());QVERIFY(fromTemplate.value("text").toString().contains("../../"+assets.value("template")));
            const auto trash=recovery.value("trashItems").toList();QCOMPARE(trash.size(),1);
            QVERIFY(store.dispatch("restore",{{"token",trash.first().toMap().value("token")}}).value("ok").toBool());
            const auto deleted=store.dispatch("read",{{"path",deletedPath}});QCOMPARE(deleted.value("text").toString(),deletedText);QCOMPARE(deleted.value("baseHash").toString(),deletedHash);
            // Bring historical/draft text into independent notes without changing
            // the original history, draft baseline, or legacy template bytes.
            QVERIFY(store.dispatch("saveAs",{{"path","history-copy.md"},{"sourcePath",historical},{"text",historyText}}).value("ok").toBool());
            QVERIFY(store.dispatch("saveAs",{{"path","draft-copy.md"},{"sourcePath",draftPath},{"text",draftText}}).value("ok").toBool());
            exported=store.dispatch("exportNotes",{{"destination",url(fixture.path())}});QVERIFY(exported.value("ok").toBool());
            for(auto it=assets.cbegin();it!=assets.cend();++it)QCOMPARE(read(exported.value("directory").toString()+'/'+it.value()),bytes.value(it.key()));
        }
        ManagedStore reopened(rootB);
        QCOMPARE(reopened.dispatch("read",{{"path",current}}).value("baseHash").toString(),currentHash);
        QCOMPARE(reopened.dispatch("historyVersion",{{"path",historical},{"hash",historyHash}}).value("text").toString(),historyText);
        QCOMPARE(reopened.dispatch("recovery",{}).value("drafts").toList().first().toMap().value("text").toString(),draftText);
    }
    void copyMoveHistoryAndTemplatesKeepOwnedRelativeAttachments() {
        QTemporaryDir fixture;ManagedStore store(fixture.path());QVERIFY(create(store,"seed.md","seed").value("ok").toBool());
        const auto asset=attachment(store,"owned synthetic bytes");QVERIFY(!asset.isEmpty());
        const QString source="one/two/source.md",suffix="?download=a%20b#part%202",text="# Relative\n"+image("../../"+asset+suffix);
        const auto original=create(store,source,text);QVERIFY(original.value("ok").toBool());const auto oldHash=original.value("baseHash");
        auto copy=store.dispatch("saveAs",{{"sourcePath",source},{"path","copy.md"},{"text",text}});
        QVERIFY(copy.value("ok").toBool());QCOMPARE(copy.value("text").toString(),QString("# Relative\n")+image(asset+suffix));
        auto nested=store.dispatch("saveAs",{{"sourcePath","copy.md"},{"path","three/four/five/copy.md"},{"text",copy.value("text")}});
        QVERIFY(nested.value("ok").toBool());QVERIFY(nested.value("text").toString().contains("../../../"+asset+suffix));
        auto savedTemplate=store.dispatch("templateSave",{{"path",source},{"name","Portable"},{"text",text}});
        QVERIFY(savedTemplate.value("ok").toBool());QCOMPARE(savedTemplate.value("templates").toMap().value("Portable").toString(),QString("# Relative\n")+image(asset+suffix));
        auto generated=store.dispatch("fromTemplate",{{"name","Portable"},{"folder","three/four/five"}});
        QVERIFY(generated.value("ok").toBool());QVERIFY(generated.value("text").toString().contains("../../../"+asset+suffix));
        QVERIFY(store.dispatch("rename",{{"path",source},{"target","moved.md"}}).value("ok").toBool());
        QCOMPARE(store.dispatch("read",{{"path","moved.md"}}).value("text").toString(),QString("# Relative\n")+image(asset+suffix));
        const auto historical=store.dispatch("historyVersion",{{"path","moved.md"},{"hash",oldHash}});
        QVERIFY(historical.value("ok").toBool());QCOMPARE(historical.value("text").toString(),text);QCOMPARE(historical.value("hash"),oldHash);
        const auto manifest=store.dispatch("list",{}).value("assetManifest").toMap();
        QCOMPARE(ManagedAssets::resolveIdentity({fixture.path(),QUrl::fromLocalFile(fixture.filePath("moved.md")),manifest,true},QUrl("../../"+asset+suffix)),asset);
        auto historyCopy=store.dispatch("saveAs",{{"path","recovered.md"},{"sourcePath","moved.md"},{"text",historical.value("text")}});
        QVERIFY(historyCopy.value("ok").toBool());QCOMPARE(historyCopy.value("text").toString(),QString("# Relative\n")+image(asset+suffix));
        auto recoveryCopy=store.dispatch("saveAs",{{"path","recovery-copy.md"},{"text",historical.value("text")},{"recoveryCopy",true}});
        QVERIFY(recoveryCopy.value("ok").toBool());QCOMPARE(recoveryCopy.value("text").toString(),historyCopy.value("text").toString());
        const auto draftBase=store.dispatch("read",{{"path","moved.md"}}).value("baseHash");
        QVERIFY(store.dispatch("checkpoint",{{"path","moved.md"},{"text",text+"draft"},{"baseHash",draftBase}}).value("ok").toBool());
        QVERIFY(store.dispatch("rename",{{"path","moved.md"},{"target","deeper/again/moved.md"}}).value("ok").toBool());
        const auto draft=store.dispatch("recovery",{}).value("drafts").toList().first().toMap();
        QCOMPARE(draft.value("path").toString(),QString("deeper/again/moved.md"));QCOMPARE(draft.value("text").toString(),text+"draft");QCOMPARE(draft.value("baseHash"),draftBase);
        QCOMPARE(store.dispatch("quarantineAttachment",{{"relative",asset}}).value("error").toString(),QString("attachmentInUse"));
    }
    void historyDraftTemplateAndMemoryReferencesAlonePreventQuarantine() {
        QTemporaryDir fixture;QMap<QString,QString> assets;
        {
            ManagedStore store(fixture.path());QVERIFY(create(store,"seed.md","seed").value("ok").toBool());
            for(const auto &kind:{QString("history"),QString("draft"),QString("template"),QString("deleted"),QString("unused")}) {
                assets.insert(kind,attachment(store,kind.toUtf8()));QVERIFY(!assets.value(kind).isEmpty());
            }
            const auto historical=create(store,"nested/history.md",image("../"+assets.value("history")));QVERIFY(historical.value("ok").toBool());
            QVERIFY(save(store,"nested/history.md","no current attachment\n").value("ok").toBool());
            QVERIFY(store.dispatch("rename",{{"path","nested/history.md"},{"target","history.md"}}).value("ok").toBool());
            const auto draft=create(store,"draft.md","baseline");QVERIFY(draft.value("ok").toBool());
            QVERIFY(store.dispatch("checkpoint",{{"path","draft.md"},{"text",image("../../../"+assets.value("draft"))},{"baseHash",draft.value("baseHash")}}).value("ok").toBool());
            QVERIFY(store.dispatch("templateSave",{{"name","Only template"},{"text",image(url(fixture.path()+" old/"+assets.value("template")))}}).value("ok").toBool());
            const auto deleted=create(store,"deleted.md",image(assets.value("deleted")));QVERIFY(deleted.value("ok").toBool());
            QVERIFY(store.dispatch("trash",{{"path","deleted.md"},{"baseHash",deleted.value("baseHash")}}).value("ok").toBool());
        }
        ManagedStore reopened(fixture.path());
        for(const auto &kind:{QString("history"),QString("draft"),QString("template"),QString("deleted")}) {
            QVERIFY(referenceItem(reopened,assets.value(kind)).value("references").toInt()>0);
            QCOMPARE(reopened.dispatch("quarantineAttachment",{{"relative",assets.value(kind)}}).value("error").toString(),QString("attachmentInUse"));
        }
        QVERIFY(reopened.dispatch("quarantineAttachment",{{"relative",assets.value("unused")}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("discardDraft",{{"path","draft.md"}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("quarantineAttachment",{{"relative",assets.value("draft")}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("templateDelete",{{"name","Only template"}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("quarantineAttachment",{{"relative",assets.value("template")}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("restoreAttachment",{{"token",assets.value("unused")}}).value("ok").toBool());
        RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="draftCommit";};
        const auto failed=reopened.dispatch("checkpoint",{{"path","draft.md"},{"text",image(assets.value("unused"))},{"baseHash","baseline"}});
        QVERIFY(!failed.value("ok").toBool());QVERIFY(failed.value("draftInMemory").toBool());
        QCOMPARE(reopened.dispatch("quarantineAttachment",{{"relative",assets.value("unused")}}).value("error").toString(),QString("attachmentInUse"));
        RepositoryTestHooks::managedFailure={};QVERIFY(reopened.dispatch("discardDraft",{{"path","draft.md"}}).value("ok").toBool());
        QVERIFY(reopened.dispatch("quarantineAttachment",{{"relative",assets.value("unused")}}).value("ok").toBool());
    }
    void rejectedTraversalIsNotRewrittenIntoAnOwnedAttachment() {
        QTemporaryDir fixture;ManagedStore store(fixture.filePath("Library"));QVERIFY(create(store,"seed.md","seed").value("ok").toBool());
        const auto asset=attachment(store,"owned bytes");QVERIFY(!asset.isEmpty());
        const auto text=image("child/../../"+asset);QVERIFY(create(store,"nested/source.md",text).value("ok").toBool());
        const auto copied=store.dispatch("saveAs",{{"path","copy.md"},{"sourcePath","nested/source.md"},{"text",text}});
        QVERIFY(copied.value("ok").toBool());QCOMPARE(copied.value("text").toString(),text);
        const auto saved=store.dispatch("templateSave",{{"name","Unresolved"},{"path","nested/source.md"},{"text",text}});
        QVERIFY(saved.value("ok").toBool());QCOMPARE(saved.value("templates").toMap().value("Unresolved").toString(),text);
        const auto generated=store.dispatch("fromTemplate",{{"name","Unresolved"},{"folder","deeper/nested"}});
        QVERIFY(generated.value("ok").toBool());QCOMPARE(generated.value("text").toString(),text);
        const auto exported=store.dispatch("exportNotes",{{"destination",url(fixture.path())},{"paths",QStringList{"copy.md"}}});
        QVERIFY(exported.value("ok").toBool());QCOMPARE(exported.value("warnings").toList().size(),1);
        QCOMPARE(exported.value("warnings").toList().first().toMap().value("code").toString(),QString("missingAttachment"));
        QVERIFY(!QFileInfo::exists(exported.value("directory").toString()+'/'+asset));
    }
    void mixedHtmlAndUnknownLinksArePreservedWithExplicitExportWarnings() {
        QTemporaryDir fixture;const auto root=fixture.filePath("Library");ManagedStore store(root);
        QVERIFY(create(store,"seed.md","seed").value("ok").toBool());const QByteArray bytes("export BLOB bytes");const auto asset=attachment(store,bytes);QVERIFY(!asset.isEmpty());
        const auto old=url(fixture.filePath("unavailable old/"+asset)),missing="assets/"+QString(64,'a')+".bin";
        const auto html="<img alt='[ignored](unknown.bin)' src=\""+old+"\">";
        const auto text=html+"\n"+image(old+"?raw=1#part")+image(missing)+"[remote](https://example.com/file.bin)\n"
            +"[reference][r]\n\n[r]: <"+old+"?q=a%20b#ref> \"title\"\n";
        QVERIFY(create(store,"mixed.md",text).value("ok").toBool());QVERIFY(QFile::remove(root+'/'+asset));
        const auto result=store.dispatch("exportNotes",{{"destination",url(fixture.path())},{"paths",QStringList{"mixed.md"}}});
        QVERIFY(result.value("ok").toBool());const auto markdown=QString::fromUtf8(read(result.value("directory").toString()+"/mixed.md"));
        QVERIFY(markdown.contains(html));QVERIFY(markdown.contains(image(asset+"?raw=1#part")));QVERIFY(markdown.contains(image(missing)));
        QVERIFY(markdown.contains("[r]: <"+asset+"?q=a%20b#ref> \"title\""));
        QCOMPARE(read(result.value("directory").toString()+'/'+asset),bytes);
        QSet<QString> codes;for(const auto &v:result.value("warnings").toList())codes.insert(v.toMap().value("code").toString());
        QCOMPARE(codes,(QSet<QString>{"missingAttachment","unsupportedHtml"}));QCOMPARE(result.value("warnings").toList().size(),2);
        QVERIFY(!QFileInfo::exists(result.value("directory").toString()+'/'+missing));
        QCOMPARE(store.dispatch("read",{{"path","mixed.md"}}).value("text").toString(),text);
    }
};
QTEST_GUILESS_MAIN(ManagedPortabilityTest)
#include "ManagedPortabilityTest.moc"
