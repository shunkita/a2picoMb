/*
 * Copyright (c) 2024-2026 Robert Elmer
 * Copyright (c) 2026 a2picoMb
 *
 * Based on https://github.com/relmer/Casso
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _SSI263_H
#define _SSI263_H

#include <stdint.h>
#include <stdbool.h>

#ifndef __always_inline
#if defined(__GNUC__) || defined(__clang__)
#define __always_inline inline __attribute__((always_inline))
#else
#define __always_inline inline
#endif
#endif

#ifndef MB_TIME_CRITICAL
#if defined(__arm__) || defined(__thumb__)
#define MB_TIME_CRITICAL(name) __attribute__((section(".time_critical." #name))) name
#else
#define MB_TIME_CRITICAL(name) name
#endif
#endif

#ifndef MB_SEV
#if defined(__arm__) || defined(__thumb__)
#define MB_SEV() __asm__ volatile("sev" ::: "memory")
#else
#define MB_SEV() ((void)0)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

// SSI-263A Register Offsets (A0..A2: 0..4, mirrored on 5..7)
#define SSI263_REG_DURPHON  0x00 // Reg 0: Duration (bits 7:6) and Phoneme (bits 5:0)
#define SSI263_REG_INFLECT  0x01 // Reg 1: Inflection low (bits 7:0)
#define SSI263_REG_RATEINF  0x02 // Reg 2: Rate (bits 7:4) and Inflection high (bits 3:0)
#define SSI263_REG_CTRLAMP  0x03 // Reg 3: Control/Power-Down (bit 7), Articulation (bits 6:4), Amplitude (bits 3:0)
#define SSI263_REG_FILFREQ  0x04 // Reg 4: Filter Frequency (bits 7:0)

#define SSI263_NUM_REGS     5
#define SSI263_PHONEME_COUNT 64

// A/R Status bit (bit 7 of register read output)
#define SSI263_STATUS_AR    0x80

// Nominal XCK time base (Apple II Phi2 = 1.022727 MHz on Mockingboard)
#define SSI263_XCK_HZ       1022727.0f

// Lock-free SPSC write queue (Core 1 producer -> Core 0 consumer).
// Capacity must be a power of two.
#define SSI263_QUEUE_CAPACITY 64

typedef struct {
    uint8_t reg;
    uint8_t val;
} ssi263_event_t;

typedef struct {
    // --- Shared with Core 1 (bus side) ---
    // Single-byte read cache. Written only by Core 0 (and provisional busy by Core 1), read by Core 1.
    volatile uint8_t read_cache[8];

    // SPSC ring: head is written only by Core 1, tail only by Core 0.
    ssi263_event_t    events[SSI263_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;   // Core 1 only: events dropped on a full queue

    // Reset handshake: reset_req is written only by Core 1, reset_ack only by Core 0.
    volatile uint32_t reset_req;
    volatile uint32_t reset_ack;

    // CA1 pin publication (Core 0 -> Core 1). Single word written only by Core 0:
    //   bit 0     : current level of the A/!R pin wired to VIA #1 CA1
    //   bits 31:1 : transition sequence number
    // Core 1 replays missed transitions with via6522 CA1 edge handling.
    volatile uint32_t ca1_word;

    // --- Owned by Core 0 (authoritative state, updated while draining) ---
    uint8_t  regs[SSI263_NUM_REGS];
    bool     ar_state;          // true = ready / request (D7 = 1, A/!R pin Low), false = busy
    bool     power_down;        // REG3 bit 7 (control / power-down)
    bool     phoneme_active;    // A phoneme is being timed
    uint8_t  mode;              // Latched DR1:DR0 operation mode (0 = A/R strobe disabled)
    uint64_t phoneme_end_us;    // Absolute completion time of the active phoneme
    uint32_t phoneme_dur_samples; // Total sample count of active phoneme
    uint32_t phoneme_sample_pos;  // Sample offset within active phoneme
    uint32_t events_processed;  // Diagnostic counter

    // --- Owned by Core 0 (float DSP synthesizer state) ---
    uint32_t sample_rate;
    float    source_pole;
    float    level_coef;
    float    attack_coef;
    float    release_coef;
    float    art_coef[8];
    float    res_r[3];
    float    nasal_f2_r;
    float    fric_r;
    float    fric_f5_r;

    float    f_cur[3];
    float    res_y1[3];
    float    res_y2[3];
    float    env_level;

    float    glottal_phase;
    float    pitch_trans_cur;
    int32_t  pitch_trans_target;
    float    exc_lp1;
    float    exc_lp2;
    uint32_t lfsr;
    float    noise_lp;

    float    va_cur;
    float    fa_cur;

    float    fric_lp;
    float    fric_lp2;
    float    fric_lp3;
    float    fric_y1;
    float    fric_y2;
    float    fric_f5_y1;
    float    fric_f5_y2;

    float    rad_prev;
    float    out_lp;
    float    out_lp2;
} ssi263_t;

// Initialize state. Call once before the Core 1 bus loop starts.
void ssi263_init(ssi263_t *ssi);

// Core 1: request a reset (non-blocking). Core 0 applies it on the next drain
// and discards any events queued before the request.
void ssi263_reset(ssi263_t *ssi);

// Core 1: fast inline read. A single volatile byte load from the read cache.
static __always_inline uint8_t ssi263_read(const ssi263_t *ssi, uint8_t reg) {
    return ssi->read_cache[reg & 0x07];
}

// Core 1: non-blocking, lock-free write. Queues the event; on a full queue
// increments overflow_count and returns immediately. A REG0 write that will
// start a phoneme also clears the A/R bit in the read cache at once.
void ssi263_write(ssi263_t *ssi, uint8_t reg, uint8_t data);

// Core 0: pop one queued event. Returns false when the queue is empty.
bool ssi263_pop_event(ssi263_t *ssi, ssi263_event_t *ev);

// Core 0: drain the queue, update authoritative state and refresh the read cache.
// now_us is a monotonic microsecond timestamp supplied by the caller.
void ssi263_process_events(ssi263_t *ssi, uint64_t now_us);

// Core 0: advance phoneme timing. On expiry A/R becomes ready, the read cache is
// refreshed and the CA1 pin transition is published.
void ssi263_tick(ssi263_t *ssi, uint64_t now_us);

// Core 0: directly inject a register write from Core 0 (bypasses Core 1 SPSC queue).
void ssi263_inject_reg(ssi263_t *ssi, uint8_t reg, uint8_t data, uint64_t now_us);

// Core 0: set A/R state (true = ready). Publishes a CA1 pin transition on change.
void ssi263_set_ar(ssi263_t *ssi, bool ready);

// Core 0: configure synthesizer sample rate and precompute filter coefficients.
void ssi263_set_sample_rate(ssi263_t *ssi, uint32_t sample_rate);

// Core 0: returns true when synthesizer output is silent and DSP can be skipped.
bool ssi263_is_silent(const ssi263_t *ssi);

// Core 0: render one mono float sample in [-1.0f, 1.0f].
float ssi263_render_sample(ssi263_t *ssi);

// Core 0: render a block of mono float samples.
void ssi263_render_samples(ssi263_t *ssi, float *out_buf, int num_samples);

// Phoneme duration in microseconds based on SSI-263A XCK (1.7897725 MHz):
//   Frame Duration   = 4096 * (16 - RATE) / XCK
//   Phoneme Duration = Frame Duration * (mode == 1 ? 1 : 4 - DUR)
static __always_inline uint32_t ssi263_phoneme_duration_us(uint8_t reg0, uint8_t reg2, uint8_t mode) {
    uint32_t dur = (reg0 >> 6) & 0x03;
    uint32_t rate = (reg2 >> 4) & 0x0F;
    uint32_t mult = (mode == 1) ? 1u : (4u - dur);
    float us = (4096.0f * (float)(16u - rate) * (float)mult * 1000000.0f) / SSI263_XCK_HZ;
    return (uint32_t)us;
}

#ifdef __cplusplus
}
#endif

#endif // _SSI263_H
