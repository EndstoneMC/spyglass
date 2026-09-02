#include "spyglass/reflect.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include <entt/core/any.hpp>
#include <entt/core/type_info.hpp>
#include <entt/locator/locator.hpp>
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <entt/meta/template.hpp>

#include "bedrock/cereal/context.h"
#include "bedrock/cereal/schema/basic_schema.h"
#include "bedrock/core/string/string_hash.h"
#include "bedrock/core/utility/binary_stream.h"
#include "bedrock/nbt/byte_array_tag.h"
#include "bedrock/nbt/byte_tag.h"
#include "bedrock/nbt/compound_tag.h"
#include "bedrock/nbt/list_tag.h"
#include "bedrock/nbt/tag.h"
#include "bedrock/network/packet.h"
#include "bedrock/platform/uuid.h"
#include "bedrock/version.h"
#include "spyglass/network.h"
#include "spyglass/overlay/bytes.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {

static_assert(entt::type_hash<cereal::internal::BasicSchema::TypeDescriptor>::value() == 0xD4C25870);
static_assert(entt::type_hash<cereal::internal::BasicSchema::MemberDescriptor>::value() == 0xBA234EDA);
static_assert(entt::type_hash<cereal::internal::BasicSchema::GetterDescriptor>::value() == 0xBE28A3EF);
static_assert(entt::type_hash<cereal::internal::BasicSchema::SetterDescriptor>::value() == 0x0AC66523);
static_assert(entt::type_hash<CompoundTag>::value() == 0xBD1A8574);

namespace {

template <typename T> entt::id_type type_key(const T &type)
{
    if constexpr (requires { type.id(); }) {
        return type.id();
    }
    else {
        return type.alias();
    }
}

template <template <typename...> class T> bool is_specialization(const entt::meta_type &type)
{
    const auto *context = reflection_ctx();
    return context != nullptr && type.is_template_specialization() &&
           type.template_type() == entt::resolve<entt::meta_class_template_tag<T>>(context->internal().mMetaCtx);
}

const unsigned char *bytes_of(const entt::meta_any &value)
{
    return static_cast<const unsigned char *>(value.base().data());
}

nlohmann::ordered_json text_or_bytes(const std::string_view raw)
{
    auto readable = true;
    for (std::size_t i = 0; i < raw.size() && readable;) {
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

        if (i + width > raw.size()) {
            readable = false;
            break;
        }
        for (std::size_t k = 1; k < width && readable; ++k) {
            const auto continuation = static_cast<unsigned char>(raw[i + k]);
            readable = continuation >= 0x80 && continuation <= 0xBF;
        }
        i += width;
    }

    if (readable) {
        return std::string{raw};
    }
    const std::span bytes{reinterpret_cast<const std::uint8_t *>(raw.data()), raw.size()};
    const auto encoded = format_bytes(bytes, 0, BytesFormat::Base64);
    std::string wrapped;
    wrapped.reserve(encoded.size() + 6);
    wrapped += "atob(";
    wrapped += encoded;
    wrapped += ')';
    return wrapped;
}

bool value_of(nlohmann::ordered_json &out, const entt::meta_any &value)
{
    if (const auto *v = value.try_cast<bool>()) {
        out = *v;
        return true;
    }
    if (const auto *v = value.try_cast<HashedString>()) {
        out = text_or_bytes(v->getString());
        return true;
    }
    if (const auto *v = value.try_cast<mce::UUID>()) {
        out = std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", v->data[0] >> 32, (v->data[0] >> 16) & 0xFFFF,
                          v->data[0] & 0xFFFF, v->data[1] >> 48, v->data[1] & 0xFFFF'FFFF'FFFF);
        return true;
    }
    if (const auto *v = value.try_cast<const CompoundTag>()) {
        if (value.type().size_of() != sizeof(CompoundTag)) {
            out = "<compound tag, layout not recognised>";
            return true;
        }
        return false;
    }
    const auto *owned = value.try_cast<std::string>();
    const auto *viewed = owned != nullptr ? nullptr : value.try_cast<std::string_view>();
    if (owned != nullptr || viewed != nullptr) {
        out = text_or_bytes(owned != nullptr ? std::string_view{*owned} : *viewed);
        return true;
    }
    if (const auto *v = value.try_cast<float>()) {
        out = static_cast<double>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<double>()) {
        out = *v;
        return true;
    }
    if (const auto *v = value.try_cast<std::int8_t>()) {
        out = static_cast<std::int64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::uint8_t>()) {
        out = static_cast<std::uint64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::int16_t>()) {
        out = static_cast<std::int64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::uint16_t>()) {
        out = static_cast<std::uint64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::int32_t>()) {
        out = static_cast<std::int64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::uint32_t>()) {
        out = static_cast<std::uint64_t>(*v);
        return true;
    }
    if (const auto *v = value.try_cast<std::int64_t>()) {
        out = *v;
        return true;
    }
    if (const auto *v = value.try_cast<std::uint64_t>()) {
        out = *v;
        return true;
    }

