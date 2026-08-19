#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include "spyglass/overlay/backend.h"

namespace spyglass::overlay {

// One command allocator per back buffer, reset only when its buffer comes round again.
class D3D12Backend final : public RenderBackend {
public:
    /** Returns null unless the swap chain is D3D12 and a direct queue was captured. */
    static std::unique_ptr<D3D12Backend> create(IDXGISwapChain *swap_chain, ID3D12CommandQueue *queue);

    ~D3D12Backend() override;

    void new_frame() override;
    void render(IDXGISwapChain *swap_chain) override;
    void release_buffers() override;

private:
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
        D3D12_CPU_DESCRIPTOR_HANDLE render_target{};
    };

    D3D12Backend() = default;

    bool initialise(IDXGISwapChain *swap_chain, ID3D12CommandQueue *queue);
    bool ensure_buffers(IDXGISwapChain3 *swap_chain);

    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> render_target_heap_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> shader_resource_heap_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;
    std::vector<Frame> frames_;
    std::vector<bool> shader_resource_slots_;
    std::uint32_t shader_resource_stride_{0};
};

}  // namespace spyglass::overlay
