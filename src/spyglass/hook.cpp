#include "spyglass/hook.h"

#include <cstdint>
#include <exception>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <funchook.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "spyglass/error.h"
#include "spyglass/pattern.h"

namespace spyglass {

FunctionHook::FunctionHook(const std::string_view name, void *target, void *detour, void **original)
{
    funchook_ = funchook_create();
    if (funchook_ == nullptr) {
        throw std::runtime_error{std::format("{}: funchook_create failed", name)};
    }

    *original = target;
    if (funchook_prepare(funchook_, original, detour) != FUNCHOOK_ERROR_SUCCESS ||
        funchook_install(funchook_, 0) != FUNCHOOK_ERROR_SUCCESS) {
        const std::string message{funchook_error_message(funchook_)};
        reset();
        throw std::runtime_error{std::format("{}: {}", name, message)};
    }
}

FunctionHook::~FunctionHook()
{
    reset();
}

void FunctionHook::reset() noexcept
{
    if (funchook_ == nullptr) {
        return;
    }
    funchook_uninstall(funchook_, 0);
    funchook_destroy(funchook_);
    funchook_ = nullptr;
}

namespace {

std::vector<FunctionHook> g_installed;
std::vector<VtableSwap> g_swapped;
std::unordered_map<void **, void *> g_originals;
std::mutex g_mutex;

bool write_slot(void **slot, void *value)
{
#ifdef _WIN32
    DWORD previous = 0;
    if (VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &previous) == 0) {
        return false;
    }
    __atomic_store_n(slot, value, __ATOMIC_RELEASE);
    VirtualProtect(slot, sizeof(void *), previous, &previous);
#else
    const auto page = static_cast<std::uintptr_t>(sysconf(_SC_PAGESIZE));
    auto *const start = reinterpret_cast<void *>(reinterpret_cast<std::uintptr_t>(slot) & ~(page - 1));
    if (mprotect(start, page, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }
    __atomic_store_n(slot, value, __ATOMIC_RELEASE);
    mprotect(start, page, PROT_READ);
#endif
    return true;
}

}  // namespace

VtableSwap::VtableSwap(const std::string_view name, void **slot, void *detour) : slot_{slot}, original_{*slot}
{
    if (!write_slot(slot, detour)) {
        slot_ = nullptr;
        throw std::runtime_error{std::format("{}: the vtable slot could not be made writable", name)};
    }
}

VtableSwap::~VtableSwap()
{
    if (slot_ != nullptr) {
        write_slot(slot_, original_);
    }
}

void *locate(const std::string_view name, const std::string_view pattern)
{
    try {
        return find(pattern);
    }
    catch (const std::exception &e) {
        report_error(std::format("{}: {}", name, e.what()));
        return nullptr;
    }
}

void *install_hook(const std::string_view name, const std::string_view pattern, void *detour, void **original)
{
    auto *const target = locate(name, pattern);
    if (target == nullptr) {
        return nullptr;
    }
    try {
        const std::lock_guard lock{g_mutex};
        g_installed.emplace_back(name, target, detour, original);
    }
    catch (const std::exception &e) {
        report_error(e.what());
        return nullptr;
    }
    return target;
}

bool swap_vfunc(const std::string_view name, void **vtable, const std::size_t ordinal, void *detour)
{
    auto **const slot = vtable + ordinal;
    const std::lock_guard lock{g_mutex};
    const auto [entry, fresh] = g_originals.try_emplace(slot, nullptr);
    if (!fresh) {
        return true;
    }
    try {
        g_swapped.emplace_back(name, slot, detour);
        entry->second = g_swapped.back().original();
    }
    catch (const std::exception &e) {
        g_originals.erase(entry);
        report_error(e.what());
        return false;
    }
    return true;
}

void *vfunc_original(void **vtable, const std::size_t ordinal)
{
    const std::lock_guard lock{g_mutex};
    const auto entry = g_originals.find(vtable + ordinal);
    return entry == g_originals.end() ? nullptr : entry->second;
}

}  // namespace spyglass
