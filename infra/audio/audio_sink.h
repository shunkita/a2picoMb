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
} audio_sink_backend_t;

/* Global lifecycle functions invoked by Core 0 */
bool audio_sink_init(void);
void audio_sink_task(void);
bool audio_sink_can_accept_samples(uint32_t count);
uint32_t audio_sink_write_samples(const int16_t *samples, uint32_t count);
void audio_sink_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SINK_H */

