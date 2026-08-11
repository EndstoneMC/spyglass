#include "spyglass/overlay/d3d11_backend.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>

#include "spyglass/core/log.h"

using Microsoft::WRL::ComPtr;

namespace spyglass::overlay {

std::unique_ptr<D3D11Backend> D3D11Backend::create(IDXGISwapChain *swap_chain)
{
    ComPtr<ID3D11Device> device;
    if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device)))) {
        return nullptr;
    }

    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    if (!context) {
        return nullptr;
    }

    auto backend = std::unique_ptr<D3D11Backend>{new D3D11Backend{std::move(device), std::move(context)}};
    if (!ImGui_ImplDX11_Init(backend->device_.Get(), backend->context_.Get())) {
        return nullptr;
    }
    log::info("overlay backend: Direct3D 11");
    return backend;
}

D3D11Backend::~D3D11Backend()
{
    ImGui_ImplDX11_Shutdown();
}

void D3D11Backend::new_frame()
{
    ImGui_ImplDX11_NewFrame();
}

bool D3D11Backend::ensure_render_target(IDXGISwapChain *swap_chain)
{
    if (render_target_) {
        return true;
    }

    ComPtr<ID3D11Texture2D> back_buffer;
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
        return false;
    }
    return SUCCEEDED(device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &render_target_));
}

void D3D11Backend::render(IDXGISwapChain *swap_chain)
{
    if (!ensure_render_target(swap_chain)) {
        return;
    }

    ID3D11RenderTargetView *targets[] = {render_target_.Get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

void D3D11Backend::release_buffers()
{
    render_target_.Reset();
}

}  // namespace spyglass::overlay
