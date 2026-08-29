#pragma once

#include <algorithm>
#include <array>
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
    std::uint8_t sub_id{0};
    std::optional<Node> error;
    std::optional<Node> fields;
    Body body;
};

struct Visited {
    std::uint64_t oldest{0};
    std::uint64_t newest{0};
    std::uint64_t next{0};
};

constexpr std::size_t kLengthBuckets = 10;

struct CaptureOptions {
    std::size_t records{65536};
    std::size_t bytes{64 * 1024 * 1024};
    bool outbound{true};

    bool operator==(const CaptureOptions &other) const = default;
};

struct Rate {
    std::uint64_t packets{0};
    std::size_t bytes{0};
};

struct Statistics {
    std::uint64_t total{0};
    std::uint64_t bad{0};
    std::uint64_t rejected{0};
    std::uint64_t inbound{0};
    std::uint64_t outbound{0};
    std::size_t inbound_bytes{0};
    std::size_t outbound_bytes{0};
    std::size_t retained{0};
    std::size_t retained_bytes{0};
    std::uint64_t oldest{0};
    std::uint64_t newest{0};
    double duration{0.0};
    double wall_start{0.0};
    std::vector<std::uint64_t> counts;
    std::vector<std::size_t> byte_counts;
    std::array<std::uint64_t, kLengthBuckets> lengths{};
    std::vector<Rate> rates;
};

class Capture {
public:
    void record(Record record);

    template <typename Visitor>
    Visited visit(const std::uint64_t first, Visitor &&visitor) const
    {
        const std::lock_guard lock{mutex_};
        if (records_.empty()) {
            return {};
        }
        const auto oldest = records_.front().number;
        const auto newest = records_.back().number;
        auto next = std::max(first, oldest);
        while (next <= newest && visitor(records_[static_cast<std::size_t>(next - oldest)])) {
            ++next;
        }
        return {oldest, newest, next};
    }

    [[nodiscard]] std::uint64_t total() const;
    [[nodiscard]] double wall_start() const;
    [[nodiscard]] Record at_number(std::uint64_t number) const;
    [[nodiscard]] std::optional<Record> selected_record() const;
    [[nodiscard]] std::optional<Node> fields(std::uint64_t number);
    [[nodiscard]] std::uint64_t bad() const;
    [[nodiscard]] std::vector<std::uint64_t> counts() const;
    [[nodiscard]] Statistics statistics() const;

    [[nodiscard]] std::uint64_t selected() const;
    void select(std::uint64_t number);

    [[nodiscard]] bool running() const;
    void start();
    void stop();
    void restart();

    [[nodiscard]] CaptureOptions options() const;
    void set_options(const CaptureOptions &options);

private:
    [[nodiscard]] const Record *find(std::uint64_t number) const;
    [[nodiscard]] Record *find(std::uint64_t number);
    void trim();

    mutable std::mutex mutex_;
    std::deque<Record> records_;
    std::vector<std::uint64_t> counts_;
    std::vector<std::size_t> id_bytes_;
    std::array<std::uint64_t, kLengthBuckets> lengths_{};
    std::vector<Rate> rates_;
    std::uint64_t counter_{0};
    std::uint64_t bad_{0};
    std::uint64_t rejected_{0};
    std::uint64_t inbound_{0};
    std::uint64_t outbound_{0};
    std::size_t inbound_bytes_{0};
    std::size_t outbound_bytes_{0};
    std::size_t bytes_{0};
    double started_{-1.0};
    double wall_started_{0.0};
    std::uint64_t selected_{0};
    CaptureOptions options_;
    bool running_{true};
};

}  // namespace spyglass
