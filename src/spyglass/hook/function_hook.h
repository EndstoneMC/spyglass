#pragma once

#include <functional>
#include <string_view>
#include <utility>

#include "spyglass/hook/detail.h"
#include "spyglass/hook/target.h"

using funchook_t = struct funchook;

namespace spyglass::hook {

/**
 * One installed detour. Owns its funchook instance and uninstalls on destruction,
 * so a hook that fails to install never leaves a half-patched function behind.
 */
class FunctionHook {
public:
    FunctionHook() = default;

    /**
     * `original_out` is written between prepare and install, so a detour that fires
     * on the very first call after installation already has somewhere to forward to.
     */
    FunctionHook(std::string_view name, void *target, void *detour, void **original_out = nullptr);
    ~FunctionHook();

    FunctionHook(FunctionHook &&other) noexcept
        : funchook_{std::exchange(other.funchook_, nullptr)}, original_{std::exchange(other.original_, nullptr)}
    {
    }

    FunctionHook &operator=(FunctionHook &&other) noexcept
    {
        if (this != &other) {
            reset();
            funchook_ = std::exchange(other.funchook_, nullptr);
            original_ = std::exchange(other.original_, nullptr);
        }
        return *this;
    }

    FunctionHook(const FunctionHook &) = delete;
    FunctionHook &operator=(const FunctionHook &) = delete;

    [[nodiscard]] explicit operator bool() const noexcept { return funchook_ != nullptr; }
    [[nodiscard]] void *original() const noexcept { return original_; }

    /** The original, already cast back to the signature it was hooked with. */
    template <typename Signature>
    [[nodiscard]] Signature *original_as() const noexcept
    {
        return reinterpret_cast<Signature *>(original_);
    }

private:
    void reset() noexcept;

    funchook_t *funchook_{nullptr};
    void *original_{nullptr};
};

/** Resolves `target` by pattern, installs `detour`, and remembers the original. */
void create(Target target, void *detour);
void *original(Target target);

}  // namespace spyglass::hook

#define SPYGLASS_CALL_ORIGINAL(target, fp, ...) \
    std::invoke(::spyglass::detail::fp_cast(fp, ::spyglass::hook::original(target)), __VA_ARGS__)
