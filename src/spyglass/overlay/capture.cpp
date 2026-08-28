#include "spyglass/overlay/capture.h"

#include <chrono>
#include <utility>

namespace spyglass {
namespace {

double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

double wall_now()
{
    using namespace std::chrono;
    return duration<double>(system_clock::now().time_since_epoch()).count();
}

}  // namespace

void Capture::record(Record record)
{
    const std::lock_guard lock{mutex_};
    if (!running_) {
        return;
    }
    if (!capture_filter_.matches(record)) {
        ++rejected_;
        return;
    }
    if (started_ < 0.0) {
        started_ = now();
        wall_started_ = wall_now();
    }

    record.number = ++counter_;
    record.time = now() - started_;
    if (!record.decoded) {
        ++bad_;
    }

    const auto length = record.body ? record.body->size() : 0;
    if (record.id >= 0) {
        const auto id = static_cast<std::size_t>(record.id);
        if (id >= counts_.size()) {
            counts_.resize(id + 1, 0);
            id_bytes_.resize(id + 1, 0);
        }
        ++counts_[id];
        id_bytes_[id] += length;
    }

    auto bucket = std::size_t{0};
    for (auto edge = std::size_t{20}; bucket + 1 < kLengthBuckets && length >= edge; edge *= 2) {
        ++bucket;
    }
    ++lengths_[bucket];

    if (record.direction == Direction::Outbound) {
        ++outbound_;
        outbound_bytes_ += length;
    }
    else {
        ++inbound_;
        inbound_bytes_ += length;
    }

    const auto second = static_cast<std::size_t>(record.time);
    if (second >= rates_.size()) {
        rates_.resize(second + 1);
    }
    ++rates_[second].packets;
    rates_[second].bytes += length;

    bytes_ += length;
    records_.push_back(std::move(record));
    trim();
}

void Capture::trim()
{
    while (records_.size() > retention_.records || (bytes_ > retention_.bytes && records_.size() > 1)) {
        bytes_ -= records_.front().body ? records_.front().body->size() : 0;
        records_.pop_front();
    }
}

std::uint64_t Capture::total() const
{
    const std::lock_guard lock{mutex_};
    return counter_;
}

double Capture::wall_start() const
{
    const std::lock_guard lock{mutex_};
    return wall_started_;
}

const Record *Capture::find(const std::uint64_t number) const
{
    if (records_.empty() || number < records_.front().number) {
        return nullptr;
    }
    const auto index = number - records_.front().number;
    if (index >= records_.size()) {
        return nullptr;
    }
    return &records_[static_cast<std::size_t>(index)];
}

Record Capture::at_number(const std::uint64_t number) const
{
    const std::lock_guard lock{mutex_};
    if (const auto *record = find(number)) {
        return *record;
    }
    return {};
}

std::optional<Record> Capture::selected_record() const
{
    const std::lock_guard lock{mutex_};
    if (const auto *record = find(selected_)) {
        return *record;
    }
    return std::nullopt;
}

std::uint64_t Capture::bad() const
{
    const std::lock_guard lock{mutex_};
    return bad_;
}

std::vector<std::uint64_t> Capture::counts() const
{
    const std::lock_guard lock{mutex_};
    return counts_;
}

Statistics Capture::statistics() const
{
    const std::lock_guard lock{mutex_};
    return {
        .total = counter_,
        .bad = bad_,
        .rejected = rejected_,
        .inbound = inbound_,
        .outbound = outbound_,
        .inbound_bytes = inbound_bytes_,
        .outbound_bytes = outbound_bytes_,
        .retained = records_.size(),
        .retained_bytes = bytes_,
        .oldest = records_.empty() ? 0 : records_.front().number,
        .newest = records_.empty() ? 0 : records_.back().number,
        .duration = records_.empty() ? 0.0 : records_.back().time,
        .wall_start = wall_started_,
        .counts = counts_,
        .byte_counts = id_bytes_,
        .lengths = lengths_,
        .rates = rates_,
    };
}

std::uint64_t Capture::selected() const
{
    const std::lock_guard lock{mutex_};
    return selected_;
}

void Capture::select(const std::uint64_t number)
{
    const std::lock_guard lock{mutex_};
    selected_ = number;
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
    records_.clear();
    counts_.clear();
    id_bytes_.clear();
    lengths_.fill(0);
    rates_.clear();
    counter_ = 0;
    bad_ = 0;
    rejected_ = 0;
    inbound_ = 0;
    outbound_ = 0;
    inbound_bytes_ = 0;
    outbound_bytes_ = 0;
    bytes_ = 0;
    started_ = -1.0;
    wall_started_ = 0.0;
    selected_ = 0;
    running_ = true;
}

Retention Capture::retention() const
{
    const std::lock_guard lock{mutex_};
    return retention_;
}

void Capture::set_retention(const Retention retention)
{
    const std::lock_guard lock{mutex_};
    if (retention_ == retention) {
        return;
    }
    retention_ = retention;
    trim();
}

Filter Capture::capture_filter() const
{
    const std::lock_guard lock{mutex_};
    return capture_filter_;
}

void Capture::set_capture_filter(const Filter &filter)
{
    const std::lock_guard lock{mutex_};
    capture_filter_ = filter;
}

}  // namespace spyglass
