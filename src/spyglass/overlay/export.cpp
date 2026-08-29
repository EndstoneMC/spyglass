#include "spyglass/overlay/export.h"

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
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
    const auto share = statistics.total == 0
                           ? 0.0
                           : (100.0 * static_cast<double>(statistics.bad)) / static_cast<double>(statistics.total);

    auto text = std::format("Spyglass {}\nClient: {}\n\n", SPYGLASS_VERSION, signatures().name);
    text += std::format("Duration: {:.3f} s\n", statistics.duration);
    text +=
        std::format("Packets captured: {}\nFailed decodes: {} ({:.1f}%)\n", statistics.total, statistics.bad, share);
    text += std::format("Packets on disk: {} ({} bytes, numbers {}..{})\n", statistics.written, statistics.stored_bytes,
                        statistics.oldest, statistics.newest);
    if (statistics.dropped != 0) {
        text += std::format("Dropped by the writer: {}\n", statistics.dropped);
    }
    text += std::format("Packets displayed: {}\n\n", displayed);
    text += std::format("Received: {} packets, {} bytes\n", statistics.inbound, statistics.inbound_bytes);
    text += std::format("Sent: {} packets, {} bytes\n", statistics.outbound, statistics.outbound_bytes);
    return text;
}

}  // namespace

std::string export_capture(const Capture &capture, const Filter &filter, const Export what, const BytesFormat bytes)
{
    std::string kind = "packets";
    std::string extension = "txt";
    if (what == Export::DisplayedCsv) {
        extension = "csv";
    }
    else if (what == Export::SelectedDetails) {
        kind = "packet";
    }
    else if (what == Export::SelectedBytes) {
        kind = "bytes";
    }
    else if (what == Export::Summary) {
        kind = "summary";
    }

    const auto path = output_directory() / std::format("spyglass-{}-{}.{}", stamp(), kind, extension);
    std::ofstream file{path, std::ios::binary};
    if (!file) {
        return std::format("could not write {}", path.string());
    }

    std::uint64_t written = 0;
    switch (what) {
    case Export::DisplayedText:
        capture.visit(0, [&](const Record &record) {
            if (filter.matches(record)) {
                file << report_details(capture.details(record.number)) << '\n';
                ++written;
            }
            return true;
        });
        break;
    case Export::DisplayedCsv:
        file << kCsvHeader;
        capture.visit(0, [&](const Record &record) {
            if (filter.matches(record)) {
                file << report_csv(capture.details(record.number));
                ++written;
            }
            return true;
        });
        break;
    case Export::SelectedDetails:
        if (const auto selection = capture.selected_details()) {
            file << report_details(*selection);
            ++written;
        }
        break;
    case Export::SelectedBytes:
        if (const auto selection = capture.selected_details()) {
            const std::span<const std::uint8_t> body =
                selection->body ? std::span{*selection->body} : std::span<const std::uint8_t>{};
            file << format_bytes(body, 0, bytes);
            ++written;
        }
        break;
    case Export::Summary:
        file << summary_of(capture, filter);
        ++written;
        break;
    }

    const auto size = file.tellp();
    file.close();
    if (written == 0) {
        std::filesystem::remove(path);
        return "nothing to export";
    }
    if (!file) {
        return std::format("could not write {}", path.string());
    }
    return std::format("wrote {} bytes to\n{}", static_cast<std::uint64_t>(size), path.string());
}

}  // namespace spyglass
