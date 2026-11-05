#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "NotesRepository.h"
#include "MarkdownFile.h"
#include "SafeCommit.h"
#include "RepositoryTestHooks.h"
#include <QProcess>
#include <QProcessEnvironment>
#include <cstdlib>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef Q_OS_LINUX
#include <sys/statvfs.h>
#endif

class NotesRepositoryTest : public QObject {
    Q_OBJECT
    static bool write(const QString &p, const QByteArray &bytes) {
        QFile f(p); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }
    static QByteArray read(const QString &p) {
        QFile f(p); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
    }
    static QVariantMap call(NotesRepository &r, const QString &op, const QVariantMap &args = {}) {
        QSignalSpy spy(&r, &NotesRepository::finished);
        const auto id = r.request(op, args);
        if (spy.isEmpty() && !spy.wait(10000)) return {{"ok", false}, {"error", "testTimeout"}};
        for (const auto &signal : spy)
            if (signal[0].toULongLong() == id) return signal[2].toMap();
        return {{"ok", false}, {"error", "missingRequest"}};
    }
private slots:
    void missingWikiCreationIsScopedAndNeverOverwrites() {
        QTemporaryDir d;QVERIFY(QDir(d.path()).mkdir("folder"));QVERIFY(write(d.filePath("folder/a.md"),"# Source\n"));
        NotesRepository r;call(r,"selectLibrary",{{"path",d.path()}});
        auto missing=call(r,"resolveWiki",{{"path","folder/a.md"},{"target","新笔记"}});
        QVERIFY(missing.value("missing").toBool());QCOMPARE(missing.value("targetPath").toString(),QString("folder/新笔记.md"));
        auto created=call(r,"createWiki",{{"path","folder/a.md"},{"target","新笔记"}});QVERIFY(created.value("ok").toBool());
        QVERIFY(read(d.filePath("folder/新笔记.md")).contains("# 新笔记"));
        QVERIFY(write(d.filePath("folder/新笔记.md"),"# External\nkeep\n"));
        QVERIFY(call(r,"createWiki",{{"path","folder/a.md"},{"target","新笔记"}}).value("ok").toBool());QCOMPARE(read(d.filePath("folder/新笔记.md")),QByteArray("# External\nkeep\n"));
        QVERIFY(call(r,"createWiki",{{"path","folder/a.md"},{"target","../nested/目标"}}).value("ok").toBool());QVERIFY(QFileInfo::exists(d.filePath("nested/目标.md")));
        for(const QString bad:{QString("../../outside"),QString("../.git/config"),QString("../assets/n"),QString("/absolute"),QString("bad\nname")})QVERIFY(!call(r,"createWiki",{{"path","folder/a.md"},{"target",bad}}).value("ok").toBool());
        QVERIFY(write(d.filePath("new.md"),"# Duplicate\n"));QVERIFY(write(d.filePath("another.md"),"# Duplicate\n"));
        QCOMPARE(call(r,"createWiki",{{"path","folder/a.md"},{"target","Duplicate"}}).value("error").toString(),QString("ambiguousLink"));
    }
    void tagBatchMergesPreservesHistoryAndSkipsChangedSnapshots() {
        QTemporaryDir d;NotesRepository r;
        QVERIFY(write(d.filePath("a.md"),"---\ncustom: keep\ntags: [old, keep]\n---\n# A\n"));
        QVERIFY(write(d.filePath("b.md"),"---\ntags:\n- old\n- new\n---\n# B\n"));
        call(r,"selectLibrary",{{"path",d.path()}});
        QVariantList items;for(const QString p:{QString("a.md"),QString("b.md")})items.append(QVariantMap{{"path",p},{"baseHash",call(r,"read",{{"path",p}}).value("baseHash")}});
        auto added=call(r,"tagBatch",{{"action","add"},{"tags",QStringList{"new","extra"}},{"items",items}});QCOMPARE(added.value("succeeded").toInt(),2);
        QVERIFY(read(d.filePath("a.md")).contains("custom: keep"));QVERIFY(!read(d.filePath("b.md")).contains("- old"));
        auto plan=call(r,"tagPlan",{{"action","rename"},{"tags",QStringList{"old"}},{"replacement","new"}});QCOMPARE(plan.value("count").toInt(),2);
        QVERIFY(write(d.filePath("b.md"),"external content\n"));
        auto renamed=call(r,"tagBatch",plan);QCOMPARE(renamed.value("succeeded").toInt(),1);QCOMPARE(renamed.value("failed").toInt(),1);QCOMPARE(read(d.filePath("b.md")),QByteArray("external content\n"));
        auto props=call(r,"knowledge",{{"path","a.md"}}).value("properties").toMap();QCOMPARE(props.value("tags").toStringList(),(QStringList{"new","keep","extra"}));
        QVERIFY(call(r,"history",{{"path","a.md"}}).value("versions").toList().size()>=3);
        auto doc=call(r,"read",{{"path","a.md"}});auto removed=call(r,"tagBatch",{{"action","remove"},{"tags",QStringList{"extra"}},{"items",QVariantList{QVariantMap{{"path","a.md"},{"baseHash",doc.value("baseHash")}}}}});QCOMPARE(removed.value("succeeded").toInt(),1);
        QCOMPARE(call(r,"search",{{"query","tag:extra"},{"indexed",true}}).value("notes").toList().size(),0);
    }

