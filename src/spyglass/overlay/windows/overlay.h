#pragma once

#ifdef _WIN32

#include <memory>

#include <d3d12.h>
#include <dxgi.h>

#include "spyglass/hook.h"
#include "spyglass/overlay/windows/backend.h"
#include "spyglass/overlay/windows/input.h"
#include "spyglass/overlay/view.h"

namespace spyglass {

class Overlay {
public:
    static Overlay &instance();

    void install();
    void shutdown();

    [[nodiscard]] bool owns_mouse() const;

    void present(IDXGISwapChain *swap_chain);
    void before_resize();
    void observe_command_queue(ID3D12CommandQueue *queue);

private:
    bool ensure_ready(IDXGISwapChain *swap_chain);
    void create_context();
    void follow_window_dpi();

    FunctionHook present_hook_;
    FunctionHook resize_hook_;
    FunctionHook execute_hook_;

    std::unique_ptr<RenderBackend> backend_;
    InputHook input_;

    ID3D12CommandQueue *command_queue_{nullptr};
    HWND window_{nullptr};
    float dpi_scale_{0.0F};
    bool context_ready_{false};
    bool unsupported_{false};
    bool insert_down_{false};
};

}  // namespace spyglass

#endif
