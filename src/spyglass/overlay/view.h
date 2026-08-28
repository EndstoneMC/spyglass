#pragma once

#include <string_view>

#include "spyglass/overlay/capture.h"

namespace spyglass {

class View {
public:
    static View &getInstance();

    void draw();

    void onPacketSend(std::string_view data);
    void onPacketReceive(std::string_view data);

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    Capture capture_;
    bool visible_{true};
    float list_share_{0.45F};
    float details_share_{0.30F};
    bool errors_open_{true};
};

}  // namespace spyglass
