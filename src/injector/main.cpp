#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>

#include <AccCtrl.h>
#include <AclAPI.h>
#include <ShellAPI.h>
#include <TlHelp32.h>

namespace {

constexpr auto kDefaultProcess = L"Minecraft.Windows.exe";
constexpr auto kDefaultModule = L"spyglass.dll";

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

bool module_loaded(const DWORD pid, const std::wstring &name)
{
    const UniqueHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return false;
    }

    MODULEENTRY32W entry{.dwSize = sizeof(MODULEENTRY32W)};
    for (auto ok = Module32FirstW(snapshot.get(), &entry); ok; ok = Module32NextW(snapshot.get(), &entry)) {
        if (_wcsicmp(entry.szModule, name.c_str()) == 0) {
            return true;
        }
    }
    return false;
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

bool inject(const DWORD pid, const std::filesystem::path &payload)
{
    constexpr DWORD kAccess =
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ;
    const UniqueHandle process{OpenProcess(kAccess, FALSE, pid)};
    if (!process) {
        report(L"OpenProcess");
        std::fwprintf(stderr, L"hint: injecting into a packaged app needs an elevated prompt\n");
        return false;
    }
    return load_remotely(process.get(), payload);
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

    if (payload.empty()) {
        std::wstring self(MAX_PATH, L'\0');
        self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
        payload = std::filesystem::path{self}.parent_path() / kDefaultModule;
    }

    std::error_code ec;
    payload = std::filesystem::canonical(payload, ec);
    if (ec) {
        std::fwprintf(stderr, L"error: cannot find the payload: %hs\n", ec.message().c_str());
        return 1;
    }

    const auto pid = find_process(process_name);
    if (!pid) {
        std::fwprintf(stderr, L"error: %s is not running\n", process_name.c_str());
        return 1;
    }
    if (module_loaded(*pid, payload.filename().wstring())) {
        std::fwprintf(stderr, L"error: %s is already loaded in pid %lu\n", payload.filename().c_str(), *pid);
        return 1;
    }

    if (!grant_app_package_access(payload.parent_path(), true) || !grant_app_package_access(payload, false)) {
        return 1;
    }
    if (!inject(*pid, payload)) {
        return 1;
    }

    std::wprintf(L"injected %s into %s (pid %lu)\n", payload.filename().c_str(), process_name.c_str(), *pid);
    hold.succeeded = true;
    return 0;
}
