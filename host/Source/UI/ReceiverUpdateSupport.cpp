// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#include "ReceiverUpdateSupport.h"
#include "UpdateScript.h"

#include <algorithm>
#include <cstring>
#include <set>

#if JUCE_WINDOWS
#include <Windows.h>
#include <RestartManager.h>
#include <AclAPI.h>
#endif

namespace directpipe::update_detail {
namespace {

bool incidentalDirectory(const juce::File& file)
{
    const auto name = file.getFileName().toLowerCase();
    return name == ".git" || name == "build" || name.startsWith("build-")
        || name == "dist" || name == "artifacts" || name == "_deps"
        || name.endsWith("_artefacts") || name.contains(".directpipe-")
        || name.startsWith(".dp-s-") || name.startsWith(".dp-p-");
}

#if JUCE_WINDOWS
bool hasReparseAncestor(juce::File file)
{
    while (file != juce::File()) {
        const auto attrs = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return true;
        const auto parent = file.getParentDirectory();
        if (parent == file) break;
        file = parent;
    }
    return false;
}

bool inspectPE(const juce::File& file, const char* requiredExport, juce::String& error)
{
    // Parse bytes, not LoadLibrary: discovering an installation must not execute
    // third-party initialization or acquire an audio plug-in lifetime.
    juce::MemoryBlock bytes;
    if (file.getSize() > 256 * 1024 * 1024 || !file.loadFileAsData(bytes)) {
        error = "Cannot read Receiver binary: " + file.getFullPathName();
        return false;
    }
    const auto* data = static_cast<const unsigned char*>(bytes.getData());
    const auto size = bytes.getSize();
    auto read = [&](size_t offset, auto& result) {
        if (offset > size || sizeof(result) > size - offset) return false;
        std::memcpy(&result, data + offset, sizeof(result));
        return true;
    };
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 nt{};
    if (!read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0
        || !read(static_cast<size_t>(dos.e_lfanew), nt)
        || nt.Signature != IMAGE_NT_SIGNATURE
        || nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
        || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || nt.FileHeader.NumberOfSections > 96
        || nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXPORT) {
        error = "Receiver must be a valid Windows x64 binary.";
        return false;
    }
    const auto sectionOffset = static_cast<size_t>(dos.e_lfanew) + sizeof(DWORD)
        + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
    auto offsetOf = [&](DWORD rva) -> size_t {
        for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
            IMAGE_SECTION_HEADER section{};
            if (!read(sectionOffset + i * sizeof(section), section)) return size;
            if (rva >= section.VirtualAddress
                && rva - section.VirtualAddress < section.SizeOfRawData) {
                const auto offset = static_cast<size_t>(section.PointerToRawData)
                    + rva - section.VirtualAddress;
                return offset < size ? offset : size;
            }
        }
        return size;
    };
    IMAGE_EXPORT_DIRECTORY exports{};
    if (!read(offsetOf(nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress), exports)
        || exports.NumberOfNames > 65536) {
        error = "Receiver plug-in exports cannot be inspected.";
        return false;
    }
    const auto namesOffset = offsetOf(exports.AddressOfNames);
    for (DWORD i = 0; i < exports.NumberOfNames; ++i) {
        DWORD rva = 0;
        if (!read(namesOffset + i * sizeof(DWORD), rva)) break;
        const auto offset = offsetOf(rva);
        const auto length = std::strlen(requiredExport);
        if (offset < size && length + 1 <= size - offset
            && std::memcmp(data + offset, requiredExport, length + 1) == 0)
            return true;
    }
    error = "Receiver does not expose the expected plug-in format.";
    return false;
}

bool readIdentity(const juce::File& file, juce::String& version, juce::String& error)
{
    DWORD ignored = 0;
    const auto path = file.getFullPathName();
    const DWORD length = GetFileVersionInfoSizeW(path.toWideCharPointer(), &ignored);
    if (length == 0 || length > 1024 * 1024) {
        error = "Receiver version metadata is missing or unreadable: " + path;
        return false;
    }
    std::vector<unsigned char> data(length);
    if (!GetFileVersionInfoW(path.toWideCharPointer(), 0, length, data.data())) {
        error = "Receiver version metadata cannot be read: " + path;
        return false;
    }
    struct Translation { WORD language; WORD codePage; };
    Translation* translations = nullptr;
    UINT translationsSize = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
                       reinterpret_cast<void**>(&translations), &translationsSize)) {
        error = "Receiver product metadata is missing: " + path;
        return false;
    }
    for (UINT i = 0; i < translationsSize / sizeof(Translation); ++i) {
        auto field = [&](const wchar_t* key) -> juce::String {
            wchar_t query[128]{};
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s",
                       translations[i].language, translations[i].codePage, key);
            wchar_t* value = nullptr;
            UINT chars = 0;
            if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &chars)
                || value == nullptr || chars == 0) return {};
            return juce::String(value);
        };
        if (field(L"ProductName") != "DirectPipe Receiver") continue;
        const auto fileVersion = field(L"FileVersion");
        const auto productVersion = field(L"ProductVersion");
        std::array<int, 3> parts{};
        juce::String canonical;
        if (fileVersion == productVersion
            && parseStrictReleaseVersion(fileVersion, parts, canonical)
            && canonical == fileVersion) {
            version = canonical;
            return true;
        }
    }
    error = "Receiver product/version identity is not recognized: " + path;
    return false;
}

