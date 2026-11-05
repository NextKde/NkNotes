#include <QtTest>
#include "EditorSession.h"
#include <QTextCursor>
#include <QTextBlock>
#include <QTextList>
#include <QImage>
#include <QTemporaryDir>
class EditorSessionTest : public QObject {
    Q_OBJECT
private slots:
    void sourceSnapshotsKeepEditedFrontMatterAcrossReconnect() {
        EditorSession session;session.enableWeb();session.webConnected();session.loadPrototype("---\nold: keep\n---\nbody\n");
        int epoch=session.webDocument().value("epoch").toInt();
        const QString source="---\nnew: longer value\n---\n<div>raw</div>";
        session.acceptWebState({{"epoch",epoch},{"sequence",1},{"loaded",true},{"sourceMode",true},{"fullSource",true},{"sourceEdited",true},{"source",source}});
        QCOMPARE(session.source(),source);QVERIFY(session.sourceMode());QVERIFY(session.sourceEdited());
        session.webDisconnected();session.webConnected();
        QCOMPARE(session.webDocument().value("source").toString(),QString("<div>raw</div>"));
        QCOMPARE(session.webDocument().value("prefix").toString(),QString("---\nnew: longer value\n---\n"));
    }
    void webReconnectRestoresCommittedCacheAndFreshSequence() {
        EditorSession session; session.enableWeb(); session.webConnected(); session.loadPrototype("---\ncustom: keep\n---\nold\n");
        const int epoch = session.webDocument().value("epoch").toInt();
        session.acceptWebState({{"epoch", epoch}, {"sequence", 20}, {"loaded", true}, {"source", "latest\n"}});
        QSignalSpy changes(&session, &EditorSession::sourceChanged);
        session.webDisconnected(); session.webConnected();
        QCOMPARE(session.webDocument().value("source").toString(), QStringLiteral("latest\n"));
        session.acceptWebState({{"epoch", epoch}, {"sequence", 1}, {"loaded", true}, {"source", "latest\n"}});
        QVERIFY(!session.webLoading()); QCOMPARE(changes.size(), 0);
        QCOMPARE(session.source(), QStringLiteral("---\ncustom: keep\n---\nlatest\n"));
    }
    void webSnapshotsRejectStaleAndPreedit() {
        EditorSession session; session.enableWeb(); session.webConnected();
        const QString original = "---\ncustom: keep\n---\nbody\n";
        session.loadPrototype(original);
        const int epoch = session.webDocument().value("epoch").toInt();
        QSignalSpy changes(&session, &EditorSession::sourceChanged);
        QVariantMap state{{"epoch", epoch}, {"sequence", 1}, {"loaded", true}, {"source", "body\n"}};
        session.acceptWebState(state); QCOMPARE(session.source(), original); QCOMPARE(changes.size(), 0);
        state["sequence"] = 2; state["composing"] = true; state["source"] = "拼音临时";
        session.acceptWebState(state); QVERIFY(session.composing()); QCOMPARE(session.source(), original);
        QSignalSpy flush(&session, &EditorSession::webFlushRequested);
        QVERIFY(session.flushWeb()); const int token = flush.last()[0].toInt(); QVERIFY(session.awaitingWebFlush());
        state["sequence"] = 3; state["composing"] = false; state["source"] = "正文\n"; state["flush"] = token;
        session.acceptWebState(state); QVERIFY(!session.awaitingWebFlush());
        QCOMPARE(session.source(), QStringLiteral("---\ncustom: keep\n---\n正文\n")); QCOMPARE(changes.size(), 1);
        state["source"] = "old"; session.acceptWebState(state); QCOMPARE(changes.size(), 1);
        session.loadPrototype("new file\n");
        state["sequence"] = 4; session.acceptWebState(state); QCOMPARE(session.source(), QStringLiteral("new file\n"));
    }
    void typedMarkdownHeading() {
        for (int level = 1; level <= 6; ++level) {
            QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
            session.loadPrototype("");
            QTextCursor cursor(&document); cursor.insertText(QString(level, '#'));
            QVERIFY(session.markdownHeading(level, level));
            QCOMPARE(document.begin().blockFormat().headingLevel(), level);
            QCOMPARE(document.toPlainText(), QString());
            session.undo(); QCOMPARE(document.toPlainText(), QString(level, '#'));
            session.redo(); QCOMPARE(document.begin().blockFormat().headingLevel(), level);
            QTextCursor typing(&document); typing.insertText("我的标题", document.begin().charFormat());
            QVERIFY(session.source().startsWith(QString(level, '#') + " 我的标题"));
            QVERIFY(session.endHeading(4, 4));
            QCOMPARE(document.lastBlock().blockFormat().headingLevel(), 0);
            QTextCursor body(&document); body.movePosition(QTextCursor::End); body.insertText("正文");
            const QString saved = session.source();
            session.loadPrototype(saved);
            QCOMPARE(document.begin().blockFormat().headingLevel(), level);
            QCOMPARE(document.lastBlock().blockFormat().headingLevel(), 0);
            QCOMPARE(document.toPlainText(), QStringLiteral("我的标题\n正文"));
        }
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        session.loadPrototype("text\n"); QVERIFY(!session.markdownHeading(4, 4));
        session.loadPrototype(""); QTextCursor cursor(&document); cursor.insertText("##");
        session.setComposing(true); QVERIFY(!session.markdownHeading(2, 2));
        session.setComposing(false); QVERIFY(!session.markdownHeading(0, 2));
        QVERIFY(session.markdownHeading(2, 2));
    }
    void appendedParagraphSurvivesReload() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        session.loadPrototype("original EOF\n");
        QTextCursor cursor(&document); cursor.movePosition(QTextCursor::End);
        cursor.insertBlock(); cursor.insertText("new paragraph");
        const QString saved = session.source();
        QVERIFY(saved.contains("original EOF\n\nnew paragraph"));
        session.loadPrototype(saved); QCOMPARE(document.toPlainText(), QStringLiteral("original EOF\nnew paragraph"));
    }
    void hardBreakSurvivesSaveAndReload() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        session.loadPrototype("first\n");
        QTextCursor cursor(&document); cursor.movePosition(QTextCursor::End);
        cursor.insertText(QString(QChar::LineSeparator) + QStringLiteral("中文"));
        const QString expected = document.toPlainText();
        const QString saved = session.source();
        QVERIFY(saved.contains("first  \n中文"));
        session.loadPrototype(saved); QCOMPARE(document.toPlainText(), expected);
    }
    void paragraphAndListCommands() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        session.loadPrototype("hello *world*\n\nnext\n");
        session.select(1, 3); QVERIFY(session.execute("heading2"));
        QCOMPARE(document.begin().blockFormat().headingLevel(), 2);
        QTextCursor end(&document); end.setPosition(10);
        QVERIFY(end.charFormat().fontItalic());
        QVERIFY(end.charFormat().fontWeight() >= QFont::Bold);
        session.undo(); QCOMPARE(session.source(), QStringLiteral("hello *world*\n\nnext\n"));
        session.select(0, 11); QVERIFY(session.execute("task"));
        QVERIFY(document.begin().textList());
        QCOMPARE(document.begin().blockFormat().marker(), QTextBlockFormat::MarkerType::Unchecked);
        QVERIFY(session.execute("check"));
        QCOMPARE(document.begin().blockFormat().marker(), QTextBlockFormat::MarkerType::Checked);
        QVERIFY(session.execute("task"));
        QVERIFY(!document.begin().textList());
        QCOMPARE(document.begin().blockFormat().marker(), QTextBlockFormat::MarkerType::NoMarker);
        QVERIFY(!session.execute("heading7"));
        QVERIFY(!session.execute("headingX"));
    }
    void preservedRulesAndCodeBlocks() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        const QString original = "***\n\n__Keep__\n\n```text\n---\n\ncode\n```\n\nChange me\n";
        session.loadPrototype(original);
        const int start = document.toPlainText().indexOf("Change me");
        session.select(start, start + 6); QVERIFY(session.execute("bold"));
        QVERIFY(session.source().startsWith("***\n\n__Keep__\n\n```text\n---\n\ncode\n```"));
        QVERIFY(session.source().contains("**Change**"));
        session.undo(); QCOMPARE(session.source(), original);
        session.redo(); QVERIFY(session.source().contains("**Change**"));
    }
    void richRoundTripAndMetadata() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        const QString prefix = "---\nunknown: untouched\n---\n";
        const QString original = prefix + "Hello world\n";
        session.loadPrototype(original);
        QCOMPARE(session.source(), original);
        session.select(0, 5); QVERIFY(session.execute("bold"));
        QVERIFY(session.source().startsWith(prefix));
        QVERIFY(session.source().contains("**Hello**"));
        const QString saved = session.source();
        session.undo(); QCOMPARE(session.source(), original);
        session.loadPrototype(saved); QCOMPARE(session.source(), saved);
        QCOMPARE(document.toPlainText(), QStringLiteral("Hello world"));
        session.select(0, 5); QVERIFY(session.execute("heading1"));
        QCOMPARE(document.begin().blockFormat().headingLevel(), 1);
        session.undo(); QCOMPARE(session.source(), saved);
        QVERIFY(session.execute("bullet")); QVERIFY(document.begin().textList());
        session.undo(); QCOMPARE(session.source(), saved);
    }
    void richProtectionAndComposition() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        const QString original = "<!-- keep -->\n[[note]]\n";
        session.loadPrototype(original); QVERIFY(session.protectedContent());
        QCOMPARE(session.source(), original); QVERIFY(!session.execute("heading1"));
        session.loadPrototype("text\n");
        QSignalSpy changes(&session, &EditorSession::sourceChanged);
        session.setComposing(true); QTextCursor cursor(&document); cursor.insertText("中文");
        QCOMPARE(changes.count(), 0); session.setComposing(false); QCOMPARE(changes.count(), 1);
    }

    void richUnchangedBlocksAndOrdinaryPunctuation() {
        QTextDocument document; EditorSession session; session.setRich(true); session.attachDocument(&document);
        const QString original = "Price $12 and $30\n\n---\n\n__Keep this spelling__\n\nChange me\n";
        session.loadPrototype(original);
        QVERIFY(!session.protectedContent());
        const int start = document.toPlainText().indexOf("Change me");
        session.select(start, start + 6); QVERIFY(session.execute("bold"));
        QVERIFY(session.source().contains("__Keep this spelling__"));
        QVERIFY(session.source().contains("**Change**"));
        session.undo(); QCOMPARE(session.source(), original);
    }
    void compositionCommitPrecedesState() {
        QTextDocument document; EditorSession session; session.attachDocument(&document);
        session.loadPrototype("text"); session.setComposing(true);
        QStringList emitted;
        connect(&session, &EditorSession::sourceChanged, this, [&] { emitted << "source"; });
        connect(&session, &EditorSession::stateChanged, this, [&] { emitted << "state"; });
        session.setComposing(false);
        QCOMPARE(emitted, QStringList({"source", "state"}));
    }
    void imageFitsWithoutChangingAddress() {
        QTemporaryDir dir;
        const QString path = dir.filePath("wide.png");
        QImage image(1672, 836, QImage::Format_RGB32); image.fill(Qt::blue); QVERIFY(image.save(path));
        QTextDocument document; document.setBaseUrl(QUrl::fromLocalFile(dir.path() + "/"));
        EditorSession session; session.setRich(true); session.attachDocument(&document); session.setImageWidth(400);
        const QString original = "![Forest](wide.png)\n\nText\n";
        session.loadPrototype(original);
        const auto resource = document.resource(QTextDocument::ImageResource, QUrl("wide.png")).value<QImage>();
        QCOMPARE(resource.size(), QSize(400, 200));
        QCOMPARE(session.source(), original);
        const int start = document.toPlainText().indexOf("Text"); session.select(start, start + 4); session.execute("bold");
        QVERIFY(session.source().contains("![Forest](wide.png)"));
        session.setImageWidth(200);
        QCOMPARE(document.resource(QTextDocument::ImageResource, QUrl("wide.png")).value<QImage>().size(), QSize(200, 100));
        session.undo(); QCOMPARE(session.source(), original);
    }

    void targetedEditAndSingleUndo() {
        QTextDocument document;
        EditorSession session;
        session.attachDocument(&document);
        const QString original = "---\nunknown: untouched\n---\n你好 世界\n<!-- keep -->\n";
        session.loadPrototype(original);
        const int start = original.indexOf(QStringLiteral("你好"));
        session.select(start, start + 2);
        QVERIFY(session.execute("bold"));
        QString expected = original;
        expected.replace(start, 2, "**你好**");
        QCOMPARE(session.source(), expected);
        session.undo();
        QCOMPARE(session.source(), original);
        QVERIFY(!session.canUndo());
        session.redo();
        QCOMPARE(session.source(), expected);
    }
    void compositionGuardAndUnknownCommand() {
        QTextDocument document;
        EditorSession session;
        session.attachDocument(&document);
        session.loadPrototype("中文");
        session.select(0, 2);
        session.setComposing(true);
        QVERIFY(!session.execute("bold"));
        QCOMPARE(session.source(), QStringLiteral("中文"));
        session.setComposing(false);
        QVERIFY(!session.execute("unsupported"));
    }
    void reverseSelectionAndNbsp() {
        QTextDocument document;
        EditorSession session;
        session.attachDocument(&document);
        session.loadPrototype(QStringLiteral("a\u00a0b\n"));
        session.select(3, 0);
        QVERIFY(session.execute("bold"));
        QCOMPARE(session.source(), QStringLiteral("**a\u00a0b**\n"));
        QVERIFY(session.execute("bold"));
        QCOMPARE(session.source(), QStringLiteral("a\u00a0b\n"));
    }
};
QTEST_MAIN(EditorSessionTest)
#include "EditorSessionTest.moc"
