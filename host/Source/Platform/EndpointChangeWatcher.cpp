// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack

/**
 * @file EndpointChangeWatcher.cpp
 * @brief Windows Core Audio endpoint watcher with no-op fallback elsewhere.
 */

#include "EndpointChangeWatcher.h"
#include "../Control/Log.h"

#include <array>
#include <atomic>
#include <cwchar>
#include <utility>

#if JUCE_WINDOWS
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propidl.h>

#include <cstdint>
#endif

namespace directpipe {
namespace {

#if JUCE_WINDOWS
template <typename T>
void releaseCom(T*& ptr) noexcept
{
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

juce::String readPropertyString(IPropertyStore* store, REFPROPERTYKEY key)
{
    if (!store)
        return {};

    PROPVARIANT value;
    PropVariantInit(&value);
    juce::String result;

    if (SUCCEEDED(store->GetValue(key, &value))) {
        if (value.vt == VT_LPWSTR && value.pwszVal)
            result = juce::String(value.pwszVal);
        else if (value.vt == VT_BSTR && value.bstrVal)
            result = juce::String(value.bstrVal);
    }

    PropVariantClear(&value);
    return result;
}

juce::String getEndpointId(IMMDevice* device)
{
    if (!device)
        return {};

    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId)) || !rawId)
        return {};

    const juce::String result(rawId);
    CoTaskMemFree(rawId);
    return result;
}

bool isCaptureEndpoint(IMMDevice* device)
{
    IMMEndpoint* endpoint = nullptr;
    if (!device
        || FAILED(device->QueryInterface(__uuidof(IMMEndpoint),
                                         reinterpret_cast<void**>(&endpoint)))
        || !endpoint) {
        return false;
    }

    EDataFlow flow = eAll;
    const auto result = endpoint->GetDataFlow(&flow);
    endpoint->Release();
    return SUCCEEDED(result) && flow == eCapture;
}

juce::String getEndpointFriendlyName(IMMDevice* device)
{
    IPropertyStore* properties = nullptr;
    if (!device
        || FAILED(device->OpenPropertyStore(STGM_READ, &properties))
        || !properties) {
        return {};
    }

    const auto result = readPropertyString(properties, PKEY_Device_FriendlyName);
    properties->Release();
    return result;
}

enum class PendingEndpointEvent : std::uint32_t {
    deviceAdded = 1u << 0,
    deviceRemoved = 1u << 1,
    deviceStateChanged = 1u << 2,
    propertyChanged = 1u << 3,
};

constexpr std::uint32_t eventBit(PendingEndpointEvent event) noexcept
{
    return static_cast<std::uint32_t>(event);
}

/** Publishes one fixed-size endpoint ID through a lock-free sequence counter. */
class LockFreeEndpointId {
public:
    static constexpr std::size_t maxCharacters = 512;

    bool store(const juce::String& endpointId)
    {
        const auto* rawId = endpointId.toWideCharPointer();
        const auto length = std::wcslen(rawId);
        if (length >= maxCharacters)
            return false;

        version_.fetch_add(1, std::memory_order_seq_cst); // odd: writer active
        for (std::size_t i = 0; i < length; ++i)
            value_[i].store(rawId[i], std::memory_order_seq_cst);
        value_[length].store(L'\0', std::memory_order_seq_cst);
        length_.store(length, std::memory_order_seq_cst);
        version_.fetch_add(1, std::memory_order_seq_cst); // even: published
        return true;
    }

    bool matches(LPCWSTR endpointId) noexcept
    {
        if (!endpointId)
            return false;

        for (unsigned attempt = 0; attempt < 3; ++attempt) {
            const auto versionBefore = version_.load(std::memory_order_seq_cst);
            if ((versionBefore & 1u) != 0)
                continue;

            const auto length = length_.load(std::memory_order_seq_cst);
            bool equal = length != 0;
            for (std::size_t i = 0; equal && i < length; ++i) {
                if (endpointId[i] == L'\0'
                    || endpointId[i] != value_[i].load(std::memory_order_seq_cst)) {
                    equal = false;
                }
            }
            if (equal)
                equal = endpointId[length] == L'\0';

            const auto versionAfter = version_.load(std::memory_order_seq_cst);
            if (versionBefore == versionAfter)
                return equal;
        }

        return false;
    }

private:
    static_assert(std::atomic<wchar_t>::is_always_lock_free,
                  "endpoint callbacks require lock-free endpoint characters");
    static_assert(std::atomic<std::size_t>::is_always_lock_free,
                  "endpoint callbacks require a lock-free endpoint length");
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "endpoint callbacks require a lock-free sequence counter");

    std::array<std::atomic<wchar_t>, maxCharacters> value_{};
    std::atomic<std::size_t> length_{0};
    std::atomic<std::uint64_t> version_{0};
};

