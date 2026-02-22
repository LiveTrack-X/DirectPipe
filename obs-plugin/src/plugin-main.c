/**
 * @file plugin-main.c
 * @brief OBS plugin entry point for DirectPipe Audio Source
 */

#include <obs-module.h>
#include "directpipe-source.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("obs-directpipe-source", "en-US")

bool obs_module_load(void)
{
    register_directpipe_source();
    blog(LOG_INFO, "[DirectPipe] Plugin loaded (v1.0.0)");
    return true;
}

void obs_module_unload(void)
{
    blog(LOG_INFO, "[DirectPipe] Plugin unloaded");
}

const char* obs_module_name(void)
{
    return "DirectPipe Audio";
}

const char* obs_module_description(void)
{
    return "Receives low-latency audio from DirectPipe VST Host "
           "via shared memory IPC.";
}
