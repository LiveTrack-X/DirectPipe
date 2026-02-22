/**
 * @file directpipe-source.h
 * @brief DirectPipe OBS audio source — header
 */
#ifndef DIRECTPIPE_SOURCE_H
#define DIRECTPIPE_SOURCE_H

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Register the DirectPipe audio source with OBS */
void register_directpipe_source(void);

/** The source info struct (declared in directpipe-source.c) */
extern struct obs_source_info directpipe_source_info;

#ifdef __cplusplus
}
#endif

#endif /* DIRECTPIPE_SOURCE_H */
