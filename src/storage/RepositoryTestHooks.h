#pragma once
#include <functional>
// Deterministic fault injection; configure only while the repository worker is idle.
namespace RepositoryTestHooks {
inline std::function<void(bool)> beforeScan;
}
