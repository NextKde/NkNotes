#pragma once
#include <QObject>
#include <QThread>
#include <QVariantMap>

class RepositoryWorker;
// All requests are FIFO on a dedicated worker; no filesystem work in this facade.
class NotesRepository final : public QObject {
    Q_OBJECT
public:
    explicit NotesRepository(QObject *parent = nullptr);
    ~NotesRepository() override;
    // See docs/architecture.md for operation schemas. Retain id + library generation in UI.
    Q_INVOKABLE quint64 request(const QString &operation, const QVariantMap &arguments = {});
signals:
    void finished(quint64 requestId, const QString &operation, const QVariantMap &result);
    void libraryChanged(const QVariantMap &change);
private:
    QThread m_thread;
    RepositoryWorker *m_worker;
    quint64 m_nextRequest = 0;
};
