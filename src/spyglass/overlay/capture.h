#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace spyglass {

struct Packet {
    int number;
    double time;
    std::string_view source;
    std::string_view destination;
    int length;
    std::string_view info;
    bool bad;
};

class Capture {
public:
    [[nodiscard]] std::span<const Packet> packets() const;
    [[nodiscard]] std::span<const std::uint8_t> selected_body() const;
    [[nodiscard]] std::size_t bad() const;

    [[nodiscard]] int selected() const noexcept { return selected_; }
    void select(const int index) noexcept { selected_ = index; }

    [[nodiscard]] bool running() const noexcept { return running_; }
    void start() noexcept { running_ = true; }
    void stop() noexcept { running_ = false; }

    void restart() noexcept
    {
        selected_ = -1;
        running_ = true;
    }

private:
    int selected_{12};
    bool running_{true};
};

}  // namespace spyglass
