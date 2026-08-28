#include "spyglass/filename.h"

#include <algorithm>

namespace spyglass {
namespace {

struct Entry {
    std::uint64_t hash;
    std::string_view name;
};

constexpr Entry kFilenames[] = {
#include "spyglass/filename_table.inc"
};

static_assert(filename_hash("Packet.cpp") == 0x33dfcffc70379b5fULL);

}  // namespace

std::string_view filename_of(const std::uint64_t hash)
{
    const auto match = std::ranges::find(kFilenames, hash, &Entry::hash);
    if (match == std::ranges::end(kFilenames)) {
        return {};
    }
    return match->name;
}

}  // namespace spyglass
