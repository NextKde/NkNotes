#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdlib>
#include <sqlite3.h>
#include "ManagedStore.h"
#include "MarkdownFile.h"
#include "NotesRepository.h"
#include "RepositoryTestHooks.h"
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

class ManagedRecoveryTest : public QObject {
    Q_OBJECT
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
    }
    static QByteArray read(const QString &path) {
        QFile file(path); if(!file.open(QIODevice::ReadOnly))return {}; return file.readAll();
    }
    static QString url(const QString &path) { return QUrl::fromLocalFile(path).toString(); }
    static QVariantMap call(NotesRepository &repository,const QString &operation,const QVariantMap &args={}) {
        QSignalSpy spy(&repository,&NotesRepository::finished); const auto id=repository.request(operation,args);
        if(spy.isEmpty()&&!spy.wait(15000))return {{"ok",false},{"error","testTimeout"}};
        for(const auto &signal:spy)if(signal[0].toULongLong()==id)return signal[2].toMap();
        return {{"ok",false},{"error","missingRequest"}};
    }
    static bool sql(const QString &path,const QByteArray &statement) {
        sqlite3 *raw=nullptr;
        const int opened=sqlite3_open(QFile::encodeName(path).constData(),&raw);
        const int result=opened==SQLITE_OK?sqlite3_exec(raw,statement.constData(),nullptr,nullptr,nullptr):opened;
        sqlite3_close(raw);return result==SQLITE_OK;
    }
    static bool makeBackup(const QString &parent,const QString &destination) {
        ManagedStore store(parent+"/source");
        const auto created=store.dispatch("create",{{"path","Projects/saved.md"},{"text","# Saved\n\nSynthetic backup text.\n"}});
        if(!created.value("ok").toBool())return false;
        if(!store.dispatch("importAttachment",{{"path","Projects/saved.md"},{"name","fixture.txt"},{"data",QByteArray("synthetic attachment").toBase64()}}).value("ok").toBool())return false;
        return store.dispatch("backup",{{"destination",url(destination)}}).value("ok").toBool();
    }
    static QMap<QString,QByteArray> corrupt(const QString &directory) {
        QDir().mkpath(directory);
        QMap<QString,QByteArray> files{{"notes.sqlite3",QByteArray("not a SQLite database")+QByteArray(1,'\0')+"with original bytes"},
            {"notes.sqlite3-wal",QByteArray("original WAL")+QByteArray(1,'\0')+"tail"}, {"notes.sqlite3-shm",QByteArray("original SHM")+QByteArray(1,'\0')+"tail"}};
        for(auto it=files.cbegin();it!=files.cend();++it)if(!write(directory+'/'+it.key(),it.value()))return {};
        return files;
    }
    static bool unchanged(const QString &directory,const QMap<QString,QByteArray> &files) {
        for(auto it=files.cbegin();it!=files.cend();++it)if(read(directory+'/'+it.key())!=it.value())return false;
        return true;
    }
    static quint64 inode(const QString &path) {
#ifdef Q_OS_UNIX
        struct stat status{};return ::stat(QFile::encodeName(path).constData(),&status)==0?status.st_ino:0;
#else
        Q_UNUSED(path);return 0;
#endif
    }
