#pragma once

#include <system_error>
#include <vector>

#include "bedrock/bedrock.h"
#include "bedrock/platform/callstack.h"

namespace Bedrock {

template <typename ErrorType>
struct ErrorInfo {
    ErrorType error;
    CallStack call_stack;
    std::vector<ErrorInfo> branches;
};
BEDROCK_STATIC_ASSERT_SIZE(ErrorInfo<std::error_code>, 64, 64);

}  // namespace Bedrock
