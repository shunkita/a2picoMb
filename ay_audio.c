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

#include "ay_audio.h"
#include "ayumi.h"
#include <string.h>
#include <pico/stdlib.h>

#define AYUMI_CLOCK_RATE  1022727.0f
#define AYUMI_SAMPLE_RATE 44100

static struct ayumi ayumis[NUM_AUDIO_QUEUES];
static uint8_t ay_regs[NUM_AUDIO_QUEUES][16];

void ay_audio_write_reg(uint8_t chip_index, uint8_t reg, uint8_t value) {
    if (chip_index >= NUM_AUDIO_QUEUES) return;

    struct ayumi *ay = &ayumis[chip_index];
    uint8_t *regs = ay_regs[chip_index];

    reg &= 0x0F;
    regs[reg] = value;

    switch (reg) {
    case 0:
    case 1: { // Tone A Period
        int period = regs[0] | ((regs[1] & 0x0F) << 8);
        ayumi_set_tone(ay, 0, period);
        break;
    }
    case 2:
    case 3: { // Tone B Period
        int period = regs[2] | ((regs[3] & 0x0F) << 8);
        ayumi_set_tone(ay, 1, period);
        break;
    }
    case 4:
    case 5: { // Tone C Period
        int period = regs[4] | ((regs[5] & 0x0F) << 8);
        ayumi_set_tone(ay, 2, period);
        break;
    }
    case 6: { // Noise Period
        ayumi_set_noise(ay, regs[6] & 0x1F);
        break;
    }
    case 7: { // Mixer / Enable
        for (int ch = 0; ch < 3; ch++) {
            int t_off = (regs[7] >> ch) & 1;
            int n_off = (regs[7] >> (ch + 3)) & 1;
            int e_on  = (regs[8 + ch] >> 4) & 1;
            ayumi_set_mixer(ay, ch, t_off, n_off, e_on);
        }
        break;
    }
    case 8:
    case 9:
    case 10: { // Amplitude A, B, C
        int ch = reg - 8;
        int t_off = (regs[7] >> ch) & 1;
        int n_off = (regs[7] >> (ch + 3)) & 1;
        int e_on  = (value >> 4) & 1;
        ayumi_set_volume(ay, ch, value & 0x0F);
        ayumi_set_mixer(ay, ch, t_off, n_off, e_on);
        break;
    }
    case 11:
    case 12: { // Envelope Period
        int period = regs[11] | (regs[12] << 8);
        ayumi_set_envelope(ay, period);
        break;
    }
    case 13: { // Envelope Shape
        ayumi_set_envelope_shape(ay, value & 0x0F);
        break;
    }
    default:
        // R14, R15: I/O Ports (no sound effect)
        break;
    }
}

void ay_audio_reset(uint8_t chip_index) {
    if (chip_index >= NUM_AUDIO_QUEUES) return;

    struct ayumi *ay = &ayumis[chip_index];
    uint8_t *regs = ay_regs[chip_index];

    memset(regs, 0, 16);
    ayumi_configure(ay, 0, AYUMI_CLOCK_RATE, AYUMI_SAMPLE_RATE);
    for (int ch = 0; ch < 3; ch++) {
        ayumi_set_pan(ay, ch, 0.5f, 0);
        ayumi_set_mixer(ay, ch, 1, 1, 0); // all off (tone off, noise off, env off)
        ayumi_set_volume(ay, ch, 0);
    }
}

void ay_audio_handle_event(uint8_t chip_index, const ay_event_t *ev) {
    if (!ev || chip_index >= NUM_AUDIO_QUEUES) return;

    if (ev->type == AY_EVENT_REGISTER_WRITE) {
        ay_audio_write_reg(chip_index, ev->reg, ev->value);
    } else if (ev->type == AY_EVENT_RESET) {
        ay_audio_reset(chip_index);
    }
}

void ay_audio_init(void) {
    // Initialize 4 AYUMI instances (Core 0)
    for (int q = 0; q < NUM_AUDIO_QUEUES; q++) {
        ay_audio_reset(q);
    }


}

static uint32_t ay_pcm_call_count = 0;
static int16_t ay_last_max_amp = 0;

static uint32_t stat_pcm_samples = 0;
static int16_t  stat_min_val = 32767;
static int16_t  stat_max_val = -32768;
static uint32_t stat_silent_samples = 0;
static uint32_t stat_current_silent_run = 0;
static uint32_t stat_max_silent_run = 0;

#define MAX_PCM_TEMP_SAMPLES 256
static float ay_temp_out[2][MAX_PCM_TEMP_SAMPLES][2];
static float mix_temp_buf[MAX_PCM_TEMP_SAMPLES][2];

static uint32_t last_pcm_total_us = 0;
static uint32_t last_ay0_us = 0;
static uint32_t last_ay1_us = 0;
static uint32_t last_mix_us = 0;
static uint32_t last_post_us = 0;
static volatile bool need_timing_sample = true;

