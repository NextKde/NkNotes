#pragma once
#include <QObject>
#include <QVariantMap>
#include <QUrl>
#include <QStringList>
#include <QTimer>
#include <QHash>
#include <functional>
#include "NotesListModel.h"
class QUrl;
class EditorSession;
class NotesRepository;

// One editable buffer; every asynchronous response is scoped to its request,
// library generation, document epoch and (for saves) submitted revision.
class NotesController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap missingLink MEMBER m_missingLink NOTIFY changed)
    Q_PROPERTY(QVariantMap tagPlan MEMBER m_tagPlan NOTIFY changed)
    Q_PROPERTY(QVariantMap templates MEMBER m_templates NOTIFY changed)
    Q_PROPERTY(QVariantMap knowledgeData MEMBER m_knowledge NOTIFY changed)
    Q_PROPERTY(QVariantMap historyData MEMBER m_versions NOTIFY changed)
    Q_PROPERTY(QVariantMap versionData MEMBER m_version NOTIFY changed)
    Q_PROPERTY(QVariantMap smartFolders MEMBER m_smartFolders NOTIFY changed)
    Q_PROPERTY(QVariantList linkCandidates MEMBER m_linkCandidates NOTIFY changed)
    Q_PROPERTY(QVariantMap operationProgress MEMBER m_operationProgress NOTIFY changed)
    Q_PROPERTY(QVariantMap backupResult MEMBER m_backupResult NOTIFY changed)
    Q_PROPERTY(QVariantMap importPlan MEMBER m_importPlan NOTIFY changed)
    Q_PROPERTY(QVariantMap importReport MEMBER m_importReport NOTIFY changed)
    Q_PROPERTY(QVariantList importHistory MEMBER m_importHistory NOTIFY changed)
    Q_PROPERTY(bool externalDocument READ externalDocument NOTIFY changed)
    Q_PROPERTY(QString assetRoot READ assetRoot NOTIFY changed)
    Q_PROPERTY(bool systemFileDialogs READ systemFileDialogs CONSTANT)
    Q_PROPERTY(bool recoveryAvailable MEMBER m_recoveryAvailable NOTIFY changed)
    Q_PROPERTY(QUrl documentUrl READ documentUrl NOTIFY changed)
    Q_PROPERTY(QString legacyLibrary READ legacyLibrary NOTIFY changed)
    Q_PROPERTY(QString library MEMBER m_library NOTIFY changed)
    Q_PROPERTY(QString path MEMBER m_path NOTIFY changed)
    Q_PROPERTY(QString selectedId MEMBER m_selectedId NOTIFY changed)
    Q_PROPERTY(QString noteTitle READ noteTitle NOTIFY changed)
    Q_PROPERTY(QString status MEMBER m_status NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY changed)
    Q_PROPERTY(QVariantList notes MEMBER m_notes NOTIFY notesChanged)
    Q_PROPERTY(QAbstractItemModel *noteModel READ noteModel CONSTANT)
    Q_PROPERTY(QVariantList folders MEMBER m_folders NOTIFY foldersChanged)
    Q_PROPERTY(QVariantMap folderIcons MEMBER m_folderIcons NOTIFY foldersChanged)
    Q_PROPERTY(QVariantMap folderDetails MEMBER m_folderDetails NOTIFY foldersChanged)
    Q_PROPERTY(QVariantMap recovery MEMBER m_recovery NOTIFY changed)
    Q_PROPERTY(QString highlightQuery MEMBER m_highlightQuery NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY changed)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY changed)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY changed)
    Q_PROPERTY(QVariantMap batchReport MEMBER m_batchReport NOTIFY batchFinished)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY changed)
    Q_PROPERTY(QVariantMap attachments MEMBER m_attachments NOTIFY changed)