private slots:
    void cleanup() {
        RepositoryTestHooks::managedFailure={};
        RepositoryTestHooks::managedRecoveryStage={};
        RepositoryTestHooks::externalSaveStage={};
    }
    void corruptInitializationRestoresToSiblingAndReopens() {
        QTemporaryDir fixture;QVERIFY(fixture.isValid());
        const auto backup=fixture.filePath("saved.nkbackup"), entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup)); const auto original=corrupt(entry);QVERIFY(!original.isEmpty());
        QString replacement;quint64 lockInode=0;
        {
            NotesRepository repository;
            const auto failed=call(repository,"selectLibrary",{{"path",entry},{"managed",true}});
            QCOMPARE(failed.value("error").toString(),QString("databaseCorrupt"));QVERIFY(failed.value("recoveryAvailable").toBool());
            QCOMPARE(failed.value("entryRoot").toString(),entry);QVERIFY(unchanged(entry,original));
            lockInode=inode(entry+"/managed.instance.lock");
            const auto restored=call(repository,"restoreBackup",{{"source",url(backup)},{"generation",failed.value("generation")}});
            QVERIFY2(restored.value("ok").toBool(),qPrintable(restored.value("detail").toString()));
            QVERIFY(restored.value("recoveredInitialization").toBool());QCOMPARE(restored.value("entryRoot").toString(),entry);
            QCOMPARE(restored.value("preservedDirectory").toString(),entry);QVERIFY(!restored.contains("safetyCopy"));
            QVERIFY(restored.value("generation").toULongLong()>failed.value("generation").toULongLong());
            replacement=restored.value("library").toString();QVERIFY(replacement!=entry);
            QCOMPARE(QFileInfo(replacement).absolutePath(),fixture.path());
            QCOMPARE(call(repository,"read",{{"path","Projects/saved.md"}}).value("text").toString(),QString("# Saved\n\nSynthetic backup text.\n"));
            QVERIFY(unchanged(entry,original));QCOMPARE(inode(entry+"/managed.instance.lock"),lockInode);
            const auto manifest=restored.value("assetManifest").toMap();QCOMPARE(manifest.size(),1);
            QCOMPARE(read(replacement+'/'+manifest.firstKey()),QByteArray("synthetic attachment"));
        }
        NotesRepository reopened;
        const auto selected=call(reopened,"selectLibrary",{{"path",entry},{"managed",true}});
        QVERIFY(selected.value("ok").toBool());QCOMPARE(selected.value("library").toString(),replacement);
        QCOMPARE(selected.value("entryRoot").toString(),entry);QVERIFY(unchanged(entry,original));QCOMPARE(inode(entry+"/managed.instance.lock"),lockInode);
    }
    void repeatedRecoveryRetainsBothPreviousDirectories() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");QVERIFY(makeBackup(fixture.path(),backup));
        const auto original=corrupt(entry);QString first;
        {ManagedStore failed(entry,true);const auto recovered=failed.dispatch("restoreBackup",{{"source",url(backup)}});QVERIFY(recovered.value("ok").toBool());first=failed.directory();}
        const auto failedReplacement=corrupt(first);const auto pointer=read(entry+"/active-library.json");
        {
            ManagedStore failed(entry,true);QCOMPARE(failed.directory(),first);QVERIFY(failed.initializationError().value("recoveryAvailable").toBool());
            const auto rejected=failed.dispatch("restoreBackup",{{"source",url(fixture.filePath("missing.nkbackup"))}});QVERIFY(!rejected.value("ok").toBool());QCOMPARE(read(entry+"/active-library.json"),pointer);
            const auto recovered=failed.dispatch("restoreBackup",{{"source",url(backup)}});QVERIFY(recovered.value("ok").toBool());
            QCOMPARE(recovered.value("preservedDirectory").toString(),first);QVERIFY(failed.directory()!=first);QCOMPARE(failed.entryRoot(),entry);
        }
        QVERIFY(unchanged(entry,original));QVERIFY(unchanged(first,failedReplacement));ManagedStore reopened(entry);
        QVERIFY(reopened.dispatch("read",{{"path","Projects/saved.md"}}).value("ok").toBool());
    }
    void invalidBackupDoesNotPublishOrChangeFailedFiles_data() {
        QTest::addColumn<QByteArray>("mutation");
        QTest::newRow("application")<<QByteArray("PRAGMA application_id=42");
        QTest::newRow("new-version")<<QByteArray("PRAGMA user_version=99");
        QTest::newRow("trigger")<<QByteArray("CREATE TRIGGER malicious AFTER INSERT ON notes BEGIN DELETE FROM drafts; END");
        QTest::newRow("view")<<QByteArray("CREATE VIEW unexpected AS SELECT text FROM notes");
        QTest::newRow("missing-column")<<QByteArray("ALTER TABLE drafts RENAME COLUMN base_hash TO lost_hash");
        QTest::newRow("asset-escape")<<QByteArray("UPDATE assets SET path='assets/../../escape'");
        QTest::newRow("asset-identity")<<QByteArray("UPDATE assets SET data=x'626164'");
        QTest::newRow("versions-without-rowid")<<QByteArray(
            "ALTER TABLE versions RENAME TO old_versions;"
            "CREATE TABLE versions(note_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,hash TEXT NOT NULL,text TEXT NOT NULL,time TEXT NOT NULL,PRIMARY KEY(note_id,hash)) WITHOUT ROWID;"
            "INSERT INTO versions SELECT * FROM old_versions;DROP TABLE old_versions;");
    }
    void invalidBackupDoesNotPublishOrChangeFailedFiles() {
        QFETCH(QByteArray,mutation);QTemporaryDir fixture;
        const auto backup=fixture.filePath("invalid.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));QVERIFY(sql(backup,mutation));
        const auto original=corrupt(entry);ManagedStore failed(entry,true);
        QCOMPARE(failed.initializationError().value("error").toString(),QString("databaseCorrupt"));
        const auto directories=QDir(fixture.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);
        const auto result=failed.dispatch("restoreBackup",{{"source",url(backup)}});
        QCOMPARE(result.value("error").toString(),QString("invalidBackup"));
        QVERIFY(unchanged(entry,original));QVERIFY(!QFileInfo::exists(entry+"/active-library.json"));
        QCOMPARE(QDir(fixture.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot),directories);
    }
    void managedLockCannotBeBypassedThroughRecovery() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));const auto original=corrupt(entry);
        ManagedStore owner(entry,true);QVERIFY(owner.initializationError().value("recoveryAvailable").toBool());
        const auto lockInode=inode(entry+"/managed.instance.lock");
        NotesRepository contender;
        const auto result=call(contender,"selectLibrary",{{"path",entry},{"managed",true}});
        QCOMPARE(result.value("error").toString(),QString("libraryLocked"));QVERIFY(!result.value("recoveryAvailable").toBool());
        QCOMPARE(call(contender,"restoreBackup",{{"source",url(backup)}}).value("error").toString(),QString("libraryLocked"));
        QVERIFY(unchanged(entry,original));QCOMPARE(inode(entry+"/managed.instance.lock"),lockInode);
    }
    void preflightRejectsExistingSchemasWithoutChangingSidecars_data() {
        QTest::addColumn<QByteArray>("mutation");QTest::addColumn<QString>("error");
        QTest::newRow("new-version")<<QByteArray("PRAGMA user_version=2")<<QString("databaseVersion");
        QTest::newRow("missing-column")<<QByteArray("ALTER TABLE notes RENAME COLUMN text TO missing")<<QString("databaseCorrupt");
    }
    void preflightRejectsExistingSchemasWithoutChangingSidecars() {
        QFETCH(QByteArray,mutation);QFETCH(QString,error);QTemporaryDir fixture;
        const auto entry=fixture.filePath("Library");{ManagedStore store(entry);}
        QVERIFY(sql(entry+"/notes.sqlite3",mutation));
        QMap<QString,QByteArray> original{{"notes.sqlite3",read(entry+"/notes.sqlite3")},{"notes.sqlite3-wal","retained old WAL"},{"notes.sqlite3-shm","retained old SHM"}};
        QVERIFY(write(entry+"/notes.sqlite3-wal",original.value("notes.sqlite3-wal")));QVERIFY(write(entry+"/notes.sqlite3-shm",original.value("notes.sqlite3-shm")));
        ManagedStore store(entry,true);QCOMPARE(store.initializationError().value("error").toString(),error);QVERIFY(unchanged(entry,original));
    }
    void blockedLegacyCacheIsRejectedBeforeOriginalDatabaseChanges() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));QVERIFY(QDir().mkpath(entry));
        QVERIFY(QFile::copy(backup,entry+"/notes.sqlite3"));QVERIFY(sql(entry+"/notes.sqlite3","PRAGMA application_id=0"));
        QVERIFY(write(entry+"/assets","occupied cache directory"));
        QMap<QString,QByteArray> original{{"notes.sqlite3",read(entry+"/notes.sqlite3")},{"notes.sqlite3-wal",QByteArray()},{"notes.sqlite3-shm","untouched SHM"}};
        QVERIFY(write(entry+"/notes.sqlite3-wal",original.value("notes.sqlite3-wal")));
        QVERIFY(write(entry+"/notes.sqlite3-shm",original.value("notes.sqlite3-shm")));
        ManagedStore store(entry,true);
        QCOMPARE(store.initializationError().value("error").toString(),QString("unsafePath"));
        QVERIFY(unchanged(entry,original));
        for(auto it=original.cbegin();it!=original.cend();++it)QVERIFY(QFileInfo::exists(entry+'/'+it.key()));
        QCOMPARE(read(entry+"/assets"),QByteArray("occupied cache directory"));
    }
    void cacheWriteFailureLeavesLibraryUsableAndWarns() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library");QString asset;
        {
            ManagedStore store(entry);QVERIFY(store.dispatch("create",{{"path","note.md"},{"text","kept"}}).value("ok").toBool());
            const auto result=store.dispatch("importAttachment",{{"path","note.md"},{"name","fixture.bin"},{"data",QByteArray("kept asset").toBase64()}});
            QVERIFY(result.value("ok").toBool());asset=result.value("path").toString();
        }
        QVERIFY(QFile::remove(entry+'/'+asset));
        RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="cacheWrite";};
        {
            ManagedStore reopened(entry,true);QVERIFY(reopened.initializationError().isEmpty());
            const auto listing=reopened.dispatch("list",{});QVERIFY(listing.value("ok").toBool());
            QCOMPARE(listing.value("cacheFailures").toInt(),1);QVERIFY(listing.value("initializationNotice").toString().contains("附件缓存"));
            QCOMPARE(reopened.dispatch("read",{{"path","note.md"}}).value("text").toString(),QString("kept"));
            QVERIFY(reopened.dispatch("create",{{"path","new.md"},{"text","still writable"}}).value("ok").toBool());
        }
        RepositoryTestHooks::managedFailure={};
        ManagedStore repaired(entry);QCOMPARE(repaired.dispatch("list",{}).value("cacheFailures").toInt(),0);
        QCOMPARE(read(entry+'/'+asset),QByteArray("kept asset"));
    }
    void invalidPointerNeverFallsBackToCreatingDatabase_data() {
        QTest::addColumn<QString>("kind");
        for(const auto &kind:{"traversal","absolute","wrong-id","extra-key","symlink","chain","missing-db","duplicate-key"})QTest::newRow(kind)<<QString(kind);
    }
    void invalidPointerNeverFallsBackToCreatingDatabase() {
        QFETCH(QString,kind);QTemporaryDir fixture;const auto entry=fixture.filePath("Library");QVERIFY(QDir().mkpath(entry));
        const QString id="12345678-1234-1234-1234-123456789abc",name="Library-restored-"+id;
        const auto target=fixture.filePath(name);QVERIFY(QDir().mkpath(target));QVERIFY(write(target+"/notes.sqlite3","unchanged"));
        QJsonObject pointer{{"version",1},{"recoveryId",id},{"directory",name}};
        if(kind=="traversal")pointer.insert("directory","../outside");
        if(kind=="absolute")pointer.insert("directory",target);
        if(kind=="wrong-id")pointer.insert("recoveryId","bad");
        if(kind=="extra-key")pointer.insert("extra",true);
        if(kind=="chain")QVERIFY(write(target+"/active-library.json","{}"));
        if(kind=="missing-db")QVERIFY(QFile::remove(target+"/notes.sqlite3"));
        if(kind=="symlink") {
            const auto outside=fixture.filePath("outside");QVERIFY(QDir().mkpath(outside));QVERIFY(QDir(target).removeRecursively());QVERIFY(QFile::link(outside,target));
        }
        auto bytes=QJsonDocument(pointer).toJson(QJsonDocument::Compact);
        if(kind=="duplicate-key")bytes.insert(1,"\"version\":1,");
        QVERIFY(write(entry+"/active-library.json",bytes));
        ManagedStore store(entry,true);
        QCOMPARE(store.initializationError().value("error").toString(),QString("recoveryPointerInvalid"));
        QVERIFY(!store.initializationError().value("recoveryAvailable").toBool());
        QCOMPARE(read(entry+"/active-library.json"),bytes);QVERIFY(!QFileInfo::exists(entry+"/notes.sqlite3"));
    }
    void recoveryDoesNotPublishAnIncompleteAttachmentCache() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));const auto original=corrupt(entry);
        ManagedStore store(entry,true);
        RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="cacheWrite";};
        const auto failed=store.dispatch("restoreBackup",{{"source",url(backup)}});
        QCOMPARE(failed.value("error").toString(),QString("writeFailed"));
        QVERIFY(!QFileInfo::exists(entry+"/active-library.json"));QVERIFY(unchanged(entry,original));
        RepositoryTestHooks::managedFailure={};
        const auto restored=store.dispatch("restoreBackup",{{"source",url(backup)}});
        QVERIFY(restored.value("ok").toBool());QVERIFY(unchanged(entry,original));
    }
    void failedPublicationLeavesOriginalSelectedAndDurabilityFailureBlocksWrites() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));const auto original=corrupt(entry);
        {
            ManagedStore store(entry,true);
            RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="pointerCommit";};
            const auto failed=store.dispatch("restoreBackup",{{"source",url(backup)}});
            QVERIFY(!failed.value("ok").toBool());QVERIFY(!failed.value("activationUncertain").toBool());
            QVERIFY(!QFileInfo::exists(entry+"/active-library.json"));QVERIFY(unchanged(entry,original));
            RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="pointerDirectorySync";};
            const auto uncertain=store.dispatch("restoreBackup",{{"source",url(backup)}});
            QCOMPARE(uncertain.value("error").toString(),QString("recoveryActivationUncertain"));QVERIFY(uncertain.value("activationUncertain").toBool());
            QCOMPARE(store.dispatch("create",{{"path","unsafe.md"},{"text","must not write"}}).value("error").toString(),QString("recoveryActivationUncertain"));
            QVERIFY(unchanged(entry,original));
        }
        RepositoryTestHooks::managedFailure={};ManagedStore reopened(entry);
        QVERIFY(reopened.dispatch("read",{{"path","Projects/saved.md"}}).value("ok").toBool());QVERIFY(unchanged(entry,original));
    }
    void crashRecoveryHelper() {
        const auto entry=qEnvironmentVariable("NKNOTES_RECOVERY_ENTRY");
        if(entry.isEmpty())QSKIP("Child-process helper only");
        const auto requested=qEnvironmentVariable("NKNOTES_RECOVERY_STAGE");
        RepositoryTestHooks::managedRecoveryStage=[requested](const QString &stage){if(stage==requested)std::_Exit(42);};
        ManagedStore store(entry,true);
        store.dispatch("restoreBackup",{{"source",url(qEnvironmentVariable("NKNOTES_RECOVERY_BACKUP"))}});
        std::_Exit(43);
    }
    void crashPublicationSelectsOnlyOriginalOrCompleteReplacement_data() {
        QTest::addColumn<QString>("stage");QTest::addColumn<bool>("published");
        QTest::newRow("validated")<<QString("snapshotValidated")<<false;
        QTest::newRow("ready")<<QString("replacementReady")<<false;
        QTest::newRow("before-commit")<<QString("beforePointerCommit")<<false;
        QTest::newRow("renamed")<<QString("pointerRenamed")<<true;
        QTest::newRow("durable")<<QString("pointerDurable")<<true;
    }
    void crashPublicationSelectsOnlyOriginalOrCompleteReplacement() {
        QFETCH(QString,stage);QFETCH(bool,published);QTemporaryDir fixture;
        const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");
        QVERIFY(makeBackup(fixture.path(),backup));const auto original=corrupt(entry);
        QProcess child;auto environment=QProcessEnvironment::systemEnvironment();
        environment.insert("NKNOTES_RECOVERY_ENTRY",entry);environment.insert("NKNOTES_RECOVERY_BACKUP",backup);environment.insert("NKNOTES_RECOVERY_STAGE",stage);
        child.setProcessEnvironment(environment);child.start(QCoreApplication::applicationFilePath(),{"crashRecoveryHelper"});
        QVERIFY(child.waitForFinished(15000));QCOMPARE(child.exitCode(),42);
        ManagedStore reopened(entry,true);QVERIFY(unchanged(entry,original));
        if(published) {
            QVERIFY(reopened.initializationError().isEmpty());
            QCOMPARE(reopened.dispatch("read",{{"path","Projects/saved.md"}}).value("text").toString(),QString("# Saved\n\nSynthetic backup text.\n"));
            QCOMPARE(reopened.dispatch("list",{}).value("assetManifest").toMap().size(),1);
        } else QCOMPARE(reopened.initializationError().value("error").toString(),QString("databaseCorrupt"));
    }
    void conflictDraftReopensExactlyWithoutReadingExternalPath() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library"),file=fixture.filePath("external.md"),path="external:"+file;
        const QString draft="# Local\n\n精确恢复\n",disk="changed externally\n";QString base;
        QVERIFY(write(file,"original\n"));
        {
            ManagedStore store(entry);const auto document=store.dispatch("read",{{"path",path}});base=document.value("baseHash").toString();
            QVERIFY(write(file,disk.toUtf8()));
            const auto result=store.dispatch("save",{{"path",path},{"text",draft},{"baseHash",base}});
            QCOMPARE(result.value("error").toString(),QString("conflict"));QVERIFY(result.value("recoveryDurable").toBool());
            QCOMPARE(result.value("diskText").toString(),disk);QCOMPARE(read(file),disk.toUtf8());
        }
        QVERIFY(QFile::remove(file));ManagedStore reopened(entry);
        const auto drafts=reopened.dispatch("recovery",{}).value("drafts").toList();QCOMPARE(drafts.size(),1);
        const auto recovered=drafts.first().toMap();QCOMPARE(recovered.value("text").toString(),draft);QCOMPARE(recovered.value("baseHash").toString(),base);
        QVERIFY(recovered.value("baseUnverified").toBool());QVERIFY(recovered.value("recoveryDurable").toBool());
    }
    void failedDraftCommitRetainsNewestMemoryAndPreventsReplacement() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library"),file=fixture.filePath("external.md"),path="external:"+file;
        const auto backup=fixture.filePath("saved.nkbackup");QVERIFY(makeBackup(fixture.path(),backup));QVERIFY(write(file,"original\n"));
        NotesRepository repository;QVERIFY(call(repository,"selectLibrary",{{"path",entry},{"managed",true}}).value("ok").toBool());
        const auto base=call(repository,"read",{{"path",path}}).value("baseHash");
        QVERIFY(call(repository,"checkpoint",{{"path",path},{"text","older durable"},{"baseHash",base}}).value("ok").toBool());
        RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="draftCommit";};
        const auto failed=call(repository,"save",{{"path",path},{"text","newest memory\n"},{"baseHash",base}});
        QCOMPARE(failed.value("error").toString(),QString("recoveryWriteFailed"));QVERIFY(failed.value("draftInMemory").toBool());QVERIFY(!failed.value("recoveryDurable").toBool());
        QCOMPARE(read(file),QByteArray("original\n"));
        const auto recovery=call(repository,"recovery");
        QCOMPARE(recovery.value("drafts").toList().first().toMap().value("text").toString(),QString("newest memory\n"));
        QVERIFY(!recovery.value("drafts").toList().first().toMap().value("recoveryDurable").toBool());
        QCOMPARE(call(repository,"restoreBackup",{{"source",url(backup)}}).value("error").toString(),QString("pendingDrafts"));
        QCOMPARE(call(repository,"selectLibrary",{{"path",fixture.filePath("other")},{"managed",true}}).value("error").toString(),QString("pendingDrafts"));
        RepositoryTestHooks::managedFailure={};
        QVERIFY(call(repository,"checkpoint",{{"path",path},{"text","newest memory\n"},{"baseHash",base}}).value("recoveryDurable").toBool());
        QVERIFY(call(repository,"discardDraft",{{"path",path}}).value("ok").toBool());QVERIFY(call(repository,"recovery").value("drafts").toList().isEmpty());
    }
    void successfulExternalWriteReportsCleanupWarningAndNewBaseline() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library"),file=fixture.filePath("external.md"),path="external:"+file;
        QVERIFY(write(file,"original\n"));QString newHash;
        {
            ManagedStore store(entry);const auto base=store.dispatch("read",{{"path",path}}).value("baseHash");
            RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="draftCleanupCommit";};
            const auto result=store.dispatch("save",{{"path",path},{"text","written\n"},{"baseHash",base}});
            QVERIFY(result.value("ok").toBool());QVERIFY(result.value("draftRetained").toBool());QVERIFY(result.value("recoveryCleanupFailed").toBool());
            newHash=result.value("baseHash").toString();QCOMPARE(newHash,MarkdownFile::digest(read(file)));
            QCOMPARE(read(file),QByteArray("written\n"));QCOMPARE(store.dispatch("recovery",{}).value("drafts").toList().size(),1);
            RepositoryTestHooks::managedFailure={};
            const auto next=store.dispatch("save",{{"path",path},{"text","written again\n"},{"baseHash",newHash}});
            QVERIFY(next.value("ok").toBool());QVERIFY(store.dispatch("recovery",{}).value("drafts").toList().isEmpty());
        }
        ManagedStore reopened(entry);QVERIFY(reopened.dispatch("recovery",{}).value("drafts").toList().isEmpty());
    }
    void externalDraftCrashHelper() {
        const auto entry=qEnvironmentVariable("NKNOTES_DRAFT_ENTRY");if(entry.isEmpty())QSKIP("Child-process helper only");
        const auto path="external:"+qEnvironmentVariable("NKNOTES_DRAFT_FILE");ManagedStore store(entry);
        const auto base=store.dispatch("read",{{"path",path}}).value("baseHash");
        RepositoryTestHooks::externalSaveStage=[](const QString &stage){if(stage=="draftCommitted")std::_Exit(44);};
        store.dispatch("save",{{"path",path},{"text","draft before file write\n"},{"baseHash",base}});std::_Exit(45);
    }
    void crashAfterDraftCommitPreservesOriginalAndRecoveryText() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library"),file=fixture.filePath("external.md");QVERIFY(write(file,"original\n"));
        QProcess child;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("NKNOTES_DRAFT_ENTRY",entry);environment.insert("NKNOTES_DRAFT_FILE",file);
        child.setProcessEnvironment(environment);child.start(QCoreApplication::applicationFilePath(),{"externalDraftCrashHelper"});QVERIFY(child.waitForFinished(15000));QCOMPARE(child.exitCode(),44);
        QCOMPARE(read(file),QByteArray("original\n"));ManagedStore reopened(entry);
        const auto drafts=reopened.dispatch("recovery",{}).value("drafts").toList();QCOMPARE(drafts.size(),1);
        QCOMPARE(drafts.first().toMap().value("text").toString(),QString("draft before file write\n"));
        QCOMPARE(drafts.first().toMap().value("baseHash").toString(),MarkdownFile::digest("original\n"));
    }
    void healthyRestoreStillCreatesSafetyBackup() {
        QTemporaryDir fixture;const auto backup=fixture.filePath("saved.nkbackup"),entry=fixture.filePath("Library");QVERIFY(makeBackup(fixture.path(),backup));
        ManagedStore store(entry);QVERIFY(store.dispatch("create",{{"path","before.md"},{"text","before restoration"}}).value("ok").toBool());
        const auto restored=store.dispatch("restoreBackup",{{"source",url(backup)}});QVERIFY(restored.value("ok").toBool());QVERIFY(QFileInfo::exists(restored.value("safetyCopy").toString()));
        QVERIFY(!restored.value("recoveredInitialization").toBool());QCOMPARE(store.directory(),entry);
        QVERIFY(store.dispatch("read",{{"path","Projects/saved.md"}}).value("ok").toBool());QVERIFY(!store.dispatch("read",{{"path","before.md"}}).value("ok").toBool());
        QVERIFY(store.dispatch("restoreBackup",{{"source",url(restored.value("safetyCopy").toString())}}).value("ok").toBool());
        QCOMPARE(store.dispatch("read",{{"path","before.md"}}).value("text").toString(),QString("before restoration"));
    }
    void externalWriteFailureAndUnknownBaseKeepDurableDrafts() {
        QTemporaryDir fixture;const auto entry=fixture.filePath("Library"),file=fixture.filePath("external.md"),path="external:"+file;
        QVERIFY(write(file,"original\n"));ManagedStore store(entry);
        const auto base=store.dispatch("read",{{"path",path}}).value("baseHash");
        auto result=store.dispatch("save",{{"path",path},{"text","wrong base draft"},{"baseHash","unread"}});
        QCOMPARE(result.value("error").toString(),QString("unknownBase"));QVERIFY(result.value("recoveryDurable").toBool());
        RepositoryTestHooks::managedFailure=[](const QString &stage){return stage=="externalWrite";};
        result=store.dispatch("save",{{"path",path},{"text","write failed draft"},{"baseHash",base}});
        QCOMPARE(result.value("error").toString(),QString("writeFailed"));QVERIFY(result.value("recoveryDurable").toBool());QCOMPARE(read(file),QByteArray("original\n"));
        RepositoryTestHooks::managedFailure={};QVERIFY(QFile::remove(file));
        result=store.dispatch("save",{{"path",path},{"text","removed file draft"},{"baseHash",base}});
        QCOMPARE(result.value("error").toString(),QString("notFound"));QVERIFY(result.value("recoveryDurable").toBool());
        QCOMPARE(store.dispatch("recovery",{}).value("drafts").toList().first().toMap().value("text").toString(),QString("removed file draft"));
    }
    void externalEncodingAndSourceNewlinesArePreserved() {
        QTemporaryDir fixture;const auto file=fixture.filePath("external.md"),path="external:"+file;
        QVERIFY(write(file,QByteArray::fromHex("efbbbf")+"first\r\nlast\r\n"));ManagedStore store(fixture.filePath("Library"));
        auto base=store.dispatch("read",{{"path",path}}).value("baseHash");
        auto saved=store.dispatch("save",{{"path",path},{"text","new\nlast"},{"baseHash",base},{"sourceNewlines",true}});
        QVERIFY(saved.value("ok").toBool());QCOMPARE(read(file),QByteArray::fromHex("efbbbf")+"new\r\nlast");
        base=saved.value("baseHash");saved=store.dispatch("save",{{"path",path},{"text","new\nlast\n"},{"baseHash",base},{"sourceNewlines",true}});
        QVERIFY(saved.value("ok").toBool());QCOMPARE(read(file),QByteArray::fromHex("efbbbf")+"new\r\nlast\r\n");
    }
    void reservedExternalPrefixIsRejectedBeforeCreatingOrRenaming() {
        QTemporaryDir fixture;
        {
            ManagedStore store(fixture.path());
            QVERIFY(store.dispatch("createFolder",{{"path","Projects"}}).value("ok").toBool());
            QVERIFY(store.dispatch("create",{{"path","Projects/note.md"},{"text","kept"}}).value("ok").toBool());
            QCOMPARE(store.dispatch("createFolder",{{"path","external:archive"}}).value("error").toString(),QString("invalidName"));
            QVERIFY(!store.dispatch("create",{{"path","external:note.md"},{"text","rejected"}}).value("ok").toBool());
            QCOMPARE(store.dispatch("renameFolder",{{"path","Projects"},{"target","external:archive"}}).value("error").toString(),QString("invalidName"));
            QCOMPARE(store.dispatch("rename",{{"path","Projects/note.md"},{"target","external:note.md"}}).value("error").toString(),QString("invalidName"));
        }
        ManagedStore reopened(fixture.path());QVERIFY(reopened.initializationError().isEmpty());
        QCOMPARE(reopened.dispatch("read",{{"path","Projects/note.md"}}).value("text").toString(),QString("kept"));
    }
    void newAttachmentsAreRelativeAndManifestSurvivesReopen() {
        QTemporaryDir fixture;QString relative;
        {
            ManagedStore store(fixture.path());QVERIFY(store.dispatch("create",{{"path","深层/空 格/note.md"},{"text","body"}}).value("ok").toBool());
            const auto attachment=store.dispatch("importAttachment",{{"path","深层/空 格/note.md"},{"name","image.png"},{"data",QByteArray("synthetic").toBase64()}});
            QVERIFY(attachment.value("ok").toBool());relative=attachment.value("path").toString();
            QCOMPARE(attachment.value("url").toString(),"../../"+relative);QVERIFY(attachment.value("managedAssets").toBool());
            QCOMPARE(attachment.value("assetManifest").toMap().value(relative).toString(),MarkdownFile::digest("synthetic"));
        }
        ManagedStore reopened(fixture.path());QCOMPARE(reopened.dispatch("list",{}).value("assetManifest").toMap().value(relative).toString(),MarkdownFile::digest("synthetic"));
    }
};
QTEST_GUILESS_MAIN(ManagedRecoveryTest)
#include "ManagedRecoveryTest.moc"
