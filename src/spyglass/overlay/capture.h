#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace spyglass {

enum class Direction : int {
    Inbound = 0,
    Outbound = 1,
};

using Body = std::shared_ptr<const std::vector<std::uint8_t>>;

struct Node {
    std::string label;
    std::vector<Node> children;
};

struct Record {
    std::uint64_t number{0};
    double time{0.0};
    Direction direction{Direction::Inbound};
    int id{-1};
    std::string name;
    bool decoded{true};
    std::uint32_t unread{0};
    std::optional<Node> error;
    Body body;
};

class Capture {
public:
    void record(Record record);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] std::uint64_t total() const;
    [[nodiscard]] Record at(std::size_t index) const;
    [[nodiscard]] std::optional<Record> selected_record() const;
    [[nodiscard]] std::uint64_t oldest() const;
    [[nodiscard]] std::uint64_t bad() const;

    [[nodiscard]] std::uint64_t selected() const;
    void select(std::uint64_t number);

    [[nodiscard]] bool running() const;
    void start();
    void stop();
    void restart();

private:
    [[nodiscard]] const Record *at_number(std::uint64_t number) const;

    mutable std::mutex mutex_;
    std::deque<Record> records_;
    std::uint64_t counter_{0};
    std::uint64_t bad_{0};
    std::size_t bytes_{0};
    double started_{-1.0};
    std::uint64_t selected_{0};
    bool running_{true};
};

}  // namespace spyglass
