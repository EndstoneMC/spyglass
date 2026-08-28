#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace spyglass {

enum class Direction : int {
    Inbound = 0,
    Outbound = 1,
};

struct Packet {
    std::uint64_t number;
    double time;
    std::string_view source;
    std::string_view destination;
    std::uint32_t length;
    std::string info;
    bool bad;
};

class Capture {
public:
    void record(Direction direction, std::string_view data);

    [[nodiscard]] std::vector<Packet> packets() const;
    [[nodiscard]] std::vector<std::uint8_t> selected_body() const;
    [[nodiscard]] std::size_t bad() const;

    [[nodiscard]] int selected() const;
    void select(int index);

    [[nodiscard]] bool running() const;
    void start();
    void stop();
    void restart();

private:
    struct Entry {
        Packet packet;
        std::vector<std::uint8_t> body;
    };

    mutable std::mutex mutex_;
    std::deque<Entry> entries_;
    std::uint64_t counter_{0};
    double started_{-1.0};
    int selected_{-1};
    bool running_{true};
};

}  // namespace spyglass
