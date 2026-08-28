#pragma once

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

}  // namespace spyglass

#define SPYGLASS_CALL_ORIGINAL(fp, original, ...) std::invoke(spyglass::detail::fp_cast(fp, original), ##__VA_ARGS__)

#define SPYGLASS_CALL_ORIGINAL_CTOR(original, ...) spyglass::detail::ctor_cast<__VA_ARGS__>(original)
