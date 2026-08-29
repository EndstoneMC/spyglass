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
#include <variant>

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
#include "bedrock/nbt/compound_tag_variant.h"
#include "bedrock/nbt/double_tag.h"
#include "bedrock/nbt/float_tag.h"
#include "bedrock/nbt/int64_tag.h"
#include "bedrock/nbt/int_array_tag.h"
#include "bedrock/nbt/int_tag.h"
#include "bedrock/nbt/list_tag.h"
#include "bedrock/nbt/short_tag.h"
#include "bedrock/nbt/string_tag.h"
#include "bedrock/nbt/tag.h"
#include "bedrock/network/packet.h"
#include "bedrock/platform/uuid.h"
#include "spyglass/network.h"
#include "spyglass/overlay/capture.h"

namespace spyglass {

static_assert(entt::type_hash<cereal::internal::BasicSchema::TypeDescriptor>::value() == 0xD4C25870);
static_assert(entt::type_hash<cereal::internal::BasicSchema::MemberDescriptor>::value() == 0xBA234EDA);
static_assert(entt::type_hash<cereal::internal::BasicSchema::GetterDescriptor>::value() == 0xBE28A3EF);
static_assert(entt::type_hash<cereal::internal::BasicSchema::SetterDescriptor>::value() == 0x0AC66523);
static_assert(entt::type_hash<CompoundTag>::value() == 0xBD1A8574);

namespace {

constexpr int kMaxDepth = 8;
constexpr std::size_t kMaxElements = 64;
constexpr int kMaxNodes = 4096;
constexpr std::size_t kMaxText = 96;
constexpr std::size_t kMaxBytes = 16;

int g_nodes = 0;

template <template <typename...> class T>
bool is_specialization(const entt::meta_type &type)
{
    const auto *context = reflection_ctx();
    return context != nullptr && type.is_template_specialization() &&
           type.template_type() == entt::resolve<entt::meta_class_template_tag<T>>(context->internal().mMetaCtx);
}

const unsigned char *bytes_of(const entt::meta_any &value)
{
    return static_cast<const unsigned char *>(value.base().data());
}

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
    if (const auto *v = value.try_cast<HashedString>()) {
        return blob_or_text(v->getString(), v->getString().size());
    }
    if (const auto *v = value.try_cast<mce::UUID>()) {
        return std::format("{:08x}-{:04x}-{:04x}-{:04x}-{:012x}", v->data[0] >> 32, (v->data[0] >> 16) & 0xFFFF,
                           v->data[0] & 0xFFFF, v->data[1] >> 48, v->data[1] & 0xFFFF'FFFF'FFFF);
    }
    if (const auto *v = value.try_cast<CompoundTag>()) {
        if (value.type().size_of() != sizeof(CompoundTag)) {
            return "<compound tag, layout not recognised>";
        }
        const auto entries = v->size();
        return std::format("<compound, {} {}>", entries, entries == 1 ? "entry" : "entries");
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
                name = descriptor->mNameExt.empty() ? descriptor->mName : descriptor->mNameExt;
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

#ifdef _WIN32
constexpr int kMaxTagDepth = 16;
constexpr int kMaxTreeDepth = 64;
constexpr std::size_t kMaxTagElements = 64;
constexpr int kMaxTagNodes = 4096;

int g_tag_nodes = 0;

void append_tag(Node &parent, const Tag &tag, const Tag::Type type, const std::string_view name, const int depth)
{
    if (depth > kMaxTagDepth || ++g_tag_nodes > kMaxTagNodes) {
        return;
    }

    switch (type) {
    case Tag::Type::End:
        parent.children.push_back({.label = std::format("ED {}", name)});
        return;
    case Tag::Type::Byte:
        parent.children.push_back({.label = std::format("BY {}: {}", name, static_cast<const ByteTag &>(tag).data)});
        return;
    case Tag::Type::Short:
        parent.children.push_back({.label = std::format("SH {}: {}", name, static_cast<const ShortTag &>(tag).data)});
        return;
    case Tag::Type::Int:
        parent.children.push_back({.label = std::format("IN {}: {}", name, static_cast<const IntTag &>(tag).data)});
        return;
    case Tag::Type::Int64:
        parent.children.push_back({.label = std::format("LO {}: {}", name, static_cast<const Int64Tag &>(tag).data)});
        return;
    case Tag::Type::Float:
        parent.children.push_back({.label = std::format("FL {}: {}", name, static_cast<const FloatTag &>(tag).data)});
        return;
    case Tag::Type::Double:
        parent.children.push_back({.label = std::format("DO {}: {}", name, static_cast<const DoubleTag &>(tag).data)});
        return;
    case Tag::Type::ByteArray: {
        const auto &data = static_cast<const ByteArrayTag &>(tag).mData;
        const auto *bytes = reinterpret_cast<const char *>(data.data());
        const std::string_view raw =
            bytes != nullptr ? std::string_view{bytes, std::min<std::size_t>(data.size(), kMaxText)}
                             : std::string_view{};
        parent.children.push_back({.label = std::format("BA {}: {}", name, blob_or_text(raw, data.size()))});
        return;
    }
    case Tag::Type::String: {
        const auto &data = static_cast<const StringTag &>(tag).data;
        parent.children.push_back({.label = std::format("ST {}: {}", name, blob_or_text(data, data.size()))});
        return;
    }
    case Tag::Type::List: {
        const auto &list = static_cast<const ListTag &>(tag);
        const auto shown = list.mList.data() != nullptr ? std::min<std::size_t>(list.mList.size(), kMaxTagElements) : 0;
        Node node{.label = std::format("LI {} [{}]", name, list.mList.size())};
        for (std::size_t i = 0; i < shown; ++i) {
            const Tag *element = list.mList[i].get();
            if (element != nullptr && reinterpret_cast<std::uintptr_t>(element) % alignof(Tag) == 0) {
                append_tag(node, *element, list.mType, std::format("[{}]", i), depth + 1);
            }
        }
        if (list.mList.size() > shown) {
            node.children.push_back({.label = "..."});
        }
        parent.children.push_back(std::move(node));
        return;
    }
    case Tag::Type::Compound: {
        const auto &compound = static_cast<const CompoundTag &>(tag);
        const auto *head = compound.head();
        const auto *cursor =
            head != nullptr && reinterpret_cast<std::uintptr_t>(head) % alignof(CompoundTag::TagNode) == 0
                ? head->mParent
                : nullptr;
        const auto shown = std::min<std::size_t>(compound.size(), kMaxTagElements);

        Node node{.label = std::format("CO {} [{}]", name, compound.size())};
        const CompoundTag::TagNode *pending[kMaxTreeDepth];
        int depth_left = 0;
        for (std::size_t i = 0; i < shown; ++i) {
            while (cursor != nullptr &&
                   reinterpret_cast<std::uintptr_t>(cursor) % alignof(CompoundTag::TagNode) == 0 &&
                   cursor->mIsNil == 0 && depth_left < kMaxTreeDepth) {
                pending[depth_left++] = cursor;
                cursor = cursor->mLeft;
            }
            if (depth_left == 0) {
                break;
            }
            const auto *entry = pending[--depth_left];
            append_tag(node, *entry->mValue, entry->mValue.index(),
                       std::string_view{entry->mKey}.substr(0, kMaxText), depth + 1);
            cursor = entry->mRight;
        }
        if (compound.size() > shown) {
            node.children.push_back({.label = "..."});
        }
        parent.children.push_back(std::move(node));
        return;
    }
    case Tag::Type::IntArray: {
        const auto &data = static_cast<const IntArrayTag &>(tag).mData;
        const auto shown = data.data() != nullptr ? std::min<std::size_t>(data.size(), kMaxTagElements) : 0;
        Node node{.label = std::format("IA {} [{}]", name, data.size())};
        for (std::size_t i = 0; i < shown; ++i) {
            node.children.push_back({.label = std::format("IN [{}]: {}", i, data[i])});
        }
        if (data.size() > shown) {
            node.children.push_back({.label = "..."});
        }
        parent.children.push_back(std::move(node));
        return;
    }
    default:
        parent.children.push_back({.label = std::format("?? {}: <tag type {}>", name, static_cast<int>(type))});
        return;
    }
}
#endif

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
        const cereal::internal::BasicSchema::MemberDescriptor *descriptor = data.custom();
        if (descriptor != nullptr) {
            name = descriptor->mName;
        }
        if (name.empty()) {
            name = std::string{data.name()};
        }

        const auto *context = reflection_ctx();
        if (descriptor != nullptr && descriptor->mDynamicSetterArgCtor != nullptr && context != nullptr) {
            using Serialize = entt::meta_any (*)(const entt::meta_any &, const cereal::SerializerContext &);
            alignas(16) static const unsigned char inert[1024]{};
            const auto thunk = data.get(value);
            const auto *slot = thunk ? static_cast<const Serialize *>(thunk.base().data()) : nullptr;

            if (slot != nullptr && *slot != nullptr) {
                if (auto produced = (*slot)(value, *reinterpret_cast<const cereal::SerializerContext *>(inert));
                    produced) {
                    append(node, std::move(produced), std::move(name), depth + 1);
                    added = true;
                    continue;
                }
            }

            const auto bound = descriptor->mDynamicSetterArgCtor(context->internal().mMetaCtx);
            node.children.push_back({.label = std::format("{}: {}", name, bound.info().name())});
            added = true;
            continue;
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

#ifdef _WIN32
    if (const auto *tag = value.try_cast<CompoundTag>();
        tag != nullptr && value.type().size_of() == sizeof(CompoundTag)) {
        g_tag_nodes = 0;
        append_tag(parent, *tag, Tag::Type::Compound, name, 0);
        return;
    }
#endif

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

    if (is_specialization<std::optional>(type)) {
        const auto contained = type.template_arity() == 1 ? type.template_arg(0) : entt::meta_type{};
        const auto *bytes = bytes_of(value);
        if (bytes != nullptr && contained && contained.size_of() > 0 && contained.size_of() < type.size_of()) {
            if (bytes[contained.size_of()] == 0) {
                parent.children.push_back({.label = std::format("{}: (none)", name)});
            }
            else {
                append(parent, contained.from_void(bytes), std::move(name), depth + 1);
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
            if (first != nullptr && last >= first &&
                static_cast<std::size_t>(last - first) % element.size_of() == 0) {
                const auto count = static_cast<std::size_t>(last - first) / element.size_of();
                Node node{.label = std::format("{} [{}]", name, count)};
                for (std::size_t i = 0; i < std::min(count, kMaxElements); ++i) {
                    append(node, element.from_void(first + (i * element.size_of())), std::format("[{}]", i),
                           depth + 1);
                }
                if (count > kMaxElements) {
                    node.children.push_back({.label = "..."});
                }
                parent.children.push_back(std::move(node));
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
                Node node{.label = std::format("{}: {}", name, alternative.info().name())};
                append_members(node, active, depth + 1);
                parent.children.push_back(std::move(node));
                return;
            }
        }
    }

    if (type.is_pointer_like()) {
        if (auto pointed = *value; pointed) {
            append(parent, std::move(pointed), std::move(name), depth + 1);
        }
        else {
            parent.children.push_back({.label = std::format("{}: (none)", name)});
        }
        return;
    }

    for (auto &&[func_id, func] : type.func()) {
        const cereal::internal::BasicSchema::GetterDescriptor *getter = func.custom();
        if (getter == nullptr) {
            continue;
        }
        if (auto serialized = func.invoke(value); serialized) {
            append(parent, std::move(serialized), std::move(name), depth + 1);
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
            if (++member == members.end() && (descriptor == nullptr || descriptor->mDynamicSetterArgCtor == nullptr)) {
                if (auto wrapped = only.get(value); wrapped) {
                    std::string inner = descriptor != nullptr ? descriptor->mName : std::string{};
                    if (inner.empty()) {
                        inner = std::string{only.name()};
                    }
                    append(parent, std::move(wrapped), name.empty() ? std::move(inner) : std::move(name), depth + 1);
                    return;
                }
            }
        }

        Node node{.label = name};
        append_members(node, value, depth);
        if (node.children.empty()) {
            node.label = std::format("{}: <class \"{}\" id {:#x} size {}>", name, type.info().name(), type.id(),
                                     type.size_of());
        }
        parent.children.push_back(std::move(node));
        return;
    }

    parent.children.push_back({.label = std::format("{}: <\"{}\" id {:#x} size {}{}{}>", name, type.info().name(), type.id(),
                                                    type.size_of(), type.is_enum() ? " enum" : "",
                                                    type.is_class() ? " class" : "")});
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
