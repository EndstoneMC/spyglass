#pragma once

#include <cstddef>
#include <functional>
#include <string_view>
#include <utility>

#include "spyglass/detail.h"

using funchook_t = struct funchook;

namespace spyglass {

class FunctionHook {
public:
    FunctionHook() = default;
    FunctionHook(std::string_view name, void *target, void *detour, void **original);
    ~FunctionHook();

    FunctionHook(FunctionHook &&other) noexcept : funchook_{std::exchange(other.funchook_, nullptr)} {}

    FunctionHook &operator=(FunctionHook &&other) noexcept
    {
        if (this != &other) {
            reset();
            funchook_ = std::exchange(other.funchook_, nullptr);
        }
        return *this;
    }

    FunctionHook(const FunctionHook &) = delete;
    FunctionHook &operator=(const FunctionHook &) = delete;

private:
    void reset() noexcept;

    funchook_t *funchook_{nullptr};
};

class VtableSwap {
public:
    VtableSwap(std::string_view name, void **slot, void *detour);
    ~VtableSwap();

    VtableSwap(VtableSwap &&other) noexcept
        : slot_{std::exchange(other.slot_, nullptr)}, original_{other.original_}
    {
    }

    VtableSwap &operator=(VtableSwap &&other) noexcept
    {
        if (this != &other) {
            slot_ = std::exchange(other.slot_, nullptr);
            original_ = other.original_;
        }
        return *this;
    }

    VtableSwap(const VtableSwap &) = delete;
    VtableSwap &operator=(const VtableSwap &) = delete;

    [[nodiscard]] void *original() const { return original_; }

private:
    void **slot_{nullptr};
    void *original_{nullptr};
};

void *locate(std::string_view name, std::string_view pattern);

void *install_hook(std::string_view name, std::string_view pattern, void *detour, void **original);

bool swap_vfunc(std::string_view name, void **vtable, std::size_t ordinal, void *detour);

void *vfunc_original(void **vtable, std::size_t ordinal);

}  // namespace spyglass

#define SPYGLASS_CALL_ORIGINAL(fp, original, ...) std::invoke(spyglass::detail::fp_cast(fp, original), ##__VA_ARGS__)

#define SPYGLASS_CALL_ORIGINAL_CTOR(original, ...) spyglass::detail::ctor_cast<__VA_ARGS__>(original)
