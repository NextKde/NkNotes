#pragma once
#include <QString>
#include <memory>

// The configured entry directory never changes. Recovery publishes a sibling
// directory through a small, strictly validated pointer while retaining its lock.
class ManagedLibrary {
public:
    struct Lease;
    explicit ManagedLibrary(const QString &entryDirectory);
    ~ManagedLibrary();
    QString entryRoot() const;
    QString activeRoot() const;
    static std::shared_ptr<Lease> lockDirectory(const QString &directory);
    QString createReplacement(const QString &recoveryId) const;
    void publish(const QString &directory, const QString &recoveryId, std::shared_ptr<Lease> lease);
    bool activationUncertain() const;
    static void syncFile(const QString &path);
    static void syncDirectory(const QString &path);
private:
    QString entry, active;
    std::shared_ptr<Lease> entryLease, activeLease, pendingLease;
    bool uncertain = false;
};
