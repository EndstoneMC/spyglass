#pragma once

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/pane/filter_window.h"
#include "spyglass/overlay/pane/packet_bytes.h"
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
    void set_interactive(const bool interactive) noexcept { interactive_ = interactive; }

private:
    Capture capture_;
    Filter filter_;
    bool visible_{true};
    bool interactive_{true};
    float list_share_{0.45F};
    float details_share_{0.30F};
    PacketList list_;
    FilterWindow filter_window_;
    BytesView bytes_view_;
    bool errors_open_{true};
    bool filter_open_{false};
};

}  // namespace spyglass
