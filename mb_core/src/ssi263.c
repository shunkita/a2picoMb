/*
 * Copyright (c) 2024-2026 Robert Elmer
 * Copyright (c) 2026 a2picoMb
 *
 * Based on https://github.com/relmer/Casso
 *
 * SPDX-License-Identifier: MIT
 */

#include "ssi263_internal.h"
#include <string.h>

// 64-phoneme acoustic target table derived from SSI-263P parameter ROM
const ssi263_phoneme_spec_t ssi263_phonemes[SSI263_PHONEME_COUNT] = {
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

void MB_TIME_CRITICAL(ssi263_reset)(ssi263_t *ssi) {
    // Core 1 is the only writer of reset_req.
    uint32_t req = __atomic_load_n(&ssi->reset_req, __ATOMIC_RELAXED);
    __atomic_store_n(&ssi->reset_req, req + 1, __ATOMIC_RELEASE);
    MB_SEV();
}

void MB_TIME_CRITICAL(ssi263_write)(ssi263_t *ssi, uint8_t reg, uint8_t data) {
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
    MB_SEV();
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
    const ssi263_phoneme_spec_t *spec = &ssi263_phonemes[ssi->regs[SSI263_REG_DURPHON] & 0x3F];
    bool has_source = (spec->voiced_level > 0.0f) || (spec->fric_level > 0.0f);
    bool quiet = ssi->power_down || (amp == 0) || !has_source;
    return quiet && (ssi->env_level < 0.001f);
}