void ay_audio_trigger_timing_sample(void) {
    need_timing_sample = true;
}

void ay_audio_produce_pcm(int16_t *pcm_buffer, int num_samples) {
    ay_pcm_call_count++;

    if (num_samples > MAX_PCM_TEMP_SAMPLES) {
        num_samples = MAX_PCM_TEMP_SAMPLES;
    }

    bool do_measure = need_timing_sample;
    uint32_t t_start = 0, t1 = 0, t2 = 0, t3 = 0, t4 = 0;
    if (do_measure) {
        t_start = to_us_since_boot(get_absolute_time());
    }

    // 1. AY0 PCM generation (ayumi_process + ayumi_remove_dc)
    for (int i = 0; i < num_samples; i++) {
        ayumi_process(&ayumis[0]);
        ayumi_remove_dc(&ayumis[0]);
        ay_temp_out[0][i][0] = ayumis[0].left;
        ay_temp_out[0][i][1] = ayumis[0].right;
    }
    if (do_measure) {
        t1 = to_us_since_boot(get_absolute_time());
    }

    // 2. AY1 PCM generation (ayumi_process + ayumi_remove_dc)
    for (int i = 0; i < num_samples; i++) {
        ayumi_process(&ayumis[1]);
        ayumi_remove_dc(&ayumis[1]);
        ay_temp_out[1][i][0] = ayumis[1].left;
        ay_temp_out[1][i][1] = ayumis[1].right;
    }
    if (do_measure) {
        t2 = to_us_since_boot(get_absolute_time());
    }

    // 3. 2-chip mixing
    for (int i = 0; i < num_samples; i++) {
        mix_temp_buf[i][0] = ay_temp_out[0][i][0] + ay_temp_out[1][i][0];
        mix_temp_buf[i][1] = ay_temp_out[0][i][1] + ay_temp_out[1][i][1];
    }
    if (do_measure) {
        t3 = to_us_since_boot(get_absolute_time());
    }

    // 4. Post-processing (scaling, clipping, int16 conversion, PCM buffer store)
    for (int i = 0; i < num_samples; i++) {
        float l = mix_temp_buf[i][0] * 16384.0f;
        float r = mix_temp_buf[i][1] * 16384.0f;
        if (l > 32767.0f) l = 32767.0f;
        if (l < -32768.0f) l = -32768.0f;
        if (r > 32767.0f) r = 32767.0f;
        if (r < -32768.0f) r = -32768.0f;
        pcm_buffer[i * 2]     = (int16_t)l;
        pcm_buffer[i * 2 + 1] = (int16_t)r;
    }
    if (do_measure) {
        t4 = to_us_since_boot(get_absolute_time());
        last_pcm_total_us = t4 - t_start;
        last_ay0_us = t1 - t_start;
        last_ay1_us = t2 - t1;
        last_mix_us = t3 - t2;
        last_post_us = t4 - t3;
        need_timing_sample = false;
    }

    stat_pcm_samples += num_samples;
}

void ay_audio_get_pcm_breakdown(uint32_t *total_us, uint32_t *ay0_us, uint32_t *ay1_us,
                                uint32_t *mix_us, uint32_t *post_us) {
    if (total_us) *total_us = last_pcm_total_us;
    if (ay0_us)   *ay0_us   = last_ay0_us;
    if (ay1_us)   *ay1_us   = last_ay1_us;
    if (mix_us)   *mix_us   = last_mix_us;
    if (post_us)  *post_us  = last_post_us;
}

uint32_t ay_audio_get_pcm_calls(void) {
    return ay_pcm_call_count;
}

int16_t ay_audio_get_last_max_amp(void) {
    return ay_last_max_amp;
}

uint8_t ay_audio_get_reg(uint8_t chip_index, uint8_t reg) {
    if (chip_index >= NUM_AUDIO_QUEUES || reg >= 16) return 0;
    return ay_regs[chip_index][reg];
}

void ay_audio_get_and_reset_pcm_stats(uint32_t *pcm_samples, int16_t *min_val, int16_t *max_val,
                                      uint32_t *silent_samples, uint32_t *max_silent_run) {
    if (pcm_samples) *pcm_samples = stat_pcm_samples;
    if (min_val) *min_val = (stat_pcm_samples > 0) ? stat_min_val : 0;
    if (max_val) *max_val = (stat_pcm_samples > 0) ? stat_max_val : 0;
    if (silent_samples) *silent_samples = stat_silent_samples;
    if (max_silent_run) *max_silent_run = stat_max_silent_run;

    stat_pcm_samples = 0;
    stat_min_val = 32767;
    stat_max_val = -32768;
    stat_silent_samples = 0;
    stat_max_silent_run = 0;
}
