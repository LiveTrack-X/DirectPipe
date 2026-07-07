// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file PluginPreloadCache.cpp
 * @brief Plugin pre-loading cache implementation
 */

#include "PluginPreloadCache.h"
#include "PluginLoadHelper.h"

#if JUCE_WINDOWS
 #include <windows.h>  // PeekMessage / DispatchMessage for safe message-thread joins
 #include <objbase.h>   // CoInitializeEx / CoUninitialize (VST3 COM requirement)
#endif

namespace directpipe {
namespace {

void waitForJoinSlice(bool onMessageThread, juce::MessageManager* messageManager)
{
#if JUCE_WINDOWS
    if (onMessageThread) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
#endif

#if defined(JUCE_MODAL_LOOPS_PERMITTED) && JUCE_MODAL_LOOPS_PERMITTED
    if (onMessageThread && messageManager) {
        messageManager->runDispatchLoopUntil(10);
        return;
    }
#else
    juce::ignoreUnused(onMessageThread, messageManager);
#endif

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

} // namespace

PluginPreloadCache::~PluginPreloadCache()
{
    cancelAndWait();
}

std::unique_ptr<PluginPreloadCache::CachedSlot> PluginPreloadCache::take(
    int slotIndex, double currentSR, int currentBS)
{
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->cacheMutex);
    auto it = state->cache.find(slotIndex);
    if (it == state->cache.end()) return nullptr;

    auto& cached = it->second;
    // SR/BS mismatch → stale cache
    if (cached->sampleRate != currentSR || cached->blockSize != currentBS) {
        state->cache.erase(it);
        return nullptr;
    }

    auto result = std::move(it->second);
    state->cache.erase(it);
    return result;
}

bool PluginPreloadCache::isCached(int slotIndex, double currentSR, int currentBS)
{
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->cacheMutex);
    auto it = state->cache.find(slotIndex);
    if (it == state->cache.end()) return false;
    return it->second->sampleRate == currentSR && it->second->blockSize == currentBS;
}

