#include <algorithm>
#include <array>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>

#include <AccCtrl.h>
#include <AclAPI.h>
#include <appmodel.h>
#include <ShellAPI.h>
#include <TlHelp32.h>

namespace {

constexpr auto kDefaultProcess = L"Minecraft.Windows.exe";
constexpr std::wstring_view kPayloadPrefix = L"spyglass-" SPYGLASS_VERSION L"-";
constexpr auto kPreviewPackage = L"Microsoft.MinecraftWindowsBeta";

struct HandleDeleter {
    void operator()(HANDLE handle) const noexcept
    {
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE) {
            CloseHandle(handle);
        }
    }
};

using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

struct LocalDeleter {
    void operator()(void *memory) const noexcept { LocalFree(memory); }
};

void report(const std::wstring_view what, const DWORD error = GetLastError())
{
    wchar_t *text = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                   error, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    const std::unique_ptr<wchar_t, LocalDeleter> owned{text};
    std::fwprintf(stderr, L"error: %.*s failed (%lu): %s", static_cast<int>(what.size()), what.data(), error,
                  text != nullptr ? text : L"\n");
}

bool elevated()
{
    HANDLE raw = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw) == 0) {
        return false;
    }
    const UniqueHandle token{raw};
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) != 0 &&
           elevation.TokenIsElevated != 0;
}

std::wstring_view arguments_of(const std::wstring_view command_line)
{
    std::size_t cursor = 0;
    if (!command_line.empty() && command_line.front() == L'"') {
        cursor = command_line.find(L'"', 1);
        cursor = cursor == std::wstring_view::npos ? command_line.size() : cursor + 1;
    }
    else {
        cursor = command_line.find_first_of(L" \t");
        cursor = cursor == std::wstring_view::npos ? command_line.size() : cursor;
    }

    const auto rest = command_line.substr(cursor);
    const auto start = rest.find_first_not_of(L" \t");
    return start == std::wstring_view::npos ? std::wstring_view{} : rest.substr(start);
}

