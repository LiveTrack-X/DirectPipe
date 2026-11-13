// SPDX-License-Identifier: GPL-3.0-or-later
#include "DpwimInstaller.h"
#include <set>
#include <exception>
#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace directpipe::dpwim {
namespace {
constexpr int64_t maxArchive = 32 * 1024 * 1024;
constexpr int64_t maxExpanded = 96 * 1024 * 1024;
const juce::String bundleName = "DirectPipe Windows Input Mixer.vst3";
const juce::String binaryRelative = "Contents/x86_64-win/DirectPipe Windows Input Mixer.vst3";
juce::Result fail(const juce::String& text) { return juce::Result::fail(text); }
bool validHash(const juce::String& hash)
{
    return hash.length() == 64 && hash.containsOnly("0123456789abcdef");
}
bool reparseAncestor(juce::File file)
{
   #if JUCE_WINDOWS
    while (file != juce::File()) {
        const auto attrs = GetFileAttributesW(file.getFullPathName().toWideCharPointer());
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            const auto error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return true;
        } else if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0) return true;
        const auto parent = file.getParentDirectory();
        if (parent == file) break;
        file = parent;
    }
   #else
    while (file != juce::File()) {
        if (file.isSymbolicLink()) return true;
        const auto parent = file.getParentDirectory();
        if (parent == file) break;
        file = parent;
    }
   #endif
    return false;
}
bool safeEntry(const juce::String& name)
{
    if (name.isEmpty() || name.startsWithChar('/') || name.containsAnyOf("\\:*?\"<>|")) return false;
    const auto parts = juce::StringArray::fromTokens(name, "/", "");
    for (int i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        if (part.isEmpty() && i == parts.size() - 1) continue;
        if (part.isEmpty() || part == "." || part == ".." || part.trimEnd() != part || part.endsWithChar('.')) return false;
        const auto stem = part.upToFirstOccurrenceOf(".", false, false).toUpperCase();
        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL"
            || (stem.length() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT"))
                && stem[3] >= '0' && stem[3] <= '9')) return false;
    }
    return name.startsWith(bundleName + "/") || name == "LICENSE" || name == "README.md"
        || name == "THIRD_PARTY_NOTICES.md";
}
// JUCE's VST3 helper emits trailing commas. Remove only commas outside strings
// immediately before a closing object/array, without changing string values.
juce::String moduleJson(const juce::String& input)
{
    juce::String result;
    bool quoted = false, escaped = false;
    for (int i = 0; i < input.length(); ++i) {
        const auto c = input[i];
        if (!quoted && c == ',') {
            int next = i + 1;
            while (next < input.length() && juce::CharacterFunctions::isWhitespace(input[next])) ++next;
            if (next < input.length() && (input[next] == '}' || input[next] == ']')) continue;
        }
        result += juce::String::charToString(c);
        if (quoted && c == '\\' && !escaped) { escaped = true; continue; }
        if (c == '"' && !escaped) quoted = !quoted;
        escaped = false;
    }
    return result;
}
Outcome errorOutcome(const juce::String& text) { Outcome out; out.message = text; return out; }
struct WorkTree {
    juce::File root, directory;
    ~WorkTree() {
        if (directory.isAChildOf(root) && directory.getFileName().startsWith(".dpwim-")
            && !reparseAncestor(directory)) directory.deleteRecursively();
    }
};
} // namespace

