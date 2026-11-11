// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include "UpdateStartupSupport.h"
#include "UpdateScript.h"

#include <array>
#include <vector>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace directpipe::update_detail {

namespace {

bool validAttempt(const juce::String& value)
{
    return value.length() == 32 && value.containsOnly("0123456789abcdef")
        && !value.containsOnly("0");
}

bool trueField(const juce::var& value, const char* name)
{
    const auto field = value.getProperty(name, false);
    return field.isBool() && static_cast<bool>(field);
}

#if JUCE_WINDOWS
bool hasNoReparseAncestor(juce::File file)
{
    while (file.getFullPathName().isNotEmpty()) {
        const auto attributes = ::GetFileAttributesW(file.getFullPathName().toWideCharPointer());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            return false;
        const auto parent = file.getParentDirectory();
        if (parent == file) break;
        file = parent;
    }
    return true;
}

class LockedHostStream final : public juce::InputStream {
public:
    LockedHostStream(const juce::File& file, bool forDeletion)
    {
        handle_ = ::CreateFileW(file.getFullPathName().toWideCharPointer(),
            GENERIC_READ | (forDeletion ? DELETE : 0), forDeletion ? 0 : FILE_SHARE_READ,
            nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        LARGE_INTEGER length{};
        BY_HANDLE_FILE_INFORMATION information{};
        if (handle_ != INVALID_HANDLE_VALUE && ::GetFileSizeEx(handle_, &length)
            && ::GetFileInformationByHandle(handle_, &information)
            && (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) == 0)
            length_ = length.QuadPart;
        else
            failed_ = true;
    }
    ~LockedHostStream() override
    {
        if (handle_ != INVALID_HANDLE_VALUE) ::CloseHandle(handle_);
    }
    bool openedOk() const { return !failed_ && handle_ != INVALID_HANDLE_VALUE; }
    bool failed() const { return failed_; }
    juce::int64 getTotalLength() override { return length_; }
    juce::int64 getPosition() override { return position_; }
    bool isExhausted() override { return failed_ || position_ >= length_; }
    bool setPosition(juce::int64 position) override
    {
        LARGE_INTEGER requested{};
        requested.QuadPart = position;
        if (position < 0 || !::SetFilePointerEx(handle_, requested, nullptr, FILE_BEGIN))
            return false;
        position_ = position;
        return true;
    }
    int read(void* destination, int bytes) override
    {
        DWORD readBytes = 0;
        if (!openedOk() || bytes <= 0) return 0;
        if (!::ReadFile(handle_, destination, static_cast<DWORD>(bytes), &readBytes, nullptr)) {
            failed_ = true;
            return 0;
        }
        position_ += readBytes;
        return static_cast<int>(readBytes);
    }
    bool markForDeletion()
    {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;
        return ::SetFileInformationByHandle(handle_, FileDispositionInfo,
            &disposition, sizeof(disposition)) != FALSE;
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    juce::int64 length_ = 0, position_ = 0;
    bool failed_ = false;
};
#endif

juce::String fileHash(const juce::File& file, juce::String& error)
{
#if JUCE_WINDOWS
    LockedHostStream input(file, false);
#else
    juce::FileInputStream input(file);
#endif
    if (!input.openedOk()) {
        error = "Cannot read the update executable; check file locks and permissions: " + file.getFileName();
        return {};
    }
    const auto hash = juce::SHA256(input).toHexString();
#if JUCE_WINDOWS
    if (input.failed() || input.getPosition() != input.getTotalLength()) {
        error = "Could not finish verifying the update executable: " + file.getFileName();
        return {};
    }
#endif
    return hash;
}

bool deleteVerifiedExecutable(const juce::File& file, const juce::String& expectedHash,
                              juce::String& error)
{
#if JUCE_WINDOWS
    // Hash and delete the same exclusive handle. A renamed/replaced file cannot
    // be substituted between identity verification and deletion.
    LockedHostStream input(file, true);
    if (!input.openedOk()) {
        error = "Old update executable is locked or not writable: " + file.getFileName();
        return false;
    }
    const auto hash = juce::SHA256(input).toHexString();
    if (input.failed() || input.getPosition() != input.getTotalLength()
        || !hash.equalsIgnoreCase(expectedHash)) {
        error = "Old update executable changed; it was retained: " + file.getFileName();
        return false;
    }
    if (!input.markForDeletion()) {
        error = "Could not remove old update executable; check locks and folder permissions: " + file.getFileName();
        return false;
    }
    return true;
#else
    if (!fileHash(file, error).equalsIgnoreCase(expectedHash) || !file.deleteFile()) {
        if (error.isEmpty()) error = "Old update executable was retained: " + file.getFileName();
        return false;
    }
    return true;
#endif
}

} // namespace

juce::String startupAttemptFromArguments(const juce::StringArray& arguments,
                                        juce::String& error)
{
    error.clear();
    const juce::String prefix("--directpipe-update-attempt=");
    juce::String attempt;
    int matches = 0;
    for (const auto& argument : arguments) {
        if (argument == "--directpipe-update-attempt" || argument.startsWith(prefix)) {
            ++matches;
            attempt = argument.substring(prefix.length());
        }
    }
    if (matches > 1 || (matches == 1 && !validAttempt(attempt))) {
        error = "The update startup attempt is invalid or duplicated.";
        return {};
    }
    return attempt;
}

juce::File startupAckFileForAttempt(const juce::File& receiptFile,
                                    const juce::String& attemptId)
{
    if (!validAttempt(attemptId) || !juce::File::isAbsolutePath(receiptFile.getFullPathName()))
        return {};
    return receiptFile.getSiblingFile(receiptFile.getFileNameWithoutExtension()
        + "." + attemptId + ".startup.json");
}

bool writeHostStartupAcknowledgement(const juce::File& receiptFile,
                                     const juce::String& attemptId,
                                     const juce::File& currentExe,
                                     const juce::String& runningVersion,
                                     unsigned long processId, bool ready,
                                     const juce::String& reason, juce::String& error)
{
    error.clear();
    const auto reject = [&error](const juce::String& message) { error = message; return false; };
    if (!validAttempt(attemptId) || processId == 0 || !currentExe.existsAsFile())
        return reject("Cannot confirm this update startup: attempt or process identity is missing.");
    if (!receiptFile.existsAsFile() || receiptFile.getSize() > 1024 * 1024)
        return reject("The update startup receipt is missing or invalid.");
    const auto receipt = juce::JSON::parse(receiptFile);
    if (!receipt.isObject() || !trueField(receipt, "requireStartupAck")
        || !trueField(receipt, "filesCommitted") || !trueField(receipt, "inProgress")
        || trueField(receipt, "startupReady"))
        return reject("The update is not awaiting this application's startup confirmation.");
    if (receipt["startupAttemptId"].toString() != attemptId)
        return reject("The update startup attempt does not match the pending receipt.");
    const auto requestedExe = receipt["currentExePath"].toString();
    if (!juce::File::isAbsolutePath(requestedExe) || juce::File(requestedExe) != currentExe)
        return reject("The update startup receipt belongs to a different executable.");
    std::array<int, 3> expected{}, running{};
    juce::String expectedVersion, actualVersion;
    if (!parseStrictReleaseVersion(receipt["expectedVersion"].toString(), expected, expectedVersion)
        || !parseStrictReleaseVersion(runningVersion, running, actualVersion) || expected != running)
        return reject("The running application version does not match the update startup attempt.");
    const auto acknowledgement = startupAckFileForAttempt(receiptFile, attemptId);
    const auto requestedAck = receipt["startupAckFilePath"].toString();
    if (acknowledgement == juce::File{} || !juce::File::isAbsolutePath(requestedAck)
        || juce::File(requestedAck) != acknowledgement)
        return reject("The update startup acknowledgement destination is not the private attempt path.");
    if (acknowledgement.exists())
        return reject("This update startup attempt already has an acknowledgement; its evidence was retained.");
    auto data = std::make_unique<juce::DynamicObject>();
    data->setProperty("attemptId", attemptId);
    data->setProperty("currentExePath", currentExe.getFullPathName());
    data->setProperty("version", actualVersion);
    data->setProperty("pid", static_cast<juce::int64>(processId));
    data->setProperty("ready", ready);
    data->setProperty("reason", reason.replaceCharacters("\r\n", "  ").substring(0, 480));
    juce::TemporaryFile temporary(acknowledgement);
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(data.release()), true))
        || !temporary.overwriteTargetFileWithTemporary())
        return reject("The application started, but its update confirmation could not be saved. Check folder permissions and file locks.");
    return true;
}

