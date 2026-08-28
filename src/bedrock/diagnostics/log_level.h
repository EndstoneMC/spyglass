#pragma once

#include <cstdint>

namespace Bedrock {

class LogLevel {
public:
    enum class Type : std::uint8_t {
        Verbose = 1,
        Info = 2,
        Warning = 4,
        Error = 8,
    };

    constexpr LogLevel(Type type) : type_(type) {}
    [[nodiscard]] constexpr Type getType() const { return type_; }

private:
    Type type_;
    bool log_in_publish_{false};
};

}  // namespace Bedrock
