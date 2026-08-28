#include "spyglass/overlay/export.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <span>

#include "spyglass/core/clock.h"
#include "spyglass/core/output.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/filter.h"
#include "spyglass/overlay/report.h"
#include "spyglass/signature.h"

namespace spyglass {
namespace {

std::string stamp()
{
    const auto parts = local_time(std::time(nullptr));
    return std::format("{:04}{:02}{:02}-{:02}{:02}{:02}", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                       parts.tm_hour, parts.tm_min, parts.tm_sec);
}

std::string summary_of(const Capture &capture, const Filter &filter)
{
    std::size_t displayed = 0;
    capture.visit(0, [&](const Record &record) {
        displayed += filter.matches(record) ? 1 : 0;
        return true;
    });

    const auto statistics = capture.statistics();
    const auto share = statistics.total == 0 ? 0.0
                                             : (100.0 * static_cast<double>(statistics.bad)) /
                                                   static_cast<double>(statistics.total);

    auto text = std::format("Spyglass {}\nClient: {}\n\n", SPYGLASS_VERSION, signatures().name);
    text += std::format("Duration: {:.3f} s\n", statistics.duration);
    text += std::format("Packets captured: {}\nFailed decodes: {} ({:.1f}%)\n", statistics.total, statistics.bad,
                        share);
    text += std::format("Packets retained: {} ({} bytes, numbers {}..{})\n", statistics.retained,
                        statistics.retained_bytes, statistics.oldest, statistics.newest);
    text += std::format("Packets displayed: {}\n\n", displayed);
    text += std::format("Received: {} packets, {} bytes\n", statistics.inbound, statistics.inbound_bytes);
    text += std::format("Sent: {} packets, {} bytes\n", statistics.outbound, statistics.outbound_bytes);
    return text;
}

}  // namespace

std::string export_capture(const Capture &capture, const Filter &filter, const Export what, const BytesFormat bytes)
{
    std::string text;
    std::string kind;
    std::string extension = "txt";

    switch (what) {
    case Export::DisplayedText:
        kind = "packets";
        capture.visit(0, [&](const Record &record) {
            if (filter.matches(record)) {
                text += report_details(record);
                text += '\n';
            }
            return true;
        });
        break;
    case Export::DisplayedCsv:
        kind = "packets";
        extension = "csv";
        text = kCsvHeader;
        capture.visit(0, [&](const Record &record) {
            if (filter.matches(record)) {
                text += report_csv(record);
            }
            return true;
        });
        break;
    case Export::SelectedDetails:
        kind = "packet";
        if (const auto record = capture.selected_record()) {
            text = report_details(*record);
        }
        break;
    case Export::SelectedBytes:
        kind = "bytes";
        if (const auto record = capture.selected_record()) {
            const std::span<const std::uint8_t> body = record->body ? std::span{*record->body}
                                                                    : std::span<const std::uint8_t>{};
            text = format_bytes(body, 0, bytes);
        }
        break;
    case Export::Summary:
        kind = "summary";
        text = summary_of(capture, filter);
        break;
    }

    if (text.empty()) {
        return "nothing to export";
    }

    const auto path = output_directory() / std::format("spyglass-{}-{}.{}", stamp(), kind, extension);
    std::ofstream file{path, std::ios::binary};
    file << text;
    if (!file) {
        return std::format("could not write {}", path.string());
    }
    return std::format("wrote {} bytes to\n{}", text.size(), path.string());
}

}  // namespace spyglass