int relaunch_elevated()
{
    std::wstring self(MAX_PATH, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    const auto directory = std::filesystem::current_path().wstring();
    const std::wstring arguments{arguments_of(GetCommandLineW())};

    SHELLEXECUTEINFOW request{
        .cbSize = sizeof(SHELLEXECUTEINFOW),
        .fMask = SEE_MASK_NOCLOSEPROCESS,
        .lpVerb = L"runas",
        .lpFile = self.c_str(),
        .lpParameters = arguments.empty() ? nullptr : arguments.c_str(),
        .lpDirectory = directory.c_str(),
        .nShow = SW_SHOWNORMAL,
    };
    if (ShellExecuteExW(&request) == 0) {
        const auto error = GetLastError();
        if (error == ERROR_CANCELLED) {
            std::fwprintf(stderr, L"error: the elevation prompt was declined\n");
            return 1;
        }
        report(L"ShellExecuteExW", error);
        return 1;
    }

    const UniqueHandle child{request.hProcess};
    WaitForSingleObject(child.get(), INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.get(), &code);
    return static_cast<int>(code);
}

struct ConsoleHold {
    bool succeeded{false};

    ~ConsoleHold()
    {
        if (succeeded) {
            return;
        }
        DWORD owners[2]{};
        const bool console_dies_with_this_process = GetConsoleProcessList(owners, 2) == 1;
        if (!console_dies_with_this_process) {
            return;
        }
        std::fwprintf(stderr, L"\npress ENTER to close\n");
        std::getwchar();
    }
};

std::optional<DWORD> find_process(const std::wstring &name)
{
    const UniqueHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        report(L"CreateToolhelp32Snapshot");
        return std::nullopt;
    }

    PROCESSENTRY32W entry{.dwSize = sizeof(PROCESSENTRY32W)};
    for (auto ok = Process32FirstW(snapshot.get(), &entry); ok; ok = Process32NextW(snapshot.get(), &entry)) {
        if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
            return entry.th32ProcessID;
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> loaded_payload(const DWORD pid, const std::wstring &name)
{
    const UniqueHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    MODULEENTRY32W entry{.dwSize = sizeof(MODULEENTRY32W)};
    for (auto ok = Module32FirstW(snapshot.get(), &entry); ok; ok = Module32NextW(snapshot.get(), &entry)) {
        const std::wstring_view module{entry.szModule};
        if (module.starts_with(L"spyglass-") || _wcsicmp(entry.szModule, name.c_str()) == 0) {
            return std::wstring{module};
        }
    }
    return std::nullopt;
}

struct Client {
    bool preview;
    std::array<unsigned, 4> version;
};

std::optional<Client> identify_by_package(const HANDLE process)
{
    UINT32 length = 0;
    if (GetPackageFullName(process, &length, nullptr) != ERROR_INSUFFICIENT_BUFFER) {
        return std::nullopt;
    }

    std::wstring package(length, L'\0');
    if (GetPackageFullName(process, &length, package.data()) != ERROR_SUCCESS) {
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

    return Client{
        .preview = package.starts_with(kPreviewPackage),
        .version = {numbers[0], numbers[1], patch_and_build / 100, patch_and_build % 100},
    };
}

std::optional<bool> image_names_preview(const std::wstring &image)
{
    const UniqueHandle file{CreateFileW(image.c_str(), GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_ATTRIBUTE_NORMAL, nullptr)};
    LARGE_INTEGER size{};
    if (file.get() == INVALID_HANDLE_VALUE || GetFileSizeEx(file.get(), &size) == 0) {
        return std::nullopt;
    }

    const UniqueHandle mapping{CreateFileMappingW(file.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
    if (!mapping) {
        return std::nullopt;
    }
    const auto *const view = static_cast<const std::byte *>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0));
    if (view == nullptr) {
        return std::nullopt;
    }

    constexpr std::wstring_view name = L"Minecraft Preview";
    const auto *const bytes = reinterpret_cast<const std::byte *>(name.data());
    const auto *const end = view + size.QuadPart;
    const auto found = std::search(view, end, bytes, bytes + name.size() * sizeof(wchar_t)) != end;
    UnmapViewOfFile(view);
    return found;
}

std::optional<Client> identify_by_image(const HANDLE process)
{
    std::wstring image(32768, L'\0');
    auto length = static_cast<DWORD>(image.size());
    if (QueryFullProcessImageNameW(process, 0, image.data(), &length) == 0) {
        return std::nullopt;
    }
    image.resize(length);

    DWORD unused = 0;
    const auto bytes = GetFileVersionInfoSizeW(image.c_str(), &unused);
    if (bytes == 0) {
        return std::nullopt;
    }

    std::vector<std::byte> block(bytes);
    VS_FIXEDFILEINFO *info = nullptr;
    UINT size = 0;
    if (GetFileVersionInfoW(image.c_str(), 0, bytes, block.data()) == 0 ||
        VerQueryValueW(block.data(), L"\\", reinterpret_cast<void **>(&info), &size) == 0 ||
        size < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }

    const auto preview = image_names_preview(image);
    if (!preview) {
        return std::nullopt;
    }

    return Client{
        .preview = *preview,
        .version = {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS),
                    LOWORD(info->dwFileVersionLS)},
    };
}

std::optional<Client> identify(const HANDLE process)
{
    if (const auto client = identify_by_package(process)) {
        return client;
    }
    if (const auto client = identify_by_image(process)) {
        return client;
    }
    std::fwprintf(stderr, L"error: the client has neither a package identity nor a readable version resource\n");
    return std::nullopt;
}

struct Target {
    bool preview;
    std::array<unsigned, 3> version;
};

std::optional<Target> parse_target(std::wstring_view tag)
{
    constexpr std::wstring_view kPreviewSuffix = L".preview";
    Target target{.preview = tag.ends_with(kPreviewSuffix)};
    if (target.preview) {
        tag.remove_suffix(kPreviewSuffix.size());
    }

    for (auto &component : target.version) {
        const auto dot = tag.find(L'.');
        const auto text = tag.substr(0, dot);
        if (text.empty() || text.find_first_not_of(L"0123456789") != std::wstring_view::npos) {
            return std::nullopt;
        }
        component = static_cast<unsigned>(std::wcstoul(std::wstring{text}.c_str(), nullptr, 10));
        tag = dot == std::wstring_view::npos ? std::wstring_view{} : tag.substr(dot + 1);
    }
    return tag.empty() ? std::optional{target} : std::nullopt;
}

std::optional<std::filesystem::path> select_payload(const std::filesystem::path &directory, const Client &client)
{
    const std::array<unsigned, 3> running{client.version[0], client.version[1], client.version[2]};
    std::optional<Target> best;
    std::filesystem::path chosen;
    std::vector<std::wstring> offered;

    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator{directory, ec}) {
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(kPayloadPrefix) || _wcsicmp(entry.path().extension().c_str(), L".dll") != 0) {
            continue;
        }
        const auto stem = entry.path().stem().wstring();
        const auto target = parse_target(std::wstring_view{stem}.substr(kPayloadPrefix.size()));
        if (!target) {
            continue;
        }
        offered.push_back(name);
        if (target->preview != client.preview || target->version > running) {
            continue;
        }
        if (!best || target->version > best->version) {
            best = target;
            chosen = entry.path();
        }
    }

    if (!best) {
        std::fwprintf(stderr, L"error: none of the payloads beside the injector target this client\n");
        for (const auto &name : offered) {
            std::fwprintf(stderr, L"       %s\n", name.c_str());
        }
        if (offered.empty()) {
            std::fwprintf(stderr, L"       %s*.dll matched nothing in %s\n", std::wstring{kPayloadPrefix}.c_str(),
                          directory.c_str());
        }
        return std::nullopt;
    }
    return chosen;
}

