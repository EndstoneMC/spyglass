#include "spyglass/signature.h"

#ifdef _WIN32

#include <algorithm>
#include <array>
#include <cstddef>
#include <cwchar>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Windows.h>

#include <appmodel.h>

#include <libhat/process.hpp>

#endif

namespace spyglass {

#ifdef _WIN32

namespace {

bool client_is_preview()
{
    constexpr std::wstring_view name = L"Minecraft Preview";
    const auto rdata = hat::process::get_process_module().get_section_data(".rdata");
    const auto *const bytes = reinterpret_cast<const std::byte *>(name.data());
    return std::search(rdata.begin(), rdata.end(), bytes, bytes + name.size() * sizeof(wchar_t)) != rdata.end();
}

std::optional<std::array<unsigned, 4>> client_version()
{
    UINT32 length = 0;
    if (GetCurrentPackageFullName(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return std::nullopt;
    }

    std::wstring package(length, L'\0');
    if (GetCurrentPackageFullName(&length, package.data()) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    package.resize(length - 1);

    const auto name_end = package.find(L'_');
    const auto version_end = package.find(L'_', name_end + 1);
    if (version_end == std::wstring::npos) {
        return std::nullopt;
    }

    std::array<unsigned, 3> numbers{};
    std::wstring_view fields{package.data() + name_end + 1, version_end - name_end - 1};
    for (auto &number : numbers) {
        const auto dot = fields.find(L'.');
        number = static_cast<unsigned>(std::wcstoul(std::wstring{fields.substr(0, dot)}.c_str(), nullptr, 10));
        fields = dot == std::wstring_view::npos ? std::wstring_view{} : fields.substr(dot + 1);
    }
    const auto patch_and_build = numbers[2];

    return std::array<unsigned, 4>{numbers[0], numbers[1], patch_and_build / 100, patch_and_build % 100};
}

}  // namespace

void verify_client()
{
    if (client_is_preview() != static_cast<bool>(MINECRAFT_PREVIEW)) {
        throw std::runtime_error{std::format("built for {} but this is the {} client, load the other payload",
                                             MINECRAFT_CLIENT, MINECRAFT_PREVIEW ? "release" : "preview")};
    }

    const auto version = client_version();
    if (version && MINECRAFT_VERSION((*version)[0], (*version)[1], (*version)[2], 0) <
                       MINECRAFT_VERSION(MINECRAFT_VERSION_MAJOR, MINECRAFT_VERSION_MINOR, MINECRAFT_VERSION_PATCH, 0)) {
        throw std::runtime_error{std::format("built for {} but this client is {}.{}.{}.{}, load an older payload",
                                             MINECRAFT_CLIENT, (*version)[0], (*version)[1], (*version)[2],
                                             (*version)[3])};
    }
}

#else

void verify_client() {}

#endif

const Signatures &signatures()
{
    return kClient;
}

}  // namespace spyglass
