/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "audio_sink.h"
#include <stdio.h>

#if defined(A2PICOMB_AUDIO_BACKEND_BTSTACK)
extern const audio_sink_backend_t audio_sink_backend_btstack;
static const audio_sink_backend_t *active_backend = &audio_sink_backend_btstack;
#elif defined(A2PICOMB_AUDIO_BACKEND_USB_DEVICE)
extern const audio_sink_backend_t audio_sink_backend_usb_device;
static const audio_sink_backend_t *active_backend = &audio_sink_backend_usb_device;
#else
static const audio_sink_backend_t *active_backend = NULL;
#endif

bool audio_sink_init(void) {
    if (active_backend && active_backend->init) {
        return active_backend->init();
    }
    return false;
}

void audio_sink_task(void) {
    if (active_backend && active_backend->task) {
        active_backend->task();
    }
}

bool audio_sink_can_accept_samples(uint32_t count) {
    if (active_backend && active_backend->can_accept_samples) {
        return active_backend->can_accept_samples(count);
    }
    return false;
}

uint32_t audio_sink_write_samples(const int16_t *samples, uint32_t count) {
    if (active_backend && active_backend->write_samples) {
        return active_backend->write_samples(samples, count);
    }
    return 0;
}

void audio_sink_deinit(void) {
    if (active_backend && active_backend->deinit) {
        active_backend->deinit();
    }
}

bool audio_sink_is_ready(void) {
    if (active_backend && active_backend->is_ready) {
        return active_backend->is_ready();
    }
    return false;
}

uint32_t audio_sink_get_buffer_level(void) {
    if (active_backend && active_backend->get_buffer_level) {
        return active_backend->get_buffer_level();
    }
    return 0;
}

bool audio_sink_handles_core_task(void) {
    if (active_backend && active_backend->handles_core_task) {
        return active_backend->handles_core_task();
    }
    return false;
}

const char *audio_sink_backend_name(void) {
    if (active_backend && active_backend->name) {
        return active_backend->name;
    }
    return "NONE";
}

