#include "spyglass/overlay/capture.h"

#include <chrono>
#include <utility>

namespace spyglass {
namespace {

constexpr std::size_t kRetained = 4096;

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
    records_.push_back(std::move(record));

    while (records_.size() > kRetained) {
        if (!records_.front().decoded) {
            --bad_;
        }
        records_.pop_front();
    }
}

std::size_t Capture::size() const
{
    const std::lock_guard lock{mutex_};
    return records_.size();
}

Record Capture::at(const std::size_t index) const
{
    const std::lock_guard lock{mutex_};
    if (index >= records_.size()) {
        return {};
    }
    return records_[index];
}

Body Capture::selected_body() const
{
    const std::lock_guard lock{mutex_};
    if (selected_ < 0 || selected_ >= static_cast<int>(records_.size())) {
        return {};
    }
    return records_[static_cast<std::size_t>(selected_)].body;
}

std::size_t Capture::bad() const
{
    const std::lock_guard lock{mutex_};
    return bad_;
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
    records_.clear();
    counter_ = 0;
    bad_ = 0;
    started_ = -1.0;
    selected_ = -1;
    running_ = true;
}

}  // namespace spyglass
