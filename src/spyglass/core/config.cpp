#include "spyglass/core/config.h"

#include <charconv>
#include <optional>
#include <string>

#include <Windows.h>

namespace spyglass {
namespace {

constexpr std::size_t kDefaultRawCaptureLimit = 2048;
constexpr std::size_t kMaximumRawCaptureLimit = 65536;
constexpr std::size_t kDefaultHistoryLimit = 200;
constexpr std::size_t kMaximumHistoryLimit = 2000;

std::optional<std::wstring> environment(const wchar_t *name)
{
    const auto length = GetEnvironmentVariableW(name, nullptr, 0);
    if (length == 0) {
        return std::nullopt;
    }
    std::wstring value(length, L'\0');
    value.resize(GetEnvironmentVariableW(name, value.data(), length));
    return value;
}

std::size_t bounded(const std::wstring_view value, const std::size_t fallback, const std::size_t maximum)
{
    const std::string narrow(value.begin(), value.end());
    std::size_t parsed = 0;
    const auto [_, ec] = std::from_chars(narrow.data(), narrow.data() + narrow.size(), parsed);
    if (ec != std::errc{} || parsed == 0) {
        return fallback;
    }
    return std::min(parsed, maximum);
}

bool flag(const wchar_t *name, const bool fallback)
{
    const auto value = environment(name);
    return value.has_value() ? *value != L"0" : fallback;
}

std::filesystem::path default_output_directory()
{
    if (const auto local = environment(L"LOCALAPPDATA")) {
        return std::filesystem::path(*local) / L"spyglass";
    }
    return std::filesystem::temp_directory_path() / L"spyglass";
}

Config load()
{
    const auto output = environment(L"SPYGLASS_OUTPUT_DIR");
    const auto raw_limit = environment(L"SPYGLASS_RAW_CAPTURE_LIMIT");
    const auto history = environment(L"SPYGLASS_HISTORY_LIMIT");

    Config result{
        .output_directory = output.has_value() ? std::filesystem::path(*output) : default_output_directory(),
        .raw_capture_limit =
            raw_limit.has_value() ? bounded(*raw_limit, kDefaultRawCaptureLimit, kMaximumRawCaptureLimit)
                                  : kDefaultRawCaptureLimit,
        .history_limit =
            history.has_value() ? bounded(*history, kDefaultHistoryLimit, kMaximumHistoryLimit) : kDefaultHistoryLimit,
        .report_trailing_bytes = flag(L"SPYGLASS_TRAILING_BYTES", true),
        .write_events = flag(L"SPYGLASS_WRITE_EVENTS", true),
    };

    std::error_code ec;
    std::filesystem::create_directories(result.output_directory, ec);
    return result;
}

}  // namespace

const Config &config()
{
    static const Config config = load();
    return config;
}

std::filesystem::path log_path()
{
    return config().output_directory / L"spyglass.log";
}

std::filesystem::path event_path()
{
    return config().output_directory / L"events.jsonl";
}

std::filesystem::path latest_path()
{
    return config().output_directory / L"latest.txt";
}

}  // namespace spyglass