bool validVersion(const juce::String& version)
{
    const auto parts = juce::StringArray::fromTokens(version, ".", "");
    if (parts.size() != 3) return false;
    for (const auto& part : parts)
        if (part.isEmpty() || !part.containsOnly("0123456789") || part.length() > 5
            || part.getIntValue() > 65535 || juce::String(part.getIntValue()) != part) return false;
    return true;
}
int compareVersions(const juce::String& a, const juce::String& b)
{
    const auto aa = juce::StringArray::fromTokens(a, ".", "");
    const auto bb = juce::StringArray::fromTokens(b, ".", "");
    for (int i = 0; i < 3; ++i) {
        if (aa[i].getIntValue() < bb[i].getIntValue()) return -1;
        if (aa[i].getIntValue() > bb[i].getIntValue()) return 1;
    }
    return 0;
}
juce::Result parseRelease(const juce::var& json, Release& release)
{
    release = {};
    if (!json.isObject() || !json["draft"].isBool() || !json["prerelease"].isBool()
        || static_cast<bool>(json["draft"]) || static_cast<bool>(json["prerelease"])) return fail("Invalid stable DPWIM release.");
    const auto tag = json["tag_name"].toString();
    if (!tag.startsWithChar('v') || !validVersion(tag.substring(1))) return fail("Invalid DPWIM version.");
    release.version = tag.substring(1);
    release.assetName = "DPWIM-" + tag + "-Windows-x64-VST3.zip";
    const auto* assets = json["assets"].getArray();
    if (!assets) return fail("DPWIM release has no assets.");
    int matches = 0;
    for (const auto& asset : *assets) {
        if (asset["name"].toString() != release.assetName) continue;
        ++matches;
        const auto size = static_cast<juce::int64>(asset["size"]);
        release.url = asset["browser_download_url"].toString();
        const auto digest = asset["digest"].toString();
        release.sha256 = digest.startsWith("sha256:") ? digest.substring(7).toLowerCase() : juce::String{};
        if (size <= 0 || size > maxArchive || !validHash(release.sha256)
            || release.url != "https://github.com/LiveTrack-X/DPWIM/releases/download/" + tag + "/" + release.assetName)
            return fail("DPWIM asset identity/checksum is missing or invalid.");
    }
    return matches == 1 ? juce::Result::ok() : fail("Expected one Windows x64 DPWIM VST3 asset.");
}
juce::Result inspectBundle(const juce::File& bundle, const juce::String& version)
{
    const auto binary = bundle.getChildFile(binaryRelative);
    const auto metadata = bundle.getChildFile("Contents/Resources/moduleinfo.json");
    if (reparseAncestor(bundle) || !binary.existsAsFile() || binary.getSize() > maxExpanded
        || !metadata.existsAsFile() || metadata.getSize() > 1024 * 1024)
        return fail("Incomplete DPWIM VST3 bundle.");
    const auto json = juce::JSON::parse(moduleJson(metadata.loadFileAsString()));
    if (json["Name"].toString() != "DirectPipe Windows Input Mixer" || json["Version"].toString() != version)
        return fail("DPWIM bundle metadata/version mismatch.");
    juce::FileInputStream stream(binary);
    if (!stream.openedOk() || stream.getTotalLength() < 64 || stream.readShort() != 0x5a4d) return fail("Invalid DPWIM binary.");
    stream.setPosition(60);
    const auto offset = static_cast<int64_t>(static_cast<uint32_t>(stream.readInt()));
    if (offset > stream.getTotalLength() - 24) return fail("Invalid DPWIM PE header.");
    stream.setPosition(offset);
    if (stream.readInt() != 0x00004550 || static_cast<uint16_t>(stream.readShort()) != 0x8664)
        return fail("DPWIM requires a Windows x64 binary.");
   #if JUCE_WINDOWS
    DWORD unused = 0;
    const auto length = GetFileVersionInfoSizeW(binary.getFullPathName().toWideCharPointer(), &unused);
    if (length == 0 || length > 1024 * 1024) return fail("DPWIM binary has no version resource.");
    juce::MemoryBlock info(length);
    if (!GetFileVersionInfoW(binary.getFullPathName().toWideCharPointer(), 0, length, info.getData())) return fail("Cannot read DPWIM version.");
    VS_FIXEDFILEINFO* fixed = nullptr; UINT size = 0;
    if (!VerQueryValueW(info.getData(), L"\\", reinterpret_cast<void**>(&fixed), &size)
        || size < sizeof(VS_FIXEDFILEINFO) || fixed == nullptr) return fail("Invalid DPWIM version resource.");
    const auto expected = juce::StringArray::fromTokens(version, ".", "");
    if (fixed->dwSignature != 0xfeef04bd || HIWORD(fixed->dwFileVersionMS) != expected[0].getIntValue()
        || LOWORD(fixed->dwFileVersionMS) != expected[1].getIntValue()
        || HIWORD(fixed->dwFileVersionLS) != expected[2].getIntValue() || LOWORD(fixed->dwFileVersionLS) != 0
        || ((fixed->dwProductVersionMS != 0 || fixed->dwProductVersionLS != 0)
            && (fixed->dwProductVersionMS != fixed->dwFileVersionMS || fixed->dwProductVersionLS != fixed->dwFileVersionLS)))
        return fail("DPWIM binary version does not match the release.");
    // JUCE 7 leaves numeric PRODUCTVERSION zero but supplies ProductVersion
    // and ProductName strings. Require those as well as the numeric FILEVERSION.
    struct Translation { WORD language, codePage; };
    Translation* translations = nullptr; UINT translationBytes = 0;
    if (!VerQueryValueW(info.getData(), L"\\VarFileInfo\\Translation",
            reinterpret_cast<void**>(&translations), &translationBytes) || translations == nullptr)
        return fail("DPWIM product identity is missing.");
    bool productMatches = false;
    for (unsigned i = 0; i < translationBytes / sizeof(Translation) && i < 16; ++i) {
        const auto query = [&](const juce::String& field) {
            const auto path = juce::String::formatted("\\StringFileInfo\\%04x%04x\\",
                static_cast<unsigned>(translations[i].language), static_cast<unsigned>(translations[i].codePage)) + field;
            wchar_t* text = nullptr; UINT characters = 0;
            if (!VerQueryValueW(info.getData(), path.toWideCharPointer(), reinterpret_cast<void**>(&text), &characters)
                || text == nullptr || characters == 0) return juce::String();
            return juce::String(text, static_cast<int>(characters)).trim();
        };
        const auto productVersion = query("ProductVersion");
        if ((productVersion == version || productVersion == version + ".0")
            && query("ProductName") == "DirectPipe Windows Input Mixer") productMatches = true;
    }
    if (!productMatches) return fail("DPWIM product name/version does not match the release.");
   #endif
    return juce::Result::ok();
}
juce::File managedRoot()
{
   #if JUCE_WINDOWS
    const auto local = juce::SystemStats::getEnvironmentVariable("LOCALAPPDATA", {});
    if (local.isNotEmpty()) return juce::File(local).getChildFile("Programs/Common/VST3/DirectPipe-DPWIM");
   #endif
    return {};
}
Outcome installedCopy(const juce::File& root, const BundleInspector& inspector)
{
    if (root == juce::File() || reparseAncestor(root)) return errorOutcome("No managed DPWIM installation.");
    const auto manifest = root.getChildFile("installed.json");
    if (manifest.getSize() > 16384) return errorOutcome("Invalid DPWIM installation record.");
    const auto json = juce::JSON::parse(manifest);
    const auto version = json["version"].toString(), directory = json["directory"].toString();
    if (!validVersion(version) || !directory.startsWith("v" + version + "-")
        || !directory.containsOnly("v0123456789abcdef.-") || directory.length() > 100)
        return errorOutcome("DPWIM is not installed by DirectPipe.");
    const auto bundle = root.getChildFile(directory).getChildFile(bundleName);
    const auto binary = bundle.getChildFile(binaryRelative);
    const auto hash = json["binarySha256"].toString();
    if (reparseAncestor(bundle) || !binary.existsAsFile() || !validHash(hash)
        || inspector(bundle, version).failed() || juce::SHA256(binary).toHexString() != hash)
        return errorOutcome("Managed DPWIM files need repair.");
    return {true, true, "DPWIM v" + version + " is installed.", version, bundle};
}
Outcome installArchive(const juce::File& root, const Release& release, const juce::MemoryBlock& archive,
                       const std::atomic<bool>& cancel, const BundleInspector& inspector)
{
    if (root == juce::File() || reparseAncestor(root) || !validVersion(release.version)
        || !validHash(release.sha256) || archive.getSize() == 0 || archive.getSize() > maxArchive
        || juce::SHA256(archive).toHexString() != release.sha256) return errorOutcome("DPWIM download/checksum validation failed.");
    if (cancel.load()) return errorOutcome("DPWIM installation cancelled.");
    const auto created = root.createDirectory();
    if (created.failed() || reparseAncestor(root)) return errorOutcome("Cannot create the DPWIM installation folder.");
    WorkTree work{root, root.getChildFile(".dpwim-" + juce::Uuid().toString())};
    if (work.directory.createDirectory().failed()) return errorOutcome("Cannot create DPWIM staging folder.");
    juce::MemoryInputStream input(archive, false); juce::ZipFile zip(input);
    if (zip.getNumEntries() < 1 || zip.getNumEntries() > 256) return errorOutcome("Invalid DPWIM archive entry count.");
    int64_t total = 0; std::set<juce::String> names;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* entry = zip.getEntry(i);
        // JUCE normalizes ZIP entry separators to backslashes on Windows.
        const auto name = entry ? entry->filename.replaceCharacter('\\', '/') : juce::String{};
        if (!entry || !safeEntry(name))
            return errorOutcome("Unsafe DPWIM archive entry: " + (entry ? entry->filename : juce::String("missing")));
        if (entry->isSymbolicLink || entry->uncompressedSize < 0
            || entry->uncompressedSize > maxExpanded || (total += entry->uncompressedSize) > maxExpanded
            || !names.insert(name.toLowerCase()).second) return errorOutcome("Unsafe or oversized DPWIM archive.");
    }
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        if (cancel.load()) return errorOutcome("DPWIM installation cancelled.");
        const auto* entry = zip.getEntry(i);
        const auto name = entry->filename.replaceCharacter('\\', '/');
        const auto destination = work.directory.getChildFile(name);
        if (!destination.isAChildOf(work.directory)) return errorOutcome("Invalid DPWIM archive destination.");
        if (name.endsWithChar('/')) {
            if (destination.createDirectory().failed()) return errorOutcome("Cannot create DPWIM bundle folder.");
            continue;
        }
        if (destination.getParentDirectory().createDirectory().failed()) return errorOutcome("Cannot create DPWIM bundle folder.");
        std::unique_ptr<juce::InputStream> content(zip.createStreamForEntry(i));
        juce::FileOutputStream output(destination);
        if (!content || !output.openedOk()) return errorOutcome("Cannot unpack DPWIM.");
        char block[32768]; int64_t count = 0;
        for (;;) {
            if (cancel.load()) return errorOutcome("DPWIM installation cancelled.");
            const auto read = content->read(block, sizeof(block));
            if (read <= 0) break;
            count += read;
            if (count > entry->uncompressedSize || !output.write(block, static_cast<size_t>(read))) return errorOutcome("DPWIM extraction failed.");
        }
        output.flush();
        if (count != entry->uncompressedSize || output.getStatus().failed()) return errorOutcome("DPWIM archive is incomplete.");
    }
    const auto inspected = inspector(work.directory.getChildFile(bundleName), release.version);
    if (inspected.failed()) return errorOutcome(inspected.getErrorMessage());
    if (cancel.load() || reparseAncestor(root)) return errorOutcome("DPWIM installation cancelled.");
    // Downloads can overlap across host instances. Serialize only the final
    // commit and re-read the current version so a slower download cannot downgrade it.
    const auto rootText = root.getFullPathName().toLowerCase();
    juce::InterProcessLock publishLock("DirectPipeDPWIM-" + juce::SHA256(rootText.toUTF8()).toHexString());
    if (!publishLock.enter(0)) return errorOutcome("Cannot acquire the DPWIM installation lock. Retry after the other installation finishes.");
    struct Unlock { juce::InterProcessLock& lock; ~Unlock() { lock.exit(); } } unlock{publishLock};
    const auto current = installedCopy(root, inspector);
    if (current.success && compareVersions(current.version, release.version) >= 0)
        return {true, true, "DPWIM v" + current.version + " is already installed; the older download was not applied.", current.version, current.bundle};
    const auto name = "v" + release.version + "-" + juce::Uuid().toString().removeCharacters("-");
    const auto destination = root.getChildFile(name);
    if (destination.exists()) return errorOutcome("DPWIM destination already exists.");
   #if JUCE_WINDOWS
    if (!MoveFileExW(work.directory.getFullPathName().toWideCharPointer(), destination.getFullPathName().toWideCharPointer(), MOVEFILE_WRITE_THROUGH))
   #else
    if (!work.directory.moveFileTo(destination))
   #endif
        return errorOutcome("Cannot publish the DPWIM installation.");
    const auto bundle = destination.getChildFile(bundleName);
    auto record = std::make_unique<juce::DynamicObject>();
    record->setProperty("version", release.version); record->setProperty("directory", name);
    record->setProperty("binarySha256", juce::SHA256(bundle.getChildFile(binaryRelative)).toHexString());
    record->setProperty("archiveSha256", release.sha256);
    juce::TemporaryFile temporary(root.getChildFile("installed.json"));
    if (!temporary.getFile().replaceWithText(juce::JSON::toString(juce::var(record.release()))) || !temporary.overwriteTargetFileWithTemporary())
        return errorOutcome("DPWIM files are staged, but its installation record could not be saved. Previous installation retained.");
    return {true, false, "DPWIM v" + release.version + " installed. Add this version from the plugin list. Existing instances and saved presets keep their previous plugin path.", release.version, bundle};
}
Outcome installLatest(const juce::File& root, const Fetch& fetch, const std::atomic<bool>& cancel, const BundleInspector& inspector)
{
    juce::MemoryBlock jsonBytes;
    auto result = fetch("https://api.github.com/repos/LiveTrack-X/DPWIM/releases/latest", jsonBytes, 2 * 1024 * 1024, cancel);
    if (result.failed()) return errorOutcome(result.getErrorMessage());
    Release release;
    result = parseRelease(juce::JSON::parse(jsonBytes.toString()), release);
    if (result.failed()) return errorOutcome(result.getErrorMessage());
    const auto installed = installedCopy(root, inspector);
    if (installed.success && compareVersions(installed.version, release.version) >= 0)
        return {true, true, "DPWIM v" + installed.version + " is already up to date.", installed.version, installed.bundle};
    juce::MemoryBlock archive;
    result = fetch(release.url, archive, maxArchive, cancel);
    if (result.failed()) return errorOutcome(result.getErrorMessage());
    return installArchive(root, release, archive, cancel, inspector);
}
juce::Result fetchHttps(const juce::String& url, juce::MemoryBlock& bytes, int64_t limit, const std::atomic<bool>& cancel)
{
    bytes.reset();
    if (!url.startsWith("https://")) return fail("HTTPS is required for DPWIM downloads.");
    int status = 0;
    auto stream = juce::URL(url).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
        .withConnectionTimeoutMs(5000).withNumRedirectsToFollow(5).withStatusCode(&status)
        .withExtraHeaders("User-Agent: DirectPipe-DPWIM-Installer\r\nAccept: */*\r\n"));
    if (!stream || status != 200 || stream->getTotalLength() > limit) return fail("Cannot download DPWIM (HTTP " + juce::String(status) + "). Check your connection or GitHub rate limit.");
    char buffer[32768]; const auto deadline = juce::Time::getMillisecondCounterHiRes() + 30000.0;
    for (;;) {
        if (cancel.load() || juce::Time::getMillisecondCounterHiRes() > deadline) return fail("DPWIM download cancelled or timed out.");
        const auto read = stream->read(buffer, sizeof(buffer));
        if (read <= 0) break;
        if (static_cast<int64_t>(bytes.getSize()) + read > limit) return fail("DPWIM download exceeds the size limit.");
        bytes.append(buffer, static_cast<size_t>(read));
    }
    return bytes.getSize() == 0 ? fail("DPWIM download was empty.") : juce::Result::ok();
}
Installer::~Installer()
{
    stopTimer(); complete_ = {};
    if (state_) state_->cancel.store(true);
    if (worker_.joinable()) {
        if (state_ && state_->done.load(std::memory_order_acquire)) worker_.join();
        else worker_.detach(); // Worker holds only shared state, never this/UI/audio objects.
    }
}
bool Installer::start(const juce::File& root, std::function<void(Outcome)> complete, Fetch fetch)
{
    if (state_ || !complete) return false;
    auto state = std::make_shared<State>();
    try {
        worker_ = std::thread([state, root, fetch = std::move(fetch)] {
            try { state->outcome = installLatest(root, fetch, state->cancel); }
            catch (const std::exception& e) { state->outcome = errorOutcome("DPWIM installation failed: " + juce::String(e.what())); }
            catch (...) { state->outcome = errorOutcome("DPWIM installation failed."); }
            state->done.store(true, std::memory_order_release);
        });
    } catch (...) { return false; }
    state_ = std::move(state); complete_ = std::move(complete); startTimer(100); return true;
}
void Installer::timerCallback()
{
    if (!state_ || !state_->done.load(std::memory_order_acquire)) return;
    stopTimer(); worker_.join(); auto outcome = std::move(state_->outcome);
    state_.reset(); auto complete = std::move(complete_); complete(std::move(outcome));
}
} // namespace directpipe::dpwim
