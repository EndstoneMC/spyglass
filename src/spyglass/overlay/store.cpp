#include "spyglass/overlay/store.h"

#include <chrono>
#include <cstring>
#include <format>
#include <random>
#include <system_error>
#include <utility>

#ifdef _WIN32

#include <Windows.h>

#else

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#endif

#include "spyglass/error.h"
#include "spyglass/network.h"
#include "spyglass/signature.h"

namespace spyglass {
namespace {

constexpr std::string_view kMagic = "SPYGCAP";
constexpr std::string_view kPrefix = "spyglass_";
constexpr std::string_view kExtension = ".cap";
constexpr std::string_view kIndexExtension = ".index";
constexpr std::string_view kFieldsExtension = ".fields";
constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
constexpr std::uint32_t kVersion = 2;
constexpr int kRandomChars = 6;
constexpr std::intptr_t kNoLock = -1;
constexpr unsigned long kLockRegion = 0x40000000;
constexpr auto kIdle = std::chrono::milliseconds{50};

std::intptr_t take_lock(const std::filesystem::path &path)
{
#ifdef _WIN32
    auto *const handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return kNoLock;
    }
    OVERLAPPED at{};
    at.OffsetHigh = kLockRegion;
    if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at) == 0) {
        CloseHandle(handle);
        return kNoLock;
    }
    return reinterpret_cast<std::intptr_t>(handle);
#else
    const auto descriptor = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return kNoLock;
    }
    if (flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        close(descriptor);
        return kNoLock;
    }
    return descriptor;
#endif
}

void drop_lock(std::intptr_t &lock)
{
    if (lock == kNoLock) {
        return;
    }
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(lock));
#else
    close(static_cast<int>(lock));
#endif
    lock = kNoLock;
}

void put(std::vector<std::uint8_t> &out, const std::uint32_t value)
{
    const auto at = out.size();
    out.resize(at + sizeof(value));
    std::memcpy(out.data() + at, &value, sizeof(value));
}

void put(std::vector<std::uint8_t> &out, const std::string_view value)
{
    put(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool take(std::string_view &in, std::uint32_t &value)
{
    if (in.size() < sizeof(value)) {
        return false;
    }
    std::memcpy(&value, in.data(), sizeof(value));
    in.remove_prefix(sizeof(value));
    return true;
}

nlohmann::ordered_json take_document(std::string_view &in)
{
    std::uint32_t length = 0;
    if (!take(in, length) || length > in.size()) {
        return {};
    }

    nlohmann::ordered_json document;
    try {
        document = nlohmann::ordered_json::from_msgpack(in.begin(), in.begin() + length, true, false);
    }
    catch (...) {
        document = {};
    }
    in.remove_prefix(length);
    return document.is_discarded() ? nlohmann::ordered_json{} : document;
}

std::vector<std::atomic<const std::string *>> &fallback_names()
{
    static std::vector<std::atomic<const std::string *>> table(static_cast<std::size_t>(signatures().max_packet_id) +
                                                               1);
    return table;
}

}  // namespace

std::string_view interned_name(const int id)
{
    if (id < 0) {
        return {};
    }
    const auto slot = static_cast<std::size_t>(id);
    if (const auto &table = packet_names(); slot < table.size() && !table[slot].empty()) {
        return table[slot];
    }
    auto &table = fallback_names();
    if (slot >= table.size()) {
        return {};
    }
    const auto *name = table[slot].load(std::memory_order_acquire);
    return name == nullptr ? std::string_view{} : std::string_view{*name};
}

void intern_name(const int id, const std::string_view name)
{
    auto &table = fallback_names();
    if (id < 0 || name.empty() || static_cast<std::size_t>(id) >= table.size()) {
        return;
    }
    auto &slot = table[static_cast<std::size_t>(id)];
    if (slot.load(std::memory_order_acquire) != nullptr) {
        return;
    }
    const auto *fresh = new std::string{name};
    const std::string *unset = nullptr;
    if (!slot.compare_exchange_strong(unset, fresh, std::memory_order_release, std::memory_order_acquire)) {
        delete fresh;
    }
}

void pack(std::vector<std::uint8_t> &blob, const std::string_view body, const nlohmann::ordered_json &error,
          const nlohmann::ordered_json &fields)
{
    blob.assign(body.begin(), body.end());
    for (const auto *document : {&error, &fields}) {
        if (document->is_null()) {
            continue;
        }
        const auto at = blob.size();
        put(blob, std::uint32_t{0});
        nlohmann::ordered_json::to_msgpack(*document, blob);
        const auto length = static_cast<std::uint32_t>(blob.size() - at - sizeof(std::uint32_t));
        std::memcpy(blob.data() + at, &length, sizeof(length));
    }
}

Store::Store()
{
    restart();
    thread_ = std::thread{[this] { run(); }};
}

Store::~Store()
{
    running_.store(false, std::memory_order_relaxed);
    ready_.notify_all();
    thread_.detach();

    writer_.close();
    index_writer_.close();
    fields_writer_.close();
    reader_.close();
    index_reader_.close();
    fields_reader_.close();
    drop_lock(lock_);

    std::error_code ec;
    std::filesystem::remove(path_, ec);
    std::filesystem::remove(index_path_, ec);
    std::filesystem::remove(fields_path_, ec);
}

void sweep_captures()
{
    std::error_code ec;
    const auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) {
        return;
    }
    for (const auto &item : std::filesystem::directory_iterator{directory, ec}) {
        const auto name = item.path().filename().string();
        if (!name.starts_with(kPrefix) || item.path().extension() != kExtension) {
            continue;
        }
        auto lock = take_lock(item.path());
        if (lock == kNoLock) {
            continue;
        }
        drop_lock(lock);
        std::filesystem::remove(item.path(), ec);
        std::filesystem::remove(std::filesystem::path{item.path()}.replace_extension(kIndexExtension), ec);
        std::filesystem::remove(std::filesystem::path{item.path()}.replace_extension(kFieldsExtension), ec);
    }
}

