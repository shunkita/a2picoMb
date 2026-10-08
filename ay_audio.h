/*

MIT License

Copyright (c) 2026 Shunichi Kitahara

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#ifndef _AY_AUDIO_H
#define _AY_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include "audio_event.h"

// Initialize AYUMI instances (Core 0)
void ay_audio_init(void);

// Produce 16-bit stereo PCM audio samples (compatible with bt_a2dp_pcm_callback_t)
void ay_audio_produce_pcm(int16_t *pcm_buffer, int num_samples);

// Write to an AY register on specified chip index (0..NUM_AUDIO_QUEUES-1)
void ay_audio_write_reg(uint8_t chip_index, uint8_t reg, uint8_t value);

// Reset specified chip index (0..NUM_AUDIO_QUEUES-1)
void ay_audio_reset(uint8_t chip_index);

// Handle an incoming audio event from Core 1
void ay_audio_handle_event(uint8_t chip_index, const ay_event_t *ev);

// Diagnostic getters
uint32_t ay_audio_get_pcm_calls(void);
int16_t ay_audio_get_last_max_amp(void);
uint8_t ay_audio_get_reg(uint8_t chip_index, uint8_t reg);
void ay_audio_get_and_reset_pcm_stats(uint32_t *pcm_samples, int16_t *min_val, int16_t *max_val,
                                      uint32_t *silent_samples, uint32_t *max_silent_run);
uint32_t ay_audio_get_last_pcm_us(void);
void ay_audio_trigger_timing_sample(void);

#endif // _AY_AUDIO_H
