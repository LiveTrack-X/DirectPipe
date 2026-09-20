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
 * @file SharedMemory.h
 * @brief Platform-specific shared memory and event signaling wrapper
 *
 * Provides Windows mappings/events and POSIX mappings/semaphores. Both audio
 * transports use mappings; only legacy v1 retains data-ready event signaling.
 */
#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace directpipe {

/**
 * @brief Cross-platform shared memory region wrapper.
 *
 * Producer calls create() to allocate the shared memory region.
 * Consumer calls open() to connect to an existing region. Mapping operations
 * run off audio callbacks; this wrapper does not validate either audio ABI.
 */
class SharedMemory {
public:
    SharedMemory() = default;
    ~SharedMemory();

    // Non-copyable
    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    // Movable
    SharedMemory(SharedMemory&& other) noexcept;
    SharedMemory& operator=(SharedMemory&& other) noexcept;

    /**
     * @brief Create a new shared memory region (producer side).
     * Windows may reopen an existing object: check createOpenedExistingObject()
     * before initializing any header. POSIX unlinks/recreates the named object;
     * readers can retain their old view until their worker detects replacement.
     * @param name The shared memory name (e.g., "Local\\DirectPipeAudio").
     * @param size Size in bytes of the shared memory region.
     * @return true if creation succeeded.
     */
    bool create(const std::string& name, size_t size);

    /**
     * @brief Open an existing shared memory region (consumer side).
     * @param name The shared memory name.
     * @param size Requested view size, not ABI validation. Use 0 to map the full
     * existing object, then pass getSize() to the transport's bounded validation.
     * @return true if open succeeded.
     */
    bool open(const std::string& name, size_t size);

    /**
     * @brief Close the shared memory region and release resources.
     * First drain all local users and detach their transport views.
     */
    void close();

    /**
     * @brief Get pointer to the mapped memory.
     * @return Pointer to the mapped region, or nullptr if not mapped.
     */
    void* getData() const { return data_; }

    /**
     * @brief Get the size of the mapped region.
     */
    size_t getSize() const { return size_; }

    /**
     * @brief Stable identity of the currently opened POSIX shared-memory object.
     *
     * A POSIX producer restart unlinks and recreates the named object while an
     * existing consumer can still hold the old mapping. Consumers can compare
     * this value with a freshly opened mapping to detect that replacement.
     * Returns zero on Windows, where retirement and fresh-creation rules apply.
     */
    uint64_t getObjectIdentity() const { return objectIdentity_; }

    /**
     * @brief Whether the most recent successful create() opened an object that
     *        already existed instead of creating a fresh object.
     *
     * On Windows, CreateFileMapping reports this through ERROR_ALREADY_EXISTS.
     * Producers must not placement-initialize such a mapping while a retained
     * consumer can still be reading it. The flag is cleared by close()/open().
     */
    bool createOpenedExistingObject() const { return createOpenedExistingObject_; }

    /**
     * @brief Check if the shared memory is currently open.
     */
    bool isOpen() const { return data_ != nullptr; }

private:
    void* data_ = nullptr;
    size_t size_ = 0;
    uint64_t objectIdentity_ = 0;
    bool createOpenedExistingObject_ = false;
    bool isCreator_ = false;  // Only creator (producer) unlinks on close

#ifdef _WIN32
    HANDLE mapping_ = nullptr;
#else
    int fd_ = -1;
    std::string name_;
#endif
};

/**
 * @brief Named event wrapper for inter-process signaling.
 *
 * Preserves legacy v1 data-ready signaling. The current Receiver polls its
 * selected queue from processBlock(); FanOut needs no event. create/open/close
 * and blocking waits are control-side operations. Legacy writeAudio retains
 * signal(), an OS call, after writing frames for older event-waiting consumers.
 */
class NamedEvent {
public:
    NamedEvent() = default;
    ~NamedEvent();

    // Non-copyable
    NamedEvent(const NamedEvent&) = delete;
    NamedEvent& operator=(const NamedEvent&) = delete;

    // Movable
    NamedEvent(NamedEvent&& other) noexcept;
    NamedEvent& operator=(NamedEvent&& other) noexcept;

    /**
     * @brief Create a new named event (producer side).
     * @param name Event name (e.g., "Local\\DirectPipeDataReady").
     * @return true if creation succeeded.
     */
    bool create(const std::string& name);

    /**
     * @brief Open an existing named event (consumer side).
     * Windows opens SYNCHRONIZE access only: this handle can wait, not signal.
     * @param name Event name.
     * @return true if open succeeded.
     */
    bool open(const std::string& name);

    /**
     * @brief Signal the event (producer calls this after writing data).
     * On Windows use a handle obtained with create(), not the wait-only open().
     */
    void signal();

    /**
     * @brief Wait for the event to be signaled.
     * @param timeout_ms Maximum wait time in milliseconds.
     * @return true if the event was signaled, false on timeout.
     */
    bool wait(uint32_t timeout_ms);

    /**
     * @brief Close the event handle.
     */
    void close();

    /**
     * @brief Check if the event is open.
     */
    bool isOpen() const;

private:
#ifdef _WIN32
    HANDLE event_ = nullptr;
#elif defined(__APPLE__)
    // macOS: POSIX named semaphore (sem_open/sem_post/sem_trywait)
    // Note: macOS doesn't support sem_timedwait, so we poll with sem_trywait.
    void* sem_ = nullptr;  // sem_t* (void* to avoid including semaphore.h in header)
    std::string name_;
    bool isCreator_ = false;  // Only creator (producer) unlinks on close
#else
    // Linux: POSIX named semaphore (sem_open/sem_post/sem_timedwait)
    void* sem_ = nullptr;  // sem_t* (void* to avoid including semaphore.h in header)
    std::string name_;
    bool isCreator_ = false;  // Only creator (producer) unlinks on close
#endif
};

} // namespace directpipe
