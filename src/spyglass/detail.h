#pragma once

#include <cstring>

namespace spyglass::detail {

template <typename Return, typename... Args>
void *fp_cast(Return (*fp)(Args...))
{
    void *v;
    std::memcpy(&v, &fp, sizeof(v));
    return v;
}

template <typename Return, typename Class, typename... Args>
void *fp_cast(Return (Class::*fp)(Args...))
{
    void *v;
    std::memcpy(&v, &fp, sizeof(v));
    return v;
}

template <typename Return, typename Class, typename... Args>
void *fp_cast(Return (Class::*fp)(Args...) const)
{
    void *v;
    std::memcpy(&v, &fp, sizeof(v));
    return v;
}

template <typename Return, typename... Arg>
Return (*fp_cast(Return (*fp)(Arg...), void *func))(Arg...)
{
    Return (*result)(Arg...);
    std::memcpy(&result, &func, sizeof(result));
    return result;
}

template <typename Return, typename Class, typename... Arg>
auto fp_cast(Return (Class::*)(Arg...), void *address)
{
    Return (Class::*result)(Arg...);
    std::memset(&result, 0, sizeof(result));
    std::memcpy(&result, &address, sizeof(address));
    return result;
}

template <typename Return, typename Class, typename... Arg>
auto fp_cast(Return (Class::*)(Arg...) const, void *address)
{
    Return (Class::*result)(Arg...) const;
    std::memset(&result, 0, sizeof(result));
    std::memcpy(&result, &address, sizeof(address));
    return result;
}

#ifdef _WIN32
template <typename Class, typename... Args>
auto ctor_cast(void *address)
{
    return reinterpret_cast<Class *(*)(Class *, Args...)>(address);
}
#else
template <typename Class, typename... Args>
auto ctor_cast(void *address)
{
    return reinterpret_cast<void (*)(Class *, Args...)>(address);
}
#endif

}  // namespace spyglass::detail