struct EndpointCallbackState {
    bool signal(PendingEndpointEvent event, LPCWSTR endpointId, DWORD state = 0) noexcept
    {
        if (!accepting.load(std::memory_order_acquire)
            || !selectedEndpointId.matches(endpointId)) {
            return false;
        }

        if (event == PendingEndpointEvent::deviceStateChanged)
            latestDeviceState.store(state, std::memory_order_relaxed);

        pendingEvents.fetch_or(eventBit(event), std::memory_order_release);
        return true;
    }

    LockFreeEndpointId selectedEndpointId;
    std::atomic<bool> accepting{false};
    std::atomic<std::uint32_t> pendingEvents{0};
    std::atomic<DWORD> latestDeviceState{0};
};

static_assert(std::atomic<bool>::is_always_lock_free,
              "endpoint callbacks require lock-free state flags");
static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
              "endpoint callbacks require lock-free event flags");
#endif

} // namespace

#if JUCE_WINDOWS

struct EndpointChangeWatcher::Impl : private juce::Timer {
    class NotificationClient final : public IMMNotificationClient {
    public:
        explicit NotificationClient(std::shared_ptr<EndpointCallbackState> state)
            : state_(std::move(state))
        {
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++refCount_;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto refs = --refCount_;
            if (refs == 0)
                delete this;
            return refs;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override
        {
            if (!object)
                return E_POINTER;

            if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
                *object = static_cast<IMMNotificationClient*>(this);
                AddRef();
                return S_OK;
            }

            *object = nullptr;
            return E_NOINTERFACE;
        }

        HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow,
                                                         ERole,
                                                         LPCWSTR) override
        {
            // A role change does not mutate the explicitly selected endpoint.
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override
        {
            state_->signal(PendingEndpointEvent::deviceAdded, deviceId);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override
        {
            state_->signal(PendingEndpointEvent::deviceRemoved, deviceId);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override
        {
            state_->signal(PendingEndpointEvent::deviceStateChanged, deviceId, newState);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR deviceId,
                                                         const PROPERTYKEY) override
        {
            state_->signal(PendingEndpointEvent::propertyChanged, deviceId);
            return S_OK;
        }

    private:
        std::atomic<ULONG> refCount_{1};
        const std::shared_ptr<EndpointCallbackState> state_;
    };

    ~Impl() override
    {
        stop();
    }

    void setInputDeviceName(const juce::String& deviceName)
    {
        inputDeviceName_ = deviceName;
        if (started_)
            resolveAndPublishSelectedEndpoint();
    }

    void setCallback(Callback callback)
    {
        callback_ = std::move(callback);
    }

    void start()
    {
        if (started_)
            return;

        const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInitialized_ = SUCCEEDED(hr);
        comThreadId_ = GetCurrentThreadId();
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
            Log::warn("AUDIO", "Windows endpoint watcher COM init failed: 0x"
                + juce::String::toHexString(static_cast<int>(static_cast<unsigned long>(hr))));
            return;
        }

        const auto createResult = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                                   nullptr,
                                                   CLSCTX_ALL,
                                                   __uuidof(IMMDeviceEnumerator),
                                                   reinterpret_cast<void**>(&enumerator_));
        if (FAILED(createResult) || !enumerator_) {
            Log::warn("AUDIO", "Windows endpoint watcher enumerator init failed: 0x"
                + juce::String::toHexString(static_cast<int>(static_cast<unsigned long>(createResult))));
            uninitializeComIfNeeded();
            return;
        }

        callbackState_ = std::make_shared<EndpointCallbackState>();
        resolveAndPublishSelectedEndpoint();

        client_ = new NotificationClient(callbackState_);
        const auto registerResult = enumerator_->RegisterEndpointNotificationCallback(client_);
        if (FAILED(registerResult)) {
            Log::warn("AUDIO", "Windows endpoint watcher registration failed: 0x"
                + juce::String::toHexString(static_cast<int>(static_cast<unsigned long>(registerResult))));
            callbackState_->accepting.store(false, std::memory_order_release);
            client_->Release();
            client_ = nullptr;
            callbackState_.reset();
            releaseCom(enumerator_);
            uninitializeComIfNeeded();
            return;
        }

        started_ = true;
        callbackState_->accepting.store(true, std::memory_order_release);
        startTimer(25);
        Log::audit("AUDIO", "Windows endpoint watcher started");
    }