// ─── Background Preload Thread ──────────────────────────────────────
// BG 스레드에서 실행 — Message thread 아님
// WARNING: Windows에서 COM STA 초기화 필수 (CoInitializeEx)
// SharedState::slotVersions: 프리로드 시작 시 캡처, 완료 시 재확인 (중간에 invalidate되면 폐기)
// SharedState::preloadGeneration: 전체 프리로드 세션 카운터 (새 요청이 이전 요청을 대체)
// SharedState::cancelPreload: non-blocking 취소 플래그
// ────────────────────────────────────────────────────────────────────
void PluginPreloadCache::preloadAllSlots(
    int exceptSlot, double sr, int bs,
    juce::AudioPluginFormatManager& formatMgr,
    const juce::KnownPluginList& knownPlugins,
    std::function<juce::String(int)> slotFileReader,
    std::function<void()> onComplete)
{
    juce::ignoreUnused(formatMgr);
    auto state = state_;

    // Generation counter: each invocation gets a unique generation.
    // Old threads detect they've been superseded and exit immediately.
    // Do NOT reset cancelPreload here — old threads still need to see it.
    auto myGeneration = state->preloadGeneration.fetch_add(1) + 1;

    // Persistent cache: do NOT clear existing entries.
    // Already-cached slots with matching SR/BS are reused (skip re-creation).
    // Only consumed slots (via take()) need re-preloading.

    // Read all slot files on message thread (file I/O is fast, JSON parsing is fast)
    struct SlotData {
        int index;
        juce::String json;
        uint32_t version;  // slot version at file-read time (stale detection)
    };
    std::vector<SlotData> slotsToLoad;
    // Load ALL slots including active slot (so switching away and back is instant).
    // Put non-active slots first (higher priority — more likely to be switched to).
    for (int i = 0; i < kNumSlots; ++i) {
        if (i == exceptSlot) continue;
        auto json = slotFileReader(i);
        if (json.isNotEmpty())
            slotsToLoad.push_back({i, json, state->slotVersions[static_cast<size_t>(i)].load()});
    }
    // Also load the active slot (lower priority — last in queue)
    if (exceptSlot >= 0 && exceptSlot < kNumSlots) {
        auto json = slotFileReader(exceptSlot);
        if (json.isNotEmpty())
            slotsToLoad.push_back({exceptSlot, json, state->slotVersions[static_cast<size_t>(exceptSlot)].load()});
    }

    if (slotsToLoad.empty()) {
        if (onComplete) juce::MessageManager::callAsync(std::move(onComplete));
        return;
    }

    // Copy known plugin types for background thread (safe snapshot)
    auto knownTypes = knownPlugins.getTypes();

    // Single lock: move old thread out AND set new thread (no window for cancelAndWait to miss).
    std::unique_ptr<std::thread> oldThread;
    {
    std::lock_guard<std::mutex> lock(state->threadMutex);
    oldThread = std::move(state->preloadThread);
    state->preloadThread = std::make_unique<std::thread>(
        [state, myGeneration, slotsToLoad = std::move(slotsToLoad), sr, bs,
         knownTypes = std::move(knownTypes),
         oldThread = oldThread.release(),
         onComplete = std::move(onComplete)]() mutable
    {
    #if JUCE_WINDOWS
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // RAII guard ensures CoUninitialize runs even if plugin loading throws
        struct ComScope { ~ComScope() { CoUninitialize(); } } comGuard;
    #endif

        // Join old preload thread (on background thread, never blocking message thread)
        {
            std::unique_ptr<std::thread> old(oldThread);
            if (old && old->joinable())
                old->join();
        }

        // Reset cancel flag AFTER old thread is joined and only if this is
        // still the current generation (avoids un-cancelling for newer invocations)
        if (state->preloadGeneration.load() == myGeneration)
            state->cancelPreload.store(false);

        juce::AudioPluginFormatManager threadFormatManager;
        threadFormatManager.addDefaultFormats();

        // Collect partially-built slots here so we never destroy plugin
        // instances on this background thread (DLL unload race condition).
        std::vector<std::unique_ptr<CachedSlot>> pendingDestroy;

        for (auto& slotData : slotsToLoad) {
            if (state->cancelPreload.load() || state->preloadGeneration.load() != myGeneration) break;

            // Skip if already cached with matching SR/BS (persistent cache)
            {
                std::lock_guard<std::mutex> lock(state->cacheMutex);
                auto it = state->cache.find(slotData.index);
                if (it != state->cache.end() &&
                    it->second->sampleRate == sr &&
                    it->second->blockSize == bs) {
                    continue;
                }
            }

            auto parsed = juce::JSON::parse(slotData.json);
            if (!parsed.isObject()) continue;

            auto* root = parsed.getDynamicObject();
            if (!root || !root->hasProperty("plugins")) continue;

            auto* pluginsArray = root->getProperty("plugins").getArray();
            if (!pluginsArray) continue;

            auto cachedSlot = std::make_unique<CachedSlot>();
            cachedSlot->sampleRate = sr;
            cachedSlot->blockSize = bs;

            for (auto& pluginVar : *pluginsArray) {
                if (state->cancelPreload.load() || state->preloadGeneration.load() != myGeneration) break;

                auto* pluginObj = pluginVar.getDynamicObject();
                if (!pluginObj) continue;

                CachedEntry entry;
                entry.name = pluginObj->getProperty("name").toString();
                entry.path = pluginObj->getProperty("path").toString();
                entry.bypassed = static_cast<bool>(pluginObj->getProperty("bypassed"));

                auto stateStr = pluginObj->getProperty("state").toString();
                if (stateStr.isNotEmpty()) {
                    entry.stateData.fromBase64Encoding(stateStr);
                    entry.hasState = entry.stateData.getSize() > 0;
                }

                auto descXml = pluginObj->getProperty("descXml").toString();
                if (descXml.isNotEmpty()) {
                    if (auto xml = juce::parseXML(descXml)) {
                        entry.desc.loadFromXml(*xml);
                    }
                }
                if (entry.desc.name.isEmpty()) {
                    for (const auto& desc : knownTypes) {
                        if (desc.fileOrIdentifier == entry.path && desc.name == entry.name) {
                            entry.desc = desc;
                            break;
                        }
                    }
                }
                if (entry.desc.name.isEmpty()) {
                    for (const auto& desc : knownTypes) {
                        if (desc.name == entry.name) {
                            entry.desc = desc;
                            break;
                        }
                    }
                }

                if (entry.desc.name.isEmpty()) continue;

                juce::String error;
                try {
                    entry.instance = createPluginOnCorrectThread(threadFormatManager, entry.desc, sr, static_cast<int>(bs), error, nullptr, &state->cancelPreload);
                } catch (const std::exception& e) {
                    juce::Logger::writeToLog("[VST] Preload crashed: " + entry.name + " - " + juce::String(e.what()));
                    continue;
                } catch (...) {
                    juce::Logger::writeToLog("[VST] Preload crashed: " + entry.name + " (unknown exception)");
                    continue;
                }
                if (!entry.instance) {
                    juce::Logger::writeToLog("[VST] Preload failed: " + entry.name + " - " + error);
                    continue;
                }

                cachedSlot->entries.push_back(std::move(entry));
            }

            if (state->cancelPreload.load() || state->preloadGeneration.load() != myGeneration) {
                // Don't destroy plugin instances on background thread!
                // Move to pendingDestroy → cleaned up on message thread.
                if (!cachedSlot->entries.empty())
                    pendingDestroy.push_back(std::move(cachedSlot));
                break;
            }

            if (!cachedSlot->entries.empty()) {
                // Check slot version: if it changed since we read the file,
                // the chain was modified and our cached data is stale.
                // This prevents the race where a stale preload overwrites
                // a cache entry that was just invalidated by saveSlot.
                if (slotData.index >= 0 && slotData.index < kNumSlots &&
                    state->slotVersions[static_cast<size_t>(slotData.index)].load() != slotData.version) {
                    pendingDestroy.push_back(std::move(cachedSlot));
                } else {
                    std::lock_guard<std::mutex> lock(state->cacheMutex);
                    // Move old entry to pendingDestroy (plugin instances must be
                    // destroyed on message thread, not this background thread)
                    auto it = state->cache.find(slotData.index);
                    if (it != state->cache.end() && it->second && !it->second->entries.empty())
                        pendingDestroy.push_back(std::move(it->second));
                    state->cache[slotData.index] = std::move(cachedSlot);
                }
            }
        }

        if (!state->cancelPreload.load() && state->preloadGeneration.load() == myGeneration) {
            int cachedCount = 0;
            {
                std::lock_guard<std::mutex> lock(state->cacheMutex);
                cachedCount = static_cast<int>(state->cache.size());
            }
            juce::Logger::writeToLog("[VST] Preload complete: " + juce::String(cachedCount) + " slots cached");
        }

        // Move any orphaned plugin instances to message thread for safe destruction.
        // Plugin DLLs (especially VST3 COM) can crash if destroyed on a different
        // thread while another thread is also unloading the same DLL.
        if (!pendingDestroy.empty()) {
            auto shared = std::make_shared<std::vector<std::unique_ptr<CachedSlot>>>(
                std::move(pendingDestroy));
            juce::MessageManager::callAsync([shared]() {
                // instances destroyed here on message thread
            });
        }

        if (onComplete)
            juce::MessageManager::callAsync(std::move(onComplete));
    });
    } // threadMutex
}

