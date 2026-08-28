#pragma once

#ifdef _WIN32

#include <atomic>
#include <bitset>
#include <cstdint>

#include <Windows.h>

namespace spyglass {

class InputHook {
public:
    bool attach(HWND window);
    void detach();

    void set_cursor_free(bool cursor_free) noexcept { cursor_free_.store(cursor_free, std::memory_order_relaxed); }

    [[nodiscard]] HWND window() const noexcept { return window_; }
    [[nodiscard]] bool cursor_free() const noexcept { return cursor_free_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t seen() const noexcept { return seen_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t seen_input() const noexcept { return seen_input_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t eaten() const noexcept { return eaten_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t pointer() const noexcept { return pointer_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t raw() const noexcept { return raw_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool installed() const noexcept
    {
        return window_ != nullptr &&
               reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window_, GWLP_WNDPROC)) == &InputHook::dispatch;
    }

private:
    static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    LRESULT process(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    HWND window_{nullptr};
    WNDPROC original_{nullptr};
    unsigned mouse_down_{0};
    std::bitset<256> keys_down_;
    std::atomic<bool> cursor_free_{false};
    std::atomic<std::uint64_t> seen_{0};
    std::atomic<std::uint64_t> seen_input_{0};
    std::atomic<std::uint64_t> eaten_{0};
    std::atomic<std::uint64_t> pointer_{0};
    std::atomic<std::uint64_t> raw_{0};
};

}  // namespace spyglass

#endif