void Store::restart()
{
    const std::scoped_lock lock{mutex_, read_mutex_, queue_mutex_, fields_mutex_};

    field_at_.clear();
    fields_offset_ = 0;
    queue_.clear();
    queued_bytes_ = 0;
    blocks_.clear();
    written_.store(0, std::memory_order_relaxed);
    dropped_.store(0, std::memory_order_relaxed);
    stored_bytes_.store(0, std::memory_order_relaxed);
    offset_ = 0;
    failure_.clear();

    writer_.close();
    index_writer_.close();
    fields_writer_.close();
    reader_.close();
    index_reader_.close();
    fields_reader_.close();
    writer_.clear();
    index_writer_.clear();
    fields_writer_.clear();
    reader_.clear();
    index_reader_.clear();
    fields_reader_.clear();
    drop_lock(lock_);

    std::error_code ec;
    if (!path_.empty()) {
        std::filesystem::remove(path_, ec);
        std::filesystem::remove(index_path_, ec);
        std::filesystem::remove(fields_path_, ec);
    }

    const auto directory = std::filesystem::temp_directory_path(ec);
    if (ec) {
        failure_ = std::format("no temporary directory: {}", ec.message());
        report_error(failure_);
        return;
    }

    std::random_device source;
    std::uniform_int_distribution<std::size_t> pick{0, kAlphabet.size() - 1};
    for (auto attempt = 0; attempt < 64 && !writer_.is_open(); ++attempt) {
        std::string name{kPrefix};
        for (auto character = 0; character < kRandomChars; ++character) {
            name += kAlphabet[pick(source)];
        }
        path_ = directory / (name + std::string{kExtension});
        index_path_ = directory / (name + std::string{kIndexExtension});
        fields_path_ = directory / (name + std::string{kFieldsExtension});
        if (std::filesystem::exists(path_, ec)) {
            continue;
        }
        writer_.open(path_, std::ios::binary | std::ios::trunc);
    }

    index_writer_.open(index_path_, std::ios::binary | std::ios::trunc);
    fields_writer_.open(fields_path_, std::ios::binary | std::ios::trunc);
    if (!writer_ || !index_writer_ || !fields_writer_) {
        failure_ = std::format("could not open {}", path_.string());
        report_error(failure_);
        return;
    }
    lock_ = take_lock(path_);

    const auto &client = signatures().name;
    std::vector<std::uint8_t> header;
    header.assign(kMagic.begin(), kMagic.end());
    header.push_back(0);
    put(header, kVersion);
    put(header, client);
    writer_.write(reinterpret_cast<const char *>(header.data()), static_cast<std::streamsize>(header.size()));
    writer_.flush();
    offset_ = header.size();

    reader_.open(path_, std::ios::binary);
    index_reader_.open(index_path_, std::ios::binary);
    fields_reader_.open(fields_path_, std::ios::binary);
    if (!reader_ || !index_reader_ || !fields_reader_) {
        failure_ = std::format("could not read back {}", path_.string());
        report_error(failure_);
    }
}

