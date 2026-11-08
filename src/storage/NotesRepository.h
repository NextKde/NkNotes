#pragma once
#include <QObject>
#include <QThread>
#include <QVariantMap>
#include <atomic>
#include <memory>

class RepositoryWorker;
// All requests are FIFO on a dedicated worker; no filesystem work in this facade.
class NotesRepository final : public QObject {
    Q_OBJECT
public:
    explicit NotesRepository(QObject *parent = nullptr);
    ~NotesRepository() override;
    // See docs/architecture.md for operation schemas. Retain id + library generation in UI.
    Q_INVOKABLE quint64 request(const QString &operation, const QVariantMap &arguments = {});
    void cancelOperation();
signals:
    void operationProgress(quint64 requestId, const QString &operation, const QVariantMap &progress);
    void finished(quint64 requestId, const QString &operation, const QVariantMap &result);
    void libraryChanged(const QVariantMap &change);
private:
    QThread m_thread;
    RepositoryWorker *m_worker;
    quint64 m_nextRequest = 0;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
