#pragma once

#ifdef _WIN32

#include <memory>

#include <d3d11.h>
#include <wrl/client.h>

#include "spyglass/overlay/windows/backend.h"

namespace spyglass {

class D3D11Backend final : public RenderBackend {
public:
    /** Returns null when the swap chain is not backed by a D3D11 device. */
    static std::unique_ptr<D3D11Backend> create(IDXGISwapChain *swap_chain);

    ~D3D11Backend() override;

    void new_frame() override;
    void render(IDXGISwapChain *swap_chain) override;
    void release_buffers() override;

private:
    using Device = Microsoft::WRL::ComPtr<ID3D11Device>;
    using Context = Microsoft::WRL::ComPtr<ID3D11DeviceContext>;

    D3D11Backend(Device device, Context context) : device_{std::move(device)}, context_{std::move(context)} {}

    bool ensure_render_target(IDXGISwapChain *swap_chain);

    Device device_;
    Context context_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> render_target_;
};

}  // namespace spyglass

#endif