bool inspectHostExecutableForCleanup(const juce::File& file,
                                     HostExecutableIdentity& identity,
                                     juce::String& error)
{
#if JUCE_WINDOWS
    error.clear();
    identity = {};
    LockedHostStream input(file, false);
    if (!input.openedOk()) {
        error = "Host executable is locked or unreadable: " + file.getFileName();
        return false;
    }
    DWORD ignored = 0;
    const auto path = file.getFullPathName();
    const DWORD length = ::GetFileVersionInfoSizeW(path.toWideCharPointer(), &ignored);
    if (length == 0 || length > 1024 * 1024) {
        error = "Host executable version metadata is missing: " + file.getFileName();
        return false;
    }
    std::vector<unsigned char> data(length);
    if (!::GetFileVersionInfoW(path.toWideCharPointer(), 0, length, data.data())) {
        error = "Host executable version metadata cannot be read: " + file.getFileName();
        return false;
    }
    struct Translation { WORD language, codePage; };
    Translation* translations = nullptr;
    UINT bytes = 0;
    if (!::VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
            reinterpret_cast<void**>(&translations), &bytes)) {
        error = "Host executable product identity is missing: " + file.getFileName();
        return false;
    }
    for (UINT index = 0; index < bytes / sizeof(Translation); ++index) {
        const auto field = [&](const wchar_t* name) -> juce::String {
            wchar_t query[128]{};
            swprintf_s(query, L"\\StringFileInfo\\%04x%04x\\%s",
                translations[index].language, translations[index].codePage, name);
            wchar_t* value = nullptr;
            UINT characters = 0;
            return ::VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &characters)
                && value != nullptr && characters > 0 ? juce::String(value) : juce::String{};
        };
        if (field(L"ProductName") != "DirectPipe") continue;
        const auto version = field(L"FileVersion");
        std::array<int, 3> parts{};
        juce::String canonical;
        if (version != field(L"ProductVersion")
            || !parseStrictReleaseVersion(version, parts, canonical) || version != canonical)
            continue;
        identity.productName = "DirectPipe";
        identity.fileVersion = identity.productVersion = canonical;
        identity.sha256 = juce::SHA256(input).toHexString();
        if (!input.failed() && input.getPosition() == input.getTotalLength()) return true;
        break;
    }
    error = "Host executable product/version identity is not recognized: " + file.getFileName();
