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
    CloseHandle(file);
    return ok;
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
    const auto ack = setting(ini, L"AckPath");
    // The fixture must never write a receipt/config file outside temporary tests.
    std::vector<wchar_t> temporary(32768), full(32768);
    const auto temporaryLength = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
    if (ack.empty() || !temporaryLength || !GetFullPathNameW(ack.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr)
        || _wcsnicmp(full.data(), temporary.data(), temporaryLength) != 0) return 3;
    writeFile(parent + L"\\startup-probe.pid", std::to_string(GetCurrentProcessId()));
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
            if (ack.size() <= suffix.size() || ack.compare(ack.size() - suffix.size(), suffix.size(), suffix) != 0) return 5;
            const auto blocked = ack.substr(0, ack.size() - suffix.size()) + L".json.tmp";
            if (!GetFullPathNameW(blocked.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr)
                || _wcsnicmp(full.data(), temporary.data(), temporaryLength) != 0
                || !CreateDirectoryW(blocked.c_str(), nullptr)) return 6;
        }
        const auto json = "{\"attemptId\":" + quoted(actualAttempt) + ",\"currentExePath\":" + quoted(actualPath)
            + ",\"version\":" + quoted(version) + ",\"pid\":" + std::to_string(pid)
            + ",\"ready\":" + (mode == L"failed" ? "false" : "true")
            + ",\"reason\":" + quoted(mode == L"failed" ? L"Stand-in startup failed" : L"") + "}";
        if (!writeFile(ack + L".tmp", json) || !MoveFileExW((ack + L".tmp").c_str(), ack.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 4;
    }
    if (mode != L"dead-ack" && lifetime <= 15000) Sleep(lifetime);
    return 0;
}
