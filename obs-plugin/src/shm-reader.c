/**
 * @file shm-reader.c
 * @brief Consumer-side shared memory reader implementation
 */

#include "shm-reader.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

/* ─── Protocol definitions (must match core/include/directpipe/Protocol.h) ─── */

#define PROTOCOL_VERSION 1
#define CACHE_LINE_SIZE 64

/* Mirror of DirectPipeHeader for C */
typedef struct {
    _Alignas(64) _Atomic(uint64_t) write_pos;
    _Alignas(64) _Atomic(uint64_t) read_pos;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t buffer_frames;
    uint32_t version;
    _Atomic(bool) producer_active;
    uint8_t reserved[64 - sizeof(_Atomic(bool)) - 4 * sizeof(uint32_t)];
} shm_header_t;

struct shm_reader {
    char shm_name[256];
    shm_state_t state;

    /* Shared memory mapping */
#ifdef _WIN32
    HANDLE mapping;
#else
    int fd;
    char posix_name[256];
#endif
    void* mapped_data;
    size_t mapped_size;

    /* Ring buffer pointers (into mapped memory) */
    shm_header_t* header;
    float* ring_data;
    uint32_t mask;
};

/* ─── Helper: calculate shared memory size ─── */
static size_t calc_shm_size(uint32_t buffer_frames, uint32_t channels)
{
    return sizeof(shm_header_t) +
           (size_t)buffer_frames * channels * sizeof(float);
}

#ifndef _WIN32
/* Convert Windows-style name to POSIX */
static void to_posix_name(const char* src, char* dst, size_t dst_size)
{
    const char* prefix = "Local\\";
    size_t prefix_len = strlen(prefix);

    dst[0] = '/';
    size_t j = 1;

    size_t start = 0;
    if (strncmp(src, prefix, prefix_len) == 0) {
        start = prefix_len;
    }

    for (size_t i = start; src[i] && j < dst_size - 1; i++) {
        dst[j++] = (src[i] == '\\') ? '_' : src[i];
    }
    dst[j] = '\0';
}
#endif

/* ═══════════════════════════════════════════════════════════════════ */

shm_reader_t* shm_reader_create(const char* shm_name)
{
    shm_reader_t* reader = (shm_reader_t*)calloc(1, sizeof(shm_reader_t));
    if (!reader) return NULL;

    strncpy(reader->shm_name, shm_name, sizeof(reader->shm_name) - 1);
    reader->state = SHM_STATE_DISCONNECTED;

#ifdef _WIN32
    reader->mapping = NULL;
#else
    reader->fd = -1;
    to_posix_name(shm_name, reader->posix_name, sizeof(reader->posix_name));
#endif
    reader->mapped_data = NULL;
    reader->mapped_size = 0;
    reader->header = NULL;
    reader->ring_data = NULL;

    return reader;
}

void shm_reader_destroy(shm_reader_t* reader)
{
    if (!reader) return;
    shm_reader_disconnect(reader);
    free(reader);
}

bool shm_reader_connect(shm_reader_t* reader)
{
    if (!reader) return false;
    if (reader->state == SHM_STATE_CONNECTED) return true;

    /* First, try to open with a default size to read the header */
    size_t initial_size = sizeof(shm_header_t) +
                          (size_t)4096 * 2 * sizeof(float); /* default estimate */

#ifdef _WIN32
    reader->mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, reader->shm_name);
    if (!reader->mapping) {
        reader->state = SHM_STATE_DISCONNECTED;
        return false;
    }

    reader->mapped_data = MapViewOfFile(reader->mapping, FILE_MAP_READ, 0, 0, initial_size);
    if (!reader->mapped_data) {
        CloseHandle(reader->mapping);
        reader->mapping = NULL;
        reader->state = SHM_STATE_ERROR;
        return false;
    }
#else
    reader->fd = shm_open(reader->posix_name, O_RDWR, 0666);
    if (reader->fd < 0) {
        reader->state = SHM_STATE_DISCONNECTED;
        return false;
    }

    reader->mapped_data = mmap(NULL, initial_size, PROT_READ | PROT_WRITE,
                                MAP_SHARED, reader->fd, 0);
    if (reader->mapped_data == MAP_FAILED) {
        reader->mapped_data = NULL;
        close(reader->fd);
        reader->fd = -1;
        reader->state = SHM_STATE_ERROR;
        return false;
    }
