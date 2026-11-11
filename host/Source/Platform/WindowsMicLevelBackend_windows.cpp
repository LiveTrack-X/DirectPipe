// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
#include "WindowsMicLevelKeeper.h"
#include <atomic>
#include <cmath>

#if JUCE_WINDOWS
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#endif

namespace directpipe {
namespace {
using Keeper = WindowsMicLevelKeeper;

#if JUCE_WINDOWS
bool supportedType(const juce::String& type)
{
    return type == "Windows Audio" || type == "Windows Audio (Low Latency Mode)";
}

template<class T> struct ComRef {
    T* value = nullptr;
    ComRef() = default;
    ~ComRef() { reset(); }
    ComRef(const ComRef&) = delete;
    ComRef& operator=(const ComRef&) = delete;
    void reset() noexcept { if (value) { value->Release(); value = nullptr; } }
    T* release() noexcept { auto* result = value; value = nullptr; return result; }
    T** put() { reset(); return &value; }
    T* operator->() const { return value; }
};

juce::String nativeError(const char* operation, HRESULT result)
{
    return juce::String(operation) + " failed (0x"
        + juce::String::toHexString(static_cast<juce::uint32>(result)) + ")";
}

// Only the ID supplied by the opened JUCE capture device is accepted. No name,
// default endpoint, process session, or endpoint enumeration can choose a mic.
bool validateCaptureEndpoint(IMMDevice* device, const juce::String& expected,
                             juce::String& error)
{
    if (device == nullptr || expected.isEmpty()) {
        error = "The opened microphone has no exact Windows endpoint identity.";
        return false;
    }
    DWORD state = 0;
    auto hr = device->GetState(&state);
    if (FAILED(hr)) { error = nativeError("Selected microphone state", hr); return false; }
    if ((state & DEVICE_STATE_ACTIVE) == 0) {
        error = "The selected microphone is no longer active.";
        return false;
    }
    ComRef<IMMEndpoint> endpoint;
    hr = device->QueryInterface(__uuidof(IMMEndpoint), reinterpret_cast<void**>(endpoint.put()));
    EDataFlow flow = eAll;
    if (SUCCEEDED(hr)) hr = endpoint->GetDataFlow(&flow);
    if (FAILED(hr)) { error = nativeError("Selected microphone direction", hr); return false; }
    if (flow != eCapture) {
        error = "The opened endpoint is not a microphone capture endpoint; loopback/output is unsupported.";
        return false;
    }
    LPWSTR raw = nullptr;
    hr = device->GetId(&raw);
    const juce::String actual(raw == nullptr ? L"" : raw);
    if (raw != nullptr) CoTaskMemFree(raw);
    if (FAILED(hr)) { error = nativeError("Selected microphone identity", hr); return false; }
    if (actual != expected) {
        error = "The opened microphone endpoint identity changed; level correction stopped.";
        return false;
    }
    error.clear();
    return true;
}

class VolumeCallback final : public IAudioEndpointVolumeCallback {
public:
    VolumeCallback(GUID context, Keeper::Backend::Callback sink)
        : context_(context), sink_(std::move(sink)) {}
    void deactivate() noexcept { active_.store(false, std::memory_order_release); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IAudioEndpointVolumeCallback)) {
            *out = static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --references_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA notification) override
    {
        if (!notification) return E_POINTER;
        if (active_.load(std::memory_order_acquire)
            && !IsEqualGUID(notification->guidEventContext, context_)) sink_(false, false);
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1};
    std::atomic<bool> active_{true};
    const GUID context_;
    const Keeper::Backend::Callback sink_;
};

class DeviceCallback final : public IMMNotificationClient {
public:
    DeviceCallback(const juce::String& id, Keeper::Backend::Callback sink)
        : id_(id.toWideCharPointer()), sink_(std::move(sink)) {}
    void deactivate() noexcept { active_.store(false, std::memory_order_release); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override
    {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { auto left = --references_; if (!left) delete this; return left; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override
    {
        if (matches(id) && (state & DEVICE_STATE_ACTIVE) == 0) sink_(false, true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR id) override { if (matches(id)) sink_(false, true); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
private:
    bool matches(LPCWSTR id) const noexcept
    {
        return active_.load(std::memory_order_acquire) && id != nullptr && id_ == id;
    }
    std::atomic<ULONG> references_{1};
    std::atomic<bool> active_{true};
    const std::wstring id_;
    const Keeper::Backend::Callback sink_;
};

class NativeMicBackend final : public Keeper::Backend {
public:
    ~NativeMicBackend() override
    {
        detach();
        if (comInitialized_) { jassert(comThread_ == GetCurrentThreadId()); CoUninitialize(); }
    }
    Keeper::Resolution resolve(const Keeper::InputContext& context) override
    {
        if (!supportedType(context.deviceType))
            return {Keeper::State::unsupported, {}, "Mic level hold supports shared Windows Audio only."};
        if (!context.captureActive)
            return {Keeper::State::unavailable, {}, "No active selected capture input."};
        if (context.endpointId.isEmpty())
            return {Keeper::State::unavailable, {}, "The opened input does not expose an exact Windows capture endpoint ID."};
        juce::String error;
        if (!initialize(error)) return {Keeper::State::error, {}, error};
        ComRef<IMMDeviceEnumerator> enumerator;
        auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator.put()));
        ComRef<IMMDevice> endpoint;
        if (SUCCEEDED(hr)) hr = enumerator->GetDevice(context.endpointId.toWideCharPointer(), endpoint.put());
        if (FAILED(hr)) return {Keeper::State::unavailable, {}, nativeError("Opened capture endpoint lookup", hr)};
        if (!validateCaptureEndpoint(endpoint.value, context.endpointId, error))
            return {Keeper::State::unavailable, {}, error};
        ComRef<IAudioEndpointVolume> volume;
        float scalar = 0.0f;
        hr = endpoint->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
            reinterpret_cast<void**>(volume.put()));
        if (SUCCEEDED(hr)) hr = volume->GetMasterVolumeLevelScalar(&scalar);
        if (FAILED(hr)) return {Keeper::State::unsupported, {}, nativeError("Windows capture level control", hr)};
        if (!std::isfinite(scalar) || scalar < 0.0f || scalar > 1.0f)
            return {Keeper::State::unsupported, {}, "The selected microphone reported an invalid Windows level."};
        Keeper::Endpoint selected{context.endpointId, "Selected Windows microphone"};
        ComRef<IPropertyStore> properties;
        if (SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ, properties.put()))) {
            PROPVARIANT value; PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
                selected.name = juce::String(value.pwszVal);
            PropVariantClear(&value);
        }
        return {Keeper::State::ready, selected, {}};
    }
    bool attach(const juce::String& id, Callback callback, juce::String& error) override
    {
        detach();
        if (!initialize(error)) return false;
        if (id.isEmpty()) { error = "No exact opened microphone endpoint was supplied."; return false; }
        auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(enumerator_.put()));
        if (SUCCEEDED(hr)) hr = enumerator_->GetDevice(id.toWideCharPointer(), endpoint_.put());
        if (FAILED(hr)) { error = nativeError("Selected microphone level binding", hr); detach(); return false; }
        if (!validateCaptureEndpoint(endpoint_.value, id, error)) { detach(); return false; }
        hr = endpoint_->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr,
            reinterpret_cast<void**>(volume_.put()));
        if (FAILED(hr)) { error = nativeError("Selected microphone level binding", hr); detach(); return false; }
        boundId_ = id;
        volumeCallback_.value = new VolumeCallback(context_, callback);
        hr = volume_->RegisterControlChangeNotify(volumeCallback_.value);
        if (FAILED(hr)) { error = nativeError("Microphone level notifications", hr); detach(); return false; }
        volumeRegistered_ = true;
        deviceCallback_.value = new DeviceCallback(id, std::move(callback));
        hr = enumerator_->RegisterEndpointNotificationCallback(deviceCallback_.value);
        if (FAILED(hr)) { error = nativeError("Microphone removal notifications", hr); detach(); return false; }
        deviceRegistered_ = true;
        error.clear(); return true;
    }
    bool readScalar(float& value, juce::String& error) override
    {
        if (!validateCaptureEndpoint(endpoint_.value, boundId_, error)) return false;
        const auto hr = volume_.value ? volume_->GetMasterVolumeLevelScalar(&value) : E_POINTER;
        if (FAILED(hr)) { error = nativeError("Reading selected microphone level", hr); return false; }
        error.clear(); return true;
    }
    bool setScalar100(juce::String& error) override
    {
        if (!validateCaptureEndpoint(endpoint_.value, boundId_, error)) return false;
        const auto hr = volume_.value ? volume_->SetMasterVolumeLevelScalar(1.0f, &context_) : E_POINTER;
        if (FAILED(hr)) { error = nativeError("Setting selected microphone level", hr); return false; }
        error.clear(); return true;
    }
    void detach() noexcept override
    {
        if (deviceCallback_.value) deviceCallback_->deactivate();
        if (volumeCallback_.value) volumeCallback_->deactivate();
        const auto deviceResult = deviceRegistered_ && enumerator_.value
            ? enumerator_->UnregisterEndpointNotificationCallback(deviceCallback_.value) : S_OK;
        const auto volumeResult = volumeRegistered_ && volume_.value
            ? volume_->UnregisterControlChangeNotify(volumeCallback_.value) : S_OK;
        // IMMDeviceEnumerator does not AddRef its callback. If unregister fails,
        // deleting our last reference would leave a callable freed pointer.
        // Keep an inert callback AND its registrar until process exit instead.
        // A latched error below prevents repeated registrations/retention.
        if (deviceResult != S_OK && deviceResult != E_NOTFOUND) {
            registrationFault_ = true;
            (void) deviceCallback_.release();
            (void) enumerator_.release();
        }
        if (volumeResult != S_OK) {
            registrationFault_ = true;
            (void) volumeCallback_.release();
            (void) volume_.release();
        }
        if (registrationFault_ && !registrationFaultLogged_) {
            registrationFaultLogged_ = true;
            juce::Logger::writeToLog("[APP] Microphone notification cleanup failed; inert native references retained until process exit. Level hold requires an application restart.");
        }
        deviceRegistered_ = false; volumeRegistered_ = false;
        deviceCallback_.reset(); volumeCallback_.reset(); volume_.reset();
        endpoint_.reset(); enumerator_.reset(); boundId_.clear();
    }
