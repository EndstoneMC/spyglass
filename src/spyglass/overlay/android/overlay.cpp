#ifdef __ANDROID__

#include "spyglass/overlay/install.h"

#include <cstring>
#include <format>
#include <stdexcept>

#include <imgui.h>

#include "spyglass/hook.h"
#include "spyglass/overlay/android/symbol.h"
#include "spyglass/overlay/view.h"

namespace spyglass {
namespace {

void (*g_render)() = nullptr;

void *require(const char *name)
{
    void *symbol = host_symbol(name);
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

    return true;
}

void render()
{
    static bool ready = [] {
        try {
            return adopt_host_context();
        }
        catch (const std::exception &) {
            return false;
        }
    }();

    if (ready) {
        if (ImGui::IsKeyPressed(ImGuiKey_F12, false)) {
            View::getInstance().toggle();
        }
        View::getInstance().draw();
    }
    g_render();
}

}  // namespace

void install_overlay()
{
    static FunctionHook hook{"ImGui::Render", require("_ZN5ImGui6RenderEv"), reinterpret_cast<void *>(&render),
                                   reinterpret_cast<void **>(&g_render)};
}

}  // namespace spyglass

#endif