#endif

    reader->mapped_size = initial_size;
    reader->header = (shm_header_t*)reader->mapped_data;

    /* Validate the header */
    if (reader->header->version != PROTOCOL_VERSION ||
        reader->header->channels == 0 ||
        reader->header->channels > 2 ||
        reader->header->buffer_frames == 0 ||
        reader->header->sample_rate == 0) {
        shm_reader_disconnect(reader);
        reader->state = SHM_STATE_ERROR;
        return false;
    }

    /* Check power of 2 */
    uint32_t bf = reader->header->buffer_frames;
    if ((bf & (bf - 1)) != 0) {
        shm_reader_disconnect(reader);
        reader->state = SHM_STATE_ERROR;
        return false;
    }

    reader->ring_data = (float*)((uint8_t*)reader->mapped_data + sizeof(shm_header_t));
    reader->mask = reader->header->buffer_frames - 1;
    reader->state = SHM_STATE_CONNECTED;

    return true;
}

void shm_reader_disconnect(shm_reader_t* reader)
{
    if (!reader) return;

#ifdef _WIN32
    if (reader->mapped_data) {
        UnmapViewOfFile(reader->mapped_data);
        reader->mapped_data = NULL;
    }
    if (reader->mapping) {
        CloseHandle(reader->mapping);
        reader->mapping = NULL;
    }
#else
    if (reader->mapped_data) {
        munmap(reader->mapped_data, reader->mapped_size);
        reader->mapped_data = NULL;
    }
    if (reader->fd >= 0) {
        close(reader->fd);
        reader->fd = -1;
    }
#endif

    reader->header = NULL;
    reader->ring_data = NULL;
    reader->mapped_size = 0;
    reader->state = SHM_STATE_DISCONNECTED;
}

uint32_t shm_reader_read(shm_reader_t* reader, float* data, uint32_t max_frames)
{
    if (!reader || reader->state != SHM_STATE_CONNECTED || !reader->header)
        return 0;

    uint32_t channels = reader->header->channels;
    uint32_t capacity = reader->header->buffer_frames;

    uint64_t write_pos = atomic_load_explicit(&reader->header->write_pos,
                                               memory_order_acquire);
    uint64_t read_pos = atomic_load_explicit(&reader->header->read_pos,
                                              memory_order_relaxed);

    uint64_t available64 = write_pos - read_pos;
    uint32_t available = (uint32_t)(available64 < capacity ? available64 : capacity);
    uint32_t to_read = max_frames < available ? max_frames : available;

    if (to_read == 0) return 0;

    uint32_t read_index = (uint32_t)read_pos & reader->mask;
    uint32_t first_chunk = to_read;
    if (read_index + first_chunk > capacity) {
        first_chunk = capacity - read_index;
    }
    uint32_t second_chunk = to_read - first_chunk;

    /* First segment */
    memcpy(data,
           reader->ring_data + (size_t)read_index * channels,
           (size_t)first_chunk * channels * sizeof(float));

    /* Second segment (wrap-around) */
    if (second_chunk > 0) {
        memcpy(data + (size_t)first_chunk * channels,
               reader->ring_data,
               (size_t)second_chunk * channels * sizeof(float));
    }

    /* Update read position */
    atomic_store_explicit(&reader->header->read_pos, read_pos + to_read,
                          memory_order_release);

    return to_read;
}

shm_state_t shm_reader_get_state(const shm_reader_t* reader)
{
    return reader ? reader->state : SHM_STATE_DISCONNECTED;
}

uint32_t shm_reader_get_sample_rate(const shm_reader_t* reader)
{
    if (reader && reader->header && reader->state == SHM_STATE_CONNECTED)
        return reader->header->sample_rate;
    return 0;
}

uint32_t shm_reader_get_channels(const shm_reader_t* reader)
{
    if (reader && reader->header && reader->state == SHM_STATE_CONNECTED)
        return reader->header->channels;
    return 0;
}

uint32_t shm_reader_available(const shm_reader_t* reader)
{
    if (!reader || reader->state != SHM_STATE_CONNECTED || !reader->header)
        return 0;

    uint64_t write_pos = atomic_load_explicit(&reader->header->write_pos,
                                               memory_order_acquire);
    uint64_t read_pos = atomic_load_explicit(&reader->header->read_pos,
                                              memory_order_relaxed);
    uint64_t avail = write_pos - read_pos;
    uint32_t capacity = reader->header->buffer_frames;
    return (uint32_t)(avail < capacity ? avail : capacity);
}