    const auto type = value.type();
    if (type.is_enum()) {
        const auto number = [](const entt::meta_any &any) -> std::optional<std::uint64_t> {
            if (const auto converted = any.allow_cast<std::uint64_t>(); converted) {
                if (const auto *raw = converted.try_cast<std::uint64_t>()) {
                    return *raw;
                }
            }
            return {};
        };

        const auto current = number(value);
        if (!current) {
            return false;
        }
        for (auto &&[constant_id, data] : type.data()) {
            const auto constant = number(data.get({}));
            if (!constant || *constant != *current) {
                continue;
            }
            std::string name;
            if (const cereal::internal::BasicSchema::MemberDescriptor *descriptor = data.custom()) {
                name = descriptor->mNameExt.empty() ? descriptor->mName : descriptor->mNameExt;
            }
            if (name.empty()) {
                name = std::string{data.name()};
            }
            if (!name.empty()) {
                out = std::format("{} ({})", name, *current);
                return true;
            }
        }
        out = std::format("{}", *current);
        return true;
    }
    return false;
}

using KeyIndex = std::optional<std::unordered_set<std::string>>;

constexpr std::size_t kIndexedFrom = 256;

void put(nlohmann::ordered_json &parent, std::string name, nlohmann::ordered_json value,
         KeyIndex *const seen = nullptr)
{
    if (parent.is_array()) {
        parent.push_back(std::move(value));
        return;
    }
    if (!parent.is_object()) {
        return;
    }

    auto &entries = parent.get_ref<nlohmann::ordered_json::object_t &>();
    if (seen != nullptr && !seen->has_value() && entries.size() >= kIndexedFrom) {
        auto &index = seen->emplace();
        index.reserve(entries.size());
        for (const auto &entry : entries) {
            index.insert(entry.first);
        }
    }
    const auto indexed = seen != nullptr && seen->has_value();
    const auto taken = [&entries, seen, indexed](const std::string &key) {
        return indexed ? (*seen)->contains(key)
                       : std::ranges::any_of(entries, [&key](const auto &entry) { return entry.first == key; });
    };
    if (taken(name)) {
        const auto base = name;
        for (auto suffix = 2; taken(name); ++suffix) {
            name = std::format("{}#{}", base, suffix);
        }
    }
    if (indexed) {
        (*seen)->insert(name);
    }
    entries.emplace_back(std::move(name), std::move(value));
}

constexpr int kDeepestType = 24;
constexpr int kMaxTagDepth = 16;
constexpr int kMaxTagNodes = 4096;

thread_local int g_tag_nodes = 0;

