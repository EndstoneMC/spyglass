#pragma once

#include <system_error>

#include <nonstd/expected.hpp>

#include "bedrock/bedrock.h"
#include "bedrock/platform/check.h"

namespace Bedrock {

template <typename T, typename E = std::error_code>
class Result : nonstd::expected<T, ErrorInfo<E>> {
    using Base = nonstd::expected<T, ErrorInfo<E>>;

public:
    using Base::Base;

    [[nodiscard]] const Base &asExpected() const { return *this; }
};
BEDROCK_STATIC_ASSERT_SIZE(Result<void>, 72, 72);
BEDROCK_STATIC_ASSERT_SIZE(Result<unsigned char>, 72, 72);

}  // namespace Bedrock