    void stop()
    {
        stopTimer();
        started_ = false;

        auto state = callbackState_;
        if (state)
            state->accepting.store(false, std::memory_order_release);

        auto* enumerator = std::exchange(enumerator_, nullptr);
        auto* client = std::exchange(client_, nullptr);

        if (enumerator && client)
            enumerator->UnregisterEndpointNotificationCallback(client);
        if (client)
            client->Release();
        releaseCom(enumerator);

        callbackState_.reset();
        resolvedEndpointId_.clear();
        uninitializeComIfNeeded();
    }

    int drainPendingEvents()
    {
        const auto state = callbackState_;
        if (!state || !state->accepting.load(std::memory_order_acquire))
            return 0;

        const auto pending = state->pendingEvents.exchange(0, std::memory_order_acq_rel);
        const auto callback = callback_;
        const auto deviceName = inputDeviceName_;
        if (pending == 0 || !callback || deviceName.isEmpty())
            return 0;

        int dispatched = 0;
        const auto dispatch = [&](std::uint32_t bit, const juce::String& reason) {
            if ((pending & bit) == 0)
                return;

            Log::audit("AUDIO", "Windows endpoint notification: target='" + deviceName
                + "' reason='" + reason + "'");
            callback(deviceName, reason);
            ++dispatched;
        };

        dispatch(eventBit(PendingEndpointEvent::deviceAdded), "capture endpoint added");
        dispatch(eventBit(PendingEndpointEvent::deviceRemoved), "capture endpoint removed");
        dispatch(eventBit(PendingEndpointEvent::deviceStateChanged),
                 "capture endpoint state changed (state=0x"
                     + juce::String::toHexString(static_cast<int>(
                         state->latestDeviceState.load(std::memory_order_relaxed)))
                     + ")");
        dispatch(eventBit(PendingEndpointEvent::propertyChanged),
                 "capture endpoint property changed");
        return dispatched;
    }

    juce::String resolveSelectedEndpointId() const
    {
        if (!enumerator_ || inputDeviceName_.isEmpty())
            return {};

        juce::String defaultCaptureId;
        IMMDevice* defaultCapture = nullptr;
        if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eCapture,
                                                          eMultimedia,
                                                          &defaultCapture))
            && defaultCapture) {
            defaultCaptureId = getEndpointId(defaultCapture);
        }
        releaseCom(defaultCapture);

        IMMDeviceCollection* collection = nullptr;
        if (FAILED(enumerator_->EnumAudioEndpoints(eAll,
                                                  DEVICE_STATE_ACTIVE,
                                                  &collection))
            || !collection) {
            return {};
        }

        UINT count = 0;
        if (FAILED(collection->GetCount(&count))) {
            releaseCom(collection);
            return {};
        }

        juce::StringArray endpointNames;
        juce::StringArray endpointIds;
        for (UINT index = 0; index < count; ++index) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(index, &device)) || !device)
                continue;

            DWORD state = 0;
            const auto active = SUCCEEDED(device->GetState(&state))
                && state == DEVICE_STATE_ACTIVE;
            if (!active || !isCaptureEndpoint(device)) {
                releaseCom(device);
                continue;
            }

            const auto endpointId = getEndpointId(device);
            const auto friendlyName = getEndpointFriendlyName(device);
            releaseCom(device);

            const auto insertionIndex = endpointId == defaultCaptureId ? 0 : -1;
            endpointIds.insert(insertionIndex, endpointId);
            endpointNames.insert(insertionIndex, friendlyName);
        }
        releaseCom(collection);

        // Mirror JUCE's WASAPI scan, including its duplicate-name suffixes.
        endpointNames.appendNumbersToDuplicates(false, false);
        const auto match = endpointNames.indexOf(inputDeviceName_, false);
        return match >= 0 ? endpointIds[match] : juce::String{};
    }

    bool publishResolvedEndpointId()
    {
        if (!callbackState_)
            return false;

        if (resolvedEndpointId_.length()
            >= static_cast<int>(LockFreeEndpointId::maxCharacters)) {
            Log::warn("AUDIO", "Windows endpoint ID is too long to monitor safely");
            resolvedEndpointId_.clear();
        }

        return callbackState_->selectedEndpointId.store(resolvedEndpointId_);
    }

    void resolveAndPublishSelectedEndpoint()
    {
        const auto state = callbackState_;
        const auto resumeNotifications = state
            && state->accepting.exchange(false, std::memory_order_acq_rel);
        if (state)
            state->pendingEvents.store(0, std::memory_order_release);

        resolvedEndpointId_ = resolveSelectedEndpointId();
        const auto published = publishResolvedEndpointId();

        if (state) {
            state->pendingEvents.store(0, std::memory_order_release);
            state->accepting.store(resumeNotifications && published,
                                   std::memory_order_release);
        }

        Log::audit("AUDIO", resolvedEndpointId_.isNotEmpty()
            ? "Windows endpoint watcher selected ID '" + resolvedEndpointId_ + "'"
            : "Windows endpoint watcher found no exact capture endpoint for '"
                + inputDeviceName_ + "'");
    }

    void timerCallback() override
    {
        drainPendingEvents();
    }

    void uninitializeComIfNeeded() noexcept
    {
        if (comInitialized_ && GetCurrentThreadId() == comThreadId_)
            CoUninitialize();
        comInitialized_ = false;
        comThreadId_ = 0;
    }

    juce::String inputDeviceName_;
    juce::String resolvedEndpointId_;
    Callback callback_;
    std::shared_ptr<EndpointCallbackState> callbackState_;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    NotificationClient* client_ = nullptr;
    bool started_ = false;
    bool comInitialized_ = false;
    DWORD comThreadId_ = 0;
};

