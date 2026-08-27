#ifdef _WIN32

#include "spyglass/overlay/windows/d3d12_backend.h"

#include <algorithm>

#include <imgui.h>
#include <imgui_impl_dx12.h>


using Microsoft::WRL::ComPtr;

namespace spyglass {
namespace {

constexpr std::uint32_t kShaderResourceDescriptors = 64;

// ImGui allocates an SRV descriptor per texture, so it needs more than a single slot.
struct DescriptorAllocator {
    ID3D12DescriptorHeap *heap{nullptr};
    std::vector<bool> *slots{nullptr};
    std::uint32_t stride{0};

    void allocate(D3D12_CPU_DESCRIPTOR_HANDLE *cpu, D3D12_GPU_DESCRIPTOR_HANDLE *gpu) const
    {
        const auto it = std::ranges::find(*slots, false);
        if (it == slots->end()) {
            *cpu = {};
            *gpu = {};
            return;
        }
        *it = true;
        const auto index = static_cast<std::uint32_t>(std::distance(slots->begin(), it));
        cpu->ptr = heap->GetCPUDescriptorHandleForHeapStart().ptr + index * stride;
        gpu->ptr = heap->GetGPUDescriptorHandleForHeapStart().ptr + index * stride;
    }

    void free(const D3D12_CPU_DESCRIPTOR_HANDLE cpu) const
    {
        const auto base = heap->GetCPUDescriptorHandleForHeapStart().ptr;
        if (cpu.ptr < base || stride == 0) {
            return;
        }
        if (const auto index = (cpu.ptr - base) / stride; index < slots->size()) {
            (*slots)[index] = false;
        }
    }
};

DescriptorAllocator g_allocator;

void allocate_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *cpu, D3D12_GPU_DESCRIPTOR_HANDLE *gpu)
{
    g_allocator.allocate(cpu, gpu);
}

void free_descriptor(ImGui_ImplDX12_InitInfo *, const D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
{
    g_allocator.free(cpu);
}

}  // namespace

std::unique_ptr<D3D12Backend> D3D12Backend::create(IDXGISwapChain *swap_chain, ID3D12CommandQueue *queue)
{
    auto backend = std::unique_ptr<D3D12Backend>{new D3D12Backend};
    if (!backend->initialise(swap_chain, queue)) {
        return nullptr;
    }
    return backend;
}

bool D3D12Backend::initialise(IDXGISwapChain *swap_chain, ID3D12CommandQueue *queue)
{
    if (queue == nullptr || FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device_)))) {
        return false;
    }
    queue_ = queue;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDesc(&desc)) || desc.BufferCount == 0) {
        return false;
    }

    const D3D12_DESCRIPTOR_HEAP_DESC render_target_desc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = desc.BufferCount,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
    };
    if (FAILED(device_->CreateDescriptorHeap(&render_target_desc, IID_PPV_ARGS(&render_target_heap_)))) {
        return false;
    }

    const D3D12_DESCRIPTOR_HEAP_DESC shader_resource_desc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = kShaderResourceDescriptors,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
    };
    if (FAILED(device_->CreateDescriptorHeap(&shader_resource_desc, IID_PPV_ARGS(&shader_resource_heap_)))) {
        return false;
    }

    const auto render_target_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    auto handle = render_target_heap_->GetCPUDescriptorHandleForHeapStart();

    frames_.resize(desc.BufferCount);
    for (auto &frame : frames_) {
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator)))) {
            return false;
        }
        frame.render_target = handle;
        handle.ptr += render_target_stride;
    }

    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_.front().allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&command_list_))) ||
        FAILED(command_list_->Close())) {
        return false;
    }

    shader_resource_slots_.assign(kShaderResourceDescriptors, false);
    shader_resource_stride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    g_allocator = {
        .heap = shader_resource_heap_.Get(),
        .slots = &shader_resource_slots_,
        .stride = shader_resource_stride_,
    };

    ImGui_ImplDX12_InitInfo info;
    info.Device = device_.Get();
    info.CommandQueue = queue_.Get();
    info.NumFramesInFlight = static_cast<int>(desc.BufferCount);
    info.RTVFormat = desc.BufferDesc.Format;
    info.DSVFormat = DXGI_FORMAT_UNKNOWN;
    info.SrvDescriptorHeap = shader_resource_heap_.Get();
    info.SrvDescriptorAllocFn = &allocate_descriptor;
    info.SrvDescriptorFreeFn = &free_descriptor;
    return ImGui_ImplDX12_Init(&info);
}

D3D12Backend::~D3D12Backend()
{
    ImGui_ImplDX12_Shutdown();
}

void D3D12Backend::new_frame()
{
    ImGui_ImplDX12_NewFrame();
}

bool D3D12Backend::ensure_buffers(IDXGISwapChain3 *swap_chain)
{
    if (frames_.front().back_buffer) {
        return true;
    }
    for (std::uint32_t i = 0; i < frames_.size(); ++i) {
        auto &frame = frames_[i];
        if (FAILED(swap_chain->GetBuffer(i, IID_PPV_ARGS(&frame.back_buffer)))) {
            return false;
        }
        device_->CreateRenderTargetView(frame.back_buffer.Get(), nullptr, frame.render_target);
    }
    return true;
}

void D3D12Backend::render(IDXGISwapChain *swap_chain)
{
    ComPtr<IDXGISwapChain3> swap_chain3;
    if (FAILED(swap_chain->QueryInterface(IID_PPV_ARGS(&swap_chain3))) || !ensure_buffers(swap_chain3.Get())) {
        return;
    }

    const auto index = swap_chain3->GetCurrentBackBufferIndex();
    if (index >= frames_.size()) {
        return;
    }
    auto &frame = frames_[index];

    if (FAILED(frame.allocator->Reset()) || FAILED(command_list_->Reset(frame.allocator.Get(), nullptr))) {
        return;
    }

    D3D12_RESOURCE_BARRIER barrier{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition =
            {
                .pResource = frame.back_buffer.Get(),
                .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
                .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET,
            },
    };
    command_list_->ResourceBarrier(1, &barrier);

    ID3D12DescriptorHeap *heaps[] = {shader_resource_heap_.Get()};
    command_list_->SetDescriptorHeaps(1, heaps);
    command_list_->OMSetRenderTargets(1, &frame.render_target, FALSE, nullptr);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), command_list_.Get());

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    command_list_->ResourceBarrier(1, &barrier);

    if (SUCCEEDED(command_list_->Close())) {
        ID3D12CommandList *lists[] = {command_list_.Get()};
        queue_->ExecuteCommandLists(1, lists);
    }
}

void D3D12Backend::release_buffers()
{
    for (auto &frame : frames_) {
        frame.back_buffer.Reset();
    }
}

}  // namespace spyglass

#endif
