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
    : data_(other.data_), size_(other.size_), isCreator_(other.isCreator_), mapping_(other.mapping_)
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.isCreator_ = false;
    other.mapping_ = nullptr;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        isCreator_ = other.isCreator_;
        mapping_ = other.mapping_;
        other.data_ = nullptr;
        other.size_ = 0;
        other.isCreator_ = false;
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
    isCreator_ = true;  // Mark as creator so close() can clean up properly
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
    isCreator_ = false;
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
// POSIX Implementation (Linux/macOS)
// ═══════════════════════════════════════════════════════════════

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace directpipe {

// ─── SharedMemory (shared between macOS and Linux) ──────────────

SharedMemory::~SharedMemory() { close(); }

SharedMemory::SharedMemory(SharedMemory&& other) noexcept
    : data_(other.data_), size_(other.size_), isCreator_(other.isCreator_),
      fd_(other.fd_), name_(::std::move(other.name_))
{
    other.data_ = nullptr;
    other.size_ = 0;
    other.isCreator_ = false;
    other.fd_ = -1;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) noexcept
{
    if (this != &other) {
        close();
        data_ = other.data_;
        size_ = other.size_;
        isCreator_ = other.isCreator_;
        fd_ = other.fd_;
        name_ = ::std::move(other.name_);
        other.data_ = nullptr;
        other.size_ = 0;
        other.isCreator_ = false;
        other.fd_ = -1;
    }
    return *this;
}

/// Convert Windows-style "Local\\Name" to POSIX "/Name"
static ::std::string toPosixName(const ::std::string& name)
{
    ::std::string result = name;
    // Remove "Local\\" prefix
    const ::std::string prefix = "Local\\";
    if (result.rfind(prefix, 0) == 0) {
        result = result.substr(prefix.size());
    }
    // Replace remaining backslashes
    for (auto& c : result) {
        if (c == '\\') c = '_';
    }
    return "/" + result;
}

bool SharedMemory::create(const ::std::string& name, size_t size)
{
    close();
    name_ = toPosixName(name);

    // Remove any existing shared memory with this name
    shm_unlink(name_.c_str());

    fd_ = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0600);
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
    isCreator_ = true;
    return true;
}

bool SharedMemory::open(const ::std::string& name, size_t size)
{
    close();
    name_ = toPosixName(name);

    fd_ = shm_open(name_.c_str(), O_RDWR, 0600);
    if (fd_ < 0) return false;

    data_ = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (data_ == MAP_FAILED) {
        data_ = nullptr;
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    size_ = size;
    isCreator_ = false;  // Consumer doesn't own the shared memory
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
    // Only the creator (producer) unlinks the shared memory name.
    // Consumer must not unlink — it would break producer's ability to keep serving
    // and prevent other consumers from reconnecting.
    if (isCreator_ && !name_.empty()) {
        shm_unlink(name_.c_str());
    }
    name_.clear();
    size_ = 0;
    isCreator_ = false;
}

// ─── NamedEvent ─────────────────────────────────────────────────

#if defined(__APPLE__)
// ═══ macOS: POSIX named semaphore (sem_open) ═══════════════════
// macOS doesn't support eventfd or sem_timedwait.
// Use sem_trywait + usleep polling for timed waits.

} // close namespace directpipe for system includes

#include <semaphore.h>
#include <chrono>

namespace directpipe {

NamedEvent::~NamedEvent() { close(); }

NamedEvent::NamedEvent(NamedEvent&& other) noexcept
    : sem_(other.sem_), name_(::std::move(other.name_)), isCreator_(other.isCreator_)
{
    other.sem_ = nullptr;
    other.isCreator_ = false;
}

NamedEvent& NamedEvent::operator=(NamedEvent&& other) noexcept
{
    if (this != &other) {
        close();
        sem_ = other.sem_;
        name_ = ::std::move(other.name_);
        isCreator_ = other.isCreator_;
        other.sem_ = nullptr;
        other.isCreator_ = false;
    }
    return *this;
}

bool NamedEvent::create(const ::std::string& name)
{
    close();
    name_ = toPosixName(name);
    // Try create first; if stale semaphore exists, unlink and retry once
    auto* s = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (s == SEM_FAILED && errno == EEXIST) {
        sem_unlink(name_.c_str());
        s = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600, 0);
    }
    if (s == SEM_FAILED) return false;
    sem_ = static_cast<void*>(s);
    isCreator_ = true;
    return true;
}

