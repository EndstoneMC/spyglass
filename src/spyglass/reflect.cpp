#include "spyglass/reflect.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <entt/core/any.hpp>
#include <entt/core/type_info.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>

#include "bedrock/cereal/context.h"
#include "bedrock/cereal/schema/basic_schema.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/network/packet.h"
#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {

static_assert(entt::type_hash<cereal::internal::BasicSchema::TypeDescriptor>::value() == 0xD4C25870);
static_assert(entt::type_hash<cereal::internal::BasicSchema::MemberDescriptor>::value() == 0xBA234EDA);

namespace {

constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxElements = 64;
constexpr int kMaxNodes = 4096;
constexpr std::size_t kMaxText = 96;
constexpr std::size_t kMaxBytes = 16;

int g_nodes = 0;

std::string blob_or_text(const std::string_view raw, const std::size_t total)
{
    const auto sampled = std::min<std::size_t>(raw.size(), kMaxText);
    std::size_t boundary = 0;
    auto readable = true;
    for (std::size_t i = 0; i < sampled && readable;) {
        const auto byte = static_cast<unsigned char>(raw[i]);
        std::size_t width = 0;
        if (byte == '\t' || byte == '\n' || byte == '\r' || (byte >= 0x20 && byte < 0x7F)) {
            width = 1;
        }
        else if (byte >= 0xC2 && byte <= 0xDF) {
            width = 2;
        }
        else if (byte >= 0xE0 && byte <= 0xEF) {
            width = 3;
        }
        else if (byte >= 0xF0 && byte <= 0xF4) {
            width = 4;
        }
        else {
            readable = false;
            break;
        }

        if (i + width > sampled) {
            break;
        }
        for (std::size_t k = 1; k < width && readable; ++k) {
            const auto continuation = static_cast<unsigned char>(raw[i + k]);
            readable = continuation >= 0x80 && continuation <= 0xBF;
        }
        if (readable) {
            i += width;
            boundary = i;
        }
    }

    if (!readable) {
        const auto shown = std::min<std::size_t>(raw.size(), kMaxBytes);
        std::string text = std::format("{} bytes:", total);
        for (std::size_t i = 0; i < shown; ++i) {
            text += std::format(" {:02x}", static_cast<unsigned char>(raw[i]));
        }
        if (total > shown) {
            text += " ...";
        }
        return text;
    }

    std::string text{"\""};
    for (std::size_t i = 0; i < boundary; ++i) {
        const auto byte = static_cast<unsigned char>(raw[i]);
        if (byte == '\t') {
            text += "\\t";
        }
        else if (byte == '\n') {
            text += "\\n";
        }
        else if (byte == '\r') {
            text += "\\r";
        }
        else {
            text.push_back(static_cast<char>(byte));
        }
    }
    text.push_back('"');
    if (total > boundary) {
        text += std::format(" ... ({} bytes)", total);
    }
    return text;
}

std::string text_of(const entt::meta_any &value)
{
    if (const auto *v = value.try_cast<bool>()) {
        return *v ? "true" : "false";
    }
    const auto *owned = value.try_cast<std::string>();
    const auto *viewed = owned != nullptr ? nullptr : value.try_cast<std::string_view>();
    if (owned != nullptr || viewed != nullptr) {
        const std::string_view raw = owned != nullptr ? std::string_view{*owned} : *viewed;
        return blob_or_text(raw, raw.size());
    }
    if (const auto *v = value.try_cast<float>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<double>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::int8_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::uint8_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::int16_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::uint16_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::int32_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::uint32_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::int64_t>()) {
        return std::format("{}", *v);
    }
    if (const auto *v = value.try_cast<std::uint64_t>()) {
        return std::format("{}", *v);
    }

    const auto type = value.type();
    if (type.is_enum()) {
        const auto number = [](const entt::meta_any &any) -> std::optional<std::uint64_t> {
            if (const auto converted = any.allow_cast<std::uint64_t>(); converted) {
                if (const auto *raw = converted.try_cast<std::uint64_t>()) {
                    return *raw;
                }
            }
            return std::nullopt;
        };

        const auto current = number(value);
        if (!current) {
            return {};
        }
        for (auto &&[constant_id, data] : type.data()) {
            const auto constant = number(data.get({}));
            if (!constant || *constant != *current) {
                continue;
            }
            std::string name;
            if (const cereal::internal::BasicSchema::MemberDescriptor *descriptor = data.custom()) {
                name = descriptor->mName;
            }
            if (name.empty()) {
                name = std::string{data.name()};
            }
            if (!name.empty()) {
                return std::format("{} ({})", name, *current);
            }
        }
        return std::format("{}", *current);
    }
    return {};
}

void append(Node &parent, entt::meta_any value, std::string name, int depth);

void append_members(Node &node, entt::meta_any &value, const int depth)
{
    if (depth > kMaxDepth) {
        return;
    }
    const auto type = value.type();
    for (auto &&[base_id, base] : type.base()) {
        const auto base_type = base.type();
        if (!base_type || base_type.id() == type.id()) {
            continue;
        }
        if (auto upcast = value.as_ref();
            upcast.allow_cast(base_type) && upcast.type().id() != type.id()) {
            append_members(node, upcast, depth + 1);
        }
    }
    auto added = false;
    for (auto &&[member_id, data] : type.data()) {
        std::string name;
        if (const cereal::internal::BasicSchema::MemberDescriptor *descriptor = data.custom()) {
            name = descriptor->mName;
        }
        if (name.empty()) {
            name = std::string{data.name()};
        }
        append(node, data.get(value), std::move(name), depth + 1);
        added = true;
    }

    if (!added) {
        if (const auto getter = type.func(cereal::internal::kCustomGetter)) {
            if (auto payload = getter.invoke(value); payload) {
                append_members(node, payload, depth + 1);
            }
        }
    }
}

void append(Node &parent, entt::meta_any value, std::string name, const int depth)
{
    if (!value || depth > kMaxDepth || ++g_nodes > kMaxNodes) {
        return;
    }

    if (const auto text = text_of(value); !text.empty()) {
        parent.children.push_back({.label = std::format("{}: {}", name, text)});
        return;
    }

    const auto type = value.type();

    if (type.is_sequence_container()) {
        auto elements = value.as_sequence_container();

        if (const auto element = elements.value_type();
            element && element.size_of() == 1 && !element.is_class() && !element.is_enum()) {
            std::string sample;
            for (const auto &byte : elements) {
                if (sample.size() >= kMaxText) {
                    break;
                }
                if (const auto number = byte.allow_cast<std::uint64_t>(); number) {
                    if (const auto *raw = number.try_cast<std::uint64_t>()) {
                        sample.push_back(static_cast<char>(*raw));
                    }
                }
            }
            parent.children.push_back(
                {.label = std::format("{}: {}", name, blob_or_text(sample, elements.size()))});
            return;
        }

        Node node{.label = std::format("{} [{}]", name, elements.size())};
        std::size_t index = 0;
        for (auto &&element : elements) {
            if (index >= kMaxElements) {
                node.children.push_back({.label = "..."});
                break;
            }
            append(node, element.as_ref(), std::format("[{}]", index), depth + 1);
            ++index;
        }
        parent.children.push_back(std::move(node));
        return;
    }

    if (type.is_associative_container()) {
        auto entries = value.as_associative_container();
        Node node{.label = std::format("{} [{}]", name, entries.size())};
        std::size_t index = 0;
        for (auto &&[key, mapped] : entries) {
            if (index >= kMaxElements) {
                node.children.push_back({.label = "..."});
                break;
            }
            append(node, mapped.as_ref(), text_of(key), depth + 1);
            ++index;
        }
        parent.children.push_back(std::move(node));
        return;
    }

    if (type.is_class()) {
        Node node{.label = name};
        append_members(node, value, depth);
        if (node.children.empty()) {
            node.label = std::format("{}: <class \"{}\" id {:#x} size {}>", name, type.name(), type.id(),
                                     type.size_of());
        }
        parent.children.push_back(std::move(node));
        return;
    }

    parent.children.push_back({.label = std::format("{}: <\"{}\" id {:#x} size {}{}{}>", name, type.name(), type.id(),
                                                    type.size_of(), type.is_enum() ? " enum" : "",
                                                    type.is_class() ? " class" : "")});
}

const std::unordered_map<int, entt::meta_type> &packet_types(const entt::meta_ctx &meta_ctx)
{
    static const auto types = [&meta_ctx] {
        std::unordered_map<int, entt::meta_type> found;
        for (auto &&[type_id, type] : entt::resolve(meta_ctx)) {
            const cereal::internal::BasicSchema::TypeDescriptor *descriptor = type.custom();
            if (descriptor == nullptr) {
                continue;
            }
            for (const auto &entry : descriptor->mUserPropertiesMap) {
                if (entry.first != "[cereal:packet]") {
                    continue;
                }
                if (const auto *id = entt::any_cast<int>(&entry.second.second)) {
                    found.emplace(*id, type);
                }
            }
        }
        return found;
    }();
    return types;
}

std::optional<Node> decode(const Record &record)
{
    g_nodes = 0;
    const auto *ctx = reflection_ctx();
    if (ctx == nullptr || !record.body || record.body->empty() || record.id < 0) {
        return std::nullopt;
    }

    const auto packet = create_packet(record.id);
    if (!packet) {
        return std::nullopt;
    }
    packet->setSerializationMode(SerializationMode::CerealOnly);

    const std::string_view body{reinterpret_cast<const char *>(record.body->data()), record.body->size()};
    ReadOnlyBinaryStream stream{body, true};

    const auto result = read_no_header(*packet, stream, *ctx, static_cast<SubClientId>(record.sub_id));

    const auto &types = packet_types(ctx->internal().mMetaCtx);
    const auto entry = types.find(record.id);
    if (entry == types.end()) {
        return std::nullopt;
    }

    auto instance = entry->second.from_void(packet.get(), false);
    if (!instance) {
        return std::nullopt;
    }

    Node root{.label = "Fields"};
    if (!result.asExpected().has_value()) {
        const auto consumed = stream.getReadPointer();
        const auto size = record.body->size();
        Node stopped{.label = consumed < size
                                  ? std::format("decoded, {} of {} bytes unconsumed", size - consumed, size)
                                  : std::format("decode failed after {} of {} bytes", consumed, size)};
        if (auto reason = error_node(result)) {
            stopped.children.push_back(std::move(*reason));
        }
        root.children.push_back(std::move(stopped));
    }
    append_members(root, instance, 0);
    if (root.children.empty()) {
        const auto type = entry->second;
        std::size_t bases = 0;
        for (auto it = type.base().begin(); it != type.base().end(); ++it) {
            ++bases;
        }
        std::size_t members = 0;
        for (auto it = type.data().begin(); it != type.data().end(); ++it) {
            ++members;
        }
        root.children.push_back(
            {.label = std::format("no fields: types {} bases {} members {} getter {}", types.size(), bases, members,
                                  type.func(cereal::internal::kCustomGetter) ? "yes" : "no")});
    }
    return root;
}

}  // namespace

const Node *decoded_fields(const Record &record)
{
    static std::uint64_t cached_number = 0;
    static const void *cached_body = nullptr;
    static std::optional<Node> cached;

    if (record.number != cached_number || record.body.get() != cached_body) {
        cached_number = record.number;
        cached_body = record.body.get();
        cached = decode(record);
    }
    return cached ? &*cached : nullptr;
}

}  // namespace spyglass