void append_tag(nlohmann::ordered_json &parent, const Tag &tag, const Tag::Type type, const std::string_view name,
                const int depth, KeyIndex *const seen = nullptr)
{
    if (depth > kMaxTagDepth || ++g_tag_nodes > kMaxTagNodes) {
        return;
    }

    switch (type) {
    case Tag::Type::End:
    case Tag::Type::Short:
    case Tag::Type::Int:
    case Tag::Type::Int64:
    case Tag::Type::Float:
    case Tag::Type::Double:
    case Tag::Type::String:
    case Tag::Type::IntArray:
        put(parent, std::string{name}, tag.toString(), seen);
        return;
    case Tag::Type::Byte:
        put(parent, std::string{name}, static_cast<unsigned>(static_cast<const ByteTag &>(tag).data), seen);
        return;
    case Tag::Type::ByteArray: {
        const auto &data = static_cast<const ByteArrayTag &>(tag).mData;
        const auto *bytes = reinterpret_cast<const char *>(data.data());
        put(parent, std::string{name},
            text_or_bytes(bytes != nullptr ? std::string_view{bytes, data.size()} : std::string_view{}), seen);
        return;
    }
    case Tag::Type::List: {
        const auto &list = static_cast<const ListTag &>(tag);
        const auto shown = list.mList.data() != nullptr ? list.mList.size() : 0;
        auto node = nlohmann::ordered_json::array();
        for (std::size_t i = 0; i < shown; ++i) {
            const Tag *element = list.mList[i].get();
            if (element != nullptr && reinterpret_cast<std::uintptr_t>(element) % alignof(Tag) == 0) {
                append_tag(node, *element, list.mType, {}, depth + 1);
            }
        }
        put(parent, std::string{name}, std::move(node), seen);
        return;
    }
    case Tag::Type::Compound: {
        const auto &tags = static_cast<const CompoundTag &>(tag).rawView();
        auto node = nlohmann::ordered_json::object();
        KeyIndex keys;
        for (const auto &[key, value] : tags) {
            append_tag(node, *value, value.index(), key, depth + 1, &keys);
        }
        if (node.empty()) {
            if (auto summary = tag.toString(); summary != "{}") {
                put(parent, std::string{name}, text_or_bytes(summary), seen);
                return;
            }
        }
        put(parent, std::string{name}, std::move(node), seen);
        return;
    }
    default:
        put(parent, std::format("?? {}", name), std::format("<tag type {}>", static_cast<int>(type)), seen);
        return;
    }
}

void append(nlohmann::ordered_json &parent, entt::meta_any value, std::string name, int depth,
            KeyIndex *seen);

void append_members(nlohmann::ordered_json &node, entt::meta_any &value, const int depth,
                    KeyIndex *const seen)
{
    const auto type = value.type();
    for (auto &&[base_id, base] : type.base()) {
        const auto base_type = base.type();
        if (!base_type || type_key(base_type) == type_key(type)) {
            continue;
        }
        if (auto upcast = value.as_ref(); upcast.allow_cast(base_type) && type_key(upcast.type()) != type_key(type)) {
            append_members(node, upcast, depth + 1, seen);
        }
    }
    auto added = false;
    for (auto &&[member_id, data] : type.data()) {
        std::string name;
        const cereal::internal::BasicSchema::MemberDescriptor *descriptor = data.custom();
        if (descriptor != nullptr) {
            name = descriptor->mName;
        }
        if (name.empty()) {
            name = std::string{data.name()};
        }

#if MINECRAFT_VERSION_HEX < MINECRAFT_VERSION(1, 26, 50, 25)
        const auto *context = reflection_ctx();
        if (descriptor != nullptr && descriptor->mDynamicSetterArgCtor != nullptr && context != nullptr) {
            using Serialize = entt::meta_any (*)(const entt::meta_any &, const cereal::SerializerContext &);
            alignas(16) static const unsigned char inert[1024]{};
            const auto thunk = data.get(value);
            const auto *slot = thunk ? static_cast<const Serialize *>(thunk.base().data()) : nullptr;

            if (slot != nullptr && *slot != nullptr) {
                if (auto produced = (*slot)(value, *reinterpret_cast<const cereal::SerializerContext *>(inert));
                    produced) {
                    append(node, std::move(produced), std::move(name), depth + 1, seen);
                    added = true;
                    continue;
                }
            }

            const auto bound = descriptor->mDynamicSetterArgCtor(context->internal().mMetaCtx);
            put(node, std::move(name), std::string{bound.info().name()}, seen);
            added = true;
            continue;
        }
#endif

        append(node, data.get(value), std::move(name), depth + 1, seen);
        added = true;
    }

    if (!added) {
        if (const auto getter = type.func(cereal::internal::kCustomGetter)) {
            if (auto payload = getter.invoke(value); payload) {
                append_members(node, payload, depth + 1, seen);
            }
        }
    }
}