#else

struct EndpointChangeWatcher::Impl {
    void setInputDeviceName(const juce::String&) {}
    void setCallback(Callback) {}
    void start() {}
    void stop() {}
};

#endif

EndpointChangeWatcher::EndpointChangeWatcher()
    : impl_(std::make_unique<Impl>())
{
}

EndpointChangeWatcher::~EndpointChangeWatcher() = default;

void EndpointChangeWatcher::setInputDeviceName(const juce::String& deviceName)
{
    impl_->setInputDeviceName(deviceName);
}

void EndpointChangeWatcher::setCallback(Callback callback)
{
    impl_->setCallback(std::move(callback));
}

void EndpointChangeWatcher::start()
{
    impl_->start();
}

void EndpointChangeWatcher::stop()
{
    impl_->stop();
}

#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
void EndpointChangeWatcherTestAccess::setResolvedEndpointId(
    EndpointChangeWatcher& watcher,
    const juce::String& endpointId)
{
#if JUCE_WINDOWS
    auto state = std::make_shared<EndpointCallbackState>();
    state->selectedEndpointId.store(endpointId);
    state->accepting.store(true, std::memory_order_release);
    watcher.impl_->callbackState_ = std::move(state);
    watcher.impl_->resolvedEndpointId_ = endpointId;
#else
    juce::ignoreUnused(watcher, endpointId);
#endif
}

bool EndpointChangeWatcherTestAccess::signalEndpointEvent(
    EndpointChangeWatcher& watcher,
    const juce::String& endpointId,
    Event event,
    unsigned long state)
{
#if JUCE_WINDOWS
    if (!watcher.impl_->callbackState_)
        return false;

    PendingEndpointEvent pendingEvent;
    switch (event) {
        case Event::deviceAdded:
            pendingEvent = PendingEndpointEvent::deviceAdded;
            break;
        case Event::deviceRemoved:
            pendingEvent = PendingEndpointEvent::deviceRemoved;
            break;
        case Event::deviceStateChanged:
            pendingEvent = PendingEndpointEvent::deviceStateChanged;
            break;
        case Event::propertyChanged:
            pendingEvent = PendingEndpointEvent::propertyChanged;
            break;
        case Event::defaultDeviceChanged:
            return false;
    }

    const auto* wideId = endpointId.toWideCharPointer();
    return watcher.impl_->callbackState_->signal(pendingEvent,
                                                 wideId,
                                                 static_cast<DWORD>(state));
#else
    juce::ignoreUnused(watcher, endpointId, event, state);
    return false;
#endif
}

int EndpointChangeWatcherTestAccess::drainPendingEvents(EndpointChangeWatcher& watcher)
{
#if JUCE_WINDOWS
    return watcher.impl_->drainPendingEvents();
#else
    juce::ignoreUnused(watcher);
    return 0;
#endif
}
#endif

} // namespace directpipe
