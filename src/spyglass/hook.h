#pragma once

#include <string_view>
#include <utility>

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
