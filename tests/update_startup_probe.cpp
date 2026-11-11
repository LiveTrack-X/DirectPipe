// Test-only, consoleless startup stand-in. Never links DirectPipe/JUCE/audio.
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace {
std::string utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), bytes, nullptr, nullptr);
    return result;
}
std::string quoted(const std::wstring& text)
{
    std::string result = "\"";
    for (const auto ch : utf8(text)) {
        if (ch == '\\' || ch == '"') result += '\\';
        if (ch == '\r') { result += "\\r"; continue; }
        if (ch == '\n') { result += "\\n"; continue; }
        result += ch;
    }
    return result + "\"";
}
bool writeFile(const std::wstring& path, const std::string& bytes)
{
    const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE && written == bytes.size();
    const auto failure = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    SetLastError(failure);
    return ok;
}
std::wstring canonicalDirectory(const std::wstring& path)
{
    const auto directory = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return {};
    std::vector<wchar_t> result(32768);
    const auto length = GetFinalPathNameByHandleW(directory, result.data(), static_cast<DWORD>(result.size()),
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const auto failure = GetLastError();
    CloseHandle(directory);
    SetLastError(failure);
    return length > 0 && length < result.size() ? std::wstring(result.data(), length) : std::wstring{};
}
bool containedDirectory(const std::wstring& directory, const std::wstring& root)
{
    if (directory.empty() || root.empty() || directory.size() < root.size()
        || _wcsnicmp(directory.c_str(), root.c_str(), root.size()) != 0) return false;
    return directory.size() == root.size() || root.back() == L'\\' || directory[root.size()] == L'\\';
}
std::wstring setting(const std::wstring& ini, const wchar_t* key, const wchar_t* fallback = L"")
{
    std::vector<wchar_t> value(32768);
    GetPrivateProfileStringW(L"StartupProbe", key, fallback, value.data(), static_cast<DWORD>(value.size()), ini.c_str());
    return value.data();
}
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::vector<wchar_t> module(32768);
    if (!GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()))) return 2;
    const std::wstring executable(module.data());
    const auto parent = executable.substr(0, executable.find_last_of(L"\\/"));
    const auto ini = parent + L"\\startup-probe.ini";
    // Resolve existing directories through handles: JUCE may use an 8.3 TEMP
    // alias while the desktop shell gives the child its equivalent long path.
    // A lexical prefix comparison would reject that owned fixture.
    std::vector<wchar_t> temporary(32768), full(32768);
    const auto temporaryLength = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    const auto temporaryDirectory = temporaryLength > 0 && temporaryLength < temporary.size()
        ? canonicalDirectory(temporary.data()) : std::wstring{};
    const bool ownedModule = containedDirectory(canonicalDirectory(parent), temporaryDirectory);
    const auto finish = [&](int code, const char* reason, DWORD nativeError = ERROR_SUCCESS) {
        if (ownedModule) {
            writeFile(parent + L"\\startup-probe.diagnostic.txt", "exit=" + std::to_string(code)
                + "\nreason=" + reason + "\nnativeError=" + std::to_string(nativeError) + "\n");
        }
        return code;
    };
    if (!ownedModule) return finish(3, "module-outside-temp");
    // The fixture must never write a receipt/config file outside temporary tests.
    if (!writeFile(parent + L"\\startup-probe.pid", std::to_string(GetCurrentProcessId())))
        return finish(7, "pid-write-failed", GetLastError());
    const auto ack = setting(ini, L"AckPath");
    const auto fullLength = ack.empty() ? 0 : GetFullPathNameW(ack.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (!fullLength || fullLength >= full.size()) return finish(3, "ack-path-invalid", GetLastError());
    const std::wstring acknowledgement(full.data(), fullLength);
    const auto slash = acknowledgement.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash + 1 == acknowledgement.size()) return finish(3, "ack-path-invalid");
    if (!containedDirectory(canonicalDirectory(acknowledgement.substr(0, slash)), temporaryDirectory))
        return finish(3, "ack-outside-temp");
    int count = 0;
    auto** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    std::wstring attempt;
    const std::wstring prefix = L"--directpipe-update-attempt=";
    for (int i = 1; arguments && i < count; ++i) {
        const std::wstring value(arguments[i]);
        if (value.compare(0, prefix.size(), prefix) == 0) attempt = value.substr(prefix.size());
    }
    if (arguments) LocalFree(arguments);
    const auto mode = setting(ini, L"Mode", L"ready");
    const auto delay = GetPrivateProfileIntW(L"StartupProbe", L"DelayMs", 0, ini.c_str());
    const auto lifetime = GetPrivateProfileIntW(L"StartupProbe", L"LifetimeMs", 5000, ini.c_str());
    if (delay > 0 && delay <= 15000) Sleep(delay);
    if (!attempt.empty() && mode != L"no-ack") {
        auto actualPath = executable;
        auto version = setting(ini, L"Version", L"4.5.0");
        auto actualAttempt = attempt;
        DWORD pid = GetCurrentProcessId();
        if (mode == L"stale-ack") actualAttempt = L"00000000000000000000000000000001";
        if (mode == L"wrong-path") actualPath += L".other";
        if (mode == L"wrong-version") version = L"0.0.0";
        if (mode == L"wrong-pid") pid = GetPrivateProfileIntW(L"StartupProbe", L"WrongPid", 0, ini.c_str());
        if (mode == L"block-receipt") {
            // The script has already saved its pending receipt. Block only the
            // derived sibling TEMP .tmp path before this valid ACK is emitted.
            const auto suffix = L"." + attempt + L".startup.json";
            if (ack.size() <= suffix.size() || ack.compare(ack.size() - suffix.size(), suffix.size(), suffix) != 0)
                return finish(5, "blocked-receipt-suffix-invalid");
            const auto blocked = ack.substr(0, ack.size() - suffix.size()) + L".json.tmp";
            const auto blockedSlash = blocked.find_last_of(L"\\/");
            if (blockedSlash == std::wstring::npos
                || !containedDirectory(canonicalDirectory(blocked.substr(0, blockedSlash)), temporaryDirectory)
                || !CreateDirectoryW(blocked.c_str(), nullptr))
                return finish(6, "blocked-receipt-create-failed", GetLastError());
        }
        const auto json = "{\"attemptId\":" + quoted(actualAttempt) + ",\"currentExePath\":" + quoted(actualPath)
            + ",\"version\":" + quoted(version) + ",\"pid\":" + std::to_string(pid)
            + ",\"ready\":" + (mode == L"failed" ? "false" : "true")
            + ",\"reason\":" + quoted(mode == L"failed" ? L"Stand-in startup failed" : L"") + "}";
        if (!writeFile(ack + L".tmp", json) || !MoveFileExW((ack + L".tmp").c_str(), ack.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return finish(4, "ack-commit-failed", GetLastError());
    }
    if (mode != L"dead-ack" && lifetime <= 15000) Sleep(lifetime);
    return finish(0, "complete");
}
