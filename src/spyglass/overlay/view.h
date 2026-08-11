#pragma once

#include <cstdint>
#include <string>

#include "spyglass/diagnostics/store.h"

namespace spyglass::overlay {

/** The ImGui windows. Reads a snapshot of the store; never touches the hook. */
class View {
public:
    void draw();

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    void draw_history(const std::vector<DiagnosticHandle> &entries);
    void draw_detail(const std::vector<DiagnosticHandle> &entries);
    void draw_status(std::size_t retained);
    const std::string &report_for(const Diagnostic &diagnostic);

    bool visible_{false};
    bool auto_show_{true};
    std::uint64_t seen_total_{0};
    std::uint64_t selected_{0};
    std::uint64_t cached_sequence_{0};
    std::string cached_report_;
};

}  // namespace spyglass::overlay
