#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "bedrock/bedrock.h"

class ReadOnlyBinaryStream {
public:
    virtual ~ReadOnlyBinaryStream() = default;

    [[nodiscard]] std::size_t getReadPointer() const { return read_pointer_; }
    [[nodiscard]] std::size_t getLength() const { return view_.size(); }
    [[nodiscard]] std::size_t getUnreadLength() const
    {
        return read_pointer_ >= view_.size() ? 0 : view_.size() - read_pointer_;
    }
    [[nodiscard]] std::string_view getView() const { return view_; }
    [[nodiscard]] bool hasOverflowed() const { return has_overflowed_; }

    // Offsets differ between the two clients only because std::string does: 32 bytes under
    // MSVC, 24 under libc++. Declaring the members is enough, each toolchain lays them out
    // the same way the client it targets did.
protected:
    std::string owned_buffer_;  // +8
    std::string_view view_;     // +40 windows, +32 android

private:
    std::size_t read_pointer_{0};  // +56 windows, +48 android
    bool has_overflowed_{false};   // +64 windows, +56 android
};
BEDROCK_STATIC_ASSERT_SIZE(ReadOnlyBinaryStream, 72, 64);