bool Store::push(Entry entry, std::vector<std::uint8_t> blob)
{
    bool idle = false;
    {
        const std::lock_guard lock{queue_mutex_};
        if (!failure_.empty() || queue_.size() >= options_.queue_records ||
            queued_bytes_ + blob.size() > options_.queue_bytes) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        idle = queue_.empty();
        queued_bytes_ += blob.size();
        queue_.emplace_back(entry, std::move(blob));
    }
    if (idle) {
        ready_.notify_one();
    }
    return true;
}

void Store::run()
{
    std::deque<std::pair<Entry, std::vector<std::uint8_t>>> batch;
    while (running_.load(std::memory_order_relaxed)) {
        {
            std::unique_lock lock{queue_mutex_};
            ready_.wait_for(lock, kIdle,
                            [this] { return !queue_.empty() || !running_.load(std::memory_order_relaxed); });
            batch.swap(queue_);
            queued_bytes_ = 0;
        }
        if (batch.empty()) {
            continue;
        }

        for (auto &[entry, blob] : batch) {
            const auto length = static_cast<std::uint32_t>(blob.size());
            entry.offset = offset_ + sizeof(length);
            writer_.write(reinterpret_cast<const char *>(&length), sizeof(length));
            writer_.write(reinterpret_cast<const char *>(blob.data()), static_cast<std::streamsize>(blob.size()));
            writer_.write(reinterpret_cast<const char *>(&length), sizeof(length));
            index_writer_.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
            offset_ += (2 * sizeof(length)) + blob.size();
        }
        writer_.flush();
        index_writer_.flush();
        if (!writer_ || !index_writer_) {
            const std::lock_guard lock{queue_mutex_};
            if (failure_.empty()) {
                failure_ = std::format("could not write {}", path_.string());
                report_error(failure_);
            }
        }

        {
            const std::lock_guard lock{mutex_};
            auto index = written_.load(std::memory_order_relaxed);
            for (const auto &pending : batch) {
                const auto block = static_cast<std::size_t>(index / kBlockEntries);
                const auto slot = static_cast<std::size_t>(index % kBlockEntries);
                if (block >= blocks_.size()) {
                    blocks_.resize(block + 1);
                }
                if (!blocks_[block]) {
                    blocks_[block] = std::make_unique<Block>();
                    blocks_[block]->entries.resize(kBlockEntries);
                }
                blocks_[block]->entries[slot] = pending.first;
                blocks_[block]->used = slot + 1;
                ++index;
            }
            evict();
        }

        stored_bytes_.store(offset_, std::memory_order_relaxed);
        written_.fetch_add(batch.size(), std::memory_order_release);
        batch.clear();
    }
}

void Store::evict() const
{
    const auto budget = resident_budget_.load(std::memory_order_relaxed);
    if (blocks_.size() <= budget) {
        return;
    }
    const auto tail = blocks_.size() - 1;
    auto resident = std::size_t{0};
    for (const auto &block : blocks_) {
        resident += block ? 1 : 0;
    }
    for (std::size_t index = 0; index < tail && resident > budget; ++index) {
        if (blocks_[index]) {
            blocks_[index].reset();
            --resident;
        }
    }
}

const Store::Block *Store::resident(const std::uint64_t index) const
{
    const auto block = static_cast<std::size_t>(index / kBlockEntries);
    if (block < blocks_.size() && blocks_[block]) {
        return blocks_[block].get();
    }
    if (!index_reader_) {
        return nullptr;
    }

    auto fresh = std::make_unique<Block>();
    fresh->entries.resize(kBlockEntries);
    index_reader_.clear();
    index_reader_.seekg(static_cast<std::streamoff>(block * kBlockEntries * sizeof(Entry)));
    index_reader_.read(reinterpret_cast<char *>(fresh->entries.data()),
                       static_cast<std::streamsize>(kBlockEntries * sizeof(Entry)));
    fresh->used = static_cast<std::uint64_t>(index_reader_.gcount()) / sizeof(Entry);
    if (fresh->used == 0) {
        return nullptr;
    }

    if (block >= blocks_.size()) {
        blocks_.resize(block + 1);
    }
    blocks_[block] = std::move(fresh);
    evict();
    return blocks_[block].get();
}

