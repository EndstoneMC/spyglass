#include "spyglass/overlay/overlay.h"

#include <atomic>
#include <optional>

#include <d3d11.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_win32.h>

#include "spyglass/core/config.h"
#include "spyglass/core/log.h"
#include "spyglass/overlay/d3d11_backend.h"
#include "spyglass/overlay/d3d12_backend.h"

using Microsoft::WRL::ComPtr;

namespace spyglass::overlay {
namespace {

// IDXGISwapChain: IUnknown(3) + IDXGIObject(4) + IDXGIDeviceSubObject(1) = 8.
constexpr std::size_t kPresentSlot = 8;
constexpr std::size_t kResizeBuffersSlot = 13;
// ID3D12CommandQueue: IUnknown(3) + ID3D12Object(4) + ID3D12DeviceChild(1) = 8.
constexpr std::size_t kExecuteCommandListsSlot = 10;

using PresentFn = HRESULT(IDXGISwapChain *, UINT, UINT);
using ResizeBuffersFn = HRESULT(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

std::atomic<PresentFn *> g_present{nullptr};
std::atomic<ResizeBuffersFn *> g_resize_buffers{nullptr};
std::atomic<ExecuteCommandListsFn *> g_execute_command_lists{nullptr};

HRESULT present_detour(IDXGISwapChain *swap_chain, const UINT sync_interval, const UINT flags)
{
    Overlay::instance().present(swap_chain);
    return g_present.load(std::memory_order_relaxed)(swap_chain, sync_interval, flags);
}

HRESULT resize_buffers_detour(IDXGISwapChain *swap_chain, const UINT buffer_count, const UINT width, const UINT height,
                              const DXGI_FORMAT format, const UINT flags)
{
    Overlay::instance().before_resize();
    return g_resize_buffers.load(std::memory_order_relaxed)(swap_chain, buffer_count, width, height, format, flags);
}

void execute_command_lists_detour(ID3D12CommandQueue *queue, const UINT count, ID3D12CommandList *const *lists)
{
    Overlay::instance().observe_command_queue(queue);
    g_execute_command_lists.load(std::memory_order_relaxed)(queue, count, lists);
}

void **vtable_of(void *object)
{
    return *static_cast<void ***>(object);
}

/** A hidden window is enough to create the throwaway swap chain we read the vtable from. */
struct ProbeWindow {
    HWND handle{nullptr};

    ProbeWindow()
    {
        handle = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, nullptr,
                                 nullptr);
    }
    ~ProbeWindow()
    {
        if (handle != nullptr) {
            DestroyWindow(handle);
        }
    }
    ProbeWindow(const ProbeWindow &) = delete;
    ProbeWindow &operator=(const ProbeWindow &) = delete;
};

std::optional<std::pair<void *, void *>> probe_swap_chain()
{
    const ProbeWindow window;
    if (window.handle == nullptr) {
        return std::nullopt;
    }

    DXGI_SWAP_CHAIN_DESC desc{
        .BufferDesc = {.Width = 8, .Height = 8, .Format = DXGI_FORMAT_R8G8B8A8_UNORM},
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .OutputWindow = window.handle,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    };

    ComPtr<IDXGISwapChain> swap_chain;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels,
                                             static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION, &desc,
                                             &swap_chain, &device, nullptr, &context))) {
        return std::nullopt;
    }

    auto **vtable = vtable_of(swap_chain.Get());
    return std::pair{vtable[kPresentSlot], vtable[kResizeBuffersSlot]};
}

void *probe_command_queue()
{
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) {
        return nullptr;
    }

    constexpr D3D12_COMMAND_QUEUE_DESC desc{.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ComPtr<ID3D12CommandQueue> queue;
    if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) {
        return nullptr;
    }
    return vtable_of(queue.Get())[kExecuteCommandListsSlot];
}

}  // namespace

Overlay &Overlay::instance()
{
    static Overlay overlay;
    return overlay;
}

void Overlay::install()
{
    if (auto *execute = probe_command_queue()) {
        execute_hook_ = hook::FunctionHook{"ID3D12CommandQueue::ExecuteCommandLists", execute,
                                           reinterpret_cast<void *>(&execute_command_lists_detour),
                                           reinterpret_cast<void **>(&g_execute_command_lists)};
    }
    else {
        log::info("Direct3D 12 is unavailable, the overlay will look for a Direct3D 11 device");
    }

    const auto swap_chain = probe_swap_chain();
    if (!swap_chain) {
        throw std::runtime_error{"could not create a probe swap chain to read the DXGI vtable from"};
    }

    resize_hook_ = hook::FunctionHook{"IDXGISwapChain::ResizeBuffers", swap_chain->second,
                                      reinterpret_cast<void *>(&resize_buffers_detour),
                                      reinterpret_cast<void **>(&g_resize_buffers)};
    present_hook_ = hook::FunctionHook{"IDXGISwapChain::Present", swap_chain->first,
                                       reinterpret_cast<void *>(&present_detour),
                                       reinterpret_cast<void **>(&g_present)};
}

void Overlay::shutdown()
{
    present_hook_ = {};
    resize_hook_ = {};
    execute_hook_ = {};
    backend_.reset();
    input_.detach();
    if (context_ready_) {
        ImGui::DestroyContext();
        context_ready_ = false;
    }
}

void Overlay::observe_command_queue(ID3D12CommandQueue *queue)
{
    if (command_queue_ != nullptr || queue == nullptr) {
        return;
    }
    if (queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        command_queue_ = queue;
    }
}

void Overlay::create_context()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The game hides the OS cursor and keeps it captured, so ImGui draws its own.
    io.MouseDrawCursor = true;
    settings_path_ = (config().output_directory / "overlay.ini").string();
    io.IniFilename = settings_path_.c_str();
    context_ready_ = true;
}

bool Overlay::ensure_ready(IDXGISwapChain *swap_chain)
{
    if (backend_) {
        return true;
    }
    if (unsupported_) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDesc(&desc)) || desc.OutputWindow == nullptr) {
        return false;
    }

    if (!context_ready_) {
        create_context();
    }
    input_.attach(desc.OutputWindow, {
                                         .visible = [this] { return view_.visible(); },
                                         .toggle = [this] { view_.toggle(); },
                                     });

    backend_ = D3D12Backend::create(swap_chain, command_queue_);
    if (!backend_) {
        backend_ = D3D11Backend::create(swap_chain);
    }
    if (!backend_) {
        // A D3D12 swap chain whose queue has not run yet is normal for the first few
        // frames; only give up once the swap chain turns out to be neither.
        ComPtr<ID3D12Device> device;
        if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device)))) {
            unsupported_ = true;
            log::error("the swap chain is neither Direct3D 11 nor Direct3D 12, the overlay is disabled");
        }
        return false;
    }
    return true;
}

void Overlay::present(IDXGISwapChain *swap_chain)
{
    if (!ensure_ready(swap_chain)) {
        return;
    }

    backend_->new_frame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    view_.draw();
    ImGui::Render();
    backend_->render(swap_chain);
}

void Overlay::before_resize()
{
    if (backend_) {
        backend_->release_buffers();
    }
}

}  // namespace spyglass::overlay