void PluginPreloadCache::invalidateSlot(int slotIndex)
{
    auto state = state_;
    if (slotIndex >= 0 && slotIndex < kNumSlots)
        state->slotVersions[static_cast<size_t>(slotIndex)].fetch_add(1);
    std::lock_guard<std::mutex> lock(state->cacheMutex);
    state->cache.erase(slotIndex);
}

void PluginPreloadCache::invalidateAll()
{
    auto state = state_;
    // Bump all slot versions so running preload thread discards stale results
    for (int i = 0; i < kNumSlots; ++i)
        state->slotVersions[static_cast<size_t>(i)].fetch_add(1);
    // Do NOT join the thread here — this method is called on the message thread.
    // Joining can deadlock if the preload thread is in createPluginInstance
    // waiting for COM STA dispatch on the message thread (VST3 plugins).
    // The next preloadAllSlots() will join the old thread on its background thread.
    state->cancelPreload.store(true);
    state->preloadGeneration.fetch_add(1);  // supersede running thread — exits on next check
    // Cache NOT cleared here — stale entries (SR/BS mismatch) are rejected by take()
    // and overwritten by next preloadAllSlots(). Avoids slow synchronous destruction
    // of dozens of plugin instances on the message thread during driver switches.
}

void PluginPreloadCache::clearAll()
{
    auto state = state_;
    state->cancelPreload.store(true);
    state->preloadGeneration.fetch_add(1);  // supersede running thread

    for (int i = 0; i < kNumSlots; ++i)
        state->slotVersions[static_cast<size_t>(i)].fetch_add(1);

    std::map<int, std::unique_ptr<CachedSlot>> oldCache;
    {
        std::lock_guard<std::mutex> lock(state->cacheMutex);
        oldCache.swap(state->cache);
    }
}

