#include "spyglass/diagnostics/store.h"

#include "spyglass/core/config.h"

namespace spyglass {

void DiagnosticStore::add(Diagnostic diagnostic)
{
    auto entry = std::make_shared<const Diagnostic>(std::move(diagnostic));

    const std::lock_guard lock{mutex_};
    entries_.push_back(std::move(entry));
    while (entries_.size() > config().history_limit) {
        entries_.pop_front();
    }
    total_.fetch_add(1, std::memory_order_relaxed);
}

void DiagnosticStore::clear()
{
    const std::lock_guard lock{mutex_};
    entries_.clear();
}

std::vector<DiagnosticHandle> DiagnosticStore::snapshot() const
{
    const std::lock_guard lock{mutex_};
    return {entries_.begin(), entries_.end()};
}

DiagnosticHandle DiagnosticStore::latest() const
{
    const std::lock_guard lock{mutex_};
    return entries_.empty() ? nullptr : entries_.back();
}

DiagnosticStore &diagnostics()
{
    static DiagnosticStore store;
    return store;
}

}  // namespace spyglass
