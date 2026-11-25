/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_SINK_H
#define AUDIO_SINK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio sink backend interface */
typedef struct {
    const char *name;
    bool (*init)(void);
    void (*task)(void);
    bool (*can_accept_samples)(uint32_t count);
    uint32_t (*write_samples)(const int16_t *samples, uint32_t count);
    void (*deinit)(void);
    /* Optional status and diagnostics callbacks */
    bool (*is_ready)(void);
    uint32_t (*get_buffer_level)(void);
    bool (*handles_core_task)(void);
} audio_sink_backend_t;

/* Global lifecycle functions invoked by Core 0 */
bool audio_sink_init(void);
void audio_sink_task(void);
bool audio_sink_can_accept_samples(uint32_t count);
uint32_t audio_sink_write_samples(const int16_t *samples, uint32_t count);
void audio_sink_deinit(void);
bool audio_sink_is_ready(void);
uint32_t audio_sink_get_buffer_level(void);
bool audio_sink_handles_core_task(void);
const char *audio_sink_backend_name(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SINK_H */

