#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace spyglass {

enum class Direction : int {
    Inbound = 0,
    Outbound = 1,
};

using Body = std::shared_ptr<const std::vector<std::uint8_t>>;

struct Node {
    std::string label;
    std::vector<Node> children;
};

constexpr std::uint8_t kOutbound = 1U << 0U;
constexpr std::uint8_t kDecoded = 1U << 1U;
constexpr std::uint8_t kHasError = 1U << 2U;

constexpr std::size_t kBlockEntries = 8192;

struct Entry {
    std::uint64_t offset{0};
    std::uint64_t time{0};
    std::uint32_t body_length{0};
    std::uint32_t blob_length{0};
    std::uint32_t unread{0};
    std::int16_t id{-1};
    std::uint8_t sub_id{0};
    std::uint8_t flags{0};
};

static_assert(sizeof(Entry) == 32);

struct Blob {
    Body body;
    std::optional<Node> error;
};

struct StoreOptions {
    std::size_t queue_bytes{4 * 1024 * 1024};
    std::size_t queue_records{4096};
    std::size_t resident_blocks{2048};

    bool operator==(const StoreOptions &other) const = default;
};

std::string_view interned_name(int id);
void intern_name(int id, std::string_view name);

void pack(std::vector<std::uint8_t> &blob, std::string_view body, const std::optional<Node> &error);

void sweep_captures();

class Store {
public:
    Store();
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool push(Entry entry, std::vector<std::uint8_t> blob);

    [[nodiscard]] std::uint64_t written() const;
    [[nodiscard]] std::uint64_t dropped() const;
    [[nodiscard]] std::uint64_t stored_bytes() const;
    [[nodiscard]] const std::filesystem::path &path() const;
    [[nodiscard]] std::string failure() const;

    [[nodiscard]] bool at(std::uint64_t number, Entry &entry) const;
    [[nodiscard]] Blob read(const Entry &entry) const;

    void restart();

    [[nodiscard]] StoreOptions options() const;
    void set_options(const StoreOptions &options);

private:
    struct Block {
        std::vector<Entry> entries;
        std::uint64_t used{0};
    };

    void run();
    void evict() const;
    [[nodiscard]] const Block *resident(std::uint64_t index) const;

    mutable std::mutex mutex_;
    mutable std::vector<std::unique_ptr<Block>> blocks_;
    mutable std::ifstream index_reader_;

    mutable std::mutex read_mutex_;
    mutable std::ifstream reader_;

    std::ofstream index_writer_;
    std::ofstream writer_;
    std::filesystem::path path_;
    std::filesystem::path index_path_;
    std::intptr_t lock_{-1};

    mutable std::mutex queue_mutex_;
    std::condition_variable ready_;
    std::deque<std::pair<Entry, std::vector<std::uint8_t>>> queue_;
    std::string failure_;
    std::size_t queued_bytes_{0};
    StoreOptions options_;

    std::atomic_size_t resident_budget_{StoreOptions{}.resident_blocks};
    std::atomic_uint64_t written_{0};
    std::atomic_uint64_t dropped_{0};
    std::atomic_uint64_t stored_bytes_{0};
    std::atomic_bool running_{true};
    std::uint64_t offset_{0};
    std::thread thread_;
};

}  // namespace spyglass
