/*
 * MIT License
 *
 * Copyright (c) 2026 Shunichi Kitahara
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "audio_sink.h"
#include <stdio.h>

#if defined(A2PICOMB_AUDIO_BACKEND_BTSTACK)
extern const audio_sink_backend_t audio_sink_backend_btstack;
static const audio_sink_backend_t *active_backend = &audio_sink_backend_btstack;
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

