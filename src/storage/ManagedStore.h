#pragma once
#include <QVariantMap>
#include <QStringList>
#include <memory>
#include <functional>
#include <atomic>

class ManagedStore {
public:
    explicit ManagedStore(const QString &directory);
    ~ManagedStore();
    QVariantMap dispatch(const QString &operation, const QVariantMap &arguments, std::function<void(const QVariantMap &)> progress = {}, std::shared_ptr<std::atomic_bool> cancelled = {});
    QString directory() const;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