    void templatesAndDailyNotesDoNotOverwriteExistingContent() {
        QTemporaryDir d,other;NotesRepository r;call(r,"selectLibrary",{{"path",d.path()}});
        QVERIFY(call(r,"templateSave",{{"name","Meeting"},{"text","---\nnextnotes_id: old-id\ntags: [\"meeting\"]\n---\n# {{title}}\n{{date}} {{time}}\n"}}).value("ok").toBool());
        auto first=call(r,"fromTemplate",{{"name","Meeting"},{"title","Weekly"}}),second=call(r,"fromTemplate",{{"name","Meeting"},{"title","Next"}});
        QVERIFY(first.value("ok").toBool());QVERIFY(second.value("ok").toBool());QVERIFY(first.value("id")!=second.value("id"));
        const auto bytes=read(d.filePath(first.value("path").toString()));QVERIFY(bytes.contains("# Weekly"));QVERIFY(!bytes.contains("{{date}}"));QVERIFY(!bytes.contains("old-id"));
        QVERIFY(call(r,"templateSave",{{"name","每日笔记"},{"text","# {{date}}\n\nDiary\n"}}).value("ok").toBool());
        auto daily=call(r,"dailyNote");QVERIFY(daily.value("ok").toBool());auto again=call(r,"dailyNote");QVERIFY(again.value("existing").toBool());QCOMPARE(daily.value("path"),again.value("path"));
        const auto before=read(d.filePath(daily.value("path").toString()));call(r,"dailyNote");QCOMPARE(read(d.filePath(daily.value("path").toString())),before);
        call(r,"smartSave",{{"name","Meetings"},{"query","tag:meeting"},{"filter","all"}});
        QVERIFY(call(r,"selectLibrary",{{"path",other.path()}}).value("ok").toBool());auto reopened=call(r,"selectLibrary",{{"path",d.path()}});QVERIFY(reopened.value("smartFolders").toMap().contains("Meetings"));
        QVERIFY(call(r,"templates").value("templates").toMap().contains("Meeting"));
    }
    void indexedThousandNoteQueriesDoNotRescan() {
        QTemporaryDir d;for(int i=0;i<1000;++i)QVERIFY(write(d.filePath(QString::number(i)+".md"),("# Note "+QString::number(i)+"\n\n中文知识检索 "+QString::number(i)+"\n").toUtf8()));
        NotesRepository r;QVERIFY(call(r,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());int scans=0;RepositoryTestHooks::beforeScan=[&](bool){++scans;};
        QElapsedTimer timer;timer.start();bool valid=true;for(int i=0;i<30;++i){auto found=call(r,"search",{{"query","中文知识检索 999"},{"indexed",true}});valid=valid&&found.value("notes").toList().size()==1;}
        auto elapsed=timer.elapsed();RepositoryTestHooks::beforeScan={};QVERIFY(valid);QCOMPARE(scans,0);qInfo()<<"1000 notes, 30 indexed searches:"<<elapsed<<"ms";
    }
    void knowledgeIndexPropertiesLinksSmartFoldersAndHistory() {
        QTemporaryDir d;QVERIFY(QDir(d.path()).mkdir("folder"));
        QVERIFY(write(d.filePath("a.md"),"---\ncustom: keep\ntags: [\"work\", \"中文\"]\nstatus: draft\n---\n# Alpha\n\n中文检索 [[./b.md|Beta]] [normal](b.md#section)\n`[[fake]]`\n"));
        QVERIFY(write(d.filePath("b.md"),"# Beta\n\n## Section\n"));
        QVERIFY(write(d.filePath("folder/b.md"),"# Other\n"));
        NotesRepository r;QVERIFY(call(r,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());
        auto search=call(r,"search",{{"query","tag:中文 prop:status=draft 中文检索"},{"indexed",true}});QCOMPARE(search.value("notes").toList().size(),1);
        QCOMPARE(call(r,"search",{{"query",QString::fromUtf8("tag:\"中文\" prop:\"status=draft\" 中文检索")},{"indexed",true}}).value("notes").toList().size(),1);
        QCOMPARE(call(r,"resolveWiki",{{"path","b.md"},{"target","Alpha"}}).value("path").toString(),QString("a.md"));
        const auto updates=search.value("indexUpdates");for(int i=0;i<5;++i)QCOMPARE(call(r,"search",{{"query","检索"},{"indexed",true}}).value("indexUpdates"),updates);
        QCOMPARE(call(r,"resolveWiki",{{"path","a.md"},{"target","b"}}).value("error").toString(),QString("ambiguousLink"));
        QCOMPARE(call(r,"resolveWiki",{{"path","a.md"},{"target","./b.md"}}).value("path").toString(),QString("b.md"));
        auto knowledge=call(r,"knowledge",{{"path","b.md"}});QCOMPARE(knowledge.value("backlinks").toList().size(),1);
        auto doc=call(r,"read",{{"path","a.md"}});auto property=call(r,"setProperty",{{"path","a.md"},{"baseHash",doc.value("baseHash")},{"key","status"},{"value","done"}});QVERIFY(property.value("ok").toBool());
        QVERIFY(read(d.filePath("a.md")).contains("custom: keep"));
        QCOMPARE(call(r,"search",{{"query","prop:status=done"},{"indexed",true}}).value("notes").toList().size(),1);
        auto history=call(r,"history",{{"path","a.md"}}).value("versions").toList();QCOMPARE(history.size(),2);
        QString oldHash;for(auto v:history)if(!v.toMap().value("current").toBool())oldHash=v.toMap().value("hash").toString();
        QVERIFY(call(r,"historyVersion",{{"path","a.md"},{"hash",oldHash}}).value("text").toString().contains("status: draft"));
        doc=call(r,"read",{{"path","a.md"}});QVERIFY(call(r,"rename",{{"path","a.md"},{"target","renamed.md"},{"baseHash",doc.value("baseHash")}}).value("ok").toBool());
        QVERIFY(call(r,"historyVersion",{{"path","renamed.md"},{"hash",oldHash}}).value("ok").toBool());
        QVERIFY(call(r,"smartSave",{{"name","Work"},{"query","tag:work"},{"filter","all"}}).value("ok").toBool());
        QVERIFY(call(r,"knowledge",{{"path","renamed.md"}}).value("smartFolders").toMap().contains("Work"));
        QVERIFY(call(r,"search",{{"query","tag:work"},{"indexed",true}}).value("smartFolders").toMap().contains("Work"));
        QVERIFY(write(d.filePath("b.md"),"# Beta\nexternal index update\n"));
        QVERIFY(call(r,"reindex").value("ok").toBool());QCOMPARE(call(r,"search",{{"query","external index update"},{"indexed",true}}).value("notes").toList().size(),1);
    }
    void sourceSaveChangesTerminalNewlineWithoutChangingEncoding() {
        QTemporaryDir d;QVERIFY(write(d.filePath("a.md"),QByteArray("\xEF\xBB\xBF")+"old\r\n"));
        NotesRepository r;call(r,"selectLibrary",{{"path",d.path()}});auto doc=call(r,"read",{{"path","a.md"}});
        auto saved=call(r,"save",{{"path","a.md"},{"baseHash",doc.value("baseHash")},{"text","line\nlast"},{"sourceNewlines",true}});
        QVERIFY(saved.value("ok").toBool());QCOMPARE(read(d.filePath("a.md")),QByteArray("\xEF\xBB\xBF")+"line\r\nlast");
    }
    void recycleBatchRestorePurgeAndChangedSnapshot() {
        QTemporaryDir d;QVERIFY(QDir(d.path()).mkdir("folder"));
        QVERIFY(write(d.filePath("a.md"),"A\n"));QVERIFY(write(d.filePath("b.md"),"B\n"));QVERIFY(write(d.filePath("folder/n.md"),"nested\n"));
        NotesRepository r;auto list=call(r,"selectLibrary",{{"path",d.path()}});
        for(const QString name:{QString("a.md"),QString("b.md")}){auto doc=call(r,"read",{{"path",name}});QVERIFY(call(r,"trash",{{"path",name},{"baseHash",doc.value("baseHash")}}).value("ok").toBool());}
        QVERIFY(call(r,"trashFolder",{{"path","folder"}}).value("ok").toBool());
        auto items=call(r,"recovery").value("trashItems").toList();QCOMPARE(items.size(),3);
        QVERIFY(write(d.filePath("a.md"),"external"));
        auto restored=call(r,"trashBatch",{{"action","restore"},{"items",items}});QCOMPARE(restored.value("succeeded").toInt(),2);QCOMPARE(restored.value("failed").toInt(),1);
        QCOMPARE(read(d.filePath("a.md")),QByteArray("external"));QVERIFY(QFileInfo::exists(d.filePath("folder/n.md")));
        auto remain=call(r,"recovery").value("trashItems").toList();QCOMPARE(remain.size(),1);
        const auto item=remain.first().toMap();const QString stored=d.filePath(".nextnotes/trash/"+item.value("token").toString()+".md");
        QVERIFY(write(stored,"changed"));auto rejected=call(r,"trashBatch",{{"action","purge"},{"items",remain}});QCOMPARE(rejected.value("failed").toInt(),1);QCOMPARE(read(stored),QByteArray("changed"));
        remain=call(r,"recovery").value("trashItems").toList();QCOMPARE(call(r,"trashBatch",{{"action","purge"},{"items",remain}}).value("succeeded").toInt(),1);QVERIFY(!QFileInfo::exists(stored));
        QVERIFY(call(r,"trashFolder",{{"path","folder"}}).value("ok").toBool());
        items=call(r,"recovery").value("trashItems").toList();QCOMPARE(call(r,"trashBatch",{{"action","purge"},{"items",items}}).value("succeeded").toInt(),1);
        QVERIFY(call(r,"recovery").value("trashItems").toList().isEmpty());
        QCOMPARE(read(d.filePath("a.md")),QByteArray("external"));
    }
    void recycleRejectsSymlinkTree() {
        QTemporaryDir d,out;QVERIFY(QDir(d.path()).mkdir("folder"));QVERIFY(write(d.filePath("folder/n.md"),"note"));QVERIFY(write(out.filePath("keep"),"outside"));
        NotesRepository r;call(r,"selectLibrary",{{"path",d.path()}});auto removed=call(r,"trashFolder",{{"path","folder"}});QVERIFY(removed.value("ok").toBool());
        const QString stored=d.filePath(".nextnotes/folder-trash/"+removed.value("trashToken").toString());
        auto before=call(r,"recovery").value("trashItems").toList();QVERIFY(QFile::link(out.path(),stored+"/link"));
        QCOMPARE(call(r,"trashBatch",{{"action","purge"},{"items",before}}).value("failed").toInt(),1);QCOMPARE(read(out.filePath("keep")),QByteArray("outside"));
    }
    void interruptedMoveCanRollbackAfterReopen() {
        QTemporaryDir d; QVERIFY(QDir(d.path()).mkdir("dest"));
        const QByteArray original("[self](a.md)\n");QVERIFY(write(d.filePath("a.md"),original));
        {
            NotesRepository r;QVERIFY(call(r,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());
            auto a=call(r,"read",{{"path","a.md"}});
            SafeCommit::afterExchange=[&]{write(d.filePath("dest/b.md"),"collision");};
            auto result=call(r,"rename",{{"path","a.md"},{"target","dest/b.md"},{"baseHash",a.value("baseHash")}});
            SafeCommit::afterExchange={};QVERIFY(!result.value("ok").toBool());
            QCOMPARE(read(d.filePath("dest/b.md")),QByteArray("collision"));
        }
        QVERIFY(QFile::remove(d.filePath("dest/b.md")));
        NotesRepository reopened;QVERIFY(call(reopened,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());
        QVERIFY(call(reopened,"read",{{"path","a.md"}}).value("readOnly").toBool());
        QVERIFY(call(reopened,"recoverMove").value("ok").toBool());
        QCOMPARE(read(d.filePath("a.md")),original);
    }
    void linkedBatchAndFolderRecycle() {
        QTemporaryDir d;QVERIFY(QDir(d.path()).mkdir("dest"));
        QVERIFY(write(d.filePath("a.md"),"[b](b.md)\n"));QVERIFY(write(d.filePath("b.md"),"[a](a.md)\n"));
        NotesRepository r;auto list=call(r,"selectLibrary",{{"path",d.path()}});
        auto result=call(r,"batch",{{"action","move"},{"folder","dest"},{"items",list.value("notes")}});
        QCOMPARE(result.value("succeeded").toInt(),2);
        QVERIFY(read(d.filePath("dest/a.md")).contains("(b.md)"));QVERIFY(read(d.filePath("dest/b.md")).contains("(a.md)"));
        QVERIFY(write(d.filePath("dest/.hidden"),"attachment"));
        auto removed=call(r,"trashFolder",{{"path","dest"}});QVERIFY(removed.value("ok").toBool());
        QVERIFY(!QFileInfo::exists(d.filePath("dest")));
        QVERIFY(call(r,"restoreFolder",{{"token",removed.value("trashToken")}}).value("ok").toBool());
        QCOMPARE(read(d.filePath("dest/.hidden")),QByteArray("attachment"));
        QVERIFY(read(d.filePath("dest/b.md")).contains("(a.md)"));
    }
    void relocationUpdatesReferences() {
        QTemporaryDir d; QVERIFY(QDir(d.path()).mkdir("folder"));
        QVERIFY(write(d.filePath("a.md"), "# A\n\n[x](b.md#hello)\n![pic](assets/p.png)\n`[code](b.md)`\n"));
        QVERIFY(write(d.filePath("b.md"), "# B\n\n[back](a.md)\n"));
        NotesRepository r; QVERIFY(call(r,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());
        auto a=call(r,"read",{{"path","a.md"}});
        auto moved=call(r,"rename",{{"path","a.md"},{"target","folder/renamed.md"},{"baseHash",a.value("baseHash")}});
        QVERIFY2(moved.value("ok").toBool(),qPrintable(moved.value("error").toString()));
        QVERIFY(read(d.filePath("folder/renamed.md")).contains("(../b.md#hello)"));
        QVERIFY(read(d.filePath("folder/renamed.md")).contains("(../assets/p.png)"));
        QVERIFY(read(d.filePath("folder/renamed.md")).contains("`[code](b.md)`"));
        QVERIFY(read(d.filePath("b.md")).contains("(folder/renamed.md)"));
        auto folder=call(r,"renameFolder",{{"path","folder"},{"target","renamed folder"}});
        QVERIFY2(folder.value("ok").toBool(),qPrintable(folder.value("error").toString()));
        QVERIFY(QFileInfo::exists(d.filePath("renamed folder/renamed.md")));
        QVERIFY(read(d.filePath("b.md")).contains("(renamed%20folder/renamed.md)"));
        QVERIFY(!call(r,"renameFolder",{{"path","renamed folder"},{"target","renamed folder/child"}}).value("ok").toBool());
    }
    void relocationRecoveryPreservesExternalChanges() {
        QTemporaryDir d; QVERIFY(QDir(d.path()).mkdir("dest"));
        QVERIFY(write(d.filePath("a.md"),"[self](a.md)\n"));
        NotesRepository r; QVERIFY(call(r,"selectLibrary",{{"path",d.path()}}).value("ok").toBool());
        auto a=call(r,"read",{{"path","a.md"}});
        SafeCommit::beforeCommit=[&]{write(d.filePath("a.md"),"external\n");};
        auto moved=call(r,"rename",{{"path","a.md"},{"target","dest/b.md"},{"baseHash",a.value("baseHash")}});
        SafeCommit::beforeCommit={};
        QVERIFY(!moved.value("ok").toBool()); QCOMPARE(read(d.filePath("a.md")),QByteArray("external\n"));
        QVERIFY(!call(r,"recoverMove").value("ok").toBool());
        QCOMPARE(read(d.filePath("a.md")),QByteArray("external\n"));
        QVERIFY(write(d.filePath("a.md"),"[self](a.md)\n"));
        QVERIFY(call(r,"recoverMove").value("ok").toBool());
        QVERIFY(!QFileInfo::exists(d.filePath(".nextnotes/move.json")));
    }
    void organizerPinsAndDeterministicSorting() {
        QTemporaryDir dir; QVERIFY(write(dir.filePath("a.md"), "# Alpha\n")); QVERIFY(write(dir.filePath("b.md"), "# Beta\n"));
        {
            NotesRepository r; auto listing = call(r, "selectLibrary", {{"path", dir.path()}}); QVERIFY(listing.value("ok").toBool());
            auto sorted = call(r, "search", {{"sort", "title"}, {"ascending", false}}).value("notes").toList();
            QCOMPARE(sorted.first().toMap().value("path").toString(), QString("b.md"));
            const auto a = sorted.last().toMap();
            auto batch = call(r, "batch", {{"action", "pin"}, {"items", QVariantList{a}}}); QCOMPARE(batch.value("succeeded").toInt(), 1);
            sorted = call(r, "search", {{"sort", "title"}, {"ascending", false}}).value("notes").toList();
            QCOMPARE(sorted.first().toMap().value("path").toString(), QString("a.md"));
            QVERIFY(sorted.first().toMap().value("pinned").toBool()); QVERIFY(!sorted.first().toMap().value("favorite").toBool());
            QVERIFY(call(r, "search", {{"filter", "favorite"}}).value("notes").toList().isEmpty());
            QCOMPARE(read(dir.filePath("a.md")), QByteArray("# Alpha\n"));
        }
        NotesRepository reopened; QVERIFY(call(reopened, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto notes = call(reopened, "search", {{"sort", "title"}, {"ascending", false}}).value("notes").toList();
        QVERIFY(notes.first().toMap().value("pinned").toBool());
        QCOMPARE(call(reopened, "batch", {{"action", "unpin"}, {"items", QVariantList{notes.first()}}}).value("failed").toInt(), 0);
        notes = call(reopened, "search", {{"sort", "title"}, {"ascending", false}}).value("notes").toList();
        QCOMPARE(notes.first().toMap().value("path").toString(), QString("b.md"));
    }
    void organizerBatchPartialFailuresCopyTrashAndRestore() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("target"));
        const QByteArray original("---\ncustom: retained\n---\n# A\n\nPlain body.\n");
        QVERIFY(write(dir.filePath("a.md"), original));
        QVERIFY(write(dir.filePath("b.md"), "# B\n\n![image](assets/a.png)\n"));
        QVERIFY(write(dir.filePath("c.md"), "# C\n")); QVERIFY(write(dir.filePath("target/c.md"), "existing\n"));
        QVERIFY(write(dir.filePath("d.md"), "# D\n"));
        NotesRepository r; auto listing = call(r, "selectLibrary", {{"path", dir.path()}}); QVERIFY(listing.value("ok").toBool());
        QVariantList items; for (const auto &v : listing.value("notes").toList()) if (!v.toMap().value("path").toString().startsWith("target/")) items.append(v);
        QVERIFY(write(dir.filePath("d.md"), "external changed\n"));
        const auto moved = call(r, "batch", {{"action", "move"}, {"folder", "target"}, {"items", items}});
        QCOMPARE(moved.value("succeeded").toInt(), 2); QCOMPARE(moved.value("failed").toInt(), 2);
        QStringList errors; for (const auto &v : moved.value("results").toList()) errors.append(v.toMap().value("error").toString());
        QVERIFY(errors.contains("alreadyExists")); QVERIFY(errors.contains("conflict"));
        QVERIFY(!QFileInfo::exists(dir.filePath("a.md"))); QCOMPARE(read(dir.filePath("target/a.md")), original);
        QCOMPARE(read(dir.filePath("target/c.md")), QByteArray("existing\n"));
        QVERIFY(read(dir.filePath("target/b.md")).contains("../assets/a.png")); QCOMPARE(read(dir.filePath("d.md")), QByteArray("external changed\n"));
        const QVariantMap source{{"path", "target/a.md"}, {"baseHash", MarkdownFile::digest(original)}};
        const auto copied = call(r, "batch", {{"action", "duplicate"}, {"items", QVariantList{source}}}); QCOMPARE(copied.value("succeeded").toInt(), 1);
        const QString copyPath = copied.value("results").toList().first().toMap().value("newPath").toString();
        QCOMPARE(read(dir.filePath(copyPath)), original);
        const auto sourceRead = call(r, "read", {{"path", "target/a.md"}}), copyRead = call(r, "read", {{"path", copyPath}});
        QVERIFY(sourceRead.value("id") != copyRead.value("id"));
        const auto removed = call(r, "batch", {{"action", "trash"}, {"items", QVariantList{source, QVariantMap{{"path", copyPath}, {"baseHash", copyRead.value("baseHash")}}}}});
        QCOMPARE(removed.value("succeeded").toInt(), 2); QVERIFY(!QFileInfo::exists(dir.filePath(copyPath)));
        const auto recovery = call(r, "recovery").value("trash").toMap(); QCOMPARE(recovery.size(), 2);
        for (auto it = recovery.begin(); it != recovery.end(); ++it) QVERIFY(call(r, "restore", {{"token", it.key()}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("target/a.md")), original); QCOMPARE(read(dir.filePath(copyPath)), original);
        const auto invalid = call(r, "batch", {{"action", "trash"}, {"items", QVariantList{QVariantMap{{"path", "../outside.md"}, {"baseHash", "fake"}}}}});
        QCOMPARE(invalid.value("results").toList().first().toMap().value("error").toString(), QString("unsafePath"));
    }
    void organizerFolderOperationsProtectContents() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("empty")); QVERIFY(QDir(dir.path()).mkdir("occupied"));
        QVERIFY(write(dir.filePath("occupied/.hidden"), "keep"));
        NotesRepository r; QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(call(r, "renameFolder", {{"path", "empty"}, {"target", "renamed"}}).value("ok").toBool());
        QVERIFY(call(r, "removeFolder", {{"path", "renamed"}}).value("ok").toBool());
        QCOMPARE(call(r, "removeFolder", {{"path", "occupied"}}).value("error").toString(), QString("folderNotEmpty"));
        QVERIFY(call(r, "renameFolder", {{"path", "occupied"}, {"target", "elsewhere"}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("elsewhere/.hidden")), QByteArray("keep"));
        QVERIFY(!call(r, "removeFolder", {{"path", ""}}).value("ok").toBool());
        QVERIFY(QFileInfo(dir.path()).isDir());
    }

    void attachmentImportReferencesAndRecoverableCleanup() {
        QTemporaryDir dir; QVERIFY(QDir(dir.path()).mkdir("nested"));
        const QString note = dir.filePath("nested/note.md"); QVERIFY(write(note, "# Note\n"));
        NotesRepository repository; QVERIFY(call(repository, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        const QByteArray bytes("attachment content");
        const QVariantMap args{{"path", "nested/note.md"}, {"name", "report.txt"}, {"data", bytes.toBase64()}};
        const auto imported = call(repository, "importAttachment", args); QVERIFY(imported.value("ok").toBool());
        const QString relative = imported.value("relative").toString();
        QVERIFY(relative.startsWith("assets/")); QVERIFY(imported.value("url").toString().startsWith("../assets/"));
        QCOMPARE(read(dir.filePath(relative)), bytes);
        QCOMPARE(call(repository, "importAttachment", args).value("relative"), imported.value("relative"));
        QVERIFY(write(note, ("# Note\n\n[Report](" + imported.value("url").toString() + ")\n").toUtf8()));
        QCOMPARE(call(repository, "quarantineAttachment", {{"relative", relative}}).value("error").toString(), QStringLiteral("attachmentInUse"));
        QVERIFY(write(note, "# Note\n"));
        const auto removed = call(repository, "quarantineAttachment", {{"relative", relative}}); QVERIFY(removed.value("ok").toBool());
        QVERIFY(!QFileInfo::exists(dir.filePath(relative)));
        QCOMPARE(call(repository, "attachments").value("removed").toList().size(), 1);
        QVERIFY(call(repository, "restoreAttachment", {{"token", removed.value("token")}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath(relative)), bytes);
        QCOMPARE(call(repository, "importAttachment", {{"path", "../outside.md"}, {"data", bytes.toBase64()}}).value("error").toString(), QStringLiteral("unsafePath"));
        QCOMPARE(call(repository, "importAttachment", {{"path", "nested/note.md"}, {"data", "$$$"}}).value("error").toString(), QStringLiteral("invalidAttachment"));
    }
    void attachmentsRejectSymlinkDestinationsAndLargeFiles() {
        QTemporaryDir dir, outside; QVERIFY(write(dir.filePath("note.md"), "note\n"));
        NotesRepository repository; QVERIFY(call(repository, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(QFile::link(outside.path(), dir.filePath("assets")));
        const QVariantMap args{{"path", "note.md"}, {"name", "file.txt"}, {"data", QByteArray("small").toBase64()}};
        QCOMPARE(call(repository, "importAttachment", args).value("error").toString(), QStringLiteral("unsafePath"));
        QVERIFY(QDir(outside.path()).entryList(QDir::Files).isEmpty());
        QFile large(outside.filePath("large.bin")); QVERIFY(large.open(QIODevice::WriteOnly)); QVERIFY(large.resize(33 * 1024 * 1024)); large.close();
        QCOMPARE(call(repository, "importAttachment", {{"path", "note.md"}, {"source", QUrl::fromLocalFile(large.fileName()).toString()}}).value("error").toString(), QStringLiteral("attachmentTooLarge"));
    }
    void selectionPreparationFailures() {
        QTemporaryDir oldDir, blockedDir, scanDir;
        NotesRepository r;
        auto selected = call(r, "selectLibrary", {{"path", oldDir.path()}});
        QVERIFY(selected.value("ok").toBool());
        QVERIFY(call(r, "create", {{"path", "keep.md"}, {"text", "原文"}}).value("ok").toBool());
        // A regular file at the required directory is deterministic even as root.
        QVERIFY(write(blockedDir.filePath(".nextnotes"), "blocked"));
        auto failed = call(r, "selectLibrary", {{"path", blockedDir.path()}});
        QVERIFY(!failed.value("ok").toBool());
        QCOMPARE(failed.value("generation"), selected.value("generation"));
        QVERIFY(write(scanDir.filePath("bad.md"), "scan"));
        RepositoryTestHooks::beforeScan = [&](bool) {
            QFile::remove(scanDir.filePath("bad.md"));
            QDir().mkpath(scanDir.filePath("bad.md"));
            // Make metadata persistence after scan fail deterministically.
            QFile::remove(scanDir.filePath(".nextnotes/state.json"));
            QDir().mkpath(scanDir.filePath(".nextnotes/state.json"));
            write(scanDir.filePath("new.md"), "new");
        };
        failed = call(r, "selectLibrary", {{"path", scanDir.path()}});
        RepositoryTestHooks::beforeScan = {};
        QVERIFY(!failed.value("ok").toBool());
        QCOMPARE(failed.value("library"), selected.value("library"));
        QCOMPARE(failed.value("generation"), selected.value("generation"));
        auto loaded = call(r, "read", {{"path", "keep.md"}});
        QVERIFY(loaded.value("ok").toBool());
        QVERIFY(call(r, "save", {{"path", "keep.md"}, {"text", "仍可保存"}, {"baseHash", loaded.value("baseHash")}}).value("ok").toBool());
    }
    void failedSelectionRetainsOldLibrary() {
        QTemporaryDir oldDir, brokenDir;
        NotesRepository r;
        const auto selected = call(r, "selectLibrary", {{"path", oldDir.path()}});
        QVERIFY(selected.value("ok").toBool());
        auto created = call(r, "create", {{"path", "keep.md"}, {"text", "# 保留\n"}});
        QVERIFY(created.value("ok").toBool());
        QVERIFY(QDir().mkpath(brokenDir.filePath(".nextnotes/state.json")));
        const auto failed = call(r, "selectLibrary", {{"path", brokenDir.path()}});
        QVERIFY(!failed.value("ok").toBool());
        QCOMPARE(failed.value("library"), selected.value("library"));
        QCOMPARE(failed.value("generation"), selected.value("generation"));
        auto loaded = call(r, "read", {{"path", "keep.md"}});
        QVERIFY(loaded.value("ok").toBool());
        QVERIFY(call(r, "save", {{"path", "keep.md"}, {"text", "# 仍可保存\n"}, {"baseHash", loaded.value("baseHash")}}).value("ok").toBool());
    }

    void foldersSearchMoveAndReopen() {
        QTemporaryDir dir;
        QString id;
        {
            NotesRepository r;
            QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
            QVERIFY(call(r, "createFolder", {{"path", "研究"}}).value("ok").toBool());
            QVERIFY(call(r, "createFolder", {{"path", "研究/中文"}}).value("ok").toBool());
            QVERIFY(!call(r, "createFolder", {{"path", "缺少/子目录"}}).value("ok").toBool());
            QVERIFY(call(r, "list").value("folders").toList().contains("研究/中文"));
            auto created = call(r, "create", {{"path", "a.md"}, {"text", "# 中文标题\n正文命中月光\n"}});
            QVERIFY(created.value("ok").toBool());
            id = created.value("id").toString();
            QVERIFY(!id.isEmpty());
            auto found = call(r, "search", {{"query", "月光"}, {"filter", "all"}});
            QCOMPARE(found.value("notes").toList().size(), 1);
            QVERIFY(found.value("notes").toList()[0].toMap().value("match").toInt() > 0);
            auto original = read(dir.filePath("a.md"));
            auto failed = call(r, "rename", {{"path", "a.md"}, {"target", "不存在/a.md"}, {"baseHash", created.value("baseHash")}});
            QVERIFY(!failed.value("ok").toBool()); QCOMPARE(read(dir.filePath("a.md")), original);
            auto moved = call(r, "rename", {{"path", "a.md"}, {"target", "研究/中文/a.md"}, {"baseHash", created.value("baseHash")}});
            QVERIFY(moved.value("ok").toBool()); QCOMPARE(read(dir.filePath("研究/中文/a.md")), original);
            auto selected = call(r, "read", {{"path", "研究/中文/a.md"}});
            QVERIFY(selected.value("ok").toBool()); QCOMPARE(selected.value("id").toString(), id);
        }
        NotesRepository reopened;
        QVERIFY(call(reopened, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto note = call(reopened, "read", {{"path", "研究/中文/a.md"}});
        QCOMPARE(note.value("id").toString(), id);
        QCOMPARE(call(reopened, "search", {{"query", "中文标题"}, {"filter", "folder:研究"}}).value("notes").toList().size(), 1);
    }
    void fifoGenerationAndRecovery() {
        QTemporaryDir dir, second;
        {
            NotesRepository r;
            const auto selected = call(r, "selectLibrary", {{"path", dir.path()}});
            const auto generation = selected.value("generation");
            QSignalSpy spy(&r, &NotesRepository::finished);
            const auto a = r.request("create", {{"path", "a.md"}, {"text", "first"}});
            const auto b = r.request("read", {{"path", "a.md"}});
            QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 2, 10000);
            QCOMPARE(spy[0][0].toULongLong(), a); QCOMPARE(spy[1][0].toULongLong(), b);
            QVERIFY(spy[1][2].toMap().value("ok").toBool());
            QVERIFY(call(r, "selectLibrary", {{"path", second.path()}}).value("ok").toBool());
            QCOMPARE(call(r, "list", {{"generation", generation}}).value("error").toString(), QString("staleGeneration"));
            QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
            QVERIFY(call(r, "checkpoint", {{"path", "a.md"}, {"text", "recover"}, {"baseHash", "old"}}).value("ok").toBool());
        }
        NotesRepository reopened;
        QVERIFY(call(reopened, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto recovery = call(reopened, "recovery");
        QVERIFY(!recovery.value("drafts").toList().isEmpty());
        QVERIFY(!recovery.value("commits").toList().isEmpty());
        QVERIFY(!recovery.value("drafts").toList().first().toMap().value("matchesBase").toBool());
    }
    void scalarAndHeading() {
        QCOMPARE(MarkdownFile::field("---\nnextnotes_id: 'abc' # copy\n---\n", "nextnotes_id"), QString("abc"));
        QCOMPARE(MarkdownFile::field("---\nnextnotes_id: abc # copy\n---\n", "nextnotes_id"), QString("abc"));
        QVERIFY(!MarkdownFile::decode("---\nnextnotes_id: abc\nnextnotes_id: def\n---\n").readOnlyReason.isEmpty());
        QVERIFY(!MarkdownFile::decode("---\nnextnotes_id: abc\n").readOnlyReason.isEmpty());
        QCOMPARE(MarkdownFile::title("```md\n# false\n```\n# Real\n", "fallback"), QString("Real"));
        QCOMPARE(MarkdownFile::title("---\ntitle: Hello # comment\n---\n", "fallback"), QString("Hello"));
    }
    void setextHeadings_data() {
        QTest::addColumn<QString>("source"); QTest::addColumn<QString>("expected");
        QTest::newRow("setext-first") << QStringLiteral("Heading\n=======\n# Later\n") << QStringLiteral("Heading");
        QTest::newRow("atx-first") << QStringLiteral("# First\nHeading\n=======\n") << QStringLiteral("First");
        QTest::newRow("multiline") << QStringLiteral("First line\nsecond line\n===\n") << QStringLiteral("First line second line");
        QTest::newRow("front-title") << QStringLiteral("---\ntitle: Chosen\n---\nHeading\n===\n") << QStringLiteral("Chosen");
        QTest::newRow("excluded") << QStringLiteral("---\nHidden\n===\n---\n```\nHidden\n===\n```\n~~~\n# Hidden\n~~~\nReal\n===\n") << QStringLiteral("Real");
        QTest::newRow("h2-code") << QStringLiteral("Second\n---\n\n    Code\n    ====\n\n# Real\n") << QStringLiteral("Real");
        QTest::newRow("fallback") << QStringLiteral("Plain\n\n===\n") << QStringLiteral("fallback");
    }
    void setextHeadings() {
        QFETCH(QString, source); QFETCH(QString, expected);
        QCOMPARE(MarkdownFile::title(source, "fallback"), expected);
    }
    void internalScanKeepsExternalChanges_data() {
        QTest::addColumn<bool>("failedSave");
        QTest::newRow("list") << false;
        QTest::newRow("failed-save") << true;
    }
    void internalScanKeepsExternalChanges() {
        QFETCH(bool, failedSave);
        QTemporaryDir dir; NotesRepository r;
        QVERIFY(write(dir.filePath("a.md"), "base\n"));
        QVERIFY(write(dir.filePath("b.md"), "other\n"));
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        const auto d = call(r, "read", {{"path", "a.md"}});
        QSignalSpy changes(&r, &NotesRepository::libraryChanged);
        bool injected = false, wrote = false;
        RepositoryTestHooks::beforeScan = [&](bool notify) {
            if (!notify && !injected) {
                injected = true;
                wrote = write(dir.filePath("b.md"), "external\n");
                if (failedSave) wrote = write(dir.filePath("a.md"), "conflicting\n") && wrote;
            }
        };
        const auto result = failedSave
            ? call(r, "save", {{"path", "a.md"}, {"text", "local\n"}, {"baseHash", d.value("baseHash")}})
            : call(r, "list");
        RepositoryTestHooks::beforeScan = {};
        QVERIFY(injected && wrote);
        if (failedSave) QCOMPARE(result.value("error").toString(), QString("conflict"));
        else QVERIFY(result.value("ok").toBool());
        auto reported = [&] {
            for (const auto &signal : changes)
                for (const auto &item : signal[0].toMap().value("changes").toList())
                    if (item.toMap().value("path") == "b.md" && item.toMap().value("hash") == MarkdownFile::digest("external\n")) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(reported(), 10000);
    }
    void corruptMetadataAndRecovery() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "original"));
        QVERIFY(QDir().mkpath(dir.filePath(".nextnotes")));
        const QByteArray broken("{invalid-json");
        QVERIFY(write(dir.filePath(".nextnotes/state.json"), broken));
        {
            NotesRepository r;
            QCOMPARE(call(r, "selectLibrary", {{"path", dir.path()}}).value("error").toString(), QString("metadataCorrupt"));
        }
        QCOMPARE(read(dir.filePath(".nextnotes/state.json")), broken);
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("original"));
        QVERIFY(QFile::remove(dir.filePath(".nextnotes/state.json")));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(write(dir.filePath(".nextnotes/drafts/broken.json"), broken));
        QCOMPARE(call(r, "recovery").value("error").toString(), QString("recoveryCorrupt"));
        QCOMPARE(read(dir.filePath(".nextnotes/drafts/broken.json")), broken);
        QVERIFY(write(dir.filePath(".nextnotes/drafts/broken.json"), "{}"));
        QCOMPARE(call(r, "recovery").value("error").toString(), QString("recoveryCorrupt"));
        QVERIFY(QFile::remove(dir.filePath(".nextnotes/drafts/broken.json")));
        QVERIFY(write(dir.filePath(".nextnotes/commits/broken.intent"), broken));
        QCOMPARE(call(r, "recovery").value("error").toString(), QString("recoveryCorrupt"));
        QCOMPARE(read(dir.filePath(".nextnotes/commits/broken.intent")), broken);
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("original"));
    }
    void crashChild() {
        const QString root = qEnvironmentVariable("NEXTNOTES_CRASH_ROOT");
        if (root.isEmpty()) QSKIP("Helper slot: invoked only by interruptedCommit parent test.");
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", root}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "a.md"}});
        if (qEnvironmentVariable("NEXTNOTES_CRASH_STAGE") == "intent")
            SafeCommit::beforeCommit = [] { std::_Exit(73); };
        else SafeCommit::afterExchange = [] { std::_Exit(74); };
        call(r, "save", {{"path", "a.md"}, {"text", "local\n"}, {"baseHash", d.value("baseHash")}});
        QFAIL("Commit hook was not reached");
    }
    void interruptedCommit_data() {
        QTest::addColumn<QString>("stage");
        QTest::newRow("durable-intent") << QStringLiteral("intent");
        QTest::newRow("after-exchange") << QStringLiteral("exchange");
    }
    void interruptedCommit() {
        QFETCH(QString, stage);
        QTemporaryDir dir; QVERIFY(write(dir.filePath("a.md"), "base\n"));
        QProcess child; auto env = QProcessEnvironment::systemEnvironment();
        env.insert("NEXTNOTES_CRASH_ROOT", dir.path()); env.insert("NEXTNOTES_CRASH_STAGE", stage);
        child.setProcessEnvironment(env);
        child.start(QCoreApplication::applicationFilePath(), {"crashChild"});
        QVERIFY(child.waitForFinished(15000));
        QCOMPARE(child.exitCode(), stage == "intent" ? 73 : 74);
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        const auto recovery = call(r, "recovery"); QVERIFY(recovery.value("ok").toBool());
        QCOMPARE(recovery.value("drafts").toList().size(), 1);
        QCOMPARE(recovery.value("drafts").toList()[0].toMap().value("text").toString(), QString("local\n"));
        const auto commits = recovery.value("commits").toList(); QCOMPARE(commits.size(), 1);
        const auto summary = call(r, "recovery", {{"includeCommitText", false}});
        QVERIFY(summary.value("ok").toBool());
        QCOMPARE(summary.value("drafts"), recovery.value("drafts"));
        QCOMPARE(summary.value("commits").toList().size(), commits.size());
        QVERIFY(summary.value("commits").toList()[0].toMap().value("versions").toList().isEmpty());
        QCOMPARE(call(r, "recovery").value("commits"), recovery.value("commits"));
        const QString token = commits[0].toMap().value("token").toString();
        const QString journal = dir.filePath(".nextnotes/commits/" + token);
        const QString retained = QString::fromUtf8(read(journal)).section(u'\n', 1);
        for (const auto &version : {QString("before"), QString("pending")}) {
            auto recovered = call(r, "recoveryVersion", {{"token", token}, {"version", version}});
            QVERIFY(recovered.value("ok").toBool());
            QCOMPARE(recovered.value("bytes").toByteArray(), version == "before" ? QByteArray("base\n") : QByteArray("local\n"));
            auto copy = call(r, "recoverCommit", {{"token", token}, {"version", version}});
            QVERIFY(copy.value("ok").toBool());
            QVERIFY(read(dir.filePath(copy.value("path").toString())).contains(version == "before" ? "base\n" : "local\n"));
        }
        QVERIFY(!call(r, "recoveryVersion", {{"token", "../escape.intent"}, {"version", "before"}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("a.md")), stage == "intent" ? QByteArray("base\n") : QByteArray("local\n"));
        QCOMPARE(read(retained), stage == "intent" ? QByteArray("local\n") : QByteArray("base\n"));
        QVERIFY(write(dir.filePath("a.md"), "new-external\n"));
        QVERIFY(call(r, "recovery").value("ok").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("new-external\n"));
        QVERIFY(QFileInfo::exists(retained));
        const QByteArray originalJournal = read(journal);
        QVERIFY(write(journal, originalJournal.left(originalJournal.indexOf('\n')) + "\n/tmp/outside.swap"));
        QVERIFY(!call(r, "recoveryVersion", {{"token", token}, {"version", "before"}}).value("ok").toBool());
        QVERIFY(write(journal, "broken\n/tmp/outside.swap"));
        QVERIFY(!call(r, "recoverCommit", {{"token", token}, {"version", "before"}}).value("ok").toBool());
    }
    void realDiskFull() {
#ifndef Q_OS_LINUX
        QSKIP("Linux capacity-limited tmpfs required.");
#else
        const QString root = qEnvironmentVariable("NEXTNOTES_ENOSPC_DIR");
        if (root.isEmpty()) QSKIP("Set NEXTNOTES_ENOSPC_DIR to a dedicated capacity-limited tmpfs in the VM; see architecture.md.");
        struct statvfs fs {};
        QVERIFY(::statvfs(QFile::encodeName(root).constData(), &fs) == 0);
        QVERIFY2(quint64(fs.f_blocks) * fs.f_frsize <= 64 * 1024 * 1024, "Refusing to fill a filesystem larger than 64 MiB; use dedicated tmpfs.");
        QTemporaryDir dir(root + "/nextnotes-enospc-XXXXXX"); QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath("a.md"), "base\n"));
        NotesRepository r; QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "a.md"}});
        QFile filler(dir.filePath("filler")); QVERIFY(filler.open(QIODevice::WriteOnly));
        const QByteArray block(4096, 'x');
        qint64 count = 0;
        while (count <= 64 * 1024 * 1024 && filler.write(block) == block.size() && filler.flush()) count += block.size();
        QVERIFY(count <= 64 * 1024 * 1024);
        qInfo() << "ENOSPC fixture" << root << "capacity bytes" << quint64(fs.f_blocks) * fs.f_frsize
                << "filler bytes" << filler.size() << "write error" << filler.errorString();
        QVERIFY(filler.error() != QFileDevice::NoError);
        const auto result = call(r, "save", {{"path", "a.md"}, {"text", QString(32768, u'x')}, {"baseHash", d.value("baseHash")}});
        filler.close(); QVERIFY(filler.remove());
        QCOMPARE(result.value("error").toString(), QString("recoveryWriteFailed"));
        QVERIFY(!result.value("recoveryDurable").toBool()); QVERIFY(result.value("draftInMemory").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("base\n"));
        QCOMPARE(call(r, "recovery").value("memoryDrafts").toList()[0].toMap().value("text").toString(), QString(32768, u'x'));
        QVERIFY(call(r, "save", {{"path", "a.md"}, {"text", "recovered\n"}, {"baseHash", d.value("baseHash")}}).value("ok").toBool());
#endif
    }
    void commitWindow() {
        QTemporaryDir dir; NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(write(dir.filePath("n.md"), "base\n"));
        auto d = call(r, "read", {{"path", "n.md"}});
        SafeCommit::beforeCommit = [&] { write(dir.filePath("n.md"), "external\n"); };
        const auto result = call(r, "save", {{"path", "n.md"}, {"text", "local\n"}, {"baseHash", d.value("baseHash")}});
        SafeCommit::beforeCommit = {};
        QCOMPARE(result.value("error").toString(), QString("conflict"));
        QCOMPARE(read(result.value("retainedPath").toString()), QByteArray("external\n"));
        QCOMPARE(read(dir.filePath("n.md")), QByteArray("local\n"));
        QVERIFY(!call(r, "recovery").value("drafts").toList().isEmpty());
        SafeCommit::beforeCommit = [&] { write(dir.filePath("race.md"), "other\n"); };
        const auto created = call(r, "create", {{"path", "race.md"}, {"text", "mine"}});
        SafeCommit::beforeCommit = {};
        QVERIFY(!created.value("ok").toBool());
        QCOMPARE(read(dir.filePath("race.md")), QByteArray("other\n"));
    }
    void newFileFormat() {
        QTemporaryDir dir; NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(call(r, "create", {{"path", "ok.md"}, {"text", "a\r\nb\r\n"}}).value("ok").toBool());
        QVERIFY(!read(dir.filePath("ok.md")).contains('\r'));
        QVERIFY(!call(r, "create", {{"path", "bad.md"}, {"text", QString(QChar(0x2028))}}).value("ok").toBool());
    }

    void bytePreservation_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("lf") << QByteArray("---\ntitle: 'Unknown'\nextra: [a, b]\n---\n\n<!--x-->\n# Hello\n  \n");
        QTest::newRow("bom-crlf") << QByteArray("\xEF\xBB\xBF---\r\ncustom: {one: 1}\r\n---\r\n# Hello\r\n");
        QTest::newRow("no-terminal-newline") << QByteArray("# Hello\n![[unknown]]");
        QTest::newRow("unicode") << QStringLiteral("# 中文\n保留\u00a0空格\n").toUtf8();
    }
    void bytePreservation() {
        QFETCH(QByteArray, bytes);
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(write(dir.filePath("note.md"), bytes));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "note.md"}});
        QVERIFY(d.value("ok").toBool());
        auto saved = call(r, "save", {{"path", "note.md"}, {"text", d.value("text")}, {"baseHash", d.value("baseHash")}});
        QVERIFY2(saved.value("ok").toBool(), qPrintable(saved.value("error").toString()));
        QCOMPARE(read(dir.filePath("note.md")), bytes);
        QString edited = d.value("text").toString(); edited.replace("Hello", "Edited");
        QVERIFY(call(r, "save", {{"path", "note.md"}, {"text", edited}, {"baseHash", saved.value("baseHash")}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("note.md")), QByteArray(bytes).replace("Hello", "Edited"));
    }
    void conservativeReadOnly_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid") << QByteArray("\xFF text");
        QTest::newRow("mixed") << QByteArray("a\r\nb\n");
        QTest::newRow("bare-cr") << QByteArray("a\rb");
        QTest::newRow("separator") << QStringLiteral("a\u2029b").toUtf8();
    }
    void conservativeReadOnly() {
        QFETCH(QByteArray, bytes);
        auto d = MarkdownFile::decode(bytes);
        QVERIFY(!d.readOnlyReason.isEmpty());
        QCOMPARE(MarkdownFile::encode(d, d.text), bytes);
    }
    void conflictAndRecovery() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "base\n"));
        QString base;
        {
            NotesRepository r;
            QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
            auto d = call(r, "read", {{"path", "a.md"}}); base = d.value("baseHash").toString();
            QVERIFY(write(dir.filePath("a.md"), "external\n"));
            auto result = call(r, "save", {{"path", "a.md"}, {"text", "local\n"}, {"baseHash", base}});
            QCOMPARE(result.value("error").toString(), "conflict");
            QVERIFY(result.value("recoveryDurable").toBool());
            QCOMPARE(read(dir.filePath("a.md")), QByteArray("external\n"));
        }
        NotesRepository restarted;
        QVERIFY(call(restarted, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto drafts = call(restarted, "recovery").value("drafts").toList();
        QCOMPARE(drafts.size(), 1);
        QCOMPARE(drafts[0].toMap().value("text").toString(), "local\n");
        QCOMPARE(drafts[0].toMap().value("baseHash").toString(), base);
        QVERIFY(!drafts[0].toMap().value("matchesBase").toBool());
        QVERIFY(call(restarted, "saveAs", {{"path", "local-copy.md"}, {"text", "local\n"}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("external\n"));
        QCOMPARE(read(dir.filePath("local-copy.md")), QByteArray("local\n"));
    }
    void stableIdsAndDuplicates() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("old.md"), "# Original\n"));
        QString id;
        {
            NotesRepository r;
            auto listing = call(r, "selectLibrary", {{"path", dir.path()}});
            id = listing.value("notes").toList()[0].toMap().value("id").toString();
            QVERIFY(!id.isEmpty());
            QVERIFY(call(r, "favorite", {{"path", "old.md"}, {"favorite", true}}).value("ok").toBool());
        }
        NotesRepository r;
        auto listing = call(r, "selectLibrary", {{"path", dir.path()}});
        auto note = listing.value("notes").toList()[0].toMap();
        QCOMPARE(note.value("id").toString(), id); QVERIFY(note.value("favorite").toBool());
        QCOMPARE(read(dir.filePath("old.md")), QByteArray("# Original\n"));
        auto created = call(r, "create", {{"path", "new.md"}, {"text", "# New"}});
        QVERIFY(created.value("ok").toBool());
        QVERIFY(!MarkdownFile::field(created.value("text").toString(), "nextnotes_id").isEmpty());
        QVERIFY(write(dir.filePath("duplicate.md"), read(dir.filePath("new.md"))));
        QCOMPARE(call(r, "list").value("duplicateIds").toList().size(), 1);
        auto result = call(r, "save", {{"path", "new.md"}, {"text", created.value("text")}, {"baseHash", created.value("baseHash")}});
        QCOMPARE(result.value("error").toString(), "duplicateId");
    }
    void renameRecycleRestore() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "# Heading\n"));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QVERIFY(call(r, "favorite", {{"path", "a.md"}, {"favorite", true}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "a.md"}});
        QVERIFY(call(r, "rename", {{"path", "a.md"}, {"target", "b.md"}, {"baseHash", d.value("baseHash")}}).value("ok").toBool());
        QCOMPARE(read(dir.filePath("b.md")), QByteArray("# Heading\n"));
        QVERIFY(!QFileInfo::exists(dir.filePath("a.md")));
        auto recycled = call(r, "trash", {{"path", "b.md"}, {"baseHash", d.value("baseHash")}});
        QVERIFY(recycled.value("ok").toBool());
        QVERIFY(write(dir.filePath("b.md"), "occupied"));
        QCOMPARE(call(r, "restore", {{"token", recycled.value("trashToken")}}).value("error").toString(), "alreadyExists");
        QCOMPARE(read(dir.filePath("b.md")), QByteArray("occupied"));
        QVERIFY(QFile::remove(dir.filePath("b.md")));
        auto restored = call(r, "restore", {{"token", recycled.value("trashToken")}});
        QVERIFY(restored.value("ok").toBool());
        QCOMPARE(read(dir.filePath("b.md")), QByteArray("# Heading\n"));
        auto notes = restored.value("listing").toMap().value("notes").toList();
        QVERIFY(notes[0].toMap().value("favorite").toBool());
    }
    void confinementAndCollisions() {
        QTemporaryDir dir, outside;
        QVERIFY(write(outside.filePath("outside.md"), "untouched"));
        QVERIFY(QDir().mkdir(dir.filePath("assets")));
        QVERIFY(write(dir.filePath("assets/asset.md"), "asset"));
        QVERIFY(write(dir.filePath("a.md"), "original"));
        NotesRepository r;
        auto listing = call(r, "selectLibrary", {{"path", dir.path()}});
        QVERIFY(listing.value("ok").toBool());
        QCOMPARE(listing.value("notes").toList().size(), 1);
        for (const QString &p : {QString("../escape.md"), outside.filePath("outside.md"), QString("assets/asset.md"), QString(".nextnotes/state.md")})
            QCOMPARE(call(r, "create", {{"path", p}}).value("error").toString(), "unsafePath");
        QCOMPARE(call(r, "create", {{"path", "a.md"}}).value("error").toString(), "alreadyExists");
#ifdef Q_OS_UNIX
        QVERIFY(QFile::link(outside.path(), dir.filePath("linked")));
        QCOMPARE(call(r, "read", {{"path", "linked/outside.md"}}).value("error").toString(), "unsafePath");
#endif
        QCOMPARE(read(outside.filePath("outside.md")), QByteArray("untouched"));
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("original"));
    }
    void failedRecoveryRetainsMemory() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "original"));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "a.md"}});
        QVERIFY(QDir().rmdir(dir.filePath(".nextnotes/drafts")));
        QVERIFY(write(dir.filePath(".nextnotes/drafts"), "blocks-directory"));
        auto result = call(r, "save", {{"path", "a.md"}, {"text", "unsaved"}, {"baseHash", d.value("baseHash")}});
        QCOMPARE(result.value("error").toString(), "recoveryWriteFailed");
        QVERIFY(result.value("draftInMemory").toBool());
        QVERIFY(!result.value("recoveryDurable").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("original"));
        QCOMPARE(call(r, "recovery").value("memoryDrafts").toList().size(), 1);
    }
    void permissionFailure() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "original"));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        auto d = call(r, "read", {{"path", "a.md"}});
        const auto permissions = QFileInfo(dir.path()).permissions();
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        auto result = call(r, "save", {{"path", "a.md"}, {"text", "unsaved"}, {"baseHash", d.value("baseHash")}});
        QVERIFY(QFile::setPermissions(dir.path(), permissions));
        if (result.value("ok").toBool()) QSKIP("This account bypasses directory permissions; run as unprivileged VM user.");
        QCOMPARE(result.value("error").toString(), "writeFailed");
        QVERIFY(result.value("recoveryDurable").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("original"));
    }
    void externalNotifications() {
        QTemporaryDir dir;
        QVERIFY(write(dir.filePath("a.md"), "original"));
        NotesRepository r;
        QVERIFY(call(r, "selectLibrary", {{"path", dir.path()}}).value("ok").toBool());
        QSignalSpy changes(&r, &NotesRepository::libraryChanged);
        QVERIFY(write(dir.filePath("a.md"), "external"));
        QVERIFY(changes.wait(10000));
        auto change = changes.first()[0].toMap();
        QCOMPARE(change.value("changes").toList()[0].toMap().value("path").toString(), "a.md");
    }
};
QTEST_GUILESS_MAIN(NotesRepositoryTest)
#include "NotesRepositoryTest.moc"
