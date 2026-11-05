#pragma once
#include <QObject>
#include <QPointer>
#include <QTextDocument>
#include <QQuickTextDocument>
#include <QVector>
#include <QStringList>
#include <QVariantMap>

// Rich editing retains the loaded source and unchanged Markdown block spellings.
class EditorSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool rich READ rich WRITE setRich NOTIFY stateChanged)
    Q_PROPERTY(int headingLevel READ headingLevel NOTIFY stateChanged)
    Q_PROPERTY(QString formatState READ formatState NOTIFY stateChanged)
    Q_PROPERTY(QString fidelityNotice READ fidelityNotice NOTIFY stateChanged)
    Q_PROPERTY(bool protectedContent READ protectedContent NOTIFY protectionChanged)
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY stateChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
    Q_PROPERTY(bool composing READ composing WRITE setComposing NOTIFY stateChanged)
    Q_PROPERTY(bool sourceMode READ sourceMode NOTIFY stateChanged)
    Q_PROPERTY(bool webLoading READ webLoading NOTIFY stateChanged)
public:
    explicit EditorSession(QObject *parent = nullptr);
    bool rich() const { return m_rich; }
    void setRich(bool value) { m_rich = value; }
    int headingLevel() const;
    QString formatState() const;
    Q_INVOKABLE void setImageWidth(qreal width);
    QString fidelityNotice() const;
    bool protectedContent() const { return m_protected; }
    QString source() const;
    bool modified() const;
    bool canUndo() const;
    bool canRedo() const;
    bool composing() const { return m_composing; }
    void setComposing(bool value);
    Q_INVOKABLE void attach(QQuickTextDocument *document);
    void attachDocument(QTextDocument *document); // Also used by headless VM tests.
    Q_INVOKABLE void select(int anchor, int position);
    Q_INVOKABLE bool link(const QString &url);
    Q_INVOKABLE bool execute(const QString &command);
    Q_INVOKABLE bool markdownHeading(int anchor, int position);
    Q_INVOKABLE bool endHeading(int anchor, int position);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void loadPrototype(const QString &source);
    Q_INVOKABLE void enableWeb();
    Q_INVOKABLE QVariantMap webDocument() const;
    Q_INVOKABLE void webConnected();
    Q_INVOKABLE void webDisconnected();
    Q_INVOKABLE void acceptWebState(const QVariantMap &snapshot);
    bool flushWeb();
    bool awaitingWebFlush() const { return m_webFlushPending != 0; }
    bool sourceMode() const { return m_sourceMode; }
    bool sourceEdited() const { return m_sourceEdited; }
    bool webLoading() const { return m_web && !m_webLoaded; }
signals:
    void typingFormatRequested(const QString &command);
    void sourceChanged();
    void protectionChanged();
    void stateChanged();
    void selectionRequested(int anchor, int position);
    void webLoadRequested(const QVariantMap &document);
    void webCommandRequested(const QVariantMap &command);
    void webFlushRequested(int token);
private:
    bool m_sourceMode = false, m_sourceEdited = false;
    bool m_web = false, m_webConnected = false, m_webLoaded = false;
    bool m_webUndo = false, m_webRedo = false;
    int m_webEpoch = 0, m_webSequence = -1, m_webHeading = 0;
    int m_webFlushSerial = 0, m_webFlushPending = 0;
    QString m_webSource, m_webBody, m_webFormat;
    void loadWeb(const QString &source);
    bool commandWeb(const QString &command, const QString &argument = {});
    QPointer<QTextDocument> m_document;
    int m_anchor = 0;
    int m_position = 0;
    bool m_composing = false;
    bool m_rich = false, m_loading = false, m_protected = false;
    QString m_original, m_prefix;
    QString m_loadedRich;
    QVector<QPair<QString, QString>> m_originalBlocks;
    QStringList m_imageNames;
    qreal m_imageWidth = 640;
    void cacheImages();
};
