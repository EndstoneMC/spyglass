#pragma once

#include <system_error>
#include <vector>

#include "bedrock/platform/callstack.h"

namespace Bedrock {

template <typename ErrorType>
struct ErrorInfo {
    ErrorType error;
    CallStack call_stack;
    std::vector<ErrorInfo> branches;
};

}  // namespace Bedrock
