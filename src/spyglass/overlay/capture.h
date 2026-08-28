#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace spyglass {

enum class Direction : int {
    Inbound = 0,
    Outbound = 1,
};

using Body = std::shared_ptr<const std::vector<std::uint8_t>>;

struct Record {
    std::uint64_t number{0};
    double time{0.0};
    Direction direction{Direction::Inbound};
    int id{-1};
    std::string name;
    bool decoded{true};
    std::uint32_t unread{0};
    Body body;
};

class Capture {
public:
    void record(Record record);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] Record at(std::size_t index) const;
    [[nodiscard]] Body selected_body() const;
    [[nodiscard]] std::size_t bad() const;

    [[nodiscard]] int selected() const;
    void select(int index);

    [[nodiscard]] bool running() const;
    void start();
    void stop();
    void restart();

private:
    mutable std::mutex mutex_;
    std::deque<Record> records_;
    std::uint64_t counter_{0};
    std::size_t bad_{0};
    double started_{-1.0};
    int selected_{-1};
    bool running_{true};
};

}  // namespace spyglass
