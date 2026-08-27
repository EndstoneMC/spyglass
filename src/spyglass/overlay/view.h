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
    float list_height_{0.0F};
    float details_height_{0.0F};
};

}  // namespace spyglass