public:
    explicit NotesController(EditorSession *, NotesRepository *, QObject *parent = nullptr);
    QAbstractItemModel *noteModel() { return &m_noteModel; }
    bool dirty() const { return m_revision != m_savedRevision; }
    bool busy() const { return m_transition || m_loading || m_exporting || m_organizing; }
    bool readOnly() const;
    QString query() const { return m_query; }
    QString filter() const { return m_filter; }
    QString sortKey() const;
    bool sortAscending() const;
    void setSortKey(const QString &);
    void setSortAscending(bool);
    Q_INVOKABLE void organize(const QString &action, const QStringList &paths, const QString &folder = {});
    Q_INVOKABLE void manageFolder(const QString &operation, const QString &path, const QString &target = {});
    Q_INVOKABLE void editFolder(const QString &path, const QString &target, const QString &icon);
    Q_INVOKABLE void moveFolder(const QString &path, const QString &parent, const QString &sibling, bool after);
    void setQuery(const QString &);
    void setFilter(const QString &);
    Q_INVOKABLE void selectCategory(const QString &filter, const QString &query = {});
    Q_INVOKABLE void restoreWorkspace();
    Q_INVOKABLE void selectLibrary(const QUrl &);
    Q_INVOKABLE void previewImport(const QList<QUrl> &sources, const QString &folder = {}, bool keepStructure = true);
    Q_INVOKABLE void confirmImport(bool keepDuplicates = false);
    Q_INVOKABLE void cancelImport();
    Q_INVOKABLE void cancelImportOperation();
    Q_INVOKABLE void createBackup(const QUrl &destination);
    Q_INVOKABLE void restoreBackup(const QUrl &source);
    Q_INVOKABLE void inspectImports();
    Q_INVOKABLE void openExternal(const QUrl &source);
    Q_INVOKABLE void closeExternal();
    Q_INVOKABLE void importExternal();
    Q_INVOKABLE void exportNotes(const QUrl &destination, const QStringList &paths = {}, const QString &folder = {});
    bool externalDocument() const { return m_path.startsWith("external:"); }
    QString noteTitle() const;
    QString assetRoot() const;
    QUrl documentUrl() const;
    bool systemFileDialogs() const;
    bool managedAssets() const { return m_managedAssets && !externalDocument(); }
    QVariantMap assetManifest() const { return managedAssets() ? m_assetManifest : QVariantMap(); }
    Q_INVOKABLE QUrl resolveAsset(const QUrl &source) const;
    QString legacyLibrary() const;
    Q_INVOKABLE void openNote(const QString &path, int match = -1);
    Q_INVOKABLE void createNote();
    Q_INVOKABLE void renameNote(const QString &title);
    Q_INVOKABLE void createFolder(const QString &, const QString &icon = QStringLiteral("folder"));
    Q_INVOKABLE void save();
    Q_INVOKABLE void requestClose();
    Q_INVOKABLE void refresh(bool indexed = false);
    Q_INVOKABLE void favorite();
    Q_INVOKABLE void trash();
    Q_INVOKABLE void restore(const QString &token, const QString &target = {});
    Q_INVOKABLE void move(const QString &target);
    Q_INVOKABLE void inspectRecovery();
    Q_INVOKABLE void setRecoveryVisible(bool visible);
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
    Q_INVOKABLE void importAttachment(const QString &request, int webEpoch, const QUrl &source, const QString &name = {}, const QString &data = {});
    Q_INVOKABLE void inspectAttachments();
    Q_INVOKABLE void quarantineAttachment(const QString &relative);
    Q_INVOKABLE void restoreAttachment(const QString &token);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE QVariantMap imageWidths() const;
    Q_INVOKABLE void setImageWidth(const QString &source, int width);
    Q_INVOKABLE QVariantList tableWidths() const;
    Q_INVOKABLE void setTableWidths(const QVariantList &widths);
    Q_INVOKABLE void inspectTemplates();
    Q_INVOKABLE void saveTemplate(const QString &name, const QString &text);
    Q_INVOKABLE void saveCurrentTemplate(const QString &name);
    Q_INVOKABLE void deleteTemplate(const QString &name);
    Q_INVOKABLE void createFromTemplate(const QString &name, const QString &title);
    Q_INVOKABLE void createDailyNote();
    Q_INVOKABLE void inspectKnowledge();
    Q_INVOKABLE void createMissingLink();
    Q_INVOKABLE void editTags(const QString &action, const QStringList &paths, const QStringList &tags);
    Q_INVOKABLE void previewTagRename(const QString &from, const QString &to);
    Q_INVOKABLE void confirmTagRename();
    Q_INVOKABLE void lookupLinks(const QString &query, int request = 0);
    Q_INVOKABLE void setNoteProperty(const QString &key, const QVariant &value, bool remove = false);
    Q_INVOKABLE void inspectHistory();
    Q_INVOKABLE void historyVersion(const QString &hash, bool restore = false);
    Q_INVOKABLE void saveSmartFolder(const QString &name);
    Q_INVOKABLE void deleteSmartFolder(const QString &name);
    Q_INVOKABLE void openSmartFolder(const QString &name);
    Q_INVOKABLE void rebuildIndex();
    Q_INVOKABLE void navigateBack();
    Q_INVOKABLE void navigateForward();
    Q_INVOKABLE void recoverMove();
    Q_INVOKABLE void recycle(const QString &action, const QVariantList &items);
    Q_INVOKABLE void restoreFolder(const QString &token);
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString undoText READ undoText NOTIFY changed)
    Q_PROPERTY(QString redoText READ redoText NOTIFY changed)
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;
    QString redoText() const;
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    bool canGoBack() const { return m_historyIndex > 0; }
    bool canGoForward() const { return m_historyIndex + 1 < m_history.size(); }
    Q_INVOKABLE void openLink(const QString &href);
    Q_INVOKABLE void exportPdf(const QString &path, const QString &body, const QString &title, bool printing);
    QString exportDocument(const QString &body, const QString &title) const;
    Q_INVOKABLE bool exportHtml(const QUrl &target, const QString &body, const QString &title);
    Q_INVOKABLE QString outputPath(const QUrl &target, const QString &extension) const;
    Q_INVOKABLE QString printPreviewPath() const;
    Q_INVOKABLE void exportFinished(bool success, const QString &path, bool printPreview = false);
    Q_INVOKABLE void setEditorExporting(bool value);
    // Public response seam allows deterministic asynchronous contract tests without IO.
    void receive(quint64 id, const QString &operation, const QVariantMap &result);
    void externalChange(const QVariantMap &);
