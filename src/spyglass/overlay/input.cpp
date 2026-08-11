#include "spyglass/overlay/input.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

#include "spyglass/core/log.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM w_param,
                                                             LPARAM l_param);

namespace spyglass::overlay {
namespace {

constexpr int kToggleKey = VK_INSERT;

InputHook *g_hook = nullptr;

bool is_input_message(const UINT message)
{
    return (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
           (message >= WM_KEYFIRST && message <= WM_KEYLAST) || message == WM_MOUSEHWHEEL ||
           message == WM_SETCURSOR || message == WM_INPUT;
}

}  // namespace

void InputHook::attach(const HWND window, Callbacks callbacks)
{
    if (window_ != nullptr || window == nullptr) {
        return;
    }
    callbacks_ = std::move(callbacks);
    window_ = window;
    g_hook = this;
    original_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InputHook::dispatch)));
    if (original_ == nullptr) {
        window_ = nullptr;
        g_hook = nullptr;
        log::error("could not subclass the game window, the overlay will not take input");
        return;
    }
    ImGui_ImplWin32_Init(window);
    log::info("overlay input attached, press INSERT to open");
}

void InputHook::detach()
{
    if (window_ == nullptr) {
        return;
    }
    SetWindowLongPtrW(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
    ImGui_ImplWin32_Shutdown();
    window_ = nullptr;
    original_ = nullptr;
    g_hook = nullptr;
}

LRESULT CALLBACK InputHook::dispatch(const HWND window, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
    if (g_hook != nullptr) {
        return g_hook->process(window, message, w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT InputHook::process(const HWND window, const UINT message, const WPARAM w_param, const LPARAM l_param)
{
    if (message == WM_KEYDOWN && static_cast<int>(w_param) == kToggleKey) {
        callbacks_.toggle();
        return 0;
    }

    if (callbacks_.visible()) {
        ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
        if (is_input_message(message)) {
            return message == WM_SETCURSOR ? TRUE : 0;
        }
    }
    return CallWindowProcW(original_, window, message, w_param, l_param);
}

}  // namespace spyglass::overlay
