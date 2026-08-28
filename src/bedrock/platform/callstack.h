#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bedrock/diagnostics/log_area.h"
#include "bedrock/diagnostics/log_level.h"

namespace Bedrock {

struct CallStack {
    struct Frame {
        std::size_t filename_hash;
        std::string_view filename;
        std::uint32_t line;
    };

    struct Context {
        std::string value;
        std::optional<LogLevel> log_level;
        std::optional<BedrockLog::LogAreaID> log_area;
    };

    struct FrameWithContext {
        Frame frame;
        std::optional<Context> context;
    };

    std::vector<FrameWithContext> frames;
};

}  // namespace Bedrock
