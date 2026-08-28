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

}  // namespace Bedrock