bool isProtectedLocation(const juce::File& file)
{
    for (const auto* name : { "ProgramFiles", "ProgramW6432", "ProgramFiles(x86)", "SystemRoot" }) {
        const auto value = juce::SystemStats::getEnvironmentVariable(name, {});
        if (value.isNotEmpty() && file.isAChildOf(juce::File(value))) return true;
    }
    // Read the actual parent ACL as well as known protected roots. A custom
    // plug-in folder can require elevation without being under Program Files.
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const auto directory = file.getParentDirectory().getFullPathName();
    const auto code = GetNamedSecurityInfoW(directory.toWideCharPointer(), SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr, &descriptor);
    if (code != ERROR_SUCCESS || descriptor == nullptr) return true;
    HANDLE token = nullptr, impersonation = nullptr;
    bool writable = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token)
        && DuplicateToken(token, SecurityImpersonation, &impersonation)) {
        GENERIC_MAPPING mapping{ FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
        DWORD wanted = FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY | FILE_DELETE_CHILD;
        MapGenericMask(&wanted, &mapping);
        std::vector<unsigned char> privileges(4096);
        DWORD privilegeSize = static_cast<DWORD>(privileges.size()), granted = 0;
        BOOL allowed = FALSE;
        writable = AccessCheck(descriptor, impersonation, wanted, &mapping,
            reinterpret_cast<PRIVILEGE_SET*>(privileges.data()), &privilegeSize, &granted, &allowed)
            && allowed;
    }
    if (impersonation) CloseHandle(impersonation);
    if (token) CloseHandle(token);
    LocalFree(descriptor);
    return !writable;
}
#endif

} // namespace

bool receiverNeedsUpdate(const juce::String& installedVersion, const juce::String& releaseVersion)
{
    std::array<int, 3> installed{}, release{};
    juce::String canonical;
    return parseStrictReleaseVersion(installedVersion, installed, canonical)
        && parseStrictReleaseVersion(releaseVersion, release, canonical)
        && installed < release;
}