bool grant_app_package_access(const std::filesystem::path &path, const bool inherit)
{
    std::vector<std::byte> storage(SECURITY_MAX_SID_SIZE);
    auto *sid = static_cast<PSID>(storage.data());
    auto size = static_cast<DWORD>(storage.size());
    if (!CreateWellKnownSid(WinBuiltinAnyPackageSid, nullptr, sid, &size)) {
        report(L"CreateWellKnownSid");
        return false;
    }

    PACL existing = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    auto status = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                        &existing, nullptr, &descriptor);
    if (status != ERROR_SUCCESS) {
        report(L"GetNamedSecurityInfoW", status);
        return false;
    }
    const std::unique_ptr<void, LocalDeleter> owned_descriptor{descriptor};

    EXPLICIT_ACCESS_W access{
        .grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE,
        .grfAccessMode = GRANT_ACCESS,
        .grfInheritance = inherit ? static_cast<DWORD>(OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE) : NO_INHERITANCE,
        .Trustee =
            {
                .TrusteeForm = TRUSTEE_IS_SID,
                .TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP,
                .ptstrName = static_cast<LPWSTR>(sid),
            },
    };

    PACL updated = nullptr;
    status = SetEntriesInAclW(1, &access, existing, &updated);
    const std::unique_ptr<ACL, LocalDeleter> owned_acl{updated};
    if (status == ERROR_SUCCESS) {
        auto target = path.wstring();
        status = SetNamedSecurityInfoW(target.data(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                       updated, nullptr);
    }
    if (status != ERROR_SUCCESS) {
        report(L"SetNamedSecurityInfoW", status);
        return false;
    }
    return true;
}

bool load_remotely(const HANDLE process, const std::filesystem::path &payload)
{
    const auto text = payload.wstring();
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);

    auto *remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remote == nullptr) {
        report(L"VirtualAllocEx");
        return false;
    }

    auto ok = WriteProcessMemory(process, remote, text.c_str(), bytes, nullptr) != 0;
    if (!ok) {
        report(L"WriteProcessMemory");
    }

    if (ok) {
        auto *loader = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
        const UniqueHandle thread{CreateRemoteThread(
            process, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(loader), remote, 0, nullptr)};
        if (!thread) {
            report(L"CreateRemoteThread");
            ok = false;
        }
        else {
            WaitForSingleObject(thread.get(), INFINITE);
            DWORD exit_code = 0;
            GetExitCodeThread(thread.get(), &exit_code);
            if (exit_code == 0) {
                std::fwprintf(stderr, L"error: LoadLibraryW returned null inside the target\n");
                ok = false;
            }
        }
    }

    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return ok;
}

UniqueHandle open_client(const DWORD pid)
{
    constexpr DWORD kAccess =
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    UniqueHandle process{OpenProcess(kAccess, FALSE, pid)};
    if (!process) {
        report(L"OpenProcess");
        std::fwprintf(stderr, L"hint: injecting into a packaged app needs an elevated prompt\n");
    }
    return process;
}

}  // namespace

int wmain(const int argc, wchar_t **argv)
{
    std::filesystem::path payload;
    std::wstring process_name = kDefaultProcess;

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view argument = argv[i];
        if (argument == L"--dll" && i + 1 < argc) {
            payload = argv[++i];
        }
        else if (argument == L"--process" && i + 1 < argc) {
            process_name = argv[++i];
        }
        else {
            std::fwprintf(stderr, L"usage: spyglass [--dll <path>] [--process <name>]\n");
            return 2;
        }
    }

    if (!elevated()) {
        std::wprintf(L"asking for administrator rights, the run continues in a new window\n");
        return relaunch_elevated();
    }

    ConsoleHold hold;

    const auto pid = find_process(process_name);
    if (!pid) {
        std::fwprintf(stderr, L"error: %s is not running\n", process_name.c_str());
        return 1;
    }

    const auto process = open_client(*pid);
    if (!process) {
        return 1;
    }

    if (payload.empty()) {
        const auto client = identify(process.get());
        if (!client) {
            std::fwprintf(stderr, L"hint: name the payload yourself with --dll <path>\n");
            return 1;
        }
        std::wprintf(L"client: Minecraft%s %u.%u.%u.%u\n", client->preview ? L" Preview" : L"", client->version[0],
                     client->version[1], client->version[2], client->version[3]);

        std::wstring self(MAX_PATH, L'\0');
        self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
        auto chosen = select_payload(std::filesystem::path{self}.parent_path(), *client);
        if (!chosen) {
            return 1;
        }
        payload = std::move(*chosen);
    }

    std::error_code ec;
    payload = std::filesystem::canonical(payload, ec);
    if (ec) {
        std::fwprintf(stderr, L"error: cannot find the payload: %hs\n", ec.message().c_str());
        return 1;
    }
    if (const auto loaded = loaded_payload(*pid, payload.filename().wstring())) {
        std::fwprintf(stderr, L"error: %s is already loaded in pid %lu\n", loaded->c_str(), *pid);
        return 1;
    }

    if (!grant_app_package_access(payload.parent_path(), true) || !grant_app_package_access(payload, false)) {
        return 1;
    }
    if (!load_remotely(process.get(), payload)) {
        return 1;
    }

    std::wprintf(L"injected %s into %s (pid %lu)\n", payload.filename().c_str(), process_name.c_str(), *pid);
    hold.succeeded = true;
    return 0;
}