bool Store::at(const std::uint64_t number, Entry &entry) const
{
    if (number == 0 || number > written_.load(std::memory_order_acquire)) {
        return false;
    }
    const auto index = number - 1;
    const std::lock_guard lock{mutex_};
    const auto *block = resident(index);
    if (block == nullptr || index % kBlockEntries >= block->used) {
        return false;
    }
    entry = block->entries[index % kBlockEntries];
    return true;
}

Blob Store::read(const Entry &entry, const bool with_fields) const
{
    std::vector<std::uint8_t> raw;
    {
        const std::lock_guard lock{read_mutex_};
        if (!reader_.is_open()) {
            return {};
        }
        if (entry.blob_length == 0) {
            return {.body = std::make_shared<const std::vector<std::uint8_t>>()};
        }
        raw.resize(entry.blob_length);
        reader_.clear();
        reader_.seekg(static_cast<std::streamoff>(entry.offset));
        reader_.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(entry.blob_length));
        if (reader_.gcount() != static_cast<std::streamsize>(entry.blob_length)) {
            return {};
        }
    }

    Blob blob;
    blob.body = std::make_shared<const std::vector<std::uint8_t>>(raw.begin(), raw.begin() + entry.body_length);
    std::string_view rest{reinterpret_cast<const char *>(raw.data()) + entry.body_length,
                          entry.blob_length - entry.body_length};
    if ((entry.flags & kHasError) != 0) {
        blob.error = take_document(rest);
        if (blob.error.is_null()) {
            return blob;
        }
    }
    if (with_fields && (entry.flags & kHasFields) != 0) {
        blob.fields = take_document(rest);
    }
    return blob;
}

nlohmann::ordered_json Store::fields(const std::uint64_t number, const Entry &entry) const
{
    if ((entry.flags & kHasFields) != 0) {
        return read(entry, true).fields;
    }

    const std::lock_guard lock{fields_mutex_};
    const auto at = field_at_.find(number);
    if (at == field_at_.end() || !fields_reader_.is_open()) {
        return {};
    }

    std::vector<std::uint8_t> raw(at->second.second);
    fields_reader_.clear();
    fields_reader_.seekg(static_cast<std::streamoff>(at->second.first));
    fields_reader_.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (fields_reader_.gcount() != static_cast<std::streamsize>(raw.size())) {
        return {};
    }

    std::string_view rest{reinterpret_cast<const char *>(raw.data()), raw.size()};
    return take_document(rest);
}

void Store::store_fields(const std::uint64_t number, const nlohmann::ordered_json &fields)
{
    std::vector<std::uint8_t> raw;
    put(raw, std::uint32_t{0});
    nlohmann::ordered_json::to_msgpack(fields, raw);
    const auto length = static_cast<std::uint32_t>(raw.size() - sizeof(std::uint32_t));
    std::memcpy(raw.data(), &length, sizeof(length));

    const std::lock_guard lock{fields_mutex_};
    if (!fields_writer_.is_open() || field_at_.contains(number)) {
        return;
    }
    fields_writer_.write(reinterpret_cast<const char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
    fields_writer_.flush();
    if (!fields_writer_) {
        return;
    }
    field_at_.emplace(number, std::pair{fields_offset_, static_cast<std::uint32_t>(raw.size())});
    fields_offset_ += raw.size();
}

std::uint64_t Store::written() const
{
    return written_.load(std::memory_order_acquire);
}

std::uint64_t Store::dropped() const
{
    return dropped_.load(std::memory_order_relaxed);
}

std::uint64_t Store::stored_bytes() const
{
    return stored_bytes_.load(std::memory_order_relaxed);
}

const std::filesystem::path &Store::path() const
{
    return path_;
}

std::string Store::failure() const
{
    const std::lock_guard lock{queue_mutex_};
    return failure_;
}

StoreOptions Store::options() const
{
    const std::lock_guard lock{queue_mutex_};
    return options_;
}

void Store::set_options(const StoreOptions &options)
{
    const std::lock_guard lock{queue_mutex_};
    options_ = options;
    resident_budget_.store(options.resident_blocks, std::memory_order_relaxed);
}

}  // namespace spyglass
