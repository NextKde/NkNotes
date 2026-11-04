#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include "NotesRepository.h"
#include "MarkdownFile.h"

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
