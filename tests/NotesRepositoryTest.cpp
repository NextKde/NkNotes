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
        const QString retained = commits[0].toMap().value("intent").toString().section(u'\n', 1);
        QCOMPARE(read(dir.filePath("a.md")), stage == "intent" ? QByteArray("base\n") : QByteArray("local\n"));
        QCOMPARE(read(retained), stage == "intent" ? QByteArray("local\n") : QByteArray("base\n"));
        QVERIFY(write(dir.filePath("a.md"), "new-external\n"));
        QVERIFY(call(r, "recovery").value("ok").toBool());
        QCOMPARE(read(dir.filePath("a.md")), QByteArray("new-external\n"));
        QVERIFY(QFileInfo::exists(retained));
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
