/**
 * @file directpipe-source.c
 * @brief DirectPipe OBS audio source implementation
 *
 * Reads processed audio from the DirectPipe host application via
 * shared memory and presents it as an OBS audio source.
 */

#include "directpipe-source.h"
#include "shm-reader.h"

#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <string.h>
#include <stdbool.h>

/* ─── Constants ───────────────────────────────────────────────── */

#define DIRECTPIPE_SHM_NAME      "Local\\DirectPipeAudio"
#define DIRECTPIPE_EVENT_NAME    "Local\\DirectPipeDataReady"
#define MAX_READ_FRAMES          4096
#define MAX_CHANNELS             2
#define READ_BUF_SIZE            (MAX_READ_FRAMES * MAX_CHANNELS)
#define EVENT_TIMEOUT_MS         500
#define RECONNECT_INTERVAL_MS    1000

/* ─── Source Context ──────────────────────────────────────────── */

struct directpipe_source {
    obs_source_t* source;
    shm_reader_t* reader;

#ifdef _WIN32
    HANDLE data_event;
#endif

    pthread_t read_thread;
    volatile bool active;
    volatile bool thread_running;

    /* Pre-allocated read buffer (interleaved PCM float32) */
    float read_buf[READ_BUF_SIZE];

    /* Status */
    volatile bool connected;
    uint64_t total_frames_read;
    uint64_t underrun_count;
};

/* ─── Read Thread ─────────────────────────────────────────────── */

static void* dp_read_thread_func(void* param)
{
    struct directpipe_source* ctx = (struct directpipe_source*)param;

    os_set_thread_name("directpipe-reader");

    while (ctx->active) {
        /* Try to connect if not connected */
        if (shm_reader_get_state(ctx->reader) != SHM_STATE_CONNECTED) {
            ctx->connected = false;
            if (!shm_reader_connect(ctx->reader)) {
                os_sleep_ms(RECONNECT_INTERVAL_MS);
                continue;
            }

#ifdef _WIN32
            /* Re-open the event handle on reconnection */
            if (ctx->data_event) {
                CloseHandle(ctx->data_event);
                ctx->data_event = NULL;
            }
            ctx->data_event = OpenEventA(SYNCHRONIZE, FALSE, DIRECTPIPE_EVENT_NAME);
#endif
            ctx->connected = true;
        }

        /* Wait for data signal */
        bool data_ready = false;

#ifdef _WIN32
        if (ctx->data_event) {
            DWORD wait_result = WaitForSingleObject(ctx->data_event, EVENT_TIMEOUT_MS);
            data_ready = (wait_result == WAIT_OBJECT_0);
        } else {
            os_sleep_ms(10);
            data_ready = (shm_reader_available(ctx->reader) > 0);
        }
#else
        /* POSIX: poll-based (simplified for non-Windows) */
        os_sleep_ms(2);  /* ~2.67ms period for 128 samples @ 48kHz */
        data_ready = (shm_reader_available(ctx->reader) > 0);
#endif

        if (!data_ready) {
            /* Check if producer is still alive */
            if (shm_reader_get_state(ctx->reader) == SHM_STATE_CONNECTED) {
                ctx->underrun_count++;
            }
            continue;
        }

        /* Read audio from shared memory */
        uint32_t frames_read = shm_reader_read(ctx->reader, ctx->read_buf, 128);

        if (frames_read > 0 && ctx->active) {
            uint32_t sample_rate = shm_reader_get_sample_rate(ctx->reader);
            uint32_t channels = shm_reader_get_channels(ctx->reader);

            if (sample_rate == 0) sample_rate = 48000;
            if (channels == 0) channels = 2;

            struct obs_source_audio audio;
            memset(&audio, 0, sizeof(audio));

            audio.data[0] = (uint8_t*)ctx->read_buf;
            audio.frames = frames_read;
            audio.speakers = (channels == 1) ? SPEAKERS_MONO : SPEAKERS_STEREO;
            audio.format = AUDIO_FORMAT_FLOAT;  /* interleaved float */
            audio.samples_per_sec = sample_rate;
            audio.timestamp = os_gettime_ns();

            obs_source_output_audio(ctx->source, &audio);

            ctx->total_frames_read += frames_read;
        }
    }

    ctx->thread_running = false;
    return NULL;
}

/* ─── OBS Source Callbacks ────────────────────────────────────── */

static const char* dp_get_name(void* unused)
{
    UNUSED_PARAMETER(unused);
    return obs_module_text("DirectPipe Audio");
}

static void* dp_create(obs_data_t* settings, obs_source_t* source)
{
    UNUSED_PARAMETER(settings);

    struct directpipe_source* ctx = bzalloc(sizeof(struct directpipe_source));
    ctx->source = source;
    ctx->active = false;
    ctx->connected = false;
    ctx->thread_running = false;
    ctx->total_frames_read = 0;
    ctx->underrun_count = 0;

#ifdef _WIN32
    ctx->data_event = NULL;
#endif

    ctx->reader = shm_reader_create(DIRECTPIPE_SHM_NAME);

    return ctx;
}

static void dp_destroy(void* data)
{
    struct directpipe_source* ctx = (struct directpipe_source*)data;
    if (!ctx) return;

    /* Ensure thread is stopped */
    if (ctx->active) {
        ctx->active = false;
        if (ctx->thread_running) {
            pthread_join(ctx->read_thread, NULL);
        }
    }

    shm_reader_destroy(ctx->reader);

#ifdef _WIN32
    if (ctx->data_event) {
        CloseHandle(ctx->data_event);
    }
#endif

    bfree(ctx);
}

static void dp_activate(void* data)
{
    struct directpipe_source* ctx = (struct directpipe_source*)data;
    if (!ctx || ctx->active) return;

    ctx->active = true;
    ctx->thread_running = true;

    if (pthread_create(&ctx->read_thread, NULL, dp_read_thread_func, ctx) != 0) {
        ctx->active = false;
        ctx->thread_running = false;
        blog(LOG_ERROR, "[DirectPipe] Failed to create read thread");
    }
}

static void dp_deactivate(void* data)
{
    struct directpipe_source* ctx = (struct directpipe_source*)data;
    if (!ctx || !ctx->active) return;

    ctx->active = false;

    if (ctx->thread_running) {
        pthread_join(ctx->read_thread, NULL);
    }

    shm_reader_disconnect(ctx->reader);
    ctx->connected = false;
}

static obs_properties_t* dp_get_properties(void* data)
{
    UNUSED_PARAMETER(data);

    obs_properties_t* props = obs_properties_create();

    obs_properties_add_text(props, "status",
                            obs_module_text("Connection Status"),
                            OBS_TEXT_INFO);

    return props;
}

static void dp_get_defaults(obs_data_t* settings)
{
    UNUSED_PARAMETER(settings);
}

static void dp_update(void* data, obs_data_t* settings)
{
    struct directpipe_source* ctx = (struct directpipe_source*)data;
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(settings);
}

/* ─── Source Info Registration ────────────────────────────────── */

struct obs_source_info directpipe_source_info = {
    .id             = "directpipe_audio_source",
    .type           = OBS_SOURCE_TYPE_INPUT,
    .output_flags   = OBS_SOURCE_AUDIO,
    .get_name       = dp_get_name,
    .create         = dp_create,
    .destroy        = dp_destroy,
    .activate       = dp_activate,
    .deactivate     = dp_deactivate,
    .get_properties = dp_get_properties,
    .get_defaults   = dp_get_defaults,
    .update         = dp_update,
};

void register_directpipe_source(void)
{
    obs_register_source(&directpipe_source_info);
}
