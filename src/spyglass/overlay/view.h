#pragma once

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/pane/packet_list.h"

namespace spyglass {

class View {
public:
    static View &getInstance();

    void draw();

    void onPacketSend(Record record);
    void onPacketReceive(Record record);

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    Capture capture_;
    bool visible_{true};
    float list_share_{0.45F};
    float details_share_{0.30F};
    ListScroll list_scroll_;
    bool errors_open_{true};
};

}  // namespace spyglass