std::vector<juce::File> receiverSearchDirectories()
{
    std::vector<juce::File> result;
#if JUCE_WINDOWS
    auto add = [&](const juce::String& value) {
        if (value.isEmpty() || !juce::File::isAbsolutePath(value)) return;
        const juce::File directory(value);
        if (std::find(result.begin(), result.end(), directory) == result.end())
            result.push_back(directory);
    };
    for (const auto* name : { "CommonProgramFiles", "CommonProgramW6432" }) {
        const auto value = juce::SystemStats::getEnvironmentVariable(name, {});
        if (value.isNotEmpty()) {
            add(juce::File(value).getChildFile("VST2").getFullPathName());
            add(juce::File(value).getChildFile("VST3").getFullPathName());
        }
    }
    const auto programFiles = juce::SystemStats::getEnvironmentVariable("ProgramW6432",
        juce::SystemStats::getEnvironmentVariable("ProgramFiles", {}));
    if (programFiles.isNotEmpty()) {
        add(juce::File(programFiles).getChildFile("VSTPlugins").getFullPathName());
        add(juce::File(programFiles).getChildFile("Steinberg/VstPlugins").getFullPathName());
    }
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty())
        add(juce::File(local).getChildFile("Programs/Common/VST3").getFullPathName());
    for (auto hive : { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE }) {
        for (auto view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY }) {
            HKEY key = nullptr;
            if (RegOpenKeyExW(hive, L"SOFTWARE\\VST", 0, KEY_READ | view, &key) != ERROR_SUCCESS)
                continue;
            wchar_t value[32768]{};
            DWORD type = 0, bytes = sizeof(value);
            if (RegQueryValueExW(key, L"VSTPluginsPath", nullptr, &type,
                    reinterpret_cast<BYTE*>(value), &bytes) == ERROR_SUCCESS
                && (type == REG_SZ || type == REG_EXPAND_SZ)) {
                value[std::size(value) - 1] = 0;
                wchar_t expanded[32768]{};
                const auto length = ExpandEnvironmentStringsW(value, expanded, static_cast<DWORD>(std::size(expanded)));
                if (length > 0 && length <= std::size(expanded)) add(juce::String(expanded));
            }
            RegCloseKey(key);
        }
    }
#endif
    return result;
}

bool inspectReceiverTarget(const juce::File& path, ReceiverInstallTarget& target, juce::String& error)
{
    error.clear();
#if JUCE_WINDOWS
    const bool vst2 = path.getFileName().equalsIgnoreCase("DirectPipe Receiver.dll");
    const bool vst3 = path.getFileName().equalsIgnoreCase("DirectPipe Receiver.vst3") && path.isDirectory();
    if (!vst2 && !vst3) { error = "Select the DirectPipe Receiver DLL or complete VST3 bundle."; return false; }
    const auto binary = vst2 ? path : path.getChildFile("Contents/x86_64-win/DirectPipe Receiver.vst3");
    if (!binary.existsAsFile() || hasReparseAncestor(binary)) {
        error = "Receiver is missing, inaccessible, or uses a linked path: " + binary.getFullPathName();
        return false;
    }
    juce::String version;
    if (!readIdentity(binary, version, error)
        || !inspectPE(binary, vst2 ? "VSTPluginMain" : "GetPluginFactory", error)) return false;
    target = { vst2 ? ReceiverFormat::vst2 : ReceiverFormat::vst3,
               path.getFullPathName(), binary.getFullPathName(), version, isProtectedLocation(path) };
    return true;
#else
    juce::ignoreUnused(path, target);
    error = "Automatic Receiver installation is available on Windows only.";
    return false;
#endif
}

ReceiverDiscoveryResult discoverReceiversInDirectories(const std::vector<juce::File>& directories,
                                                       const ReceiverTargetInspector& inspector)
{
    ReceiverDiscoveryResult result;
    std::set<juce::String> visited;
    size_t visitedCount = 0;
    bool depthLimited = false;
    std::function<void(const juce::File&, int)> visit = [&](const juce::File& path, int depth) {
        if (depth > 6) { depthLimited = true; return; }
        if (++visitedCount > 20000) return;
        if (!visited.insert(path.getFullPathName().toLowerCase()).second) return;
        const auto name = path.getFileName();
        if (name.equalsIgnoreCase("DirectPipe Receiver.dll")
            || name.equalsIgnoreCase("DirectPipe Receiver.vst3")) {
            ReceiverInstallTarget target;
            juce::String error;
            if (inspector(path, target, error)) result.targets.push_back(std::move(target));
            else result.warnings.add(error.isEmpty() ? "Cannot inspect " + path.getFullPathName() : error);
            return;
        }
        if (incidentalDirectory(path)) return;
#if JUCE_WINDOWS
        const auto attrs = GetFileAttributesW(path.getFullPathName().toWideCharPointer());
        if (attrs == INVALID_FILE_ATTRIBUTES
            && (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND)) return;
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            result.warnings.add("Cannot inspect linked or inaccessible directory: " + path.getFullPathName());
            return;
        }
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) return;
        // Test list access explicitly: an empty result must not conceal ACCESS_DENIED.
        WIN32_FIND_DATAW info{};
        const auto query = path.getChildFile("*").getFullPathName();
        const auto find = FindFirstFileW(query.toWideCharPointer(), &info);
        if (find == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_NOT_FOUND) {
            result.warnings.add("Cannot list Receiver directory: " + path.getFullPathName());
            return;
        }
        if (find != INVALID_HANDLE_VALUE) FindClose(find);
