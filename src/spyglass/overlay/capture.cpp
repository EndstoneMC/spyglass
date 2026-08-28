#include "spyglass/overlay/capture.h"

#include <algorithm>
#include <chrono>
#include <format>

namespace spyglass {
namespace {

constexpr std::size_t kRetained = 4096;

double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

int read_packet_id(const std::string_view data)
{
    int value = 0;
    int shift = 0;
    for (const char c : data) {
        const auto byte = static_cast<std::uint8_t>(c);
        value |= (byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            return value & 0x3FF;
        }
        shift += 7;
        if (shift > 28) {
            break;
        }
    }
    return -1;
}

}  // namespace

void Capture::record(const Direction direction, const std::string_view data)
{
    const std::lock_guard lock{mutex_};
    if (!running_) {
        return;
    }
    if (started_ < 0.0) {
        started_ = now();
    }

    const auto id = read_packet_id(data);
    entries_.push_back({{
                            .number = ++counter_,
                            .time = now() - started_,
                            .source = direction == Direction::Outbound ? "client" : "server",
                            .destination = direction == Direction::Outbound ? "server" : "client",
                            .length = static_cast<std::uint32_t>(data.size()),
                            .info = id < 0 ? std::string{"malformed header"} : std::format("id {}", id),
                            .bad = id < 0,
                        },
                        std::vector<std::uint8_t>{data.begin(), data.end()}});
    while (entries_.size() > kRetained) {
        entries_.pop_front();
    }
}

std::vector<Packet> Capture::packets() const
{
    const std::lock_guard lock{mutex_};
    std::vector<Packet> out;
    out.reserve(entries_.size());
    for (const auto &entry : entries_) {
        out.push_back(entry.packet);
    }
    return out;
}

std::vector<std::uint8_t> Capture::selected_body() const
{
    const std::lock_guard lock{mutex_};
    if (selected_ < 0 || selected_ >= static_cast<int>(entries_.size())) {
        return {};
    }
    return entries_[static_cast<std::size_t>(selected_)].body;
}

std::size_t Capture::bad() const
{
    const std::lock_guard lock{mutex_};
    return static_cast<std::size_t>(
        std::ranges::count_if(entries_, [](const Entry &entry) { return entry.packet.bad; }));
}

int Capture::selected() const
{
    const std::lock_guard lock{mutex_};
    return selected_;
}

void Capture::select(const int index)
{
    const std::lock_guard lock{mutex_};
    selected_ = index;
}

bool Capture::running() const
{
    const std::lock_guard lock{mutex_};
    return running_;
}

void Capture::start()
{
    const std::lock_guard lock{mutex_};
    running_ = true;
}

void Capture::stop()
{
    const std::lock_guard lock{mutex_};
    running_ = false;
}

void Capture::restart()
{
    const std::lock_guard lock{mutex_};
    entries_.clear();
    counter_ = 0;
    started_ = -1.0;
    selected_ = -1;
    running_ = true;
}

}  // namespace spyglass
