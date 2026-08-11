#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bedrock/bedrock.h"
#include "bedrock/diagnostics/log_area.h"
#include "bedrock/diagnostics/log_level.h"

namespace Bedrock {

struct CallStack {
    struct Frame {
        std::size_t filename_hash;
        std::string_view filename;
        std::uint32_t line;
    };
    BEDROCK_STATIC_ASSERT_SIZE(Frame, 32);

    struct Context {
        std::string value;
        std::optional<LogLevel> log_level;
        std::optional<BedrockLog::LogAreaID> log_area;
    };
    BEDROCK_STATIC_ASSERT_SIZE(Context, 48);

    struct FrameWithContext {
        Frame frame;
        std::optional<Context> context;
    };
    BEDROCK_STATIC_ASSERT_SIZE(FrameWithContext, 88);

    std::vector<FrameWithContext> frames;
};
BEDROCK_STATIC_ASSERT_SIZE(CallStack, 24);

}  // namespace Bedrock