private:
    bool initialize(juce::String& error)
    {
        if (registrationFault_) {
            error = "Microphone notification cleanup failed; level correction is stopped until DirectPipe restarts.";
            return false;
        }
        if (comInitialized_) return true;
        auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(hr)) { error = nativeError("Microphone control COM initialization", hr); return false; }
        comInitialized_ = true; comThread_ = GetCurrentThreadId();
        hr = CoCreateGuid(&context_);
        if (FAILED(hr)) {
            error = nativeError("Microphone event context", hr);
            CoUninitialize(); comInitialized_ = false; return false;
        }
        return true;
    }
    bool comInitialized_ = false, volumeRegistered_ = false, deviceRegistered_ = false;
    bool registrationFault_ = false, registrationFaultLogged_ = false;
    DWORD comThread_ = 0;
    GUID context_{};
    juce::String boundId_;
    ComRef<IMMDeviceEnumerator> enumerator_;
    ComRef<IMMDevice> endpoint_;
    ComRef<IAudioEndpointVolume> volume_;
    ComRef<VolumeCallback> volumeCallback_;
    ComRef<DeviceCallback> deviceCallback_;
};
#endif
} // namespace

std::unique_ptr<WindowsMicLevelKeeper::Backend> createWindowsMicLevelBackend()
{
#if JUCE_WINDOWS
    return std::make_unique<NativeMicBackend>();
#else
    return {};
#endif
}
} // namespace directpipe
