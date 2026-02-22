/**
 * @file shm-reader.h
 * @brief Consumer-side shared memory reader for OBS plugin (C API)
 *
 * Reads PCM audio from the DirectPipe shared ring buffer.
 */
#ifndef SHM_READER_H
#define SHM_READER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque handle for the shared memory reader */
typedef struct shm_reader shm_reader_t;

/** Connection state */
typedef enum {
    SHM_STATE_DISCONNECTED = 0,
    SHM_STATE_CONNECTED,
    SHM_STATE_ERROR
} shm_state_t;

/**
 * @brief Create a shared memory reader.
 * @param shm_name Shared memory name (e.g., "Local\\DirectPipeAudio").
 * @return Reader handle, or NULL on failure.
 */
shm_reader_t* shm_reader_create(const char* shm_name);

/**
 * @brief Destroy the shared memory reader and release resources.
 */
void shm_reader_destroy(shm_reader_t* reader);

/**
 * @brief Try to connect (or reconnect) to the shared memory.
 * @return true if connected successfully.
 */
bool shm_reader_connect(shm_reader_t* reader);

/**
 * @brief Disconnect from the shared memory.
 */
void shm_reader_disconnect(shm_reader_t* reader);

/**
 * @brief Read audio frames from the ring buffer.
 * @param reader Reader handle.
 * @param data Output buffer for interleaved float PCM.
 * @param max_frames Maximum frames to read.
 * @return Number of frames actually read.
 */
uint32_t shm_reader_read(shm_reader_t* reader, float* data, uint32_t max_frames);

/**
 * @brief Get the current connection state.
 */
shm_state_t shm_reader_get_state(const shm_reader_t* reader);

/**
 * @brief Get the sample rate from the shared header.
 * @return Sample rate, or 0 if not connected.
 */
uint32_t shm_reader_get_sample_rate(const shm_reader_t* reader);

/**
 * @brief Get the number of channels from the shared header.
 * @return Channel count, or 0 if not connected.
 */
uint32_t shm_reader_get_channels(const shm_reader_t* reader);

/**
 * @brief Get number of frames available for reading.
 */
uint32_t shm_reader_available(const shm_reader_t* reader);

#ifdef __cplusplus
}
#endif

#endif /* SHM_READER_H */
