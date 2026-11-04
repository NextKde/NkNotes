#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QStringList>
#include <QTimer>
#include <QHash>
#include <functional>
class QUrl;
class EditorSession;
class NotesRepository;

// One editable buffer; every asynchronous response is scoped to its request,
// library generation, document epoch and (for saves) submitted revision.
class NotesController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString library MEMBER m_library NOTIFY changed)
    Q_PROPERTY(QString path MEMBER m_path NOTIFY changed)
    Q_PROPERTY(QString selectedId MEMBER m_selectedId NOTIFY changed)
    Q_PROPERTY(QString status MEMBER m_status NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY changed)
    Q_PROPERTY(QVariantList notes MEMBER m_notes NOTIFY changed)
    Q_PROPERTY(QVariantList folders MEMBER m_folders NOTIFY changed)
    Q_PROPERTY(QVariantMap recovery MEMBER m_recovery NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
public:
    explicit NotesController(EditorSession *, NotesRepository *, QObject *parent = nullptr);
    bool dirty() const { return m_revision != m_savedRevision; }
    bool busy() const { return m_transition || m_loading; }
    bool readOnly() const;
    QString query() const { return m_query; }
    QString filter() const { return m_filter; }
    void setQuery(const QString &);
    void setFilter(const QString &);
    Q_INVOKABLE void restoreWorkspace();
    Q_INVOKABLE void selectLibrary(const QUrl &);
    Q_INVOKABLE void openNote(const QString &path, int match = -1);
    Q_INVOKABLE void createNote();
    Q_INVOKABLE void createFolder(const QString &);
    Q_INVOKABLE void save();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void favorite();
    Q_INVOKABLE void trash();
    Q_INVOKABLE void restore(const QString &token, const QString &target = {});
    Q_INVOKABLE void move(const QString &target);
    Q_INVOKABLE void inspectRecovery();
    Q_INVOKABLE void recoverCommit(const QString &token, const QString &version);
    Q_INVOKABLE void recoverDraft(const QString &path, const QString &text, const QString &base);
    Q_INVOKABLE void saveCopy(const QString &target);
    Q_INVOKABLE void copyRecovery(const QString &text);
    Q_INVOKABLE void discardRecovery(const QString &path);
    Q_INVOKABLE void openRecoveryDirectory();
    Q_INVOKABLE void discardAndReload();
    Q_INVOKABLE void rememberPosition(int cursor, double scroll);
    Q_INVOKABLE QVariant preference(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setPreference(const QString &key, const QVariant &value);
    Q_INVOKABLE void dismissError();
    // Public response seam allows deterministic asynchronous contract tests without IO.
    void receive(quint64 id, const QString &operation, const QVariantMap &result);
    void externalChange(const QVariantMap &);
signals:
    void changed();
    void documentLoaded(int cursor, double scroll, int matchLength);
    void closeReady();
    void requestSent(quint64 id, const QString &operation, const QVariantMap &arguments);
private:
    struct Pending { QString op, path, library; quint64 generation, epoch, revision; QVariantMap args; };
    quint64 send(const QString &, QVariantMap = {});
    void drain(std::function<void()>);
    void advance();
    void load(const QString &, int match = -1);
    void applyListing(const QVariantMap &);
    void report(const QVariantMap &);
    QString positionKey() const;
    void recordActiveDocument();
    EditorSession *m_session;
    NotesRepository *m_repository;
    QTimer m_autosave, m_search;
    QHash<quint64, Pending> m_pending;
    quint64 m_next = 0, m_generation = 0, m_epoch = 0, m_revision = 0, m_savedRevision = 0, m_searchSerial = 0;
    quint64 m_saving = 0;
    bool m_restoreWorkspace = false;
    bool m_favorite = false;
    bool m_loading = false, m_transition = false, m_applying = false, m_readOnly = false, m_conflict = false;
    QString m_library, m_path, m_selectedId, m_base, m_status = QStringLiteral("请选择笔记库"), m_error, m_query, m_filter = "all";
    QStringList m_duplicateIds;
    QVariantList m_notes, m_folders;
    QVariantMap m_recovery;
    std::function<void()> m_afterSave;
};
