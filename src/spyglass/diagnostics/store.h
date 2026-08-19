#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "spyglass/diagnostics/diagnostic.h"

namespace spyglass {

// Written on the network thread and read on the render thread, so the overlay takes its
// handles under the lock and draws without holding it.
using DiagnosticHandle = std::shared_ptr<const Diagnostic>;

class DiagnosticStore {
public:
    void add(Diagnostic diagnostic);
    void clear();

    [[nodiscard]] std::vector<DiagnosticHandle> snapshot() const;
    [[nodiscard]] DiagnosticHandle latest() const;
    [[nodiscard]] std::uint64_t total() const { return total_.load(std::memory_order_relaxed); }

private:
    mutable std::mutex mutex_;
    std::deque<DiagnosticHandle> entries_;
    std::atomic_uint64_t total_{0};
};

DiagnosticStore &diagnostics();

}  // namespace spyglass
