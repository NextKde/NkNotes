#pragma once
#include <functional>
#include <QString>
// Deterministic fault injection; configure only while the repository worker is idle.
namespace RepositoryTestHooks {
inline std::function<void(bool)> beforeScan;
// Stage callbacks may terminate a test child to exercise crash boundaries.
inline std::function<void(const QString &)> managedRecoveryStage;
inline std::function<void(const QString &)> externalSaveStage;
// Return true to fail the named write/commit boundary; no environment backdoor.
inline std::function<bool(const QString &)> managedFailure;
}
