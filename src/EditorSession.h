#pragma once
#include <QObject>
#include <QPointer>
#include <QTextDocument>
#include <QQuickTextDocument>

// The plain Markdown document is the authoritative buffer. No HTML round trip.
class EditorSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(bool composing READ composing WRITE setComposing NOTIFY stateChanged)
public:
    explicit EditorSession(QObject *parent = nullptr);
    QString source() const;
    bool modified() const;
    bool canUndo() const;
    bool canRedo() const;
    bool composing() const { return m_composing; }
    void setComposing(bool value);
    Q_INVOKABLE void attach(QQuickTextDocument *document);
    void attachDocument(QTextDocument *document); // Also used by headless VM tests.
    Q_INVOKABLE void select(int anchor, int position);
    Q_INVOKABLE bool execute(const QString &command);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void loadPrototype(const QString &source);
signals:
    void sourceChanged();
    void stateChanged();
    void selectionRequested(int anchor, int position);
private:
    QPointer<QTextDocument> m_document;
    int m_anchor = 0;
    int m_position = 0;
    bool m_composing = false;
};