void append(nlohmann::ordered_json &parent, entt::meta_any value, std::string name, const int depth,
            KeyIndex *const seen)
{
    if (!value) {
        return;
    }

    if (const auto *tag = value.try_cast<const CompoundTag>();
        tag != nullptr && value.type().size_of() == sizeof(CompoundTag)) {
        g_tag_nodes = 0;
        append_tag(parent, *tag, Tag::Type::Compound, name, 0, seen);
        return;
    }

    if (nlohmann::ordered_json scalar; value_of(scalar, value)) {
        put(parent, std::move(name), std::move(scalar), seen);
        return;
    }

    const auto type = value.type();

    if (type.is_sequence_container()) {
        auto elements = value.as_sequence_container();

        if (const auto element = elements.value_type();
            element && element.size_of() == 1 && !element.is_class() && !element.is_enum()) {
            const auto count = elements.size();
            const auto *bytes = bytes_of(value);
            const unsigned char *first = nullptr;
            if (count == 0) {
                first = bytes;
            }
            else if (bytes != nullptr && type.size_of() == count) {
                first = bytes;
            }
            else if (bytes != nullptr && type.size_of() == 3 * sizeof(void *)) {
                const auto *const *range = reinterpret_cast<const unsigned char *const *>(bytes);
                const auto *last = range[1];
                if (range[0] != nullptr && last >= range[0] && static_cast<std::size_t>(last - range[0]) == count) {
                    first = range[0];
                }
            }

            if (count == 0) {
                put(parent, std::move(name), text_or_bytes({}), seen);
            }
            else if (first != nullptr) {
                put(parent, std::move(name),
                    text_or_bytes(std::string_view{reinterpret_cast<const char *>(first), count}), seen);
            }
            else {
                put(parent, std::move(name), std::format("<{} bytes, layout not recognised>", count), seen);
            }
            return;
        }

        auto node = nlohmann::ordered_json::array();
        for (auto &&element : elements) {
            append(node, element.as_ref(), {}, depth + 1, nullptr);
        }
        put(parent, std::move(name), std::move(node), seen);
        return;
    }

    if (type.is_associative_container()) {
        auto entries = value.as_associative_container();
        auto node = nlohmann::ordered_json::object();
        KeyIndex keys;
        std::size_t index = 0;
        for (auto &&[key, mapped] : entries) {
            nlohmann::ordered_json named;
            std::string text;
            if (!value_of(named, key)) {
                text = std::format("[{}]", index);
            }
            else if (named.is_string()) {
                text = named.get<std::string>();
            }
            else {
                text = named.dump();
            }
            append(node, mapped.as_ref(), std::move(text), depth + 1, &keys);
            ++index;
        }
        put(parent, std::move(name), std::move(node), seen);
        return;
    }

    if (is_specialization<std::optional>(type)) {
        const auto contained = type.template_arity() == 1 ? type.template_arg(0) : entt::meta_type{};
        const auto *bytes = bytes_of(value);
        if (bytes != nullptr && contained && contained.size_of() > 0 && contained.size_of() < type.size_of()) {
            if (bytes[contained.size_of()] == 0) {
                put(parent, std::move(name), nullptr, seen);
            }
            else {
                append(parent, contained.from_void(bytes), std::move(name), depth + 1, seen);
            }
            return;
        }
    }

    if (is_specialization<std::vector>(type)) {
        const auto element = type.template_arity() >= 1 ? type.template_arg(0) : entt::meta_type{};
        const auto *const *range = reinterpret_cast<const unsigned char *const *>(bytes_of(value));
        if (range != nullptr && element && element.size_of() > 0 && type.size_of() == 3 * sizeof(void *)) {
            const auto *first = range[0];
            const auto *last = range[1];
            if (first != nullptr && last >= first && static_cast<std::size_t>(last - first) % element.size_of() == 0) {
                const auto count = static_cast<std::size_t>(last - first) / element.size_of();
                if (element.size_of() == 1 && !element.is_class() && !element.is_enum()) {
                    put(parent, std::move(name),
                        text_or_bytes(std::string_view{reinterpret_cast<const char *>(first), count}), seen);
                    return;
                }
                auto node = nlohmann::ordered_json::array();
                for (std::size_t i = 0; i < count; ++i) {
                    append(node, element.from_void(first + (i * element.size_of())), {}, depth + 1, nullptr);
                }
                put(parent, std::move(name), std::move(node), seen);
                return;
            }
        }
    }

    if (is_specialization<std::variant>(type)) {
        const auto arity = type.template_arity();
        std::size_t storage = 0;
        for (std::size_t i = 0; i < arity; ++i) {
            if (const auto alternative = type.template_arg(i)) {
                storage = std::max(storage, alternative.size_of());
            }
        }

        const auto *bytes = bytes_of(value);
        if (bytes != nullptr && storage > 0 && storage < type.size_of()) {
            const std::size_t index = bytes[storage];
            if (const auto alternative = index < arity ? type.template_arg(index) : entt::meta_type{}) {
                auto active = alternative.from_void(bytes);
                auto held = nlohmann::ordered_json::object();
                KeyIndex alternative_keys;
                append_members(held, active, depth + 1, &alternative_keys);
                auto node = nlohmann::ordered_json::object();
                put(node, std::string{alternative.info().name()}, std::move(held));
                put(parent, std::move(name), std::move(node), seen);
                return;
            }
        }
    }

    if (type.is_pointer_like()) {
        if (auto pointed = *value; pointed) {
            append(parent, std::move(pointed), std::move(name), depth + 1, seen);
        }
        else {
            put(parent, std::move(name), nullptr, seen);
        }
        return;
    }

    for (auto &&[func_id, func] : type.func()) {
        const cereal::internal::BasicSchema::GetterDescriptor *getter = func.custom();
        if (getter == nullptr) {
            continue;
        }
        if (auto serialized = func.invoke(value); serialized) {
            append(parent, std::move(serialized), std::move(name), depth + 1, seen);
            return;
        }
    }

    if (type.is_class()) {
        const auto bases = type.base();
        const auto members = type.data();
        auto member = members.begin();
        if (bases.begin() == bases.end() && member != members.end()) {
            const auto only = member->second;
            const cereal::internal::BasicSchema::MemberDescriptor *descriptor = only.custom();
#if MINECRAFT_VERSION_HEX < MINECRAFT_VERSION(1, 26, 50, 25)
            const auto plain = descriptor == nullptr || descriptor->mDynamicSetterArgCtor == nullptr;
#else
            const auto plain = true;
#endif
            if (++member == members.end() && plain) {
                if (auto wrapped = only.get(value); wrapped) {
                    std::string inner = descriptor != nullptr ? descriptor->mName : std::string{};
                    if (inner.empty()) {
                        inner = std::string{only.name()};
                    }
                    append(parent, std::move(wrapped), name.empty() ? std::move(inner) : std::move(name), depth + 1,
                           seen);
                    return;
                }
            }
        }

        auto node = nlohmann::ordered_json::object();
        KeyIndex keys;
        append_members(node, value, depth, &keys);
        if (node.empty()) {
            put(parent, std::move(name),
                std::format("<class '{}' id {:#x} size {}>", type.info().name(), type_key(type), type.size_of()),
                seen);
            return;
        }
        put(parent, std::move(name), std::move(node), seen);
        return;
    }

    put(parent, std::move(name),
        std::format("<'{}' id {:#x} size {}{}{}>", type.info().name(), type_key(type), type.size_of(),
                    type.is_enum() ? " enum" : "", type.is_class() ? " class" : ""),
        seen);
}

