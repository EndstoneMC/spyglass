#ifdef _WIN32

#include "spyglass/overlay/windows/overlay.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_win32.h>

#include "spyglass/overlay/install.h"
#include "spyglass/overlay/windows/d3d11_backend.h"
#include "spyglass/overlay/windows/d3d12_backend.h"
#include "spyglass/overlay/windows/mouse.h"

using Microsoft::WRL::ComPtr;

namespace spyglass {
namespace {

constexpr bool kInputDiagnostics = false;

constexpr std::size_t kPresentSlot = 8;
constexpr std::size_t kResizeBuffersSlot = 13;
constexpr std::size_t kExecuteCommandListsSlot = 10;

using PresentFn = HRESULT(IDXGISwapChain *, UINT, UINT);
using ResizeBuffersFn = HRESULT(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);

PresentFn *g_present = nullptr;
ResizeBuffersFn *g_resize_buffers = nullptr;
ExecuteCommandListsFn *g_execute_command_lists = nullptr;

HRESULT present_detour(IDXGISwapChain *swap_chain, const UINT sync_interval, const UINT flags)
{
    Overlay::instance().present(swap_chain);
    return g_present(swap_chain, sync_interval, flags);
}

HRESULT resize_buffers_detour(IDXGISwapChain *swap_chain, const UINT buffer_count, const UINT width, const UINT height,
                              const DXGI_FORMAT format, const UINT flags)
{
    Overlay::instance().before_resize();
    return g_resize_buffers(swap_chain, buffer_count, width, height, format, flags);
}

void execute_command_lists_detour(ID3D12CommandQueue *queue, const UINT count, ID3D12CommandList *const *lists)
{
    Overlay::instance().observe_command_queue(queue);
    g_execute_command_lists(queue, count, lists);
}

struct ProbeWindow {
    HWND handle{nullptr};

    ProbeWindow()
    {
        handle =
            CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr, nullptr, nullptr);
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

    auto **vtable = *reinterpret_cast<void ***>(swap_chain.Get());
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
    auto **vtable = *reinterpret_cast<void ***>(queue.Get());
    return vtable[kExecuteCommandListsSlot];
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
        execute_hook_ = FunctionHook{"ID3D12CommandQueue::ExecuteCommandLists", execute,
                                           reinterpret_cast<void *>(&execute_command_lists_detour),
                                           reinterpret_cast<void **>(&g_execute_command_lists)};
    }

    const auto swap_chain = probe_swap_chain();
    if (!swap_chain) {
        throw std::runtime_error{"could not create a probe swap chain to read the DXGI vtable from"};
    }

    resize_hook_ = FunctionHook{"IDXGISwapChain::ResizeBuffers", swap_chain->second,
                                      reinterpret_cast<void *>(&resize_buffers_detour),
                                      reinterpret_cast<void **>(&g_resize_buffers)};
    present_hook_ =
        FunctionHook{"IDXGISwapChain::Present", swap_chain->first, reinterpret_cast<void *>(&present_detour),
                           reinterpret_cast<void **>(&g_present)};

    install_mouse_hook();
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

bool Overlay::owns_mouse() const
{
    return context_ready_ && input_.cursor_free() && ImGui::GetIO().WantCaptureMouse;
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
    io.IniFilename = nullptr;

    std::wstring executable(MAX_PATH, L'\0');
    executable.resize(GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size())));
    const auto mojangles =
        std::filesystem::path{executable}.parent_path() / "data" / "fonts" / "Mojangles.ttf";

    ImFontConfig font;
    font.Flags |= ImFontFlags_NoLoadError;
    font.OversampleH = 1;
    font.OversampleV = 1;
    font.PixelSnapH = true;
    io.Fonts->AddFontFromFileTTF(mojangles.string().c_str(), 11.0F, &font);

    context_ready_ = true;
}

void Overlay::follow_window_dpi()
{
    const auto scale = ImGui_ImplWin32_GetDpiScaleForHwnd(window_);
    if (scale == dpi_scale_) {
        return;
    }

    ImGuiStyle from_defaults;
    ImGui::StyleColorsDark(&from_defaults);
    from_defaults.ScaleAllSizes(scale);
    from_defaults.FontScaleDpi = scale;
    ImGui::GetStyle() = from_defaults;
    dpi_scale_ = scale;
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

    window_ = desc.OutputWindow;
    if (!context_ready_) {
        create_context();
    }
    if (!input_.attach(desc.OutputWindow)) {
        return false;
    }

    backend_ = D3D12Backend::create(swap_chain, command_queue_);
    if (!backend_) {
        backend_ = D3D11Backend::create(swap_chain);
    }
    if (!backend_) {
        ComPtr<ID3D12Device> device;
        const bool waiting_for_the_command_queue = SUCCEEDED(swap_chain->GetDevice(IID_PPV_ARGS(&device)));
        if (!waiting_for_the_command_queue) {
            unsupported_ = true;
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

    const bool insert_down = GetForegroundWindow() == window_ && (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
    if (insert_down && !insert_down_) {
        View::getInstance().toggle();
    }
    insert_down_ = insert_down;

    follow_window_dpi();

    CURSORINFO cursor{.cbSize = sizeof(CURSORINFO)};
    const bool os_cursor_visible =
        GetCursorInfo(&cursor) != 0 && (cursor.flags & CURSOR_SHOWING) != 0 && cursor.hCursor != nullptr;
    ImGui::GetIO().MouseDrawCursor = !os_cursor_visible;
    input_.set_cursor_free(os_cursor_visible);
    View::getInstance().set_interactive(os_cursor_visible);

    backend_->new_frame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    View::getInstance().draw();
    if (kInputDiagnostics) {
        ImGui::SetNextWindowBgAlpha(0.6F);
        if (ImGui::Begin("spyglass: input", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav)) {
            ImGui::Text("%s hwnd %p subclass %d msg %llu input %llu eaten %llu ptr %llu raw %llu capture %d/%d "
                        "cursor %d",
                        __TIME__, static_cast<void *>(input_.window()), input_.installed() ? 1 : 0, input_.seen(),
                        input_.seen_input(), input_.eaten(), input_.pointer(), input_.raw(),
                        ImGui::GetIO().WantCaptureMouse ? 1 : 0, ImGui::GetIO().WantCaptureKeyboard ? 1 : 0,
                        os_cursor_visible ? 1 : 0);
        }
        ImGui::End();
    }
    ImGui::Render();
    backend_->render(swap_chain);
}

void Overlay::before_resize()
{
    if (backend_) {
        backend_->release_buffers();
    }
}

void install_overlay()
{
    Overlay::instance().install();
}

}  // namespace spyglass

#endif
