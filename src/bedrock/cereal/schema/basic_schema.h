#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/container/dense_map.hpp>
#include <entt/core/any.hpp>
#include <entt/meta/context.hpp>
#include <entt/meta/meta.hpp>

#include "bedrock/version.h"

namespace cereal {

enum class SerializationTraits : std::uint8_t {
    None = 0,
    Compression = 1,
    BigEndian = 2,
    SkipAlsoReadAs = 16,
};

enum class ContextArea : std::uint8_t {
    NONE = 0,
    ALL = 255,
};

class Constraint;

namespace util::internal {

struct StringViewHash {
    using is_transparent = void;
    [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept
    {
        return std::hash<std::string_view>{}(value);
    }
};

}  // namespace util::internal

class SerializerContext;

namespace internal {

inline constexpr entt::id_type kCustomGetter = 3212167416;
inline constexpr entt::id_type kCustomSetters = 505136341;

enum class TypeTraits : std::uint16_t {
    noTraits = 0,
    isTaggedVariant = 1,
    hasTaggedVariantMembers = 2,
    hasDefaultMembers = 4,
};

enum class MemberTraits : std::uint16_t {
    noTraits = 0,
    isRequired = 1,
    isDefaultSetter = 2,
    isMemberLevelSetterGetter = 4,
#if MINECRAFT_VERSION_HEX < MINECRAFT_VERSION(1, 26, 50, 25)
    isKeyedSetterGetter = 8,
    isConstSelector = 16,
    hasDefaultValue = 32,
    isPatternMember = 64,
#else
    isConstSelector = 8,
    hasDefaultValue = 16,
    isPatternMember = 32,
#endif
};

using UserPropertiesMap = entt::dense_map<std::string,
                                          std::pair<entt::meta_type (*)(const entt::meta_ctx &), entt::any>,
                                          cereal::util::internal::StringViewHash, std::equal_to<>>;

class BasicSchema {
public:
    struct EnumMapping {
        std::vector<void *> mViews;
    };

    struct MemberFamily {
        std::string_view mPrefix;
        entt::id_type mMemberId;
    };

    struct OverrideState {
        entt::id_type mId;
    };

    struct OverridingSet {
        std::vector<OverrideState> mStorage;
    };

    struct TypeDescriptor {
        std::unique_ptr<BasicSchema> mPtr;
        std::string mName;
        EnumMapping mEnumMapping;
        UserPropertiesMap mUserPropertiesMap;
        std::vector<MemberFamily> mMemberFamilies;
        std::string mErrorMessage;
    };

    struct MemberDescriptor {
        std::unique_ptr<BasicSchema> mPtr;
        std::unique_ptr<Constraint> mConstraint;
        std::string mNameExt;
        std::string mName;
#if MINECRAFT_VERSION_HEX < MINECRAFT_VERSION(1, 26, 50, 25)
        entt::meta_type (*mDynamicSetterArgCtor)(const entt::meta_ctx &);
#endif
        UserPropertiesMap mUserPropertiesMap;
        OverridingSet mOverridingTypes;
        std::string mErrorMessage;
        cereal::SerializationTraits mSerializationTraits;
        cereal::ContextArea mAllowedAreas;
        std::optional<unsigned int> mScope;
        bool mIsDeprecatedComponent;
    };

    struct GetterDescriptor {
        std::unique_ptr<BasicSchema> mPtr;
        cereal::ContextArea mAllowedAreas;
    };

    struct SetterDescriptor {
        std::unique_ptr<BasicSchema> mPtr;
        std::unique_ptr<Constraint> mConstraint;
        cereal::ContextArea mAllowedAreas;
    };
};

#ifdef _WIN32
static_assert(sizeof(UserPropertiesMap) == 72);
static_assert(sizeof(BasicSchema::TypeDescriptor) == 0xC0);
static_assert(offsetof(BasicSchema::TypeDescriptor, mName) == 0x08);
static_assert(offsetof(BasicSchema::TypeDescriptor, mUserPropertiesMap) == 0x40);
static_assert(offsetof(BasicSchema::MemberDescriptor, mName) == 0x30);
#if MINECRAFT_VERSION_HEX < MINECRAFT_VERSION(1, 26, 50, 25)
static_assert(sizeof(BasicSchema::MemberDescriptor) == 0xE8);
static_assert(offsetof(BasicSchema::MemberDescriptor, mUserPropertiesMap) == 0x58);
static_assert(offsetof(BasicSchema::MemberDescriptor, mSerializationTraits) == 0xD8);
#else
static_assert(sizeof(BasicSchema::MemberDescriptor) == 0xE0);
static_assert(offsetof(BasicSchema::MemberDescriptor, mUserPropertiesMap) == 0x50);
static_assert(offsetof(BasicSchema::MemberDescriptor, mSerializationTraits) == 0xD0);
#endif
static_assert(sizeof(BasicSchema::GetterDescriptor) == 0x10);
static_assert(sizeof(BasicSchema::SetterDescriptor) == 0x18);
#endif

}  // namespace internal

}  // namespace cereal
