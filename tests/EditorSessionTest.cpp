#include <QtTest>
#include "EditorSession.h"
#include <QTextCursor>
class EditorSessionTest : public QObject {
    Q_OBJECT
private slots:
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
