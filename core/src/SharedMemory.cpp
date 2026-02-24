/**
 * @file SharedMemory.cpp
 * @brief Platform-specific shared memory and named event implementation
 */

#include "directpipe/SharedMemory.h"
#include <cassert>

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════════
// Windows Implementation
// ═══════════════════════════════════════════════════════════════

namespace directpipe {

// ─── SharedMemory ───────────────────────────────────────────────

SharedMemory::~SharedMemory() { close(); }

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : data_(other.data_), size_(other.size_), mapping_(other.mapping_)
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.mapping_ = nullptr;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        mapping_ = other.mapping_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.mapping_ = nullptr;
    }
    return *this;
}

bool SharedMemory::create(const std::string& name, size_t size)
{
    close();

    DWORD sizeHigh = static_cast<DWORD>((size >> 32) & 0xFFFFFFFF);
    DWORD sizeLow = static_cast<DWORD>(size & 0xFFFFFFFF);

    mapping_ = CreateFileMappingA(
        INVALID_HANDLE_VALUE,  // Use paging file
        nullptr,               // Default security
        PAGE_READWRITE,        // Read/write access
        sizeHigh, sizeLow,
        name.c_str()
    );

    if (!mapping_) return false;

    data_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!data_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    size_ = size;
    return true;
}

bool SharedMemory::open(const std::string& name, size_t size)
{
    close();

    mapping_ = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
    if (!mapping_) return false;

    data_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!data_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
        return false;
    }

    size_ = size;
    return true;
}

void SharedMemory::close()
{
    if (data_) {
        UnmapViewOfFile(data_);
        data_ = nullptr;
    }
    if (mapping_) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    size_ = 0;
}

// ─── NamedEvent ─────────────────────────────────────────────────

NamedEvent::~NamedEvent() { close(); }

NamedEvent::NamedEvent(NamedEvent&& other) noexcept : event_(other.event_)
{
    other.event_ = nullptr;
}

NamedEvent& NamedEvent::operator=(NamedEvent&& other) noexcept
{
    if (this != &other) {
        close();
        event_ = other.event_;
        other.event_ = nullptr;
    }
    return *this;
}

bool NamedEvent::create(const std::string& name)
{
    close();
    event_ = CreateEventA(nullptr, FALSE, FALSE, name.c_str());
    return event_ != nullptr;
}

bool NamedEvent::open(const std::string& name)
{
    close();
    event_ = OpenEventA(SYNCHRONIZE, FALSE, name.c_str());
    return event_ != nullptr;
}

void NamedEvent::signal()
{
    if (event_) SetEvent(event_);
}

bool NamedEvent::wait(uint32_t timeout_ms)
{
    if (!event_) return false;
    return WaitForSingleObject(event_, timeout_ms) == WAIT_OBJECT_0;
}

void NamedEvent::close()
{
    if (event_) {
        CloseHandle(event_);
        event_ = nullptr;
    }
}

bool NamedEvent::isOpen() const { return event_ != nullptr; }

} // namespace directpipe

#else
// ═══════════════════════════════════════════════════════════════
// POSIX Implementation (Linux/macOS — for development/testing)
// ═══════════════════════════════════════════════════════════════

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace directpipe {

// ─── SharedMemory ───────────────────────────────────────────────

SharedMemory::~SharedMemory() { close(); }

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : data_(other.data_), size_(other.size_), fd_(other.fd_), name_(std::move(other.name_))
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.fd_ = -1;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        fd_ = other.fd_;
        name_ = std::move(other.name_);
        other.data_ = nullptr;
        other.size_ = 0;
        other.fd_ = -1;
    }
    return *this;
}

/// Convert Windows-style "Local\\Name" to POSIX "/Name"
static std::string toPosixName(const std::string& name)
{
    std::string result = name;
    // Remove "Local\\" prefix
    const std::string prefix = "Local\\";
    if (result.rfind(prefix, 0) == 0) {
        result = result.substr(prefix.size());
    }
    // Replace remaining backslashes
    for (auto& c : result) {
        if (c == '\\') c = '_';
    }
    return "/" + result;
}

bool SharedMemory::create(const std::string& name, size_t size)
{
    close();
    name_ = toPosixName(name);

    // Remove any existing shared memory with this name
    shm_unlink(name_.c_str());

    fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd_ < 0) return false;

    if (ftruncate(fd_, static_cast<off_t>(size)) < 0) {
        ::close(fd_);
        shm_unlink(name_.c_str());
        fd_ = -1;
        return false;
    }

    data_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED) {
        data_ = nullptr;
        ::close(fd_);
        shm_unlink(name_.c_str());
        fd_ = -1;
        return false;
    }

    size_ = size;
    return true;
}

bool SharedMemory::open(const std::string& name, size_t size)
{
    close();
    name_ = toPosixName(name);

    fd_ = shm_open(name_.c_str(), O_RDWR, 0666);
    if (fd_ < 0) return false;

    data_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED) {
        data_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    size_ = size;
    return true;
}

void SharedMemory::close()
{
    if (data_) {
        munmap(data_, size_);
        data_ = nullptr;
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (!name_.empty()) {
        shm_unlink(name_.c_str());
        name_.clear();
    }
    size_ = 0;
}

// ─── NamedEvent (POSIX: eventfd or pipe-based) ─────────────────

#include <sys/eventfd.h>

NamedEvent::~NamedEvent() { close(); }

NamedEvent::NamedEvent(NamedEvent&& other) noexcept : fd_(other.fd_)
{
    other.fd_ = -1;
}

NamedEvent& NamedEvent::operator=(NamedEvent&& other) noexcept
{
    if (this != &other) {
        close();
        fd_ = other.fd_;
        other.fd_ = -1;
    }
    return *this;
}

bool NamedEvent::create(const std::string& /*name*/)
{
    close();
    fd_ = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE);
    return fd_ >= 0;
}

bool NamedEvent::open(const std::string& /*name*/)
{
    // On POSIX, named events are not natively inter-process in this way.
    // For cross-process use, you'd need a POSIX named semaphore or shared eventfd.
    // This simplified implementation works for in-process testing only.
    return fd_ >= 0;
}

void NamedEvent::signal()
{
    if (fd_ >= 0) {
        uint64_t val = 1;
        [[maybe_unused]] auto ret = ::write(fd_, &val, sizeof(val));
    }
}

bool NamedEvent::wait(uint32_t timeout_ms)
{
    if (fd_ < 0) return false;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
    if (ret > 0) {
        uint64_t val;
        [[maybe_unused]] auto r = ::read(fd_, &val, sizeof(val));
        return true;
    }
    return false;
}

void NamedEvent::close()
{
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool NamedEvent::isOpen() const { return fd_ >= 0; }

} // namespace directpipe

#endif // _WIN32
