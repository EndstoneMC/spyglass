#include "spyglass/signature.h"

#ifdef _WIN32

#include <algorithm>
#include <cstddef>
#include <string_view>

#include <libhat/process.hpp>

#endif

namespace spyglass {

const Signatures &signatures()
{
#ifdef _WIN32
    static const bool preview = [] {
        constexpr std::wstring_view name = L"Minecraft Preview";
        const auto rdata = hat::process::get_process_module().get_section_data(".rdata");
        const auto *const bytes = reinterpret_cast<const std::byte *>(name.data());
        return std::search(rdata.begin(), rdata.end(), bytes, bytes + name.size() * sizeof(wchar_t)) != rdata.end();
    }();
    return preview ? kPreviewClient : kReleaseClient;
#else
    return kAndroidClient;
#endif
}

}  // namespace spyglass
