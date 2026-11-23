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

#include "ssi263.h"
#include <math.h>
#include <string.h>

#define SSI263_PI               3.14159265358979323846f
#define SSI263_NOMINAL_FILTER_HZ 20000.0f

#define SSI263_VOICED_GAIN      0.90f
#define SSI263_NOISE_GAIN       0.14f
#define SSI263_NOISE_LP_COEF    0.20f
#define SSI263_OUTPUT_LP_COEF   0.52f
#define SSI263_FRIC_LP_COEF     0.23f
#define SSI263_OUTPUT_GAIN      14.50f
#define SSI263_SOURCE_BREAK_HZ  50.0f
#define SSI263_NASAL_F2_BW_HZ   200.0f
#define SSI263_FRIC_BW_HZ       450.0f
#define SSI263_FRIC_F5_HZ       2800.0f
#define SSI263_FRIC_F5_BW_HZ    800.0f
#define SSI263_LEVEL_TAU_SEC    0.0025f
#define SSI263_ATTACK_TAU_SEC   0.002f
#define SSI263_RELEASE_TAU_SEC  0.004f

typedef struct {
    uint16_t f1;
    uint16_t f2;
    uint16_t f3;
    uint8_t  voiced;
    uint8_t  fricative;
    uint8_t  closure;
    uint8_t  bright_fric;
    uint8_t  nasal;
    float    voiced_level;
    float    fric_level;
} ssi263_phoneme_spec_t;

