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
        const auto selected = capture.selected_record();
        if (!selected || selected->id < 0) {
            return;
        }
        id = selected->id;
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
    if (query.empty()) {
        return;
    }

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
        if (!parse_needle(query, list.find.scope == FindScope::BodyHex, needle) || needle.empty()) {
            list.find.missed = true;
            return;
        }
        break;
    }

    const auto from = capture.selected();
    std::uint64_t found = 0;
    capture.visit(forward ? from + 1 : 0, [&](const Record &record) {
        if (!forward && from != 0 && record.number >= from) {
            return false;
        }
        if (!filter.matches(record)) {
            return true;
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
        case FindScope::BodyText:
            hit = record.body != nullptr &&
                  std::search(record.body->begin(), record.body->end(), needle.begin(), needle.end()) !=
                      record.body->end();
            break;
        }
        if (!hit) {
            return true;
        }
        found = record.number;
        return !forward;
    });

    list.find.missed = found == 0;
    if (found != 0) {
        show_packet(capture, list, found);
    }
}

}  // namespace spyglass
