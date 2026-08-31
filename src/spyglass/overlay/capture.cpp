#include "spyglass/overlay/capture.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "spyglass/reflect.h"

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

Record Capture::record_of(const std::uint64_t number, const Entry &entry)
{
    return {
        .number = number,
        .time = static_cast<double>(entry.time) / 1e6,
        .name = interned_name(entry.id),
        .length = entry.body_length,
        .unread = entry.unread,
        .id = entry.id,
        .direction = (entry.flags & kOutbound) != 0 ? Direction::Outbound : Direction::Inbound,
        .decoded = (entry.flags & kDecoded) != 0,
        .sub_id = entry.sub_id,
    };
}

void Capture::record(Incoming incoming, const Direction direction)
{
    {
        const std::lock_guard lock{mutex_};
        if (!running_) {
            return;
        }
        if (!options_.outbound && direction == Direction::Outbound) {
            ++rejected_;
            return;
        }
    }

    intern_name(incoming.id, incoming.name);

    std::vector<std::uint8_t> blob;
    pack(blob, incoming.body, incoming.error, incoming.fields);

    const std::lock_guard lock{mutex_};
    if (!running_) {
        return;
    }
    if (started_ < 0.0) {
        started_ = now();
        wall_started_ = wall_now();
    }

    const auto elapsed = now() - started_;
    Entry entry{
        .time = static_cast<std::uint64_t>(elapsed * 1e6),
        .body_length = static_cast<std::uint32_t>(incoming.body.size()),
        .blob_length = static_cast<std::uint32_t>(blob.size()),
        .unread = incoming.unread,
        .id = static_cast<std::int16_t>(incoming.id),
        .sub_id = incoming.sub_id,
        .flags = static_cast<std::uint8_t>((direction == Direction::Outbound ? kOutbound : 0U) |
                                           (incoming.decoded ? kDecoded : 0U) |
                                           (incoming.error.is_null() ? 0U : kHasError) |
                                           (incoming.fields.is_null() ? 0U : kHasFields)),
    };
    if (!store_.push(entry, std::move(blob))) {
        return;
    }

    const auto number = ++counter_;
    const auto length = incoming.body.size();
    if (!incoming.decoded) {
        ++bad_;
    }

    if (incoming.id >= 0) {
        const auto id = static_cast<std::size_t>(incoming.id);
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

    if (direction == Direction::Outbound) {
        ++outbound_;
        outbound_bytes_ += length;
    }
    else {
        ++inbound_;
        inbound_bytes_ += length;
    }

    const auto second = static_cast<std::size_t>(elapsed);
    if (second >= rates_.size()) {
        rates_.resize(second + 1);
    }
    ++rates_[second].packets;
    rates_[second].bytes += length;

    if (!incoming.error.is_null()) {
        std::string reason;
        if (const auto found = incoming.error.find("reason");
            found != incoming.error.end() && found->is_string()) {
            reason = found->get_ref<const std::string &>();
        }
        const auto at = std::ranges::find_if(failures_, [&](const Failure &failure) {
            return failure.id == incoming.id && failure.reason == reason;
        });
        if (at == failures_.end()) {
            failures_.push_back({
                .reason = reason,
                .name = interned_name(incoming.id),
                .id = incoming.id,
                .count = 1,
                .first = number,
                .last = number,
            });
        }
        else {
            ++at->count;
            at->last = number;
        }
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

Record Capture::at_number(const std::uint64_t number) const
{
    Entry entry;
    if (!store_.at(number, entry)) {
        return {};
    }
    return record_of(number, entry);
}

Details Capture::details(const std::uint64_t number) const
{
    Entry entry;
    if (!store_.at(number, entry)) {
        return {};
    }
    auto blob = store_.read(entry);
    return {.record = record_of(number, entry), .body = std::move(blob.body), .error = std::move(blob.error)};
}

std::optional<Details> Capture::selected_details() const
{
    auto found = details(selected());
    if (found.record.number == 0) {
        return std::nullopt;
    }
    return found;
}

Fields Capture::fields(const std::uint64_t number)
{
    {
        const std::lock_guard lock{fields_mutex_};
        if (const auto at = field_at_.find(number); at != field_at_.end()) {
            fields_.splice(fields_.begin(), fields_, at->second);
            return at->second->second;
        }
    }

    Entry entry;
    if (!store_.at(number, entry)) {
        return {};
    }

    auto decoded = store_.fields(number, entry);
    if (decoded.is_null()) {
        if (decode_mode(entry.id) == DecodeMode::Eager) {
            return {};
        }
        const auto blob = store_.read(entry);
        if (!blob.body) {
            return {};
        }
        decoded = decode_body(entry.id, {reinterpret_cast<const char *>(blob.body->data()), blob.body->size()});
        if (decoded.is_null()) {
            decoded = nlohmann::ordered_json::object();
        }
        store_.store_fields(number, decoded);
    }

    auto held = std::make_shared<const nlohmann::ordered_json>(std::move(decoded));

    const std::lock_guard lock{fields_mutex_};
    if (const auto at = field_at_.find(number); at != field_at_.end()) {
        return at->second->second;
    }
    fields_.emplace_front(number, std::move(held));
    field_at_[number] = fields_.begin();
    while (fields_.size() > kFieldCache) {
        field_at_.erase(fields_.back().first);
        fields_.pop_back();
    }
    return fields_.front().second;
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

std::vector<Failure> Capture::failures() const
{
    const std::lock_guard lock{mutex_};
    return failures_;
}

Counters Capture::counters() const
{
    const std::lock_guard lock{mutex_};
    return {
        .total = counter_,
        .bad = bad_,
        .rejected = rejected_,
        .dropped = store_.dropped(),
        .written = store_.written(),
        .stored_bytes = store_.stored_bytes(),
    };
}

Statistics Capture::statistics() const
{
    const std::lock_guard lock{mutex_};
    const auto written = store_.written();
    return {
        .total = counter_,
        .bad = bad_,
        .rejected = rejected_,
        .dropped = store_.dropped(),
        .inbound = inbound_,
        .outbound = outbound_,
        .inbound_bytes = inbound_bytes_,
        .outbound_bytes = outbound_bytes_,
        .written = written,
        .stored_bytes = store_.stored_bytes(),
        .oldest = written == 0 ? 0ULL : 1ULL,
        .newest = written,
        .duration = rates_.empty() ? 0.0 : static_cast<double>(rates_.size() - 1),
        .wall_start = wall_started_,
        .counts = counts_,
        .byte_counts = id_bytes_,
        .lengths = lengths_,
        .rates = rates_,
    };
}

const Store &Capture::store() const
{
    return store_;
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
    {
        const std::lock_guard lock{fields_mutex_};
        fields_.clear();
        field_at_.clear();
    }

    const std::lock_guard lock{mutex_};
    store_.restart();
    counts_.clear();
    id_bytes_.clear();
    failures_.clear();
    lengths_.fill(0);
    rates_.clear();
    counter_ = 0;
    bad_ = 0;
    rejected_ = 0;
    inbound_ = 0;
    outbound_ = 0;
    inbound_bytes_ = 0;
    outbound_bytes_ = 0;
    started_ = -1.0;
    wall_started_ = 0.0;
    selected_ = 0;
    running_ = true;
}

CaptureOptions Capture::options() const
{
    const std::lock_guard lock{mutex_};
    return options_;
}

void Capture::set_options(const CaptureOptions &options)
{
    const std::lock_guard lock{mutex_};
    if (options_ == options) {
        return;
    }
    options_ = options;
    store_.set_options(options.store);
}

}  // namespace spyglass
