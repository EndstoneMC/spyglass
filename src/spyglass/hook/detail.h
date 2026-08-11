#pragma once

#include <cstring>

namespace spyglass::detail {

/**
 * Round-trips a function pointer through `void *`. Member function pointers keep
 * the MSVC `this`-in-rcx convention, which is what makes a member detour ABI
 * compatible with the member function it replaces.
 */
template <typename Return, typename... Args>
void *fp_cast(Return (*fp)(Args...))
{
    void *address;
    std::memcpy(&address, &fp, sizeof(address));
    return address;
}

template <typename Return, typename Class, typename... Args>
void *fp_cast(Return (Class::*fp)(Args...))
{
    void *address;
    std::memcpy(&address, &fp, sizeof(address));
    return address;
}

template <typename Return, typename Class, typename... Args>
void *fp_cast(Return (Class::*fp)(Args...) const)
{
    void *address;
    std::memcpy(&address, &fp, sizeof(address));
    return address;
}

template <typename Return, typename... Args>
Return (*fp_cast(Return (*)(Args...), void *address))(Args...)
{
    Return (*result)(Args...);
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

template <typename Return, typename Class, typename... Args>
auto fp_cast(Return (Class::*)(Args...), void *address)
{
    Return (Class::*result)(Args...);
    std::memset(&result, 0, sizeof(result));
    std::memcpy(&result, &address, sizeof(address));
    return result;
}

template <typename Return, typename Class, typename... Args>
auto fp_cast(Return (Class::*)(Args...) const, void *address)
{
    Return (Class::*result)(Args...) const;
    std::memset(&result, 0, sizeof(result));
    std::memcpy(&result, &address, sizeof(address));
    return result;
}

}  // namespace spyglass::detail
