#pragma once

#include <memory>
#include <string>

#include <d3d12.h>
#include <dxgi.h>

#include "spyglass/hook/function_hook.h"
#include "spyglass/overlay/backend.h"
#include "spyglass/overlay/input.h"
#include "spyglass/overlay/view.h"

namespace spyglass::overlay {

/**
 * Owns the ImGui context and everything that feeds it. The swap chain vtable is
 * read from a throwaway device at startup, so the game's own swap chain is covered
 * whether it already exists or not.
 */
class Overlay {
public:
    static Overlay &instance();

    void install();
    void shutdown();

    void present(IDXGISwapChain *swap_chain);
    void before_resize();
    void observe_command_queue(ID3D12CommandQueue *queue);

private:
    bool ensure_ready(IDXGISwapChain *swap_chain);
    void create_context();
    void follow_window_dpi();

    hook::FunctionHook present_hook_;
    hook::FunctionHook resize_hook_;
    hook::FunctionHook execute_hook_;

    std::unique_ptr<RenderBackend> backend_;
    InputHook input_;
    View view_;

    ID3D12CommandQueue *command_queue_{nullptr};
    HWND window_{nullptr};
    float dpi_scale_{0.0F};
    std::string settings_path_;
    bool context_ready_{false};
    bool unsupported_{false};
};

}  // namespace spyglass::overlay
