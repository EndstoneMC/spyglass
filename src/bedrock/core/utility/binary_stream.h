#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>

#include "bedrock/platform/result.h"

class ReadOnlyBinaryStream {
public:
    ReadOnlyBinaryStream(const std::string_view buffer, const bool copy_buffer)
    {
        auto view = buffer;
        if (copy_buffer) {
            owned_buffer_ = buffer;
            view = owned_buffer_;
        }
        view_ = view;
    }
    virtual ~ReadOnlyBinaryStream() = default;

    void setReadPointer(const std::size_t position) { read_pointer_ = position; }
    [[nodiscard]] std::size_t getReadPointer() const { return read_pointer_; }
    [[nodiscard]] std::size_t getLength() const { return view_.size(); }
    [[nodiscard]] std::size_t getUnreadLength() const
    {
        return read_pointer_ >= view_.size() ? 0 : view_.size() - read_pointer_;
    }
    [[nodiscard]] std::string_view getView() const { return view_; }
    [[nodiscard]] bool hasOverflowed() const { return has_overflowed_; }

    Bedrock::Result<unsigned char> getByte()
    {
        unsigned char value = 0;
        if (auto result = read(&value, sizeof(value)); !result.asExpected().has_value()) {
            return nonstd::make_unexpected(result.asExpected().error());
        }
        return value;
    }

    Bedrock::Result<unsigned int> getUnsignedVarInt()
    {
        unsigned int value = 0;
        for (auto i = 0;; i += 7) {
            auto byte_result = getByte();
            if (!byte_result.asExpected().has_value()) {
                return nonstd::make_unexpected(byte_result.asExpected().error());
            }
            const auto byte = byte_result.asExpected().value();
            value |= (byte & 0x7F) << i;
            if ((byte & 0x80U) == 0) {
                break;
            }
        }
        return value;
    }

private:
    virtual Bedrock::Result<void> read(void *target, const std::uint64_t num)
    {
        if (has_overflowed_) {
            return nonstd::make_unexpected(seek_error());
        }
        if (num == 0) {
            return {};
        }
        if (const auto checked = read_pointer_ + num; checked < read_pointer_ || checked > view_.size()) {
            has_overflowed_ = true;
            return nonstd::make_unexpected(seek_error());
        }
        std::memcpy(target, view_.data() + read_pointer_, num);
        read_pointer_ += num;
        return {};
    }

    static Bedrock::ErrorInfo<std::error_code> seek_error()
    {
        return {std::make_error_code(std::errc::invalid_seek), {}, {}};
    }

protected:
    std::string owned_buffer_;  // +8
    std::string_view view_;     // +40 windows, +32 android

private:
    std::size_t read_pointer_{0};  // +56 windows, +48 android
    bool has_overflowed_{false};   // +64 windows, +56 android
};

class BinaryStream : public ReadOnlyBinaryStream {
public:
    [[nodiscard]] std::string_view written() const
    {
        return buffer_ != nullptr ? std::string_view{*buffer_} : std::string_view{};
    }

private:
    std::string *buffer_{nullptr};
};

static_assert(sizeof(BinaryStream) == sizeof(ReadOnlyBinaryStream) + sizeof(void *));
