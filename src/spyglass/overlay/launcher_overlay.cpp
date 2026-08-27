#include "spyglass/overlay/install.h"

#include <cstring>
#include <format>
#include <stdexcept>

#include <imgui.h>

#include "spyglass/core/log.h"
#include "spyglass/hook/function_hook.h"
#include "spyglass/hook/host_symbol.h"
#include "spyglass/overlay/view.h"

namespace spyglass::overlay {
namespace {

void (*g_render)() = nullptr;
View g_view;

void *require(const char *name)
{
    void *symbol = hook::host_symbol(name);
    if (symbol == nullptr) {
        throw std::runtime_error{std::format("the launcher has no symbol {}, is it stripped?", name)};
    }
    return symbol;
}

bool adopt_host_context()
{
    const auto *host_version = reinterpret_cast<const char *(*)()>(require("_ZN5ImGui10GetVersionEv"))();
    if (std::strcmp(host_version, IMGUI_VERSION) != 0) {
        throw std::runtime_error{
            std::format("built against ImGui {} but the launcher runs {}", IMGUI_VERSION, host_version)};
    }

    auto *context = reinterpret_cast<ImGuiContext *(*)()>(require("_ZN5ImGui17GetCurrentContextEv"))();
    if (context == nullptr) {
        throw std::runtime_error{"the launcher has no ImGui context"};
    }

    ImGuiMemAllocFunc alloc = nullptr;
    ImGuiMemFreeFunc release = nullptr;
    void *user_data = nullptr;
    reinterpret_cast<void (*)(ImGuiMemAllocFunc *, ImGuiMemFreeFunc *, void **)>(
        require("_ZN5ImGui21GetAllocatorFunctionsEPPFPvmS0_EPPFvS0_S0_EPS0_"))(&alloc, &release, &user_data);
    ImGui::SetAllocatorFunctions(alloc, release, user_data);
    ImGui::SetCurrentContext(context);

    log::info("overlay is sharing the launcher's ImGui {}, press F12 for it", host_version);
    return true;
}

void render()
{
    static bool ready = [] {
        try {
            return adopt_host_context();
        }
        catch (const std::exception &e) {
            log::error("overlay: {}", e.what());
            return false;
        }
    }();

    if (ready) {
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            g_view.toggle();
        }
        g_view.draw();
    }
    g_render();
}

}  // namespace

void install_overlay()
{
    static hook::FunctionHook hook{"ImGui::Render", require("_ZN5ImGui6RenderEv"), reinterpret_cast<void *>(&render),
                                   reinterpret_cast<void **>(&g_render)};
}

}  // namespace spyglass::overlay
