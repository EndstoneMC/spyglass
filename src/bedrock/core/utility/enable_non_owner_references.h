#pragma once

#include <memory>

namespace Bedrock {

class EnableNonOwnerReferences {
public:
    virtual ~EnableNonOwnerReferences() = default;

    struct ControlBlock {
        bool mIsValid{false};
    };

    [[nodiscard]] const std::shared_ptr<ControlBlock> &controlBlock() const { return mControlBlock; }

private:
    std::shared_ptr<ControlBlock> mControlBlock;
};

static_assert(sizeof(EnableNonOwnerReferences) == 24);

template <typename T> class NonOwnerPointer {
public:
    [[nodiscard]] T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    std::shared_ptr<EnableNonOwnerReferences::ControlBlock> control_block_;
    T *ptr_{nullptr};
};

}  // namespace Bedrock
