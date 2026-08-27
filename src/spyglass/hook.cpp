#include "spyglass/hook.h"

#include <format>
#include <stdexcept>
#include <string>

#include <funchook.h>


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

}  // namespace spyglass
