#include "spyglass/hook/function_hook.h"

#include <format>
#include <stdexcept>
#include <unordered_map>

#include <funchook.h>

#include "spyglass/core/log.h"

namespace spyglass::hook {
namespace {

std::unordered_map<Target, void *> &originals()
{
    static std::unordered_map<Target, void *> originals;
    return originals;
}

}  // namespace

FunctionHook::FunctionHook(const std::string_view name, void *target, void *detour, void **original_out)
    : original_{target}
{
    funchook_ = funchook_create();
    if (funchook_ == nullptr) {
        throw std::runtime_error{std::format("{}: funchook_create failed", name)};
    }

    if (funchook_prepare(funchook_, &original_, detour) != FUNCHOOK_ERROR_SUCCESS) {
        const std::string message{funchook_error_message(funchook_)};
        reset();
        throw std::runtime_error{std::format("{}: {}", name, message)};
    }
    if (original_out != nullptr) {
        *original_out = original_;
    }
    if (funchook_install(funchook_, 0) != FUNCHOOK_ERROR_SUCCESS) {
        const std::string message{funchook_error_message(funchook_)};
        reset();
        throw std::runtime_error{std::format("{}: {}", name, message)};
    }
    log::info("hooked {} at {}", name, target);
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
    original_ = nullptr;
}

void create(const Target target, void *detour)
{
    const auto &info = describe(target);
    static std::unordered_map<Target, FunctionHook> hooks;
    auto hook = FunctionHook{info.name, resolve(info), detour};
    originals().insert_or_assign(target, hook.original());
    hooks.insert_or_assign(target, std::move(hook));
}

void *original(const Target target)
{
    const auto it = originals().find(target);
    if (it == originals().end()) {
        throw std::runtime_error{std::format("{}: original function not found", describe(target).name)};
    }
    return it->second;
}

}  // namespace spyglass::hook
