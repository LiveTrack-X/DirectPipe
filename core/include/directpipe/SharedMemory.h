/**
 * @file SharedMemory.h
 * @brief Platform-specific shared memory and event signaling wrapper
 *
 * Provides create/open/close for Windows shared memory (CreateFileMapping)
 * and Named Events for data-ready signaling between processes.
 */
#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace directpipe {

/**
 * @brief Windows shared memory region wrapper.
 *
 * Producer calls create() to allocate the shared memory region.
 * Consumer calls open() to connect to an existing region.
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
     * @param name The shared memory name (e.g., "Local\\DirectPipeAudio").
     * @param size Size in bytes of the shared memory region.
     * @return true if creation succeeded.
     */
    bool create(const std::string& name, size_t size);

    /**
     * @brief Open an existing shared memory region (consumer side).
     * @param name The shared memory name.
     * @param size Expected size in bytes.
     * @return true if open succeeded.
     */
    bool open(const std::string& name, size_t size);

    /**
     * @brief Close the shared memory region and release resources.
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
     * @brief Check if the shared memory is currently open.
     */
    bool isOpen() const { return data_ != nullptr; }

private:
    void* data_ = nullptr;
    size_t size_ = 0;

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
 * Used to notify the consumer (OBS plugin) when new audio data
 * is available in the shared ring buffer.
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
     * @param name Event name.
     * @return true if open succeeded.
     */
    bool open(const std::string& name);

    /**
     * @brief Signal the event (producer calls this after writing data).
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
#else
    // POSIX implementation using named semaphore or eventfd
    int fd_ = -1;
    std::string name_;
#endif
};

} // namespace directpipe
