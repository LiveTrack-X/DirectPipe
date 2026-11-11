// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack
#include "WindowsMicLevelKeeper.h"
#include <atomic>
#include <cmath>



namespace directpipe {
std::unique_ptr<WindowsMicLevelKeeper::Backend> createWindowsMicLevelBackend();
namespace {
using Keeper = WindowsMicLevelKeeper;
bool supportedType(const juce::String& type)
{
    return type == "Windows Audio" || type == "Windows Audio (Low Latency Mode)";
}
bool validIds(const juce::StringArray& ids, juce::String& error)
{
    if (ids.size() > 128) { error = "Too many microphone endpoint preferences"; return false; }
    juce::StringArray seen;
    for (const auto& id : ids) {
        if (id.trim().isEmpty() || id.length() > 1024 || id.containsAnyOf("\r\n")) {
            error = "Invalid microphone endpoint ID"; return false;
        }
        for (auto character : id)
            if (character < 32 || character == 127) { error = "Invalid microphone endpoint ID"; return false; }
        if (seen.contains(id)) { error = "Duplicate microphone endpoint preference"; return false; }
        seen.add(id);
    }
    error.clear(); return true;
}
} // namespace

struct WindowsMicLevelKeeper::Impl : private juce::Thread {
    struct Queue {
        std::atomic<juce::uint64> generation{1}, binding{0};
        std::atomic<unsigned> events{0};
        std::atomic<bool> stopped{false};
        juce::WaitableEvent wake;
    };
    Impl(WindowsMicLevelKeeper& ownerIn, std::unique_ptr<Backend> supplied, bool startWorker)
        : juce::Thread("Windows microphone level"), owner(ownerIn), backend(std::move(supplied)),
          testMode(!startWorker && backend != nullptr)
    {
        if (!backend && startWorker) backend = createWindowsMicLevelBackend();
        if (startWorker && backend && !startThread(juce::Thread::Priority::low)) {
            status.state = State::error; status.message = "Microphone level worker could not start.";
        }
        if (!backend) { status.state = State::unsupported; status.message = "Windows microphone level control is unavailable on this platform."; }
    }
    ~Impl() override { shutdown(); }
    void run() override
    {
        while (!threadShouldExit()) {
            service(static_cast<juce::int64>(juce::Time::getMillisecondCounterHiRes()));
            queue->wake.wait(25);
        }
        detach(); backend.reset(); // Release native COM on its owning worker.
    }
    void signal() { queue->wake.signal(); owner.sendChangeMessage(); }
    void nextGeneration() { ++generation; queue->generation.store(generation, std::memory_order_release); }
    void detach()
    {
        queue->binding.fetch_add(1, std::memory_order_acq_rel);
        if (backend && bound.isNotEmpty()) backend->detach();
        bound.clear(); correctionDue = -1; pendingRead = false;
        queue->events.store(0, std::memory_order_release);
    }
    void shutdown()
    {
        if (queue->stopped.exchange(true, std::memory_order_acq_rel)) return;
        signalThreadShouldExit(); queue->wake.signal();
        if (isThreadRunning()) stopThread(-1);
        else { detach(); backend.reset(); }
    }
    void publish(Status next)
    {
        const juce::ScopedLock lock(mutex);
        if (queue->generation.load(std::memory_order_acquire) != seenGeneration || queue->stopped.load()) return;
        const bool changed = status.state != next.state || status.endpointId != next.endpointId
            || status.endpointName != next.endpointName || status.message != next.message
            || status.canEnable != next.canEnable || status.enabled != next.enabled;
        status = std::move(next);
        if (changed) owner.sendChangeMessage();
    }
    void fault(const juce::String& message, bool disconnected, const Preferences& policy)
    {
        detach(); failed = true;
        Status next; next.state = disconnected ? State::unavailable : State::error;
        next.endpointId = resolved.endpoint.id; next.endpointName = resolved.endpoint.name;
        next.enabled = policy.enabledEndpointIds.contains(next.endpointId);
        next.message = message; publish(std::move(next));
    }
    void service(juce::int64 now)
    {
        if (!backend || queue->stopped.load()) return;
        InputContext selected; Preferences policy; bool ready = false; juce::uint64 epoch = 0;
        {
            const juce::ScopedLock lock(mutex);
            selected = context; policy = preferences; ready = startupReady; epoch = generation;
        }
        if (epoch != seenGeneration) {
            detach(); seenGeneration = epoch; failed = false; nextDiscovery = now; resolved = {};
        }
        if (!supportedType(selected.deviceType)) {
            detach(); Status next; next.state = State::unsupported;
            next.message = "Available only with shared Windows Audio; ASIO and exclusive mode are unsupported.";
            publish(std::move(next)); return;
        }
        if (!selected.captureActive || selected.endpointId.isEmpty()) {
            detach(); Status next;
            next.endpointId = selected.endpointId;
            next.endpointName = selected.inputDeviceName;
            next.enabled = next.endpointId.isNotEmpty() && policy.enabledEndpointIds.contains(next.endpointId);
            next.message = "No exact active Windows capture endpoint is available; correction is stopped.";
            publish(std::move(next)); return;
        }
        if (failed) return; // OFF/ON or a real context/restore change retries.
        const auto events = queue->events.exchange(0, std::memory_order_acq_rel);
        if ((events & 2u) != 0) { fault("The selected microphone disconnected; level correction stopped.", true, policy); return; }
        if (now >= nextDiscovery) {
            resolved = backend->resolve(selected); nextDiscovery = now + 1000;
        }
        if (resolved.state == State::ready && resolved.endpoint.id != selected.endpointId) {
            detach(); Status next; next.message = "Capture endpoint identity does not match the selected input.";
            publish(std::move(next)); return;
        }
        Status next;
        next.state = resolved.state; next.endpointId = resolved.endpoint.id; next.endpointName = resolved.endpoint.name;
        next.message = resolved.message;
        next.enabled = next.endpointId.isNotEmpty() && policy.enabledEndpointIds.contains(next.endpointId);
        next.canEnable = ready && resolved.state == State::ready && next.endpointId.isNotEmpty();
        if (resolved.state != State::ready || next.endpointId.isEmpty()) {
            detach(); publish(std::move(next)); return;
        }
        if (!ready || !next.enabled) {
            detach(); next.state = State::ready;
            next.message = ready ? "Off for this Windows microphone (system-wide input level)." : "Waiting for stable application restore; microphone level is unchanged.";
            publish(std::move(next)); return;
        }
        if (bound != next.endpointId) {
            detach(); bound = next.endpointId;
            const auto binding = queue->binding.load(std::memory_order_acquire);
            std::weak_ptr<Queue> weak(queue);
            juce::String error;
            if (!backend->attach(bound, [weak, epoch, binding](bool self, bool disconnected) {
                    if (self) return;
                    if (const auto current = weak.lock()) {
                        if (current->stopped.load(std::memory_order_acquire)
                            || current->generation.load(std::memory_order_acquire) != epoch
                            || current->binding.load(std::memory_order_acquire) != binding) return;
                        current->events.fetch_or(disconnected ? 2u : 1u, std::memory_order_release);
                        current->wake.signal();
                    }
                }, error)) { fault(error, false, policy); return; }
            pendingRead = true;
        }
        if ((events & 1u) != 0 && correctionDue < 0) correctionDue = juce::jmax(now + 50, lastWrite + 250);
        const bool due = correctionDue >= 0 && now >= correctionDue;
        if (pendingRead || due) {
            float scalar = 0.0f; juce::String error;
            if (!backend->readScalar(scalar, error) || !std::isfinite(scalar) || scalar < 0.0f || scalar > 1.0f) {
                fault(error.isNotEmpty() ? error : "The microphone reported an invalid level; correction stopped.", false, policy); return;
            }
            pendingRead = false;
            if (scalar >= 0.99999f) correctionDue = -1;
            else if (!due) correctionDue = juce::jmax(now + 50, lastWrite + 250);
            else {
                const auto identity = backend->resolve(selected);
                if (identity.state != State::ready || identity.endpoint.id != bound) {
                    detach(); resolved = identity; nextDiscovery = now;
                    Status changed; changed.state = identity.state; changed.endpointId = identity.endpoint.id;
                    changed.endpointName = identity.endpoint.name; changed.message = "Selected capture identity changed; correction stopped.";
                    changed.canEnable = identity.state == State::ready && identity.endpoint.id == selected.endpointId;
                    changed.enabled = policy.enabledEndpointIds.contains(identity.endpoint.id); publish(std::move(changed)); return;
                }
                bool written = false;
                {
                    // A policy setter cannot return between final admission
                    // and the native write. OFF/suspension therefore completes
                    // after an in-flight write, and prevents every queued one.
                    // Native callbacks only publish atomics, never this lock.
                    const juce::ScopedLock lock(mutex);
                    if (generation != epoch || !startupReady || !context.captureActive || context.endpointId != bound
                        || !preferences.enabledEndpointIds.contains(bound) || queue->stopped.load()) return;
                    lastWrite = now; correctionDue = -1;
                    written = backend->setScalar100(error);
                }
                if (!written) { fault(error, false, policy); return; }
            }
        }
        next.state = correctionDue >= 0 ? State::ready : State::holding;
        next.message = correctionDue >= 0
            ? "Microphone level correction to 100 is queued (system-wide input level)."
            : "Keeping this Windows microphone level at 100 (system-wide driver maximum).";
        publish(std::move(next));
    }
    WindowsMicLevelKeeper& owner;
    mutable juce::CriticalSection mutex;
    std::unique_ptr<Backend> backend;
    std::shared_ptr<Queue> queue = std::make_shared<Queue>();
    InputContext context;
    Status status;
    Preferences preferences;
    bool startupReady = false;
    const bool testMode;
    juce::uint64 generation = 1, seenGeneration = 0;
    juce::int64 nextDiscovery = 0, correctionDue = -1, lastWrite = -1000000;
    bool failed = false, pendingRead = false;
    Resolution resolved;
    juce::String bound;
};

WindowsMicLevelKeeper::WindowsMicLevelKeeper(std::unique_ptr<Backend> backend, bool startWorker)
    : impl_(std::make_unique<Impl>(*this, std::move(backend), startWorker)) {}
WindowsMicLevelKeeper::~WindowsMicLevelKeeper() { impl_->shutdown(); }
void WindowsMicLevelKeeper::updateInputContext(const InputContext& context)
{
    {
        const juce::ScopedLock lock(impl_->mutex);
        if (impl_->context.deviceType == context.deviceType && impl_->context.inputDeviceName == context.inputDeviceName
            && impl_->context.captureActive == context.captureActive && impl_->context.endpointId == context.endpointId) return;
        impl_->context = context; impl_->nextGeneration(); impl_->status = {};
    }
    impl_->signal();
}
void WindowsMicLevelKeeper::setStartupReady(bool ready)
{
    {
        const juce::ScopedLock lock(impl_->mutex);
        if (impl_->startupReady == ready) return;
        impl_->startupReady = ready; impl_->nextGeneration();
        if (!ready) impl_->status.canEnable = false;
    }
    impl_->signal();
}
WindowsMicLevelKeeper::Status WindowsMicLevelKeeper::getStatus() const
{
    const juce::ScopedLock lock(impl_->mutex); return impl_->status;
}
bool WindowsMicLevelKeeper::setEnabledForCurrentEndpoint(bool enabled, juce::String& error)
{
    {
        const juce::ScopedLock lock(impl_->mutex);
        const auto id = impl_->status.endpointId;
        if (impl_->queue->stopped.load() || id.isEmpty() || (enabled && !impl_->status.canEnable)) {
            error = "The selected microphone cannot be identified or controlled exactly."; return false;
        }
        if (enabled) impl_->preferences.enabledEndpointIds.addIfNotAlreadyThere(id);
        else impl_->preferences.enabledEndpointIds.removeString(id);
        impl_->status.enabled = enabled;
        if (!enabled && impl_->startupReady && impl_->context.captureActive
            && supportedType(impl_->context.deviceType)) impl_->status.canEnable = true;
        impl_->nextGeneration();
    }
    error.clear(); impl_->signal(); return true;
}
WindowsMicLevelKeeper::Preferences WindowsMicLevelKeeper::getPreferences() const
{
    const juce::ScopedLock lock(impl_->mutex); return impl_->preferences;
}
bool WindowsMicLevelKeeper::setPreferences(const Preferences& preferences, juce::String& error)
{
    if (!validIds(preferences.enabledEndpointIds, error)) return false;
    {
        const juce::ScopedLock lock(impl_->mutex);
        if (impl_->preferences.enabledEndpointIds == preferences.enabledEndpointIds) return true;
        impl_->preferences = preferences; impl_->nextGeneration();
        impl_->status.enabled = preferences.enabledEndpointIds.contains(impl_->status.endpointId);
    }
    impl_->signal(); return true;
}
void WindowsMicLevelKeeper::stop() { impl_->shutdown(); }
bool WindowsMicLevelKeeper::parsePreferences(const juce::var& value, Preferences& result, juce::String& error)
{
    const auto* object = value.getDynamicObject();
    if (!object || !object->hasProperty("version") || !object->hasProperty("enabledEndpointIds")
        || object->getProperties().size() != 2) { error = "Invalid Windows microphone level preferences object"; return false; }
    const auto version = object->getProperty("version");
    const auto ids = object->getProperty("enabledEndpointIds");
    if ((!version.isInt() && !version.isInt64()) || static_cast<juce::int64>(version) != 1 || !ids.isArray()) {
        error = "Invalid Windows microphone level preference version or endpoint list"; return false;
    }
    Preferences parsed;
    for (const auto& id : *ids.getArray()) {
        if (!id.isString()) { error = "Microphone endpoint IDs must be strings"; return false; }
        parsed.enabledEndpointIds.add(id.toString());
    }
    if (!validIds(parsed.enabledEndpointIds, error)) return false;
    result = std::move(parsed); error.clear(); return true;
}
juce::var WindowsMicLevelKeeper::serializePreferences(const Preferences& preferences)
{
    auto value = std::make_unique<juce::DynamicObject>();
    juce::Array<juce::var> ids;
    for (const auto& id : preferences.enabledEndpointIds) ids.add(id);
    value->setProperty("version", 1); value->setProperty("enabledEndpointIds", ids);
    return juce::var(value.release());
}
#if defined(DIRECTPIPE_ENABLE_TEST_ACCESS)
void WindowsMicLevelKeeperTestAccess::drain(WindowsMicLevelKeeper& keeper, juce::int64 nowMs)
{
    if (keeper.impl_->testMode) keeper.impl_->service(nowMs);
}
#endif
} // namespace directpipe
