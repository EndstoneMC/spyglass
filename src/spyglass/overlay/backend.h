#pragma once

#include <dxgi.h>

namespace spyglass::overlay {

/**
 * Draws the current ImGui frame onto the swap chain's back buffer. Implementations
 * run inside the game's Present call, on the game's render thread, and must leave
 * the device state as they found it.
 */
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

    /** Drops every view into the swap chain's buffers, before the game resizes them. */
    virtual void release_buffers() = 0;
};

}  // namespace spyglass::overlay
