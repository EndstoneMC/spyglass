#include "spyglass/hook/vtables.h"

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include <elf.h>

#include "spyglass/core/log.h"

namespace spyglass::hook {
namespace {

constexpr std::string_view kClientLibrary = "libminecraftpe.so";

struct Image {
    std::string path;
    std::uintptr_t base{0};
    std::uintptr_t limit{0};
};

/** The client is mapped by the launcher's linker, but the mapping is still file backed. */
Image find_client()
{
    Image found;
    std::ifstream maps{"/proc/self/maps"};
    for (std::string line; std::getline(maps, line);) {
        const auto at = line.find(kClientLibrary);
        if (at == std::string::npos) {
            continue;
        }
        const auto dash = line.find('-');
        auto field = line.find(' ', dash);
        field = line.find(' ', field + 1);
        if (dash == std::string::npos || field == std::string::npos) {
            continue;
        }
        // Only the mapping of the start of the file gives the base its addresses are relative to.
        if (std::stoull(line.substr(field + 1), nullptr, 16) != 0) {
            continue;
        }
        const auto path_at = line.find('/');
        if (path_at == std::string::npos) {
            continue;
        }
        found.path = line.substr(path_at);
        found.base = std::stoull(line.substr(0, dash), nullptr, 16);
        break;
    }

    if (found.base == 0) {
        return {};
    }
    // The library spans several mappings. The far end of the last one bounds anything that can
    // legitimately be a pointer into it.
    maps.clear();
    maps.seekg(0);
    for (std::string line; std::getline(maps, line);) {
        if (line.find(kClientLibrary) == std::string::npos) {
            continue;
        }
        const auto dash = line.find('-');
        const auto space = line.find(' ');
        if (dash == std::string::npos || space == std::string::npos) {
            continue;
        }
        const auto end = static_cast<std::uintptr_t>(std::stoull(line.substr(dash + 1, space - dash - 1), nullptr, 16));
        found.limit = std::max(found.limit, end);
    }
    return found;
}

std::vector<char> read_at(std::ifstream &file, const std::size_t offset, const std::size_t size)
{
    std::vector<char> buffer(size);
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return file ? buffer : std::vector<char>{};
}

/** A mangled type name for a packet looks like "10TextPacket": a length then the class name. */
bool is_packet_name(const std::string_view name)
{
    std::size_t digits = 0;
    while (digits < name.size() && name[digits] >= '0' && name[digits] <= '9') {
        ++digits;
    }
    if (digits == 0 || digits == name.size()) {
        return false;
    }
    const auto identifier = name.substr(digits);
    return identifier.size() > 6 && identifier.ends_with("Packet") &&
           std::stoul(std::string{name.substr(0, digits)}) == identifier.size();
}

std::vector<PacketClass> collect()
{
    const auto image = find_client();
    if (image.base == 0) {
        log::error("outbound: the client library is not mapped");
        return {};
    }

    std::ifstream file{image.path, std::ios::binary};
    if (!file) {
        log::error("outbound: cannot open {}", image.path);
        return {};
    }

    Elf64_Ehdr header{};
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0) {
        return {};
    }
    const auto sections = read_at(file, header.e_shoff, sizeof(Elf64_Shdr) * header.e_shnum);
    if (sections.empty()) {
        return {};
    }
    const auto *headers = reinterpret_cast<const Elf64_Shdr *>(sections.data());
    const auto &names_header = headers[header.e_shstrndx];
    const auto section_names = read_at(file, names_header.sh_offset, names_header.sh_size);

    std::vector<char> rodata;
    std::uintptr_t rodata_addr = 0;
    std::vector<char> rela;
    for (std::size_t i = 0; i < header.e_shnum; ++i) {
        const std::string_view name = &section_names[headers[i].sh_name];
        if (name == ".rodata") {
            rodata = read_at(file, headers[i].sh_offset, headers[i].sh_size);
            rodata_addr = headers[i].sh_addr;
        }
        else if (headers[i].sh_type == SHT_RELA && name == ".rela.dyn") {
            rela = read_at(file, headers[i].sh_offset, headers[i].sh_size);
        }
    }
    if (rodata.empty() || rela.empty()) {
        log::error("outbound: the client has no .rodata or .rela.dyn to read");
        return {};
    }

    // Type names first, so the relocations can be walked looking for pointers at them.
    std::unordered_map<std::uintptr_t, std::string> names;
    for (std::size_t i = 0; i < rodata.size();) {
        const std::string_view candidate{&rodata[i]};
        if (is_packet_name(candidate)) {
            names.emplace(rodata_addr + i, std::string{candidate});
        }
        i += candidate.size() + 1;
    }

    // The pointers this walks are filled in by relocation, so they are zero in the file itself and
    // only the relocation table says where each one points.
    const auto *entries = reinterpret_cast<const Elf64_Rela *>(rela.data());
    const auto count = rela.size() / sizeof(Elf64_Rela);

    std::unordered_map<std::uintptr_t, std::string> type_info;
    for (std::size_t i = 0; i < count; ++i) {
        if (ELF64_R_TYPE(entries[i].r_info) != R_X86_64_RELATIVE) {
            continue;
        }
        if (const auto it = names.find(static_cast<std::uintptr_t>(entries[i].r_addend)); it != names.end()) {
            // The name pointer is the second word of the type info object.
            type_info.emplace(entries[i].r_offset - sizeof(void *), it->second);
        }
    }

    std::vector<PacketClass> classes;
    for (std::size_t i = 0; i < count; ++i) {
        if (ELF64_R_TYPE(entries[i].r_info) != R_X86_64_RELATIVE) {
            continue;
        }
        if (const auto it = type_info.find(static_cast<std::uintptr_t>(entries[i].r_addend)); it != type_info.end()) {
            // A vtable holds the type info pointer just before its first function.
            auto *functions = reinterpret_cast<void **>(image.base + entries[i].r_offset + sizeof(void *));
            classes.push_back(PacketClass{it->second, functions});
        }
    }
    log::info("outbound: the client names {} packet classes across {} vtables", type_info.size(), classes.size());
    return classes;
}

}  // namespace

const std::vector<PacketClass> &packet_classes()
{
    static const std::vector<PacketClass> classes = collect();
    return classes;
}

namespace {
const Image &client()
{
    static const Image image = find_client();
    return image;
}
}  // namespace

std::uintptr_t client_base()
{
    return client().base;
}

std::uintptr_t client_limit()
{
    return client().limit;
}

}  // namespace spyglass::hook
