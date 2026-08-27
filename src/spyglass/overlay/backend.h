#pragma once

#include <dxgi.h>

namespace spyglass::overlay {

class RenderBackend {
public:
    RenderBackend() = default;
    virtual ~RenderBackend() = default;
    RenderBackend(const RenderBackend &) = delete;
    RenderBackend &operator=(const RenderBackend &) = delete;
    RenderBackend(RenderBackend &&) = delete;
    RenderBackend &operator=(RenderBackend &&) = delete;

    virtual void new_frame() = 0;
    virtual void render(IDXGISwapChain *swap_chain) = 0;
    virtual void release_buffers() = 0;
};

}  // namespace spyglass::overlay