bool schema_members(const entt::meta_type &type, std::unordered_set<entt::id_type> &seen, const int depth)
{
    if (!type || depth > kDeepestType || !seen.insert(type_key(type)).second) {
        return false;
    }
    if (type.data().begin() != type.data().end() || type.func(cereal::internal::kCustomGetter)) {
        return true;
    }
    for (auto &&[base_id, base] : type.base()) {
        if (schema_members(base.type(), seen, depth + 1)) {
            return true;
        }
    }
    return false;
}

const std::unordered_map<int, entt::meta_type> &packet_types(const entt::meta_ctx &meta_ctx)
{
    static const auto types = [&meta_ctx] {
        entt::locator<entt::meta_ctx>::reset(const_cast<entt::meta_ctx *>(&meta_ctx), [](entt::meta_ctx *) {});

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

}  // namespace

DecodeMode decode_mode(const int id)
{
    static constexpr std::array kRegistryPackets{
        MinecraftPacketIds::AddPlayer,        MinecraftPacketIds::AddItemActor,
        MinecraftPacketIds::InventoryTransaction, MinecraftPacketIds::PlayerEquipment,
        MinecraftPacketIds::MobArmorEquipment, MinecraftPacketIds::InventoryContent,
        MinecraftPacketIds::InventorySlot,     MinecraftPacketIds::CraftingData,
        MinecraftPacketIds::PlayerAuthInputPacket, MinecraftPacketIds::CreativeContent,
        MinecraftPacketIds::ItemStackRequest,  MinecraftPacketIds::ItemRegistryPacket,
    };

    return std::ranges::find(kRegistryPackets, static_cast<MinecraftPacketIds>(id)) != kRegistryPackets.end()
               ? DecodeMode::Eager
               : DecodeMode::Lazy;
}

bool has_fields(const int id)
{
    static std::array<std::atomic<std::int8_t>, 1024> known{};
    if (id < 0 || id >= static_cast<int>(known.size())) {
        return false;
    }

    auto &slot = known[static_cast<std::size_t>(id)];
    if (const auto seen = slot.load(std::memory_order_relaxed); seen != 0) {
        return seen == 2;
    }

    const auto *ctx = reflection_ctx();
    if (ctx == nullptr) {
        return false;
    }
    const auto &types = packet_types(ctx->internal().mMetaCtx);
    const auto entry = types.find(id);
    if (entry == types.end()) {
        return false;
    }

    std::unordered_set<entt::id_type> walked;
    const auto any = schema_members(entry->second, walked, 0);
    slot.store(any ? 2 : 1, std::memory_order_relaxed);
    return any;
}

nlohmann::ordered_json decode_fields(Packet &packet, const int id)
{
    try {
        const auto *ctx = reflection_ctx();
        if (ctx == nullptr || id < 0) {
            return {};
        }

        const auto &types = packet_types(ctx->internal().mMetaCtx);
        const auto entry = types.find(id);
        if (entry == types.end()) {
            return {};
        }

        auto instance = entry->second.from_void(&packet, false);
        if (!instance) {
            return {};
        }

        auto root = nlohmann::ordered_json::object();
        KeyIndex keys;
        append_members(root, instance, 0, &keys);
        if (root.empty()) {
            const auto type = entry->second;
            std::size_t bases = 0;
            for (auto it = type.base().begin(); it != type.base().end(); ++it) {
                ++bases;
            }
            std::size_t members = 0;
            for (auto it = type.data().begin(); it != type.data().end(); ++it) {
                ++members;
            }
            put(root, "no fields",
                std::format("types {} bases {} members {} getter {}", types.size(), bases, members,
                            type.func(cereal::internal::kCustomGetter) ? "yes" : "no"),
                &keys);
        }
        return root;
    }
    catch (...) {
        return {};
    }
}

nlohmann::ordered_json decode_body(const int id, const std::string_view body)
{
    try {
        const auto *ctx = reflection_ctx();
        if (ctx == nullptr || id < 0 || body.empty()) {
            return {};
        }

        const auto packet = create_packet(id);
        if (!packet) {
            return {};
        }
        packet->setSerializationMode(SerializationMode::CerealOnly);

        ReadOnlyBinaryStream stream{body, true};
        read_no_header(*packet, stream, *ctx, SubClientId::PrimaryClient);
        return decode_fields(*packet, id);
    }
    catch (...) {
        return {};
    }
}

}  // namespace spyglass
