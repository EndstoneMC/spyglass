#include "spyglass/overlay/capture.h"

#include <chrono>
#include <utility>

namespace spyglass {
namespace {

constexpr std::size_t kRetained = 65536;
constexpr std::size_t kRetainedBytes = 64 * 1024 * 1024;

double now()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

void Capture::record(Record record)
{
    const std::lock_guard lock{mutex_};
    if (!running_) {
        return;
    }
    if (started_ < 0.0) {
        started_ = now();
    }

    record.number = ++counter_;
    record.time = now() - started_;
    if (!record.decoded) {
        ++bad_;
    }
    bytes_ += record.body ? record.body->size() : 0;
    records_.push_back(std::move(record));

    while (records_.size() > kRetained || (bytes_ > kRetainedBytes && records_.size() > 1)) {
        bytes_ -= records_.front().body ? records_.front().body->size() : 0;
        records_.pop_front();
    }
}

std::size_t Capture::size() const
{
    const std::lock_guard lock{mutex_};
    return records_.size();
}

std::uint64_t Capture::total() const
{
    const std::lock_guard lock{mutex_};
    return counter_;
}

Record Capture::at(const std::size_t index) const
{
    const std::lock_guard lock{mutex_};
    if (index >= records_.size()) {
        return {};
    }
    return records_[index];
}

const Record *Capture::at_number(const std::uint64_t number) const
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

std::optional<Record> Capture::selected_record() const
{
    const std::lock_guard lock{mutex_};
    if (const auto *record = at_number(selected_)) {
        return *record;
    }
    return std::nullopt;
}

std::uint64_t Capture::oldest() const
{
    const std::lock_guard lock{mutex_};
    return records_.empty() ? 0 : records_.front().number;
}

std::uint64_t Capture::bad() const
{
    const std::lock_guard lock{mutex_};
    return bad_;
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
    counter_ = 0;
    bad_ = 0;
    bytes_ = 0;
    started_ = -1.0;
    selected_ = 0;
    running_ = true;
}

}  // namespace spyglass
