#include <QtTest>
#include <QTextDocument>
#include <QTextCursor>
#include "EditorSession.h"
#include "NotesController.h"

class NotesControllerTest : public QObject {
    Q_OBJECT
    struct Fixture {
        QTextDocument document;
        EditorSession session;
        NotesController controller{&session, nullptr};
        QSignalSpy requests{&controller, &NotesController::requestSent};
        Fixture() {
            session.attachDocument(&document);
            controller.selectLibrary(QUrl::fromLocalFile("/virtual/library"));
            answer("selectLibrary", {{"notes", QVariantList{QVariantMap{{"id", "A"}, {"path", "a.md"}, {"baseHash", "base"}}}}, {"folders", QVariantList{}}});
            controller.openNote("a.md");
            answer("read", {{"path", "a.md"}, {"id", "A"}, {"text", "原文"}, {"baseHash", "base"}});
            requests.clear();
        }
        QList<QVariant> take(const QString &op) {
            for (int i = 0; i < requests.size(); ++i) if (requests[i][1].toString() == op) return requests.takeAt(i);
            return {};
        }
        void answer(const QString &op, QVariantMap result = {}) {
            const auto request = take(op); Q_ASSERT(!request.isEmpty());
            result.insert("ok", result.value("ok", true));
            result.insert("library", "/virtual/library"); result.insert("generation", 1);
            controller.receive(request[0].toULongLong(), op, result);
        }
        void type(const QString &text) { QTextCursor cursor(&document); cursor.movePosition(QTextCursor::End); cursor.insertText(text); }
        int count(const QString &op) { int n = 0; for (const auto &r : requests) if (r[1] == op) ++n; return n; }
    };
private slots:
    void automaticSaveDebounce() {
        Fixture f; f.type("中文自动保存");
        QCOMPARE(f.count("save"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(f.count("save"), 1, 2000);
        const auto request = f.take("save");
        QCOMPARE(request[2].toMap().value("text").toString(), QString("原文中文自动保存"));
    }
    void cleanExternalReload() {
        Fixture f;
        f.controller.externalChange({{"library", "/virtual/library"}, {"generation", 1}, {"changes", QVariantList{QVariantMap{{"path", "a.md"}, {"hash", "external"}}}}});
        QCOMPARE(f.count("read"), 1);
        f.answer("read", {{"path", "a.md"}, {"text", "外部新版本"}, {"baseHash", "external"}});
        QCOMPARE(f.session.source(), QString("外部新版本")); QVERIFY(!f.controller.dirty());
    }
    void revisionSaveAndSwitch() {
        Fixture f; f.type("一"); f.controller.save();
        QCOMPARE(f.count("save"), 1);
        f.type("二"); f.controller.openNote("b.md");
        QCOMPARE(f.count("read"), 0);
        f.answer("save", {{"baseHash", "first"}, {"text", "不得回写"}});
        QVERIFY(f.controller.dirty()); QCOMPARE(f.session.source(), QString("原文一二"));
        QCOMPARE(f.count("save"), 1);
        auto request = f.take("save"); QCOMPARE(request[2].toMap().value("text").toString(), QString("原文一二"));
        QCOMPARE(request[2].toMap().value("baseHash").toString(), QString("first"));
        f.controller.receive(request[0].toULongLong(), "save", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"baseHash", "second"}});
        QVERIFY(!f.controller.dirty()); QCOMPARE(f.count("read"), 1);
        f.answer("read", {{"path", "b.md"}, {"text", "B"}, {"baseHash", "b"}});
        QCOMPARE(f.session.source(), QString("B"));
    }
    void closeWaitsForNewestRevision() {
        Fixture f; QSignalSpy closed(&f.controller, &NotesController::closeReady);
        f.type("一"); f.controller.save(); f.type("二"); f.controller.requestClose();
        QCOMPARE(closed.size(), 0);
        f.answer("save", {{"baseHash", "first"}}); QCOMPARE(closed.size(), 0);
        f.answer("save", {{"baseHash", "second"}}); QCOMPARE(closed.size(), 1);
    }
    void failedSaveBlocksCloseAndKeepsText() {
        Fixture f; QSignalSpy closed(&f.controller, &NotesController::closeReady);
        f.type("不能丢失"); const auto expected = f.session.source(); f.controller.requestClose();
        f.answer("save", {{"ok", false}, {"error", "writeFailed"}, {"recoveryDurable", false}});
        QCOMPARE(closed.size(), 0); QCOMPARE(f.session.source(), expected); QVERIFY(f.controller.dirty());
        QVERIFY(!f.controller.property("error").toString().isEmpty());
        f.controller.save(); f.answer("save", {{"baseHash", "retry"}});
        f.controller.requestClose(); QCOMPARE(closed.size(), 1);
    }
    void composeGuard() {
        Fixture f; f.session.setComposing(true); f.type("中文输入");
        f.controller.save(); f.controller.requestClose(); QCOMPARE(f.count("save"), 0);
        QVERIFY(!f.controller.readOnly());
        f.session.setComposing(false); QCOMPARE(f.count("save"), 1);
        const auto before = f.session.source(); f.answer("save", {{"baseHash", "next"}, {"text", "旧回包"}});
        QCOMPARE(f.session.source(), before);
    }
    void staleSearchAndUnknownReply() {
        Fixture f; f.controller.setQuery("旧"); f.controller.refresh(); auto old = f.take("search");
        f.controller.setQuery("新"); f.controller.refresh();
        f.answer("search", {{"notes", QVariantList{QVariantMap{{"title", "新"}}}}});
        f.controller.receive(old[0].toULongLong(), "search", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"notes", QVariantList{}}});
        QCOMPARE(f.controller.property("notes").toList().size(), 1);
        f.controller.receive(99999, "read", {{"ok", true}, {"text", "恶意陈旧"}});
        QCOMPARE(f.session.source(), QString("原文"));
    }
    void oldLibraryResponseIgnored() {
        Fixture f;
        f.controller.refresh(); const auto old = f.take("search");
        f.controller.selectLibrary(QUrl::fromLocalFile("/virtual/other"));
        const auto selection = f.take("selectLibrary");
        f.controller.receive(selection[0].toULongLong(), "selectLibrary", {{"ok", true}, {"library", "/virtual/other"}, {"generation", 2}, {"notes", QVariantList{}}});
        f.controller.receive(old[0].toULongLong(), "search", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"notes", QVariantList{QVariantMap{{"title", "旧库"}}}}});
        QCOMPARE(f.controller.property("library").toString(), QString("/virtual/other"));
        QVERIFY(f.controller.property("notes").toList().isEmpty());
    }
    void conflictAndReadFailureKeepBuffer() {
        Fixture f; f.type("本地"); const QString text = f.session.source();
        f.controller.externalChange({{"library", "/virtual/library"}, {"generation", 1}, {"changes", QVariantList{QVariantMap{{"path", "a.md"}, {"hash", "external"}}}}});
        QCOMPARE(f.count("checkpoint"), 1); f.controller.save(); QCOMPARE(f.count("save"), 0);
        f.controller.discardAndReload();
        f.answer("read", {{"ok", false}, {"error", "readFailed"}});
        QCOMPARE(f.session.source(), text); QVERIFY(f.controller.dirty());
    }
    void removedFileCheckpointFailureBlocksClose() {
        Fixture f; QSignalSpy closed(&f.controller, &NotesController::closeReady);
        f.controller.externalChange({{"library", "/virtual/library"}, {"generation", 1}, {"changes", QVariantList{QVariantMap{{"path", "a.md"}, {"removed", true}}}}});
        f.answer("checkpoint", {{"ok", false}, {"error", "recoveryWriteFailed"}, {"recoveryDurable", false}});
        f.controller.requestClose(); QCOMPARE(closed.size(), 0); QVERIFY(f.controller.dirty());
        QCOMPARE(f.session.source(), QString("原文"));
    }
    void pendingDraftRequiresChoice() {
        Fixture f; f.controller.inspectRecovery();
        f.answer("recovery", {{"drafts", QVariantList{QVariantMap{{"path", "b.md"}, {"text", "旧草稿"}, {"baseHash", "old"}}}}});
        f.controller.openNote("b.md"); f.answer("read", {{"path", "b.md"}, {"text", "外部正文"}, {"baseHash", "new"}});
        QVERIFY(f.controller.readOnly()); QCOMPARE(f.session.source(), QString("外部正文")); QCOMPARE(f.count("save"), 0);
        f.controller.recoverDraft("b.md", "旧草稿", "old");
        f.answer("read", {{"path", "b.md"}, {"text", "外部正文"}, {"baseHash", "new"}});
        QCOMPARE(f.session.source(), QString("旧草稿")); QVERIFY(f.controller.dirty());
        f.controller.save(); QCOMPARE(f.count("save"), 0);
    }
};
QTEST_MAIN(NotesControllerTest)
#include "NotesControllerTest.moc"
