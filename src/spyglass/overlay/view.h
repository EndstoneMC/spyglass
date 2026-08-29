#pragma once

#include <cstdint>
#include <vector>

#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/options.h"
#include "spyglass/overlay/pane/expert_window.h"
#include "spyglass/overlay/pane/filter_window.h"
#include "spyglass/overlay/pane/packet_bytes.h"
#include "spyglass/overlay/pane/packet_list.h"

namespace spyglass {

struct DetachedPacket {
    std::uint64_t number{0};
    bool open{true};
    BytesView bytes;
};

class View {
public:
    static View &getInstance();

    void draw();

    void onPacketSend(Incoming incoming);
    void onPacketReceive(Incoming incoming);

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void set_interactive(const bool interactive) noexcept { interactive_ = interactive; }

private:
    Capture capture_;
    Filter filter_;
    ViewOptions options_;
    bool visible_{true};
    bool interactive_{true};
    float list_share_{0.45F};
    float details_share_{0.30F};
    PacketList list_;
    FilterWindow filter_window_;
    ExpertWindow expert_window_;
    BytesView bytes_view_;
    std::vector<DetachedPacket> detached_;
};

}  // namespace spyglass
