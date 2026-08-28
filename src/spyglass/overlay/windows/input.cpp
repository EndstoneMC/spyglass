#ifdef _WIN32

#include "spyglass/overlay/windows/input.h"

#include <cstddef>

#include <imgui.h>
#include <imgui_impl_win32.h>


extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM w_param,
                                                             LPARAM l_param);

namespace spyglass {
namespace {

InputHook *g_hook = nullptr;

}  // namespace

bool InputHook::attach(const HWND window)
{
    if (window_ != nullptr) {
        return true;
    }
    if (window == nullptr) {
        return false;
    }

    original_ = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if (original_ == nullptr) {
        return false;
    }

    window_ = window;
    g_hook = this;
    if (SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&InputHook::dispatch)) == 0) {
        window_ = nullptr;
        original_ = nullptr;
        g_hook = nullptr;
        return false;
    }

    ImGui_ImplWin32_Init(window);
    return true;
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
    mouse_down_ = 0;
    keys_down_.reset();
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
    seen_.fetch_add(1, std::memory_order_relaxed);
    if ((message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) || (message >= WM_KEYFIRST && message <= WM_KEYLAST)) {
        seen_input_.fetch_add(1, std::memory_order_relaxed);
    }
    if (message >= WM_POINTERUPDATE && message <= WM_POINTERLEAVE) {
        pointer_.fetch_add(1, std::memory_order_relaxed);
    }
    if (message == WM_INPUT) {
        raw_.fetch_add(1, std::memory_order_relaxed);
    }

    if (message == WM_NCMOUSEMOVE || message == WM_MOUSELEAVE || message == WM_NCMOUSELEAVE ||
        message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_INPUTLANGCHANGE ||
        message == WM_DEVICECHANGE) {
        if (message == WM_KILLFOCUS) {
            mouse_down_ = 0;
            keys_down_.reset();
        }
        ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
        return CallWindowProcW(original_, window, message, w_param, l_param);
    }

    const ImGuiIO &io = ImGui::GetIO();
    const bool cursor_free = cursor_free_.load(std::memory_order_relaxed);

    switch (message) {
    case WM_MOUSEMOVE:
        ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
        if (cursor_free && io.WantCaptureMouse) {
            eaten_.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONDBLCLK:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: {
        unsigned bit = 1U;
        if (message == WM_RBUTTONDOWN || message == WM_RBUTTONDBLCLK || message == WM_RBUTTONUP) {
            bit = 1U << 1U;
        }
        if (message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK || message == WM_MBUTTONUP) {
            bit = 1U << 2U;
        }
        if (message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK || message == WM_XBUTTONUP) {
            bit = GET_XBUTTON_WPARAM(w_param) == XBUTTON1 ? 1U << 3U : 1U << 4U;
        }

        const bool down =
            message != WM_LBUTTONUP && message != WM_RBUTTONUP && message != WM_MBUTTONUP && message != WM_XBUTTONUP;
        if (down ? cursor_free && io.WantCaptureMouse : (mouse_down_ & bit) != 0) {
            mouse_down_ = down ? mouse_down_ | bit : mouse_down_ & ~bit;
            eaten_.fetch_add(1, std::memory_order_relaxed);
            ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
            return 0;
        }
        break;
    }
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
        if (cursor_free && io.WantCaptureMouse) {
            ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (cursor_free && io.WantCaptureMouse && LOWORD(l_param) == HTCLIENT) {
            ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
            return TRUE;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        const auto key = static_cast<std::size_t>(w_param) & 0xFFU;
        if (key == VK_INSERT) {
            return 0;
        }
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (down ? cursor_free && io.WantCaptureKeyboard : keys_down_.test(key)) {
            keys_down_.set(key, down);
            ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
            return 0;
        }
        break;
    }
    case WM_CHAR:
        if (cursor_free && io.WantCaptureKeyboard) {
            ImGui_ImplWin32_WndProcHandler(window, message, w_param, l_param);
            return 0;
        }
        break;
    default:
        break;
    }

    return CallWindowProcW(original_, window, message, w_param, l_param);
}

}  // namespace spyglass

#endif