signals:
    void importPreviewReady();
    void importFinished();
    void backupFinished();
    void notesExported(const QUrl &directory, int count, const QVariantList &warnings);
    void libraryOperationFailed(const QString &message);
    void missingLinkRequested();
    void tagRenameReady();
    void wikiCandidatesReady(int request, const QVariantList &items);
    void pdfExportFinished();
    void changed();
    void notesChanged();
    void foldersChanged();
    void folderCreated(const QString &path);
    void folderCreationFailed(const QString &message);
    void folderEdited(bool success, const QString &message);
    void folderDeleted(bool success, const QString &message);
    void batchFinished();
    void documentAnchorRequested(const QString &heading);
    void documentLoaded(int cursor, double scroll, int matchLength);
    void closeReady();
    void requestSent(quint64 id, const QString &operation, const QVariantMap &arguments);
    void attachmentReady(const QString &request, int webEpoch, const QVariantMap &result);
private:
    QVariantMap m_undoState;
    bool m_organizationHistory = false, m_textHistoryReplay = false;
    void replayHistory(bool redo);
    QVariantMap m_importPlan, m_importReport, m_operationProgress, m_backupResult;
    QVariantList m_importHistory;
    QVariantMap m_missingLink, m_tagPlan;
    QVariantMap m_templates, m_knowledge, m_versions, m_version, m_smartFolders;
    QVariantList m_linkCandidates;
    int m_linkRequest = 0;
    QStringList m_history;
    int m_historyIndex = -1, m_historyTarget = -1;
    void relocateHistory(const QString &, const QString &);
    struct Pending { QString op, path, library; quint64 generation, epoch, revision; QVariantMap args; };
    quint64 send(const QString &, QVariantMap = {});
    void drain(std::function<void()>);
    void advance();
    void load(const QString &, int match = -1, bool preserveCategory = false);
    void applyListing(const QVariantMap &);
    void report(const QVariantMap &);
    QString positionKey() const;
    void recordActiveDocument();
    QString sortPreferenceKey() const;
    QString organizerError(const QString &) const;
    void acceptBatch(QVariantMap);
    NotesListModel m_noteModel;
    EditorSession *m_session;
    NotesRepository *m_repository;
    QTimer m_autosave, m_search;
    QHash<quint64, Pending> m_pending;
    quint64 m_next = 0, m_generation = 0, m_epoch = 0, m_revision = 0, m_savedRevision = 0, m_searchSerial = 0;
    quint64 m_saving = 0;
    bool m_restoreWorkspace = false, m_exporting = false, m_organizing = false;
    bool m_recoveryAvailable = false, m_managedAssets = false;
    QVariantMap m_assetManifest;
    bool m_favorite = false, m_recoveryVisible = false;
    bool m_loading = false, m_transition = false, m_applying = false, m_readOnly = false, m_conflict = false;
    QString m_highlightQuery;
    QString m_library, m_path, m_selectedId, m_base, m_status = QStringLiteral("正在打开我的笔记…"), m_error, m_query, m_filter = "all";
    QStringList m_duplicateIds;
    QVariantList m_notes, m_folders;
    QVariantMap m_folderIcons, m_folderDetails;
    QVariantMap m_recovery, m_batchReport;
    QVariantMap m_attachments;
    std::function<void()> m_afterSave;
};
