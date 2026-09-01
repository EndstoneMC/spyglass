#include "spyglass/overlay/export.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <exception>
#include <format>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

#include "spyglass/core/clock.h"
#include "spyglass/core/output.h"
#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"
#include "spyglass/overlay/report.h"
#include "spyglass/overlay/store.h"
#include "spyglass/reflect.h"
#include "spyglass/signature.h"

namespace spyglass {
namespace {

constexpr auto kJsonReplace = nlohmann::ordered_json::error_handler_t::replace;
constexpr auto kFrameBudget = std::chrono::milliseconds{12};
constexpr std::uint64_t kBudgetStride = 32;

void finish(ExportJob &job, std::string message)
{
    if (job.file.is_open()) {
        job.file.close();
    }
    std::error_code ec;
    std::filesystem::remove(job.temp, ec);
    job.running = false;
    job.message = std::move(message);
}

}  // namespace

std::string export_name(const std::string_view kind, const std::string_view extension)
{
    const auto parts = local_time(std::time(nullptr));
    return std::format("spyglass-{:04}{:02}{:02}-{:02}{:02}{:02}-{}.{}", parts.tm_year + 1900, parts.tm_mon + 1,
                       parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec, kind, extension);
}

ExportRange parse_range(const std::string_view text)
{
    constexpr std::string_view spaces = " \t";
    constexpr auto highest = std::numeric_limits<std::uint64_t>::max();

    ExportRange range;
    std::vector<std::string_view> terms;
    for (std::size_t at = 0;;) {
        const auto comma = text.find(',', at);
        auto piece = comma == std::string_view::npos ? text.substr(at) : text.substr(at, comma - at);
        if (const auto first = piece.find_first_not_of(spaces); first == std::string_view::npos) {
            piece = {};
        }
        else {
            piece = piece.substr(first, piece.find_last_not_of(spaces) - first + 1);
        }
        terms.push_back(piece);
        if (comma == std::string_view::npos) {
            break;
        }
        at = comma + 1;
    }
    if (terms.size() > 1 && terms.back().empty()) {
        terms.pop_back();
    }
    if (terms.size() == 1 && terms.front().empty()) {
        return range;
    }

    for (const auto term : terms) {
        if (term.empty()) {
            range.spans.clear();
            range.error = "an empty range between commas";
            return range;
        }

        auto low = std::uint64_t{1};
        auto high = highest;
        auto reason = std::string_view{};
        const auto number = [&reason](const std::string_view piece, std::uint64_t &value) {
            const auto *const end = piece.data() + piece.size();
            const auto parsed = std::from_chars(piece.data(), end, value);
            if (parsed.ec == std::errc::result_out_of_range) {
                reason = "is too large";
                return false;
            }
            if (parsed.ec != std::errc{} || parsed.ptr != end) {
                reason = "is not a packet number";
                return false;
            }
            return true;
        };

        if (const auto dash = term.find('-'); dash == std::string_view::npos) {
            if (!number(term, low)) {
                range.spans.clear();
                range.error = std::format("\"{}\" {}", term, reason);
                return range;
            }
            high = low;
        }
        else {
            const auto before = term.substr(0, dash);
            const auto after = term.substr(dash + 1);
            if ((before.empty() && after.empty()) || (!before.empty() && !number(before, low)) ||
                (!after.empty() && !number(after, high))) {
                range.spans.clear();
                range.error = std::format("\"{}\" {}", term, reason.empty() ? "is not a range" : reason);
                return range;
            }
        }

        if (low > high) {
            range.spans.clear();
            range.error = std::format("\"{}\" ends before it starts", term);
            return range;
        }
        range.spans.emplace_back(low, high);
    }

    std::sort(range.spans.begin(), range.spans.end());
    std::vector<std::pair<std::uint64_t, std::uint64_t>> merged;
    for (const auto &span : range.spans) {
        if (!merged.empty() && (merged.back().second == highest || span.first <= merged.back().second + 1)) {
            merged.back().second = std::max(merged.back().second, span.second);
            continue;
        }
        merged.push_back(span);
    }
    range.spans = std::move(merged);
    return range;
}

bool in_range(const ExportRange &range, const std::uint64_t number)
{
    for (const auto &[low, high] : range.spans) {
        if (number >= low && number <= high) {
            return true;
        }
    }
    return false;
}

std::uint64_t range_size(const ExportRange &range, const std::uint64_t newest)
{
    std::uint64_t total = 0;
    for (const auto &[low, high] : range.spans) {
        const auto first = std::max<std::uint64_t>(low, 1);
        const auto last = std::min(high, newest);
        if (first <= last) {
            total += last - first + 1;
        }
    }
    return total;
}

void begin_export(ExportJob &job, const Capture &capture, const Filter &filter, const std::set<std::uint64_t> &marks,
                  const ExportOptions &options, const std::filesystem::path &path)
{
    if (job.running) {
        return;
    }

    job.options = options;
    job.filter = filter;
    job.marks = marks;
    job.selected = capture.selected();
    job.path = path;
    job.temp = path;
    job.temp += path_of(".part");
    job.next = 1;
    job.last = capture.store().written();
    job.written = 0;
    job.bytes = 0;
    job.unreadable = 0;
    job.message.clear();

    if (options.selection == PacketSelection::Selected) {
        job.next = std::max<std::uint64_t>(job.selected, 1);
        job.last = std::min(job.last, job.selected);
    }
    else if (options.selection == PacketSelection::Range && !options.range.spans.empty()) {
        job.next = std::max<std::uint64_t>(options.range.spans.front().first, 1);
        job.last = std::min(job.last, options.range.spans.back().second);
    }
    else if (options.selection == PacketSelection::Marked && !marks.empty()) {
        job.next = std::max<std::uint64_t>(*marks.begin(), 1);
        job.last = std::min(job.last, *marks.rbegin());
    }

    job.file.open(job.temp, std::ios::binary | std::ios::trunc);
    if (!job.file) {
        job.message = std::format("Could not write {}", path_text(job.path));
        return;
    }

    std::string chunk;
    if (options.format == ExportFormat::Csv) {
        chunk = kCsvHeader;
    }
    else if (options.format == ExportFormat::Json) {
        const auto statistics = capture.statistics();
        const nlohmann::ordered_json session{
            {"spyglass", SPYGLASS_VERSION},
            {"client", std::string{signatures().name}},
            {"bytes_encoding", "base64"},
            {"duration", statistics.duration},
            {"captured", statistics.total},
            {"written", statistics.written},
            {"bad", statistics.bad},
            {"dropped", statistics.dropped},
            {"stored_bytes", statistics.stored_bytes},
        };
        chunk = std::format("{{\n  \"session\": {},\n  \"packets\": [", session.dump(-1, ' ', false, kJsonReplace));
    }
    if (!chunk.empty()) {
        job.file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        job.bytes += chunk.size();
    }
    job.running = true;
}

void advance_export(ExportJob &job, const Capture &capture)
{
    if (!job.running) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + kFrameBudget;
    std::string chunk;
    std::uint64_t examined = 0;

    try {
        const auto visited = capture.visit(job.next, [&](const Record &record) {
            if (record.number > job.last) {
                return false;
            }
            if (++examined % kBudgetStride == 0 && std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            auto wanted = true;
            switch (job.options.selection) {
            case PacketSelection::All:
                break;
            case PacketSelection::Selected:
                wanted = record.number == job.selected;
                break;
            case PacketSelection::Marked:
                wanted = job.marks.contains(record.number);
                break;
            case PacketSelection::Range:
                wanted = in_range(job.options.range, record.number);
                break;
            }
            if (!wanted || (job.options.scope == PacketScope::Displayed && !job.filter.matches(record))) {
                return true;
            }

            Entry entry{};
            const auto found = capture.store().at(record.number, entry);
            const auto wants_error = job.options.format == ExportFormat::Json && (entry.flags & kHasError) != 0;
            const auto reads =
                job.options.format == ExportFormat::Csv || job.options.details || job.options.bytes || wants_error;

            Blob blob;
            if (found && reads) {
                blob = capture.store().read(entry, job.options.details);
                if (!blob.body && entry.blob_length != 0) {
                    ++job.unreadable;
                    return true;
                }
            }

            if (job.options.details && blob.fields.is_null() && blob.body &&
                decode_mode(record.id) != DecodeMode::Eager) {
                blob.fields =
                    decode_body(record.id, {reinterpret_cast<const char *>(blob.body->data()), blob.body->size()});
            }

            const Details details{.record = record, .body = blob.body, .error = std::move(blob.error)};
            const std::span<const std::uint8_t> body =
                details.body ? std::span{*details.body} : std::span<const std::uint8_t>{};
            chunk.clear();

            switch (job.options.format) {
            case ExportFormat::Text:
                if (job.options.summary) {
                    chunk += report_row(record);
                    chunk += '\n';
                }
                if (job.options.details) {
                    chunk += report_details(details, job.options.bytes);
                    if (!blob.fields.is_null()) {
                        chunk += "  Fields\n";
                        chunk += report_node(blob.fields, 2, true);
                    }
                }
                else if (job.options.bytes) {
                    chunk += format_bytes(body, 0, BytesFormat::HexDump);
                }
                chunk += '\n';
                break;
            case ExportFormat::Csv:
                chunk += report_csv(details);
                break;
            case ExportFormat::Json: {
                nlohmann::ordered_json packet{
                    {"number", record.number},
                    {"time", record.time},
                    {"source", record.direction == Direction::Outbound ? "client" : "server"},
                    {"destination", record.direction == Direction::Outbound ? "server" : "client"},
                    {"id", record.id},
                    {"name", std::string{record.name}},
                    {"length", record.length},
                    {"decoded", record.decoded},
                    {"unread", record.unread},
                };
                if (!details.error.is_null()) {
                    packet["error"] = details.error;
                }
                if (job.options.details && !blob.fields.is_null()) {
                    packet["fields"] = blob.fields;
                }
                if (job.options.bytes) {
                    packet["bytes"] = std::format("atob({})", format_bytes(body, 0, BytesFormat::Base64));
                }
                chunk += job.written == 0 ? "\n    " : ",\n    ";
                chunk += packet.dump(-1, ' ', false, kJsonReplace);
                break;
            }
            }

            job.file.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            job.bytes += chunk.size();
            ++job.written;
            return static_cast<bool>(job.file);
        });
        job.next = std::max(job.next, visited.next);
    }
    catch (const std::exception &error) {
        finish(job, std::format("Export failed\n{}", error.what()));
        return;
    }

    if (!job.file) {
        finish(job, std::format("Could not write {}", path_text(job.path)));
        return;
    }
    if (job.next <= job.last) {
        return;
    }
    if (job.written == 0) {
        finish(job, "nothing to export");
        return;
    }

    if (job.options.format == ExportFormat::Json) {
        constexpr std::string_view trailer = "\n  ]\n}\n";
        job.file.write(trailer.data(), static_cast<std::streamsize>(trailer.size()));
        job.bytes += trailer.size();
    }

    job.file.close();
    if (!job.file) {
        finish(job, std::format("Could not write {}", path_text(job.path)));
        return;
    }

    std::error_code ec;
    std::filesystem::rename(job.temp, job.path, ec);
    if (ec) {
        finish(job, std::format("Could not write {}\n{}", path_text(job.path), ec.message()));
        return;
    }

    auto message = std::format("Wrote {} packets, {} bytes to\n{}", job.written, job.bytes, path_text(job.path));
    if (job.unreadable != 0) {
        message += std::format("\n{} packets could not be read back", job.unreadable);
    }
    finish(job, std::move(message));
}

void cancel_export(ExportJob &job)
{
    finish(job, std::format("Export cancelled after {} packets.", job.written));
}

std::string export_bytes(const std::filesystem::path &path, const std::span<const std::uint8_t> bytes)
{
    auto temp = path;
    temp += path_of(".part");

    std::ofstream file{temp, std::ios::binary | std::ios::trunc};
    if (!file) {
        return std::format("Could not write {}", path_text(path));
    }
    if (!bytes.empty()) {
        file.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    file.close();

    std::error_code ec;
    if (!file) {
        std::filesystem::remove(temp, ec);
        return std::format("Could not write {}", path_text(path));
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return std::format("Could not write {}\n{}", path_text(path), ec.message());
    }
    return std::format("Wrote {} bytes to\n{}", bytes.size(), path_text(path));
}

std::string export_summary(const std::filesystem::path &path, const Capture &capture, const Filter &filter)
{
    std::uint64_t displayed = 0;
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

    return export_bytes(path, {reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

}  // namespace spyglass
