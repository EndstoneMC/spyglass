#pragma once

#include <functional>

#include <Windows.h>

namespace spyglass::overlay {

// Subclasses the game window so ImGui sees input, and swallows what the overlay uses.
class InputHook {
public:
    struct Callbacks {
        std::function<bool()> visible;
        std::function<void()> toggle;
    };

    void attach(HWND window, Callbacks callbacks);
    void detach();

    [[nodiscard]] HWND window() const noexcept { return window_; }

private:
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT process(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    HWND window_{nullptr};
    WNDPROC original_{nullptr};
    Callbacks callbacks_;
};

}  // namespace spyglass::overlay
