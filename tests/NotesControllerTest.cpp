#include <QtTest>
#include <QTextDocument>
#include <QTextCursor>
#include <QSettings>
#include <QTemporaryDir>
#include <QFile>
#include <QJSEngine>
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
            controller.setPreference("managedStorage", "/virtual/library");
            controller.restoreWorkspace();
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
            const auto request = take(op);
            if (request.isEmpty()) qFatal("Missing mock request: %s", qPrintable(op));
            result.insert("ok", result.value("ok", true));
            result.insert("library", "/virtual/library"); result.insert("generation", 1);
            controller.receive(request[0].toULongLong(), op, result);
        }
        void type(const QString &text) { QTextCursor cursor(&document); cursor.movePosition(QTextCursor::End); cursor.insertText(text); }
        int count(const QString &op) { int n = 0; for (const auto &r : requests) if (r[1] == op) ++n; return n; }
    };
private slots:
    void newNoteStartsWithAnIndependentNameAndEmptyBody() {
        Fixture f;
        f.controller.createNote();
        const auto request = f.take("create");
        QVERIFY(!request.isEmpty());
        QCOMPARE(request[2].toMap().value("text").toString(), QString("---\ntitle: 未命名笔记\n---\n\n"));
    }
    void changingNoteNameWaitsForTheBodyToSave() {
        Fixture f;
        f.type("正文修改");
        f.controller.renameNote("独立名称");
        QCOMPARE(f.count("renameNote"), 0);
        f.answer("save", {{"baseHash", "saved-body"}});
        const auto request = f.take("renameNote");
        QVERIFY(!request.isEmpty());
        QCOMPARE(request[2].toMap().value("baseHash").toString(), QString("saved-body"));
        QCOMPARE(request[2].toMap().value("title").toString(), QString("独立名称"));
        QCOMPARE(f.document.toPlainText(), QString("原文正文修改"));
    }
    void directNoteDeletionSavesBeforeRemovingCurrentDocument() {
        Fixture f;
        f.type(" changed");
        f.controller.organize("purgeNotes", {"a.md"});
        QCOMPARE(f.count("batch"), 0);
        f.answer("save", {{"baseHash", "saved"}});
        QCOMPARE(f.count("batch"), 1);
        QVariantMap pending;
        for (const auto &request : f.requests) if (request[1] == "batch") pending = request[2].toMap();
        QCOMPARE(pending.value("items").toList().first().toMap().value("baseHash").toString(), QString("saved"));
        f.answer("batch", {{"action", "purgeNotes"}, {"results", QVariantList{QVariantMap{{"path", "a.md"}, {"ok", true}}}}, {"succeeded", 1}, {"failed", 0},
                           {"undoState", QVariantMap{{"canUndo", false}, {"canRedo", false}}}});
        QVERIFY(f.controller.property("path").toString().isEmpty());
        QVERIFY(f.document.toPlainText().isEmpty());
        QVERIFY(!f.controller.canUndo()); QVERIFY(!f.controller.canRedo());
        QVERIFY(!f.controller.busy());
    }
    void failedDirectNoteDeletionKeepsCurrentDocument() {
        Fixture f;
        f.controller.organize("purgeNotes", {"a.md"});
        f.answer("batch", {{"action", "purgeNotes"}, {"results", QVariantList{QVariantMap{{"path", "a.md"}, {"ok", false}, {"error", "pendingDrafts"}}}}, {"succeeded", 0}, {"failed", 1}});
        QCOMPARE(f.controller.property("path").toString(), QString("a.md"));
        QCOMPARE(f.document.toPlainText(), QString("原文"));
        QVERIFY(!f.controller.busy());
        const auto result = f.controller.property("batchReport").toMap().value("results").toList().first().toMap();
        QVERIFY(!result.value("message").toString().isEmpty());
    }
    void directFolderDeletionDrainsAndClearsTheOpenDocument() {
        Fixture f;
        f.controller.openNote("Folder/Child/note.md");
        f.answer("read", {{"path", "Folder/Child/note.md"}, {"id", "N"}, {"text", "# Note"}, {"baseHash", "n"}});
        QSignalSpy deleted(&f.controller, &NotesController::folderDeleted);
        f.type(" edited");
        f.controller.manageFolder("purgeFolder", "Folder");
        QCOMPARE(f.count("purgeFolder"), 0);
        f.answer("save", {{"baseHash", "saved"}});
        QCOMPARE(f.count("purgeFolder"), 1);
        f.answer("purgeFolder", {{"undoState", QVariantMap{{"canUndo", false}, {"canRedo", false}}}});
        QCOMPARE(deleted.size(), 1); QVERIFY(deleted[0][0].toBool());
        QVERIFY(f.controller.property("path").toString().isEmpty());
        QVERIFY(f.document.toPlainText().isEmpty());
        QVERIFY(!f.controller.busy());
        QVERIFY(!f.controller.canUndo());
        QVERIFY(!f.controller.canRedo());
        QVERIFY(!f.controller.canGoForward());
    }
    void failedDirectFolderDeletionKeepsTheOpenDocument() {
        Fixture f;
        QSignalSpy deleted(&f.controller, &NotesController::folderDeleted);
        f.controller.manageFolder("purgeFolder", "Folder");
        f.answer("purgeFolder", {{"ok", false}, {"error", "pendingDrafts"}});
        QCOMPARE(deleted.size(), 1); QVERIFY(!deleted[0][0].toBool());
        QVERIFY(!deleted[0][1].toString().isEmpty());
        QCOMPARE(f.controller.property("path").toString(), QString("a.md"));
        QVERIFY(!f.controller.busy());
    }
    void normalSaveRefreshesRecoveryWithoutVersionBodies() {
        Fixture f; f.type("changed"); f.controller.save();
        f.answer("save", {{"baseHash", "saved"}});
        const auto summary = f.take("recovery"); QVERIFY(!summary.isEmpty());
        QCOMPARE(summary[2].toMap().value("includeCommitText"), QVariant(false));
        f.controller.inspectRecovery();
        const auto preview = f.take("recovery"); QVERIFY(!preview.isEmpty());
        QVERIFY(preview[2].toMap().value("includeCommitText", true).toBool());
        f.controller.setRecoveryVisible(true);
        f.controller.receive(summary[0].toULongLong(), "recovery", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"commits", QVariantList{}}});
        QVERIFY(!f.take("recovery").isEmpty());
        f.type("more"); f.controller.save(); f.answer("save", {{"baseHash", "saved-again"}});
        QVERIFY(f.take("recovery")[2].toMap().value("includeCommitText").toBool());
        f.controller.setRecoveryVisible(false);
        QVERIFY(!f.take("recovery")[2].toMap().value("includeCommitText", true).toBool());
    }
    void missingLinkCreationAndTagsWaitForCurrentSave() {
        Fixture f;QSignalSpy prompt(&f.controller,&NotesController::missingLinkRequested);
        f.controller.openLink("wiki:New");f.answer("resolveWiki",{{"path","a.md"},{"missing",true},{"target","New"},{"targetPath","New.md"}});QCOMPARE(prompt.size(),1);
        f.type("draft");f.controller.createMissingLink();QCOMPARE(f.count("createWiki"),0);f.answer("save",{{"baseHash","saved"}});QCOMPARE(f.count("createWiki"),1);
        f.answer("createWiki",{{"path","New.md"}});f.answer("read",{{"path","New.md"},{"id","N"},{"text","# New"},{"baseHash","new"}});
        f.type("more");f.controller.editTags("add",{"New.md"},{"tag"});QCOMPARE(f.count("tagBatch"),0);f.answer("save",{{"baseHash","latest"}});
        const auto request=f.take("tagBatch");QVERIFY(!request.isEmpty());QCOMPARE(request[2].toMap().value("items").toList().first().toMap().value("baseHash").toString(),QString("latest"));
    }

    void propertyArraysDetachBeforeWaitingForSave() {
        Fixture f; f.type("changed");
        {
            QJSEngine engine;
            f.controller.setNoteProperty("tags", QVariant::fromValue(engine.evaluate("['工作', 'project alpha']")));
            QCOMPARE(f.count("setProperty"), 0);
        }
        f.answer("save", {{"baseHash", "saved-latest"}});
        const auto request=f.take("setProperty");QVERIFY(!request.isEmpty());
        const auto args=request[2].toMap();
        QCOMPARE(args.value("baseHash").toString(), QString("saved-latest"));
        QCOMPARE(args.value("value").metaType(), QMetaType::fromType<QStringList>());
        QCOMPARE(args.value("value").toStringList(), (QStringList{"工作", "project alpha"}));
    }
    void navigationHistoryWaitsForSaveAndKeepsPositionOnFailure() {
        Fixture f; QVERIFY(!f.controller.canGoBack());
        f.controller.openNote("b.md");f.answer("read",{{"path","b.md"},{"id","B"},{"text","# B"},{"baseHash","b"}});
        QVERIFY(f.controller.canGoBack());f.type("changed");f.controller.navigateBack();
        QCOMPARE(f.count("read"),0);f.answer("save",{{"baseHash","saved"}});
        QCOMPARE(f.count("read"),1);f.answer("read",{{"ok",false},{"error","notFound"}});
        QVERIFY(f.controller.canGoBack());QCOMPARE(f.controller.property("path").toString(),QString("b.md"));
        f.controller.dismissError();f.controller.navigateBack();f.answer("read",{{"path","a.md"},{"id","A"},{"text","A"},{"baseHash","a"}});
        QVERIFY(f.controller.canGoForward());QVERIFY(!f.controller.canGoBack());
        f.controller.navigateForward();f.answer("read",{{"path","b.md"},{"id","B"},{"text","B"},{"baseHash","b"}});
        QVERIFY(f.controller.canGoBack());QVERIFY(!f.controller.canGoForward());
    }
    void organizeWaitsForSaveAndUsesLatestBaseline() {
        Fixture f; f.type("新输入"); f.controller.organize("trash", {"a.md"});
        QCOMPARE(f.count("batch"), 0); QCOMPARE(f.count("save"), 1);
        f.answer("save", {{"baseHash", "saved-latest"}});
        QCOMPARE(f.count("batch"), 1); QVERIFY(f.controller.busy());
        for (const auto &request : f.requests) if (request[1] == "batch") {
            const auto item = request[2].toMap().value("items").toList().first().toMap();
            QCOMPARE(item.value("baseHash").toString(), QString("saved-latest"));
        }
        QSignalSpy completed(&f.controller, &NotesController::batchFinished);
        f.answer("batch", {{"action", "trash"}, {"succeeded", 0}, {"failed", 1}, {"results", QVariantList{QVariantMap{{"path", "a.md"}, {"ok", false}, {"error", "conflict"}}}}});
        QCOMPARE(completed.size(), 1); QVERIFY(!f.controller.busy());
        QCOMPARE(f.controller.property("path").toString(), QString("a.md"));
        QVERIFY(f.session.source().contains("新输入"));
        QVERIFY(!f.controller.property("batchReport").toMap().value("results").toList().first().toMap().value("message").toString().isEmpty());
    }

    void autosaveDoesNotPublishRawScanOrderingOrUnfilteredRows() {
        Fixture f;
        const QVariantMap a{{"id", "A"}, {"path", "a.md"}, {"baseHash", "base"}, {"summary", "query match"}};
        const QVariantMap b{{"id", "B"}, {"path", "b.md"}};
        const QVariantMap c{{"id", "C"}, {"path", "c.md"}};
        f.controller.refresh(); f.answer("search", {{"notes", QVariantList{a,b}}});
        QSignalSpy moves(f.controller.noteModel(), &QAbstractItemModel::rowsMoved);
        QSignalSpy rows(&f.controller, &NotesController::notesChanged);
        f.type("输入"); f.controller.save();
        auto updated = a; updated["baseHash"] = "saved"; updated["summary"] = "new query match";
        f.answer("save", {{"baseHash", "saved"}, {"listing", QVariantMap{{"notes", QVariantList{b,c,updated}}}}});
        QCOMPARE(rows.size(), 0); QCOMPARE(moves.size(), 0);
        QCOMPARE(f.controller.property("notes").toList(), (QVariantList{a,b}));
        f.answer("search", {{"notes", QVariantList{updated,b}}});
        QCOMPARE(rows.size(), 1); QCOMPARE(moves.size(), 0);
        QCOMPARE(f.controller.property("notes").toList(), (QVariantList{updated,b}));
    }

    void listingsKeepPersistentRowsAndOnlyNotifyOnChanges() {
        NotesListModel model;
        QVariantMap a{{"id", "A"}, {"path", "a.md"}, {"summary", "old"}};
        QVariantMap b{{"id", "B"}, {"path", "b.md"}};
        model.replace({a,b});
        QPersistentModelIndex first(model.index(0));
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&model, &QAbstractItemModel::dataChanged);
        model.replace({a,b}); QCOMPARE(changes.size(), 0);
        a["summary"] = "new"; model.replace({a,b});
        QCOMPARE(changes.size(), 1); QVERIFY(first.isValid()); QCOMPARE(first.row(), 0);
        model.replace({b,a}); QCOMPARE(first.row(), 1);
        QCOMPARE(first.data(Qt::UserRole).toMap().value("summary").toString(), QString("new"));
        model.replace({a}); QCOMPARE(first.row(), 0); QCOMPARE(model.rowCount(), 1);
        QCOMPARE(resets.size(), 0);
        Fixture f; QSignalSpy listings(&f.controller, &NotesController::notesChanged);
        QTextCursor cursor(&f.document); cursor.insertText("编辑");
        QCOMPARE(listings.size(), 0);
    }

    void htmlExportEmbedsImagesWithoutChangingLiteralPaths() {
        Fixture f; QTemporaryDir library, output, outside;
        f.controller.setProperty("library", library.path());
        const QByteArray png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jfN8AAAAASUVORK5CYII=");
        QFile image(library.filePath("picture.png")); QVERIFY(image.open(QIODevice::WriteOnly)); QCOMPARE(image.write(png), png.size()); image.close();
        const QString url = QUrl::fromLocalFile(image.fileName()).toString();
        const QString target = output.filePath("note.html");
        QVERIFY(f.controller.exportHtml(QUrl::fromLocalFile(target), "<p>" + url + "</p><img src=\"" + url + "\">", "Title<&"));
        QFile exported(target); QVERIFY(exported.open(QIODevice::ReadOnly)); const QByteArray bytes = exported.readAll(); exported.close();
        QVERIFY(bytes.contains(("<p>" + url + "</p>").toUtf8())); QVERIFY(bytes.contains("src=\"data:image/png;base64,")); QVERIFY(bytes.contains("Title&lt;&amp;"));
        QFile other(outside.filePath("outside.png")); QVERIFY(other.open(QIODevice::WriteOnly)); other.write(png); other.close();
        QVERIFY(!f.controller.exportHtml(QUrl::fromLocalFile(target), "<img src=\"" + QUrl::fromLocalFile(other.fileName()).toString() + "\">", "Outside"));
        QVERIFY(exported.open(QIODevice::ReadOnly)); QCOMPARE(exported.readAll(), bytes);
        QVERIFY(f.controller.outputPath(QUrl("https://example.com/export"), "html").isEmpty());
    }
    void webCloseWaitsForLatestRendererSnapshot() {
        Fixture f; f.session.enableWeb(); f.session.webConnected(); f.session.loadPrototype("原文");
        const int epoch = f.session.webDocument().value("epoch").toInt();
        f.session.acceptWebState({{"epoch", epoch}, {"sequence", 1}, {"loaded", true}, {"source", "原文"}});
        QSignalSpy closed(&f.controller, &NotesController::closeReady);
        QSignalSpy flush(&f.session, &EditorSession::webFlushRequested);
        f.controller.requestClose(); QCOMPARE(closed.size(), 0); QCOMPARE(f.count("save"), 0);
        QVERIFY(!flush.isEmpty());
        f.session.acceptWebState({{"epoch", epoch}, {"sequence", 2}, {"loaded", true}, {"source", "最后输入"}, {"flush", flush.last()[0]}});
        const auto save = f.take("save"); QVERIFY(!save.isEmpty());
        QCOMPARE(save[2].toMap().value("text").toString(), QStringLiteral("最后输入"));
        f.controller.receive(save[0].toULongLong(), "save", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"baseHash", "next"}});
        QCOMPARE(closed.size(), 1);
    }
    void saveUpdatesEditedIdentity() {
        Fixture f; f.controller.rememberPosition(2, 12);
        f.type("changed"); f.controller.save();
        f.answer("save", {{"baseHash", "next"}, {"listing", QVariantMap{{"notes", QVariantList{QVariantMap{{"path", "a.md"}, {"id", "B"}}}}}}});
        QCOMPARE(f.controller.property("selectedId").toString(), QString("B"));
        QCOMPARE(f.controller.preference("lastNote").toMap().value("id").toString(), QString("B"));
    }
    void copyImmediatelyBecomesLastDocument() {
        Fixture f; QSignalSpy loaded(&f.controller, &NotesController::documentLoaded);
        f.controller.saveCopy("copy.md");
        f.answer("saveAs", {{"path", "copy.md"}, {"text", "copy"}, {"baseHash", "copyhash"}, {"listing", QVariantMap{{"notes", QVariantList{QVariantMap{{"path", "copy.md"}, {"id", "COPY"}}}}}}});
        QCOMPARE(f.controller.preference("lastNote").toMap().value("id").toString(), QString("COPY"));
        QCOMPARE(loaded.size(), 1);
        f.controller.requestClose();
        QTextDocument doc; EditorSession session; session.attachDocument(&doc);
        NotesController next(&session, nullptr); QSignalSpy requests(&next, &NotesController::requestSent);
        next.restoreWorkspace(); auto select = requests.takeFirst();
        next.receive(select[0].toULongLong(), "selectLibrary", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"notes", QVariantList{QVariantMap{{"path", "copy.md"}, {"id", "COPY"}}}}});
        QList<QVariant> recovery;
        for (const auto &r : requests) if (r[1] == "recovery") recovery = r;
        QVERIFY(!recovery.isEmpty()); requests.clear();
        next.receive(recovery[0].toULongLong(), "recovery", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}});
        QCOMPARE(requests.last()[1].toString(), QString("read"));
        QCOMPARE(requests.last()[2].toMap().value("path").toString(), QString("copy.md"));
    }
    void restartRestoresStableIdentityAfterRecovery() {
        QTemporaryDir settings;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
        Fixture first;
        first.controller.setPreference("lastNote", QVariantMap{{"library", "/virtual/library"}, {"id", "A"}, {"path", "old.md"}});
        QTextDocument doc; EditorSession session; session.attachDocument(&doc);
        NotesController controller(&session, nullptr);
        QSignalSpy requests(&controller, &NotesController::requestSent);
        controller.restoreWorkspace();
        QCOMPARE(requests.size(), 1);
        auto selection = requests.takeFirst();
        controller.receive(selection[0].toULongLong(), "selectLibrary", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"notes", QVariantList{QVariantMap{{"id", "A"}, {"path", "moved.md"}}}}});
        QList<QVariant> recovery;
        for (const auto &r : requests) { QVERIFY(r[1] != "read"); if (r[1] == "recovery") recovery = r; }
        QVERIFY(!recovery.isEmpty()); requests.clear();
        controller.receive(recovery[0].toULongLong(), "recovery", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"drafts", QVariantList{QVariantMap{{"path", "moved.md"}, {"text", "草稿"}}}}});
        QCOMPARE(requests.size(), 1); auto read = requests.takeFirst();
        QCOMPARE(read[2].toMap().value("path").toString(), QString("moved.md"));
        controller.receive(read[0].toULongLong(), "read", {{"ok", true}, {"library", "/virtual/library"}, {"generation", 1}, {"id", "A"}, {"path", "moved.md"}, {"text", "磁盘"}});
        QVERIFY(controller.readOnly()); QCOMPARE(session.source(), QString("磁盘"));
        QSettings().clear();
    }
    void missingRestoredLibraryReturnsToPicker() {
        QTextDocument doc; EditorSession session; session.attachDocument(&doc);
        NotesController controller(&session, nullptr);
        controller.setPreference("lastLibrary", "/missing/previous-library");
        QSignalSpy requests(&controller, &NotesController::requestSent);
        controller.restoreWorkspace(); QCOMPARE(requests.size(), 1);
        const auto request = requests.takeFirst();
        controller.receive(request[0].toULongLong(), "selectLibrary", {{"ok", false}, {"error", "invalidLibrary"}});
        QVERIFY(controller.property("library").toString().isEmpty());
        QVERIFY(!controller.busy());
        QVERIFY(controller.property("error").toString().contains("重新选择"));
        controller.selectLibrary(QUrl::fromLocalFile("/valid"));
        QCOMPARE(requests.size(), 1);
        QSettings().clear();
    }
    void failedSwitchStillAllowsSaveAndClose() {
        Fixture f; f.controller.selectLibrary(QUrl::fromLocalFile("/missing"));
        f.answer("selectLibrary", {{"ok", false}, {"error", "invalidLibrary"}});
        QCOMPARE(f.controller.property("library").toString(), QString("/virtual/library"));
        f.type("继续"); f.controller.save(); f.answer("save", {{"baseHash", "new"}});
        QSignalSpy closed(&f.controller, &NotesController::closeReady);
        f.controller.requestClose(); QCOMPARE(closed.size(), 1);
    }
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
