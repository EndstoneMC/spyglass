#pragma once

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <entt/core/type_info.hpp>
#include <entt/meta/context.hpp>

#include "bedrock/core/utility/enable_non_owner_references.h"

namespace cereal {

enum class JSONSchemaOutput : bool {
    Exclude = 0,
    Include = 1,
};

namespace internal {

struct ReflectionContext {
    entt::meta_ctx mMetaCtx;
    std::vector<std::tuple<std::string, entt::type_info, JSONSchemaOutput>> mKnownProperties;
    std::optional<JSONSchemaOutput> mForcedJSONSchemaOutput;
};

}  // namespace internal

#ifdef _WIN32
struct ReflectionCtx : Bedrock::EnableNonOwnerReferences, private internal::ReflectionContext {
#else
struct ReflectionCtx : private internal::ReflectionContext, Bedrock::EnableNonOwnerReferences {
#endif
    [[nodiscard]] const internal::ReflectionContext &internal() const
    {
        return static_cast<const internal::ReflectionContext &>(*this);
    }
};

#ifdef _WIN32
static_assert(sizeof(ReflectionCtx) == 0x80);
#endif

}  // namespace cereal
