#pragma once

#include <cstdint>
#include <vector>

namespace spyglass {

struct Record;

struct Filter {
    std::vector<std::uint8_t> enabled;
    bool bad_only{false};
    bool inbound{true};
    bool outbound{true};

    [[nodiscard]] bool active() const;
    [[nodiscard]] bool matches(const Record &record) const;
    [[nodiscard]] bool allowed(int id) const;
    void allow(int id, bool on);

    bool operator==(const Filter &other) const = default;
};

}  // namespace spyglass
