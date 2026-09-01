#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "spyglass/overlay/store.h"

namespace spyglass {

struct Record {
    std::uint64_t number{0};
    double time{0.0};
    std::string_view name;
    std::size_t length{0};
    std::uint32_t unread{0};
    int id{-1};
    Direction direction{Direction::Inbound};
    bool decoded{true};
    std::uint8_t sub_id{0};
};

struct Details {
    Record record;
    Body body;
    nlohmann::ordered_json error;
};

struct Incoming {
    int id{-1};
    std::string_view name;
    bool decoded{true};
    std::uint32_t unread{0};
    std::uint8_t sub_id{0};
    nlohmann::ordered_json error;
    nlohmann::ordered_json fields;
    std::string_view body;
};

struct Visited {
    std::uint64_t oldest{0};
    std::uint64_t newest{0};
    std::uint64_t next{0};
};

struct Failure {
    std::string reason;
    std::string_view name;
    int id{-1};
    std::uint64_t count{0};
    std::uint64_t first{0};
    std::uint64_t last{0};
};

constexpr std::size_t kLengthBuckets = 10;
constexpr std::size_t kFieldCache = 256;

struct CaptureOptions {
    StoreOptions store;
    bool outbound{true};

    bool operator==(const CaptureOptions &other) const = default;
};

struct Rate {
    std::uint64_t packets{0};
    std::size_t bytes{0};
};

struct Counters {
    std::uint64_t total{0};
    std::uint64_t bad{0};
    std::uint64_t rejected{0};
    std::uint64_t dropped{0};
    std::uint64_t written{0};
    std::uint64_t stored_bytes{0};
};

struct Statistics {
    std::uint64_t total{0};
    std::uint64_t bad{0};
    std::uint64_t rejected{0};
    std::uint64_t dropped{0};
    std::uint64_t inbound{0};
    std::uint64_t outbound{0};
    std::size_t inbound_bytes{0};
    std::size_t outbound_bytes{0};
    std::uint64_t written{0};
    std::uint64_t stored_bytes{0};
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
    void record(Incoming incoming, Direction direction);
    [[nodiscard]] bool accepts(Direction direction) const;

    template <typename Visitor> Visited visit(const std::uint64_t first, Visitor &&visitor) const
    {
        const auto newest = store_.written();
        if (newest == 0) {
            return {};
        }
        auto next = std::max<std::uint64_t>(first, 1);
        Entry entry;
        while (next <= newest && store_.at(next, entry) && visitor(record_of(next, entry))) {
            ++next;
        }
        return {1, newest, next};
    }

    [[nodiscard]] std::uint64_t total() const;
    [[nodiscard]] double wall_start() const;
    [[nodiscard]] Record at_number(std::uint64_t number) const;
    [[nodiscard]] Details details(std::uint64_t number) const;
    [[nodiscard]] std::optional<Details> selected_details() const;
    [[nodiscard]] Fields fields(std::uint64_t number);
    [[nodiscard]] std::uint64_t bad() const;
    [[nodiscard]] std::vector<std::uint64_t> counts() const;
    [[nodiscard]] std::vector<Failure> failures() const;
    [[nodiscard]] Counters counters() const;
    [[nodiscard]] Statistics statistics() const;
    [[nodiscard]] const Store &store() const;

    [[nodiscard]] std::uint64_t selected() const;
    void select(std::uint64_t number);

    [[nodiscard]] bool running() const;
    void start();
    void stop();
    void restart();

    [[nodiscard]] CaptureOptions options() const;
    void set_options(const CaptureOptions &options);

private:
    [[nodiscard]] static Record record_of(std::uint64_t number, const Entry &entry);

    Store store_;

    mutable std::mutex mutex_;
    std::vector<std::uint64_t> counts_;
    std::vector<std::size_t> id_bytes_;
    std::vector<Failure> failures_;
    std::array<std::uint64_t, kLengthBuckets> lengths_{};
    std::vector<Rate> rates_;
    std::uint64_t counter_{0};
    std::uint64_t bad_{0};
    std::uint64_t rejected_{0};
    std::uint64_t inbound_{0};
    std::uint64_t outbound_{0};
    std::size_t inbound_bytes_{0};
    std::size_t outbound_bytes_{0};
    double started_{-1.0};
    double wall_started_{0.0};
    std::uint64_t selected_{0};
    CaptureOptions options_;
    bool running_{true};

    mutable std::mutex fields_mutex_;
    mutable std::list<std::pair<std::uint64_t, Fields>> fields_;
    mutable std::unordered_map<std::uint64_t, std::list<std::pair<std::uint64_t, Fields>>::iterator> field_at_;
};

}  // namespace spyglass