// 64-phoneme acoustic target table derived from SSI-263P parameter ROM
static const ssi263_phoneme_spec_t s_phonemes[SSI263_PHONEME_COUNT] = {
    {    0,    0,    0, 0, 0, 0, 0, 0, 0.00f, 0.00f }, // 00 PA   (pause)
    {  373, 2170, 2718, 1, 0, 0, 0, 0, 0.80f, 0.00f }, // 01 E    meet
    {  466, 2169, 2637, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 02 E1   bent
    {  337, 2097, 2718, 1, 0, 0, 0, 0, 0.73f, 0.00f }, // 03 Y    before
    {  373, 2022, 2469, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 04 YI   year
    {  406, 2169, 2718, 1, 0, 0, 0, 0, 0.80f, 0.00f }, // 05 AY   please
    {  337, 2240, 2797, 1, 0, 0, 0, 0, 0.60f, 0.00f }, // 06 IE   any
    {  466, 1864, 2554, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 07 I    six
    {  493, 1945, 2469, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 08 A    made
    {  493, 1781, 2381, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 09 AI   care
    {  566, 1694, 2469, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 0A EH   nest
    {  589, 1781, 2469, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 0B EH1  belt
    {  652, 1781, 2469, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 0C AE   dad
    {  690, 1603, 2469, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 0D AE1  after
    {  690, 1175, 2469, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 0E AH   got
    {  691, 1295, 2469, 1, 0, 0, 0, 0, 0.47f, 0.00f }, // 0F AH1  father
    {  652, 1043, 2381, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 10 AW   office
    {  518,  892, 2469, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 11 O    store
    {  466,  892, 2469, 1, 0, 0, 0, 0, 0.60f, 0.00f }, // 12 OU   boat
    {  543, 1043, 2381, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 13 OO   look
    {  406, 1507, 2381, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 14 IU   you
    {  437, 1295, 2381, 1, 0, 0, 0, 0, 0.60f, 0.00f }, // 15 IU1  could
    {  406,  892, 2198, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 16 U    tune
    {  337,  711, 2101, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 17 U1   cartoon
    {  543, 1295, 2469, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 18 UH   wonder
    {  589, 1175, 2469, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 19 UH1  love
    {  631, 1175, 2469, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 1A UH2  what
    {  631, 1404, 2469, 1, 0, 0, 0, 0, 0.47f, 0.00f }, // 1B UH3  nut
    {  493, 1295, 1668, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 1C ER   bird
    {  406,  892, 1408, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 1D R    roof
    {  373, 1175, 1785, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 1E R1   rug
    {  518, 1507, 2291, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 1F R2   mutter
    {  406, 1175, 2718, 1, 0, 0, 0, 0, 0.47f, 0.00f }, // 20 L    lift
    {  337, 1404, 2797, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 21 L1   play
    {  466,  892, 2718, 1, 0, 0, 0, 0, 0.60f, 0.00f }, // 22 LF   fall
    {  406,  711, 2291, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 23 W    water
    {  337, 1175, 2554, 1, 0, 1, 0, 0, 0.53f, 0.00f }, // 24 B    bag
    {  337, 1781, 2718, 1, 0, 1, 0, 0, 0.53f, 0.00f }, // 25 D    paid
    {  406, 1864, 2198, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 26 KV   tag
    {  437, 1043, 2198, 0, 1, 1, 0, 0, 0.00f, 1.00f }, // 27 P    pen
    {  437, 1781, 2718, 0, 1, 1, 0, 0, 0.00f, 0.78f }, // 28 T    tart
    {  406, 1864, 2198, 0, 1, 1, 0, 0, 0.00f, 0.27f }, // 29 K    kit
    {  518, 1781, 2554, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 2A HV   (hold vocal)
    {  518, 1781, 2554, 0, 0, 1, 0, 0, 0.00f, 0.00f }, // 2B HVC  (hold vocal closure)
    {  518, 1781, 2554, 0, 1, 0, 0, 0, 0.00f, 0.53f }, // 2C HF   heart
    {  518, 1781, 2554, 0, 0, 1, 0, 0, 0.00f, 0.00f }, // 2D HFC  (hold fric closure)
    {  518, 1781, 2554, 1, 0, 0, 0, 1, 0.27f, 0.00f }, // 2E HN   (hold nasal)
    {  406, 1043, 2637, 1, 1, 0, 1, 0, 0.07f, 0.80f }, // 2F Z    zero
    {  296, 1603, 2554, 0, 1, 0, 1, 0, 0.00f, 0.96f }, // 30 S    same
    {  373, 1945, 2718, 1, 1, 0, 0, 0, 0.07f, 0.67f }, // 31 J    measure
    {  373, 1945, 2718, 0, 1, 0, 0, 0, 0.00f, 0.40f }, // 32 SCH  ship
    {  373, 1175, 2291, 1, 1, 0, 0, 0, 0.20f, 0.27f }, // 33 V    very
    {  373, 1175, 2291, 0, 1, 0, 0, 0, 0.00f, 0.27f }, // 34 F    four
    {  406, 1603, 2718, 1, 1, 0, 1, 0, 0.07f, 0.13f }, // 35 THV  there
    {  466, 1694, 2381, 0, 1, 0, 1, 0, 0.00f, 0.13f }, // 36 TH   with
    {  296, 1175, 2291, 1, 0, 0, 0, 1, 0.67f, 0.00f }, // 37 M    more
    {  296, 1694, 2637, 1, 0, 0, 0, 1, 0.53f, 0.00f }, // 38 N    nine
    {  373, 2022, 2718, 1, 0, 0, 0, 1, 0.45f, 0.00f }, // 39 NG   rang
    {  518, 1781, 2381, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 3A :A   maerchen
    {  373, 1694, 2291, 1, 0, 0, 0, 0, 0.40f, 0.00f }, // 3B :OH  loewe
    {  337, 1603, 2291, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 3C :U   fuenf
    {  296, 1781, 2381, 1, 0, 0, 0, 0, 0.67f, 0.00f }, // 3D :UH  menu
    {  493, 1603, 2381, 1, 0, 0, 0, 0, 0.47f, 0.00f }, // 3E E2   bitte
    {  337,  892, 2718, 1, 0, 0, 0, 0, 0.53f, 0.00f }, // 3F LB   lube
};

static const float s_bandwidth_hz[3] = { 50.0f, 80.0f, 120.0f };

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Refresh the read cache from Core 0 state. Called only from Core 0 context
// (or before the bus loop starts). Core 1 only ever loads single bytes.
static void ssi263_refresh_cache(ssi263_t *ssi) {
    uint8_t v = ssi->ar_state ? SSI263_STATUS_AR : 0x00;
    for (int i = 0; i < 8; i++) {
        ssi->read_cache[i] = v;
    }
}

void ssi263_set_ar(ssi263_t *ssi, bool ready) {
    if (ssi->ar_state == ready) {
        return;
    }
    ssi->ar_state = ready;
    ssi263_refresh_cache(ssi);

    // The A/!R pin is the inverse of D7: ready (request) drives the pin Low.
    uint32_t seq = (ssi->ca1_word >> 1) + 1;
    uint32_t level = ready ? 0u : 1u;
    __atomic_store_n(&ssi->ca1_word, (seq << 1) | level, __ATOMIC_RELEASE);
}

void ssi263_set_sample_rate(ssi263_t *ssi, uint32_t sample_rate) {
    ssi->sample_rate = sample_rate;
    if (sample_rate == 0) {
        return;
    }

    float fs = (float)sample_rate;
    ssi->source_pole = 1.0f - expf(-2.0f * SSI263_PI * SSI263_SOURCE_BREAK_HZ / fs);
    ssi->level_coef = 1.0f - expf(-1.0f / (SSI263_LEVEL_TAU_SEC * fs));
    ssi->attack_coef = 1.0f - expf(-1.0f / (SSI263_ATTACK_TAU_SEC * fs));
    ssi->release_coef = 1.0f - expf(-1.0f / (SSI263_RELEASE_TAU_SEC * fs));

    for (int art = 0; art < 8; art++) {
        // Map ART=0..7 to 32 ms..4 ms exponential glide tau so 1-2 frame
        // phonemes reach target formants while preserving smooth coarticulation.
        float tau = (float)(8 - art) * 0.004f;
        ssi->art_coef[art] = 1.0f - expf(-1.0f / (tau * fs));
    }

    for (int stage = 0; stage < 3; stage++) {
        ssi->res_r[stage] = expf(-SSI263_PI * s_bandwidth_hz[stage] / fs);
    }
    ssi->nasal_f2_r = expf(-SSI263_PI * SSI263_NASAL_F2_BW_HZ / fs);
    ssi->fric_r = expf(-SSI263_PI * SSI263_FRIC_BW_HZ / fs);
    ssi->fric_f5_r = expf(-SSI263_PI * SSI263_FRIC_F5_BW_HZ / fs);
}

static void ssi263_reset_dsp(ssi263_t *ssi) {
    for (int i = 0; i < 3; i++) {
        ssi->f_cur[i] = 0.0f;
        ssi->res_y1[i] = 0.0f;
        ssi->res_y2[i] = 0.0f;
    }
    ssi->env_level = 0.0f;
    ssi->glottal_phase = 0.0f;
    ssi->pitch_trans_cur = 0.0f;
    ssi->pitch_trans_target = 0;
    ssi->exc_lp1 = 0.0f;
    ssi->exc_lp2 = 0.0f;
    ssi->lfsr = 0xACE1u;
    ssi->noise_lp = 0.0f;
    ssi->va_cur = 0.0f;
    ssi->fa_cur = 0.0f;
    ssi->fric_lp = 0.0f;
    ssi->fric_lp2 = 0.0f;
    ssi->fric_lp3 = 0.0f;
    ssi->fric_y1 = 0.0f;
    ssi->fric_y2 = 0.0f;
    ssi->fric_f5_y1 = 0.0f;
    ssi->fric_f5_y2 = 0.0f;
    ssi->rad_prev = 0.0f;
    ssi->out_lp = 0.0f;
    ssi->out_lp2 = 0.0f;
}

static void ssi263_reset_state(ssi263_t *ssi) {
    memset(ssi->regs, 0, sizeof(ssi->regs));
    ssi->regs[SSI263_REG_CTRLAMP] = 0x80; // Power-down on reset
    ssi->power_down = true;
    ssi->phoneme_active = false;
    ssi->mode = 0;
    ssi->phoneme_end_us = 0;
    ssi->phoneme_dur_samples = 0;
    ssi->phoneme_sample_pos = 0;
    ssi263_set_ar(ssi, false);
    ssi263_refresh_cache(ssi);
    ssi263_reset_dsp(ssi);
}

void ssi263_init(ssi263_t *ssi) {
    ssi->head = 0;
    ssi->tail = 0;
    ssi->overflow_count = 0;
    ssi->reset_req = 0;
    ssi->reset_ack = 0;
    ssi->ca1_word = 1; // sequence 0, pin High (busy/idle)
    ssi->events_processed = 0;
    ssi->ar_state = false;
    ssi263_set_sample_rate(ssi, 44100);
    ssi263_reset_state(ssi);
}

void ssi263_reset(ssi263_t *ssi) {
    // Core 1 is the only writer of reset_req.
    uint32_t req = __atomic_load_n(&ssi->reset_req, __ATOMIC_RELAXED);
    __atomic_store_n(&ssi->reset_req, req + 1, __ATOMIC_RELEASE);
}

void ssi263_write(ssi263_t *ssi, uint8_t reg, uint8_t data) {
    uint8_t reg_idx = reg & 0x07;
    if (reg_idx >= SSI263_NUM_REGS) {
        // RS2 high selects Filter Freq (reg 4) across 4..7
        reg_idx = SSI263_REG_FILFREQ;
    }

    // Core 1 is the only writer of head.
    uint32_t head = __atomic_load_n(&ssi->head, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(&ssi->tail, __ATOMIC_ACQUIRE);
    if ((head - tail) >= SSI263_QUEUE_CAPACITY) {
        ssi->overflow_count++;
        return;
    }

    ssi263_event_t *slot = &ssi->events[head & (SSI263_QUEUE_CAPACITY - 1)];
    slot->reg = reg_idx;
    slot->val = data;

    // A REG0 write starts a phoneme regardless of DUR bits (DUR=0 is 4 frames)
    // whenever the chip is powered up and A/R mode is enabled: show busy
    // (A/R bit clear) to the 6502 immediately before publishing the event.
    if (reg_idx == SSI263_REG_DURPHON && !ssi->power_down && ssi->mode != 0) {
        for (int i = 0; i < 8; i++) {
            ssi->read_cache[i] = 0x00;
        }
    }

    __atomic_store_n(&ssi->head, head + 1, __ATOMIC_RELEASE);
}

bool ssi263_pop_event(ssi263_t *ssi, ssi263_event_t *ev) {
    // Core 0 is the only writer of tail.
    uint32_t tail = __atomic_load_n(&ssi->tail, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(&ssi->head, __ATOMIC_ACQUIRE);
    if (head == tail) {
        return false;
    }
    *ev = ssi->events[tail & (SSI263_QUEUE_CAPACITY - 1)];
    __atomic_store_n(&ssi->tail, tail + 1, __ATOMIC_RELEASE);
    return true;
}

// Begin timing a phoneme from the current register contents.
static void ssi263_start_phoneme(ssi263_t *ssi, uint64_t now_us) {
    if (ssi->mode == 0) {
        // A/R strobe disabled: no phoneme completion is reported.
        ssi->phoneme_active = false;
        return;
    }
    uint32_t dur_us = ssi263_phoneme_duration_us(ssi->regs[SSI263_REG_DURPHON],
                                                 ssi->regs[SSI263_REG_RATEINF],
                                                 ssi->mode);
    ssi->phoneme_end_us = now_us + dur_us;
    ssi->phoneme_dur_samples = (uint32_t)(((uint64_t)dur_us * ssi->sample_rate) / 1000000ULL);
    ssi->phoneme_sample_pos = 0;
    ssi->phoneme_active = (dur_us > 0);
    ssi263_set_ar(ssi, false);
}

static void ssi263_update_inflection(ssi263_t *ssi) {
    uint8_t r1 = ssi->regs[SSI263_REG_INFLECT];
    if (ssi->mode == 3) {
        ssi->pitch_trans_target = (int32_t)(r1 >> 3) * 64;
    } else {
        ssi->pitch_trans_target = (int32_t)r1 * 8;
        ssi->pitch_trans_cur = (float)ssi->pitch_trans_target;
    }
}

static void ssi263_apply_event(ssi263_t *ssi, const ssi263_event_t *ev, uint64_t now_us) {
    ssi->regs[ev->reg] = ev->val;
    ssi->events_processed++;

    if (ev->reg == SSI263_REG_CTRLAMP) {
        bool pd = (ev->val & 0x80) != 0;
        if (pd && !ssi->power_down) {
            // Entering power-down: stop timing. Do not clear env_level here
            // so the release tail decays smoothly without an audible click.
            ssi->phoneme_active = false;
            ssi->power_down = true;
            ssi263_set_ar(ssi, false);
        } else if (!pd && ssi->power_down) {
            // H->L transition: latch DR1:DR0 mode and start the phoneme.
            ssi->power_down = false;
            ssi->mode = (ssi->regs[SSI263_REG_DURPHON] >> 6) & 0x03;
            ssi263_update_inflection(ssi);
            ssi263_start_phoneme(ssi, now_us);
        }
    } else if (ev->reg == SSI263_REG_INFLECT || ev->reg == SSI263_REG_RATEINF) {
        ssi263_update_inflection(ssi);
    } else if (ev->reg == SSI263_REG_DURPHON && !ssi->power_down) {
        // Writing REG0 while powered up starts the phoneme and clears A/R.
        ssi263_start_phoneme(ssi, now_us);
    }
}

void ssi263_process_events(ssi263_t *ssi, uint64_t now_us) {
    uint32_t req = __atomic_load_n(&ssi->reset_req, __ATOMIC_ACQUIRE);
    if (req != ssi->reset_ack) {
        // Discard everything queued before the reset request.
        uint32_t head = __atomic_load_n(&ssi->head, __ATOMIC_ACQUIRE);
        __atomic_store_n(&ssi->tail, head, __ATOMIC_RELEASE);
        ssi263_reset_state(ssi);
        ssi->reset_ack = req;
    }

    bool drained = false;
    ssi263_event_t ev;
    while (ssi263_pop_event(ssi, &ev)) {
        ssi263_apply_event(ssi, &ev, now_us);
        drained = true;
    }
    if (drained) {
        ssi263_refresh_cache(ssi);
    }
}

void ssi263_tick(ssi263_t *ssi, uint64_t now_us) {
    if (!ssi->phoneme_active || ssi->power_down) {
        return;
    }
    if ((int64_t)(now_us - ssi->phoneme_end_us) >= 0) {
        ssi->phoneme_active = false;
        if (ssi->mode != 0) {
            ssi263_set_ar(ssi, true);
        }
    }
}

void ssi263_inject_reg(ssi263_t *ssi, uint8_t reg, uint8_t data, uint64_t now_us) {
    uint8_t reg_idx = reg & 0x07;
    if (reg_idx >= SSI263_NUM_REGS) {
        reg_idx = SSI263_REG_FILFREQ;
    }
    ssi263_event_t ev;
    ev.reg = reg_idx;
    ev.val = data;
    ssi263_apply_event(ssi, &ev, now_us);
    ssi263_refresh_cache(ssi);
}

bool ssi263_is_silent(const ssi263_t *ssi) {
    uint8_t amp = ssi->regs[SSI263_REG_CTRLAMP] & 0x0F;
    const ssi263_phoneme_spec_t *spec = &s_phonemes[ssi->regs[SSI263_REG_DURPHON] & 0x3F];
    bool has_source = (spec->voiced_level > 0.0f) || (spec->fric_level > 0.0f);
    bool quiet = ssi->power_down || (amp == 0) || !has_source;
    return quiet && (ssi->env_level < 0.001f);
}

static inline float ssi263_get_inflection_hz(ssi263_t *ssi, float fs) {
    static const float s_glide_mult[8] = {
        1.0f, 0.962f, 1.226f, 2.462f, 2.092f, 2.462f, 2.462f, 2.462f
    };
    uint8_t r1 = ssi->regs[SSI263_REG_INFLECT];
    uint8_t r2 = ssi->regs[SSI263_REG_RATEINF];
    float imm = (float)(((r2 & 0x08) ? 2048u : 0u) | (r2 & 0x07u));

    if (ssi->mode == 3) {
        if (ssi->env_level < 0.005f && ssi->pitch_trans_cur <= 0.0f) {
            ssi->pitch_trans_cur = (float)ssi->pitch_trans_target;
        }
        float diff = (float)ssi->pitch_trans_target - ssi->pitch_trans_cur;
        if (diff != 0.0f) {
            uint32_t rate = (r2 >> 4) & 0x0Fu;
            float gstep = (s_glide_mult[r1 & 0x07u] * SSI263_XCK_HZ) /
                          (128.0f * (float)(16u - rate) * fs);
            ssi->pitch_trans_cur = (fabsf(diff) <= gstep)
                                       ? (float)ssi->pitch_trans_target
                                       : (ssi->pitch_trans_cur + ((diff > 0.0f) ? gstep : -gstep));
        }
    } else {
        ssi->pitch_trans_cur = (float)ssi->pitch_trans_target;
    }

    float div = 8.0f * (4096.0f - (imm + ssi->pitch_trans_cur));
    return (div > 0.0f) ? (SSI263_XCK_HZ / div) : 0.0f;
}

static inline float ssi263_get_filter_hz(const ssi263_t *ssi) {
    float div = 2.0f * (float)(256u - ssi->regs[SSI263_REG_FILFREQ]);
    return (div > 0.0f) ? (SSI263_XCK_HZ / div) : 0.0f;
}

static inline const ssi263_phoneme_spec_t *ssi263_get_active_spec(const ssi263_t *ssi) {
    uint8_t idx = ssi->regs[SSI263_REG_DURPHON] & 0x3F;
    return &s_phonemes[idx];
}

static void ssi263_glide_formants(ssi263_t *ssi, const ssi263_phoneme_spec_t *spec) {
    if (spec->f1 == 0) {
        return; // Hold tract position through pauses and closures
    }

    float target[3] = { (float)spec->f1, (float)spec->f2, (float)spec->f3 };
    // Snap formants immediately when entering from silence (e.g. after PA)
    // so the new syllable does not glide from a stale prior tract position.
    if (ssi->f_cur[0] <= 0.0f || ssi->env_level < 0.005f) {
        ssi->f_cur[0] = target[0];
        ssi->f_cur[1] = target[1];
        ssi->f_cur[2] = target[2];
        return;
    }

    uint8_t art = (ssi->regs[SSI263_REG_CTRLAMP] >> 4) & 0x07;
    float base_coef = ssi->art_coef[art];
    // F1 tracks faster than F2/F3 (matching SSI-263P fsm_f1 = 2.25) for crisp vowel onsets.
    float f1_coef = clampf(base_coef * 2.25f, 0.0f, 1.0f);
    ssi->f_cur[0] += f1_coef * (target[0] - ssi->f_cur[0]);
    ssi->f_cur[1] += base_coef * (target[1] - ssi->f_cur[1]);
    ssi->f_cur[2] += base_coef * (target[2] - ssi->f_cur[2]);
}

static float ssi263_generate_excitation(ssi263_t *ssi, float fs) {
    float pitch = clampf(ssi263_get_inflection_hz(ssi, fs), 30.0f, 400.0f);
    ssi->glottal_phase += pitch / fs;
    if (ssi->glottal_phase >= 1.0f) {
        ssi->glottal_phase -= 1.0f;
    }

    // Clearer Rosenberg glottal flow pulse (Tp=0.24, Tn=0.042):
    // strengthens F2/F3 formant definition for less muffled vocal clarity.
    float ph = ssi->glottal_phase;
    float pulse = 0.0f;
    if (ph < 0.24f) {
        float t = ph / 0.24f;
        pulse = t * t * (3.0f - 2.0f * t);
    } else if (ph < 0.282f) {
        float t = (ph - 0.24f) / 0.042f;
        pulse = 1.0f - t * t;
    }

    return pulse * SSI263_VOICED_GAIN * ssi->va_cur;
}

static float ssi263_generate_noise(ssi263_t *ssi) {
    uint32_t bit = ssi->lfsr & 1u;
    ssi->lfsr >>= 1;
    if (bit != 0) {
        ssi->lfsr ^= 0xB400u;
    }
    float raw = (bit != 0) ? 1.0f : -1.0f;
    ssi->noise_lp += SSI263_NOISE_LP_COEF * (raw - ssi->noise_lp);
    return ssi->noise_lp;
}

static inline float ssi263_biquad_resonate(float input, float center_hz, float r, float fs,
                                           float *y1, float *y2) {
    float fc = clampf(center_hz, 50.0f, fs * 0.45f);
    float b = 2.0f * r * cosf(2.0f * SSI263_PI * fc / fs);
    float c = -(r * r);
    float y = (1.0f - b - c) * input + b * (*y1) + c * (*y2);
    *y2 = *y1;
    *y1 = y;
    return y;
}

float ssi263_render_sample(ssi263_t *ssi) {
    if (ssi->sample_rate == 0 || ssi263_is_silent(ssi)) {
        return 0.0f;
    }

    float fs = (float)ssi->sample_rate;
    const ssi263_phoneme_spec_t *spec = ssi263_get_active_spec(ssi);

    ssi263_glide_formants(ssi, spec);

    float voiced_target = spec->voiced_level;
    float fric_target = spec->fric_level;
    if (spec->voiced && spec->fricative && spec != &s_phonemes[0x2F] && voiced_target < 0.18f) {
        // Ensure audible vocal buzz under voiced fricatives (J, THV), while letting 0x2F Z
        // keep its low 0.07f voiced_level so vocal energy does not mask Z frication.
        voiced_target = 0.18f;
    }
    if (spec->closure && ssi->phoneme_dur_samples > 0) {
        float frac = (float)ssi->phoneme_sample_pos / (float)ssi->phoneme_dur_samples;
        if (!spec->voiced) {
            // Unvoiced stop closure and release burst: silent closure for 58%,
            // full burst for 10%, then 22% tail so word-initial/medial stops stay crisp without hiss.
            if (frac < 0.58f) {
                fric_target = 0.0f;
            } else if (frac >= 0.68f) {
                fric_target *= 0.22f;
            }
        } else {
            // Voiced stop (B, D): soften to a 35% voice-bar during closure (45%),
            // then snap open on release with a subtle alveolar burst for D (0x25).
            if (frac < 0.45f) {
                voiced_target *= 0.35f;
            } else if (spec == &s_phonemes[0x25] && frac < 0.56f) {
                fric_target = 0.40f;
            }
        }
    }
    if (ssi->phoneme_active) {
        ssi->phoneme_sample_pos++;
    }

    float va_coef = ssi->level_coef;
    if (spec->closure && spec->voiced && voiced_target > ssi->va_cur) {
        va_coef = clampf(ssi->level_coef * 3.0f, 0.0f, 1.0f);
    }
    float fa_coef = ssi->level_coef;
    if (spec->closure && fric_target > ssi->fa_cur) {
        // Crisp but smooth attack for stop release burst without high-frequency click shimmer.
        fa_coef = clampf(ssi->level_coef * 2.5f, 0.0f, 1.0f);
    }
    ssi->va_cur += va_coef * (voiced_target - ssi->va_cur);
    ssi->fa_cur += fa_coef * (fric_target - ssi->fa_cur);

    float scale = clampf(ssi263_get_filter_hz(ssi) / SSI263_NOMINAL_FILTER_HZ, 0.5f, 2.0f);

    float f2_r = spec->nasal ? ssi->nasal_f2_r : ssi->res_r[1];
    float sample = ssi263_biquad_resonate(ssi263_generate_excitation(ssi, fs), ssi->f_cur[0] * scale,
                                          ssi->res_r[0], fs, &ssi->res_y1[0], &ssi->res_y2[0]);
    sample = ssi263_biquad_resonate(sample, ssi->f_cur[1] * scale,
                                    f2_r, fs, &ssi->res_y1[1], &ssi->res_y2[1]);
    sample = ssi263_biquad_resonate(sample, ssi->f_cur[2] * scale,
                                    ssi->res_r[2], fs, &ssi->res_y1[2], &ssi->res_y2[2]);

    if (spec->fricative || ssi->fa_cur > 0.0001f) {
        float noise = ssi263_generate_noise(ssi);
        if (spec->voiced && spec->fricative) {
            // Stronger glottal-phase swell on 0x2F Z (0.80) for voiced-fricative separation;
            // keep gentle 0.30 swell on J and THV.
            if (spec == &s_phonemes[0x2F]) {
                float swell = 1.0f + 0.80f * cosf(2.0f * SSI263_PI * ssi->glottal_phase);
                noise *= swell * 0.87f;
            } else {
                float swell = 1.0f + 0.30f * cosf(2.0f * SSI263_PI * ssi->glottal_phase);
                noise *= swell * 0.96f;
            }
        }
        float fric_f2 = ssi263_biquad_resonate(noise, ssi->f_cur[1] * scale, ssi->fric_r, fs,
                                               &ssi->fric_y1, &ssi->fric_y2);
        float fric_f5 = ssi263_biquad_resonate(noise, SSI263_FRIC_F5_HZ * scale, ssi->fric_f5_r, fs,
                                               &ssi->fric_f5_y1, &ssi->fric_f5_y2);
        // Boost F5 presence on 0x2F Z (0.22) for clear word-final sibilance; keep 0.05 on S.
        float w_f5 = (spec == &s_phonemes[0x2F]) ? 0.22f : (spec->bright_fric ? 0.05f : 0.00f);
        float fric_mix = fric_f2 + w_f5 * fric_f5;
        ssi->fric_lp += SSI263_FRIC_LP_COEF * (fric_mix - ssi->fric_lp);
        ssi->fric_lp2 += SSI263_FRIC_LP_COEF * (ssi->fric_lp - ssi->fric_lp2);
        sample += ssi->fric_lp2 * SSI263_NOISE_GAIN * ssi->fa_cur;
    }

    float diffed = (sample - ssi->rad_prev) * (fs / 44100.0f);
    ssi->rad_prev = sample;
    sample = diffed;

    ssi->out_lp += SSI263_OUTPUT_LP_COEF * (sample - ssi->out_lp);
    ssi->out_lp2 += SSI263_OUTPUT_LP_COEF * (ssi->out_lp - ssi->out_lp2);
    sample = ssi->out_lp2;

    bool has_source = (spec->voiced_level > 0.0f) || (spec->fric_level > 0.0f);
    uint8_t amp = ssi->regs[SSI263_REG_CTRLAMP] & 0x0F;
    // Sustain envelope across phoneme completion (A/R strobe) until Power-Down, PA, or Amp=0
    // so sustained pitch-changing singing and adjacent syllables never drop out briefly.
    float target = (!ssi->power_down && has_source)
                       ? ((float)amp / 15.0f)
                       : 0.0f;
    float env_coef = (target > ssi->env_level) ? ssi->attack_coef : ssi->release_coef;
    ssi->env_level += env_coef * (target - ssi->env_level);

    sample *= ssi->env_level * SSI263_OUTPUT_GAIN;
    // Soft saturation strictly bounded in (-1, +1) prevents hard clipping on strong vowels/nasals
    sample = sample / (1.0f + fabsf(sample));
    return clampf(sample, -1.0f, 1.0f);
}

void ssi263_render_samples(ssi263_t *ssi, float *out_buf, int num_samples) {
    if (!out_buf || num_samples <= 0) {
        return;
    }
    if (ssi->sample_rate == 0 || ssi263_is_silent(ssi)) {
        memset(out_buf, 0, (size_t)num_samples * sizeof(float));
        return;
    }
    for (int i = 0; i < num_samples; i++) {
        out_buf[i] = ssi263_render_sample(ssi);
    }
}
