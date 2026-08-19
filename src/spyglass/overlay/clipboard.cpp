#include "spyglass/overlay/clipboard.h"

#include <format>

#include <imgui.h>

#ifndef _WIN32
#include <fstream>

#include "spyglass/core/output.h"
#endif

namespace spyglass::overlay {

std::string offer([[maybe_unused]] const std::string_view filename, const std::string &text)
{
    ImGui::SetClipboardText(text.c_str());
#ifdef _WIN32
    return "copied to the clipboard";
#else
    const auto path = output_directory() / filename;
    if (std::ofstream out{path, std::ios::trunc}; out) {
        out << text;
        return std::format("saved to {}", path.string());
    }
    return std::format("could not write {}", path.string());
#endif
}

}  // namespace spyglass::overlay