bool NamedEvent::open(const ::std::string& name)
{
    close();
    name_ = toPosixName(name);
    auto* s = sem_open(name_.c_str(), 0);
    if (s == SEM_FAILED) return false;
    sem_ = static_cast<void*>(s);
    isCreator_ = false;  // Consumer doesn't own the semaphore
    return true;
}

void NamedEvent::signal()
{
    if (sem_) sem_post(static_cast<sem_t*>(sem_));
}

bool NamedEvent::wait(uint32_t timeout_ms)
{
    if (!sem_) return false;
    auto* s = static_cast<sem_t*>(sem_);
    auto deadline = ::std::chrono::steady_clock::now()
                  + ::std::chrono::milliseconds(timeout_ms);
    while (::std::chrono::steady_clock::now() < deadline) {
        if (sem_trywait(s) == 0) return true;
        usleep(500);  // 0.5ms spin — macOS lacks sem_timedwait
    }
    return false;
}

void NamedEvent::close()
{
    if (sem_) {
        if (sem_close(static_cast<sem_t*>(sem_)) != 0) {
            // Log but continue cleanup
        }
        sem_ = nullptr;
    }
    if (isCreator_ && !name_.empty()) {
        sem_unlink(name_.c_str());
    }
    name_.clear();
    isCreator_ = false;
}

bool NamedEvent::isOpen() const { return sem_ != nullptr; }

#else
// ═══ Linux: POSIX named semaphore (sem_open/sem_post/sem_timedwait) ═══

} // close namespace directpipe for system includes

#include <semaphore.h>
#include <time.h>

namespace directpipe {

NamedEvent::~NamedEvent() { close(); }

NamedEvent::NamedEvent(NamedEvent&& other) noexcept
    : sem_(other.sem_), name_(::std::move(other.name_)), isCreator_(other.isCreator_)
{
    other.sem_ = nullptr;
    other.isCreator_ = false;
}

NamedEvent& NamedEvent::operator=(NamedEvent&& other) noexcept
{
    if (this != &other) {
        close();
        sem_ = other.sem_;
        name_ = ::std::move(other.name_);
        isCreator_ = other.isCreator_;
        other.sem_ = nullptr;
        other.isCreator_ = false;
    }
    return *this;
}

bool NamedEvent::create(const ::std::string& name)
{
    close();
    name_ = toPosixName(name);
    // Try create first; if stale semaphore exists, unlink and retry once
    auto* s = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (s == SEM_FAILED && errno == EEXIST) {
        sem_unlink(name_.c_str());
        s = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0600, 0);
    }
    if (s == SEM_FAILED) return false;
    sem_ = static_cast<void*>(s);
    isCreator_ = true;
    return true;
}

bool NamedEvent::open(const ::std::string& name)
{
    close();
    name_ = toPosixName(name);
    auto* s = sem_open(name_.c_str(), 0);
    if (s == SEM_FAILED) return false;
    sem_ = static_cast<void*>(s);
    isCreator_ = false;  // Consumer doesn't own the semaphore
    return true;
}

void NamedEvent::signal()
{
    if (sem_) sem_post(static_cast<sem_t*>(sem_));
}

bool NamedEvent::wait(uint32_t timeout_ms)
{
    if (!sem_) return false;
    auto* s = static_cast<sem_t*>(sem_);

    // Linux supports sem_timedwait (unlike macOS)
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }

    return sem_timedwait(s, &ts) == 0;
}

void NamedEvent::close()
{
    if (sem_) {
        if (sem_close(static_cast<sem_t*>(sem_)) != 0) {
            // Log but continue cleanup
        }
        sem_ = nullptr;
    }
    if (isCreator_ && !name_.empty()) {
        sem_unlink(name_.c_str());
    }
    name_.clear();
    isCreator_ = false;
}

bool NamedEvent::isOpen() const { return sem_ != nullptr; }

#endif // __APPLE__ vs Linux

} // namespace directpipe

#endif // _WIN32
