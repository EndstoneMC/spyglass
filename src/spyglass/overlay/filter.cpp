#include "spyglass/overlay/filter.h"

#include <algorithm>
#include <cstddef>

#include "spyglass/overlay/capture.h"

namespace spyglass {

bool Filter::active() const
{
    return bad_only || !inbound || !outbound ||
           std::find(enabled.begin(), enabled.end(), std::uint8_t{0}) != enabled.end();
}

bool Filter::matches(const Record &record) const
{
    if (bad_only && record.decoded) {
        return false;
    }
    if (record.direction == Direction::Inbound ? !inbound : !outbound) {
        return false;
    }
    return allowed(record.id);
}

bool Filter::allowed(const int id) const
{
    if (id < 0 || static_cast<std::size_t>(id) >= enabled.size()) {
        return true;
    }
    return enabled[static_cast<std::size_t>(id)] != 0;
}

void Filter::allow(const int id, const bool on)
{
    if (id < 0) {
        return;
    }
    const auto index = static_cast<std::size_t>(id);
    if (index >= enabled.size()) {
        enabled.resize(index + 1, 1);
    }
    enabled[index] = on ? 1 : 0;
}

}  // namespace spyglass
