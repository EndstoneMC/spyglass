#include "spyglass/overlay/launcher_overlay.h"

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

// The launcher already runs an ImGui of its own over the game. Borrowing that context is far
// less to go wrong than standing up a second one: no render backend, no input plumbing, and
// the windows are drawn and presented with the rest of its frame.
constexpr const char *kGetVersion = "_ZN5ImGui10GetVersionEv";
constexpr const char *kGetCurrentContext = "_ZN5ImGui17GetCurrentContextEv";
constexpr const char *kGetAllocatorFunctions = "_ZN5ImGui21GetAllocatorFunctionsEPPFPvmS0_EPPFvS0_S0_EPS0_";
constexpr const char *kRender = "_ZN5ImGui6RenderEv";

// F12 is bound by neither the game nor the launcher, whose menubar is on Alt.
constexpr ImGuiKey kToggleKey = ImGuiKey_F12;

using GetVersionFn = const char *(*)();
using GetCurrentContextFn = ImGuiContext *(*)();
using GetAllocatorFunctionsFn = void (*)(ImGuiMemAllocFunc *, ImGuiMemFreeFunc *, void **);
using RenderFn = void (*)();

RenderFn g_render = nullptr;
View g_view;

void *require(const char *name)
{
    void *symbol = hook::host_symbol(name);
    if (symbol == nullptr) {
        throw std::runtime_error{std::format("the launcher has no symbol {}, is it stripped?", name)};
    }
    return symbol;
}

/**
 * Two builds of ImGui sharing one context only works while they agree on the layout of it, so
 * the versions have to match exactly rather than merely be close.
 */
bool adopt_host_context()
{
    const auto *host_version = reinterpret_cast<GetVersionFn>(require(kGetVersion))();
    if (std::strcmp(host_version, IMGUI_VERSION) != 0) {
        throw std::runtime_error{
            std::format("built against ImGui {} but the launcher runs {}", IMGUI_VERSION, host_version)};
    }

    auto *context = reinterpret_cast<GetCurrentContextFn>(require(kGetCurrentContext))();
    if (context == nullptr) {
        throw std::runtime_error{"the launcher has no ImGui context"};
    }

    // A window this copy allocates is freed by the other one, so both have to be handing out
    // the same memory.
    ImGuiMemAllocFunc alloc = nullptr;
    ImGuiMemFreeFunc release = nullptr;
    void *user_data = nullptr;
    reinterpret_cast<GetAllocatorFunctionsFn>(require(kGetAllocatorFunctions))(&alloc, &release, &user_data);
    ImGui::SetAllocatorFunctions(alloc, release, user_data);
    ImGui::SetCurrentContext(context);

    log::info("overlay is sharing the launcher's ImGui {}, press F12 for it", host_version);
    return true;
}

void render()
{
    // The launcher's frame is still open here, so the windows go into the draw data it is
    // about to submit. Drawing after the original call would be a frame late.
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
        if (ImGui::IsKeyPressed(kToggleKey, false)) {
            g_view.toggle();
        }
        g_view.draw();
    }
    g_render();
}

}  // namespace

void install_launcher_overlay()
{
    static hook::FunctionHook hook{"ImGui::Render", require(kRender), reinterpret_cast<void *>(&render),
                                   reinterpret_cast<void **>(&g_render)};
}

}  // namespace spyglass::overlay