#else
    juce::ignoreUnused(file, identity);
    error = "Automatic legacy executable cleanup requires Windows identity inspection.";
#endif
    return false;
}

LegacyExecutableCleanupResult cleanupLegacyUpdateExecutables(
    const juce::File& currentExe, const juce::String& runningVersion,
    const juce::String& updatedFlagVersion, const juce::File& receiptFile,
    const juce::String& attemptId, bool applicationReady,
    const HostExecutableInspector& inspector)
{
    LegacyExecutableCleanupResult result;
    if (updatedFlagVersion.isEmpty()) return result;
    result.attempted = true;
    const std::array<juce::File, 2> candidates {
        currentExe.getSiblingFile("DirectPipe_backup.exe"),
        currentExe.getSiblingFile("DirectPipe_update.exe")
    };
    for (const auto& file : candidates)
        if (file.existsAsFile()) result.retainedFiles.add(file.getFullPathName());
    if (receiptFile.exists() || attemptId.isNotEmpty()) {
        result.warning = "Old update files were retained because an update receipt or startup attempt still requires verification.";
        return result;
    }
    if (!applicationReady) {
        result.warning = "Old update files were retained because the initial application restore did not complete.";
        return result;
    }
    std::array<int, 3> running{}, flag{};
    juce::String canonical, flagCanonical;
    if (!parseStrictReleaseVersion(runningVersion, running, canonical)
        || !parseStrictReleaseVersion(updatedFlagVersion, flag, flagCanonical) || running != flag) {
        result.warning = "Old update files were retained because the update flag does not match the running version.";
        return result;
    }
    HostExecutableIdentity currentIdentity;
    juce::String error;
#if JUCE_WINDOWS
    if (!hasNoReparseAncestor(currentExe)) {
        result.warning = "Old update executables were retained because the installation uses an unverified redirected path.";
        return result;
    }
#endif
    if (!inspector(currentExe, currentIdentity, error)
        || currentIdentity.productName != "DirectPipe"
        || currentIdentity.fileVersion != canonical || currentIdentity.productVersion != canonical
        || currentIdentity.sha256.length() != 64
        || !fileHash(currentExe, error).equalsIgnoreCase(currentIdentity.sha256)) {
        result.warning = "Old update files were retained because the running executable identity could not be verified. " + error;
        return result;
    }
    std::array<juce::String, 2> hashes;
    for (size_t index = 0; index < candidates.size(); ++index) {
        const auto& file = candidates[index];
        if (!file.existsAsFile()) continue;
#if JUCE_WINDOWS
        if (!hasNoReparseAncestor(file)) {
            result.warning = "Old update executable uses an unverified redirected path; files were retained: " + file.getFileName();
            return result;
        }
#endif
        HostExecutableIdentity identity;
        std::array<int, 3> version{};
        juce::String candidateCanonical;
        if (!inspector(file, identity, error) || identity.productName != "DirectPipe"
            || identity.fileVersion != identity.productVersion
            || !parseStrictReleaseVersion(identity.fileVersion, version, candidateCanonical)
            || identity.fileVersion != candidateCanonical
            || (index == 0 ? !(version < running) : running < version)
            || identity.sha256.length() != 64
            || !fileHash(file, error).equalsIgnoreCase(identity.sha256)) {
            result.warning = "Old update executable identity/version could not be verified; files were retained: "
                + file.getFileName() + ". " + error;
            return result;
        }
#if JUCE_WINDOWS
        LockedHostStream deleteAccess(file, true);
        if (!deleteAccess.openedOk()) {
            result.warning = "Old update executable is locked or not writable; files were retained: " + file.getFileName();
            return result;
        }
#endif
        hashes[index] = identity.sha256;
    }
    // Remove the redundant staged payload first and preserve the most useful
    // previous-host backup until every earlier cleanup step has succeeded.
    for (const size_t index : { size_t{1}, size_t{0} }) {
        if (hashes[index].isEmpty()) continue;
        const auto& file = candidates[index];
        if (deleteVerifiedExecutable(file, hashes[index], error)) {
            result.removedFiles.add(file.getFullPathName());
            result.retainedFiles.removeString(file.getFullPathName());
        } else {
            if (result.warning.isNotEmpty()) result.warning += "; ";
            result.warning += error;
            break;
        }
    }
    result.complete = result.retainedFiles.isEmpty();
    return result;
}

} // namespace directpipe::update_detail
