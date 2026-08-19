#include "spyglass/hook/host_symbol.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <elf.h>
#include <unistd.h>

namespace spyglass::hook {
namespace {

std::string executable_path()
{
    std::string path(4096, '\0');
    const auto length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) {
        return {};
    }
    path.resize(static_cast<std::size_t>(length));
    return path;
}

/** Where the kernel put the executable, which is a PIE, so every symbol value is relative. */
std::uintptr_t load_base(const std::string &path)
{
    std::ifstream maps{"/proc/self/maps"};
    for (std::string line; std::getline(maps, line);) {
        if (line.size() < path.size() || line.compare(line.size() - path.size(), path.size(), path) != 0) {
            continue;
        }
        const auto dash = line.find('-');
        if (dash == std::string::npos) {
            continue;
        }
        // Only the mapping of the start of the file gives the base the symbols are relative to.
        std::size_t field = line.find(' ', dash);
        field = line.find(' ', field + 1);
        if (field == std::string::npos || std::stoull(line.substr(field + 1), nullptr, 16) != 0) {
            continue;
        }
        return std::stoull(line.substr(0, dash), nullptr, 16);
    }
    return 0;
}

std::vector<char> read_at(std::ifstream &file, const std::size_t offset, const std::size_t size)
{
    std::vector<char> buffer(size);
    file.seekg(static_cast<std::streamoff>(offset));
    file.read(buffer.data(), static_cast<std::streamsize>(size));
    return file ? buffer : std::vector<char>{};
}

}  // namespace

void *host_symbol(const std::string_view name)
{
    const auto path = executable_path();
    const auto base = path.empty() ? 0 : load_base(path);
    if (base == 0) {
        return nullptr;
    }

    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return nullptr;
    }

    Elf64_Ehdr header{};
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (!file || std::memcmp(header.e_ident, ELFMAG, SELFMAG) != 0) {
        return nullptr;
    }

    const auto sections = read_at(file, header.e_shoff, sizeof(Elf64_Shdr) * header.e_shnum);
    if (sections.empty()) {
        return nullptr;
    }
    const auto *headers = reinterpret_cast<const Elf64_Shdr *>(sections.data());

    // A launcher built without -rdynamic keeps its own functions out of .dynsym, so .symtab is
    // where they are, and it is searched first for that reason.
    for (const auto kind : {SHT_SYMTAB, SHT_DYNSYM}) {
        for (std::size_t i = 0; i < header.e_shnum; ++i) {
            if (headers[i].sh_type != static_cast<Elf64_Word>(kind) || headers[i].sh_link >= header.e_shnum) {
                continue;
            }
            const auto symbols = read_at(file, headers[i].sh_offset, headers[i].sh_size);
            const auto &strings_header = headers[headers[i].sh_link];
            const auto strings = read_at(file, strings_header.sh_offset, strings_header.sh_size);
            if (symbols.empty() || strings.empty()) {
                continue;
            }

            const auto *entries = reinterpret_cast<const Elf64_Sym *>(symbols.data());
            for (std::size_t s = 0; s < symbols.size() / sizeof(Elf64_Sym); ++s) {
                const auto &symbol = entries[s];
                if (symbol.st_shndx == SHN_UNDEF || symbol.st_value == 0 || symbol.st_name >= strings.size()) {
                    continue;
                }
                if (name == &strings[symbol.st_name]) {
                    return reinterpret_cast<void *>(base + symbol.st_value);
                }
            }
        }
    }
    return nullptr;
}

}  // namespace spyglass::hook
