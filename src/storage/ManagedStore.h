#pragma once
#include <QVariantMap>
#include <QStringList>
#include <memory>
#include <functional>
#include <atomic>

struct ManagedStoreError { QString code, detail; };

class ManagedStore {
public:
    // Repository workers retain a failed open (and its locks) for restoreBackup.
    explicit ManagedStore(const QString &directory, bool retainInitializationFailure = false);
    ~ManagedStore();
    QVariantMap dispatch(const QString &operation, const QVariantMap &arguments, std::function<void(const QVariantMap &)> progress = {}, std::shared_ptr<std::atomic_bool> cancelled = {});
    QString directory() const;
    QString entryRoot() const;
    bool hasPendingMemoryDrafts() const;
    QVariantMap initializationError() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
