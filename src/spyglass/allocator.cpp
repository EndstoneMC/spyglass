#ifdef _WIN32

#include <cstddef>
#include <cstdlib>
#include <new>

#include <Windows.h>

namespace {

constexpr auto kClientAllocator = L"MallocRedirect.dll";

}  // namespace

void *operator new(const std::size_t size)
{
    static auto *const allocate = [] {
        auto *const client = GetModuleHandleW(kClientAllocator);
        return client != nullptr ? reinterpret_cast<decltype(&std::malloc)>(GetProcAddress(client, "malloc"))
                                 : &std::malloc;
    }();
    if (auto *const block = allocate(size); block != nullptr) {
        return block;
    }
    throw std::bad_alloc{};
}

void operator delete(void *const block) noexcept
{
    static auto *const release = [] {
        auto *const client = GetModuleHandleW(kClientAllocator);
        return client != nullptr ? reinterpret_cast<decltype(&std::free)>(GetProcAddress(client, "free")) : &std::free;
    }();
    release(block);
}

#endif