void PluginPreloadCache::cancelAndWait()
{
    auto state = state_;
    state->cancelPreload.store(true);
    state->preloadGeneration.fetch_add(1);  // supersede running thread

    // Try to join with a timeout to avoid deadlock.
    // VST3 COM STA plugins may need message thread dispatch during
    // createPluginInstance — if we block the message thread on join(),
    // that dispatch can never happen → deadlock.
    std::unique_lock<std::mutex> lock(state->threadMutex);

    if (state->preloadThread && state->preloadThread->joinable()) {
        // Release lock, join outside (thread may need the shared thread mutex too)
        auto thread = std::move(state->preloadThread);
        lock.unlock();

        // Wait with message pumping on the message thread. Some VST3/COM
        // teardown paths need message dispatch while the background thread is
        // joining; a plain message-thread join can deadlock.
        auto start = std::chrono::steady_clock::now();
        constexpr auto kTimeout = std::chrono::seconds(10);
        bool timeoutLogged = false;
        auto* messageManager = juce::MessageManager::getInstanceWithoutCreating();
        const bool onMessageThread = messageManager != nullptr
            && messageManager->isThisTheMessageThread();

        // Can't poll std::thread directly; use a helper thread to join.
        auto joinDone = std::make_shared<std::atomic<bool>>(false);
        auto heapThread = std::make_shared<std::unique_ptr<std::thread>>(std::move(thread));
        std::thread joiner([heapThread, joinDone] {
            if (*heapThread && (*heapThread)->joinable())
                (*heapThread)->join();
            joinDone->store(true);
        });

        while (!joinDone->load()) {
            if (!timeoutLogged && std::chrono::steady_clock::now() - start > kTimeout) {
                juce::Logger::writeToLog("[VST] WARNING: Preload thread join exceeded timeout; continuing cleanup in background");
                timeoutLogged = true;
                joiner.detach();
                return;
            }

            waitForJoinSlice(onMessageThread, messageManager);
        }

        joiner.join();
    }
}

void PluginPreloadCache::joinPreloadThread()
{
    cancelAndWait();
}

bool PluginPreloadCache::isCachedWithStructure(
    int slotIndex, double currentSR, int currentBS,
    const std::vector<std::pair<juce::String, juce::String>>& chainStructure)
{
    auto state = state_;
    std::lock_guard<std::mutex> lock(state->cacheMutex);
    auto it = state->cache.find(slotIndex);
    if (it == state->cache.end()) return true;  // not cached → no mismatch
    if (it->second->sampleRate != currentSR || it->second->blockSize != currentBS)
        return true;  // SR/BS mismatch → will be rejected by take() anyway
    auto& entries = it->second->entries;
    if (entries.size() != chainStructure.size()) return false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name != chainStructure[i].first ||
            entries[i].path != chainStructure[i].second)
            return false;
    }
    return true;
}

} // namespace directpipe
