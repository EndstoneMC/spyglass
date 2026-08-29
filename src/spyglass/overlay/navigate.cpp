#include "spyglass/overlay/navigate.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/pane/packet_list.h"

namespace spyglass {
namespace {

constexpr std::uint64_t kIndexScanBudget = 262144;
constexpr std::uint64_t kBodyScanBudget = 4096;

}  // namespace

void select_packet(Capture &capture, PacketList &list, const std::uint64_t number)
{
    capture.select(number);
    if (!list.history.empty() && list.history[list.history_at] == number) {
        return;
    }
    if (!list.history.empty()) {
        list.history.resize(list.history_at + 1);
    }
    list.history.push_back(number);
    list.history_at = list.history.size() - 1;
}

void show_packet(Capture &capture, PacketList &list, const std::uint64_t number)
{
    select_packet(capture, list, number);
    list.scroll_to = number;
}

void jump(Capture &capture, const Filter &filter, PacketList &list, const Jump where)
{
    if (where == Jump::Back || where == Jump::Forward) {
        if (where == Jump::Back && list.history_at > 0) {
            --list.history_at;
        }
        else if (where == Jump::Forward && list.history_at + 1 < list.history.size()) {
            ++list.history_at;
        }
        else {
            return;
        }
        capture.select(list.history[list.history_at]);
        list.scroll_to = list.history[list.history_at];
        return;
    }

    const auto relative = where != Jump::First && where != Jump::Last;
    const auto forward =
        where == Jump::First || where == Jump::NextFailed || where == Jump::NextSameId || where == Jump::NextMark;
    const auto failed = where == Jump::NextFailed || where == Jump::PreviousFailed;
    const auto marked = where == Jump::NextMark || where == Jump::PreviousMark;

    auto id = -1;
    if (where == Jump::NextSameId || where == Jump::PreviousSameId) {
        const auto selected = capture.at_number(capture.selected());
        if (selected.number == 0 || selected.id < 0) {
            return;
        }
        id = selected.id;
    }

    const std::uint64_t from = relative ? capture.selected() : 0;
    std::uint64_t found = 0;
    capture.visit(forward ? from + 1 : 0, [&](const Record &record) {
        if (!forward && relative && record.number >= from) {
            return false;
        }
        if (!filter.matches(record) || (failed && record.decoded) || (id >= 0 && record.id != id) ||
            (marked && !list.marks.contains(record.number))) {
            return true;
        }
        found = record.number;
        return !forward;
    });

    if (found != 0) {
        show_packet(capture, list, found);
    }
}

void find_packet(Capture &capture, const Filter &filter, PacketList &list, const bool forward)
{
    const std::string_view query{list.find.query};
    list.find.missed = false;
    list.find.scanning = false;
    if (query.empty()) {
        return;
    }

    if (list.find.scope == FindScope::BodyHex || list.find.scope == FindScope::BodyText) {
        std::vector<std::uint8_t> needle;
        if (!parse_needle(query, list.find.scope == FindScope::BodyHex, needle) || needle.empty()) {
            list.find.missed = true;
            return;
        }
    }

    list.find.forward = forward;
    list.find.origin = capture.selected();
    list.find.cursor = forward ? list.find.origin + 1 : 1;
    list.find.found = 0;
    list.find.scanned = 0;
    list.find.total = capture.total();
    list.find.scanning = true;
    advance_find(capture, filter, list);
}

void advance_find(Capture &capture, const Filter &filter, PacketList &list)
{
    if (!list.find.scanning) {
        return;
    }

    const std::string_view query{list.find.query};
    const auto reads_body = list.find.scope == FindScope::BodyHex || list.find.scope == FindScope::BodyText;

    std::vector<std::uint8_t> needle;
    std::string wanted;
    auto id = -1;
    switch (list.find.scope) {
    case FindScope::Name:
        for (const auto character : query) {
            wanted += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        break;
    case FindScope::Id:
        id = std::atoi(list.find.query);
        break;
    case FindScope::BodyHex:
    case FindScope::BodyText:
        parse_needle(query, list.find.scope == FindScope::BodyHex, needle);
        break;
    }

    const auto budget = reads_body ? kBodyScanBudget : kIndexScanBudget;
    auto examined = std::uint64_t{0};
    auto stopped = false;

    const auto visited = capture.visit(list.find.cursor, [&](const Record &record) {
        if (!list.find.forward && list.find.origin != 0 && record.number >= list.find.origin) {
            stopped = true;
            return false;
        }
        ++examined;
        if (!filter.matches(record)) {
            return examined < budget;
        }

        auto hit = false;
        switch (list.find.scope) {
        case FindScope::Name: {
            std::string lowered;
            for (const auto character : record.name) {
                lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
            }
            hit = lowered.find(wanted) != std::string::npos;
            break;
        }
        case FindScope::Id:
            hit = record.id == id;
            break;
        case FindScope::BodyHex:
        case FindScope::BodyText: {
            const auto body = capture.details(record.number).body;
            hit =
                body != nullptr && std::search(body->begin(), body->end(), needle.begin(), needle.end()) != body->end();
            break;
        }
        }
        if (hit) {
            list.find.found = record.number;
            if (list.find.forward) {
                stopped = true;
                return false;
            }
        }
        return examined < budget;
    });

    list.find.cursor = visited.next;
    list.find.scanned = visited.next > 1 ? visited.next - 1 : 0;
    list.find.total = std::max(list.find.total, visited.newest);

    if (!stopped && visited.next <= visited.newest) {
        return;
    }

    list.find.scanning = false;
    list.find.missed = list.find.found == 0;
    if (list.find.found != 0) {
        show_packet(capture, list, list.find.found);
    }
}

}  // namespace spyglass