#endif
        if (!path.isDirectory()) return;
        for (const auto& child : path.findChildFiles(juce::File::findFilesAndDirectories, false))
            if (child.isDirectory() || child.getFileName().equalsIgnoreCase("DirectPipe Receiver.dll"))
                visit(child, depth + 1);
    };
    for (const auto& directory : directories) visit(directory, 0);
    if (visitedCount > 20000) result.warnings.add("Receiver search limit reached; inspect a narrower custom folder.");
    if (depthLimited) result.warnings.add("Receiver search depth reached; inspect a narrower custom folder.");
    return result;
}

ReceiverDiscoveryResult discoverInstalledReceivers(const juce::File& customFolder)
{
    auto directories = receiverSearchDirectories();
    if (customFolder != juce::File()) directories.push_back(customFolder);
    return discoverReceiversInDirectories(directories);
}

ReceiverUseResult queryReceiverUse(const std::vector<ReceiverInstallTarget>& targets)
{
    // This is a point-in-time UI preflight. Handles close before return; the
    // companion transaction validates and locks target/staged files separately.
    ReceiverUseResult result;
#if JUCE_WINDOWS
    std::vector<std::wstring> paths;
    for (const auto& target : targets) {
        ReceiverInstallTarget verified;
        if (!inspectReceiverTarget(juce::File(target.installPath), verified, result.error)
            || verified.binaryPath != target.binaryPath || verified.installedVersion != target.installedVersion) {
            if (result.error.isEmpty()) result.error = "Receiver changed since it was inspected.";
            return result;
        }
        paths.emplace_back(target.binaryPath.toWideCharPointer());
    }
    if (paths.empty()) { result.inspectable = true; return result; }
    DWORD session = 0;
    wchar_t key[CCH_RM_SESSION_KEY + 1]{};
    DWORD code = RmStartSession(&session, 0, key);
    if (code != ERROR_SUCCESS) { result.error = "Cannot start Windows file-use inspection (" + juce::String(code) + ")."; return result; }
    struct SessionGuard { DWORD handle; ~SessionGuard() { RmEndSession(handle); } } guard{ session };
    std::vector<LPCWSTR> pointers;
    for (const auto& path : paths) pointers.push_back(path.c_str());
    code = RmRegisterResources(session, static_cast<UINT>(pointers.size()), pointers.data(), 0, nullptr, 0, nullptr);
    if (code != ERROR_SUCCESS) { result.error = "Cannot register exact Receiver files (" + juce::String(code) + ")."; return result; }
    std::vector<RM_PROCESS_INFO> processes;
    DWORD reasons = 0;
    for (int retry = 0; retry < 4; ++retry) {
        UINT needed = 0, count = static_cast<UINT>(processes.size());
        code = RmGetList(session, &needed, &count, processes.empty() ? nullptr : processes.data(), &reasons);
        if (code == ERROR_MORE_DATA && needed <= 4096) { processes.resize(needed); continue; }
        if (code != ERROR_SUCCESS) { result.error = "Cannot determine whether Receiver is in use (" + juce::String(code) + ")."; return result; }
        processes.resize(count);
        break;
    }
    if (code != ERROR_SUCCESS) { result.error = "Receiver file-use inspection changed repeatedly. Retry after closing the audio application."; return result; }
    for (const auto& process : processes)
        result.processes.add(juce::String(process.strAppName) + " (PID " + juce::String(process.Process.dwProcessId) + ")");
    result.busy = !processes.empty();
    for (const auto& path : paths) {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) result.busy = true;
            else { result.error = "Receiver file access cannot be verified (" + juce::String(error) + ")."; return result; }
        } else CloseHandle(file);
    }
    result.inspectable = true;
#else
    juce::ignoreUnused(targets);
    result.error = "Windows file-use inspection is unavailable on this platform.";
#endif
    return result;
}

} // namespace directpipe::update_detail
