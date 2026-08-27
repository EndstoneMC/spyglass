#pragma once

#include "spyglass/overlay/capture.h"

namespace spyglass {

class View {
public:
    void draw();

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    Capture capture_;
    bool visible_{true};
    float list_share_{0.45F};
    float details_share_{0.30F};
};

}  // namespace spyglass
