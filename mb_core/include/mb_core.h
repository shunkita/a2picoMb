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

#ifndef _MB_CORE_H
#define _MB_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(__arm__) || defined(__thumb__)
#include <hardware/sync.h>
#else
#define __dmb() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

#include "via6522.h"
#include "ayumi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MB_NUM_VIAS   2
#define MB_NUM_CHIPS  2
#define MB_NUM_QUEUES 4

#define AUDIO_QUEUE_CAPACITY 256

// Audio event types
typedef enum {
    AY_EVENT_REGISTER_WRITE = 0,
    AY_EVENT_RESET          = 1
} ay_event_type_t;

// Audio event representation
typedef struct {
    uint8_t type;
    uint8_t reg;
    uint8_t value;
} ay_event_t;

// Single-Producer Single-Consumer lock-free ring buffer
typedef struct {
    ay_event_t buffer[AUDIO_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t high_water_mark;
} audio_queue_t;

// Diagnostic data structure for Mockingboard state & IRQ inspection
typedef struct {
    volatile uint32_t t1_underflow_count[2];
    volatile uint32_t t1_clear_read_t1cl[2];
    volatile uint32_t t1_clear_write_t1ch[2];
    volatile uint32_t t1_clear_write_t1lh[2];
    volatile uint32_t t1_clear_write_ifr[2];
    volatile uint32_t irq_assert_count;
    volatile uint32_t irq_deassert_count;
    volatile uint8_t  last_ifr[2];
    volatile uint8_t  last_ier[2];
    volatile bool     irq_line_asserted;
} mb_diag_t;

extern mb_diag_t mb_diag;
extern via6522_t mb_vias[MB_NUM_VIAS];
extern audio_queue_t mb_audio_queues[MB_NUM_QUEUES];

// ============================================================================
// Core 1 (Bus & Emulation Engine) APIs
// ============================================================================

// Initialize Mockingboard core state (VIAs, AY registers, audio event queues)
void mb_core_init(void);

// Reset Mockingboard core state on Apple II bus reset
void mb_core_reset(void);

// Fast inline bus read (Apple II slot I/O read: $Cn00-$CnFF)
// Kept strictly inlined on the critical path to meet the 350ns timing budget.
static __always_inline uint8_t mb_core_bus_read(uint8_t offset) {
    int via_idx = (offset >> 7) & 1;
    uint8_t reg = offset & 0x0F;
    via6522_t *target_via = &mb_vias[via_idx];
    return via6522_read(target_via, reg);
}

// Post-read accounting (executed after a2pico_putdata to keep critical path minimal)
static __always_inline void mb_core_bus_read_post(uint8_t offset) {
    int via_idx = (offset >> 7) & 1;
    uint8_t reg = offset & 0x0F;
    if (reg == REG_T1CL) {
        mb_diag.t1_clear_read_t1cl[via_idx]++;
    }
}

// Bus write (Apple II slot I/O write: $Cn00-$CnFF)
void mb_core_bus_write(uint8_t offset, uint8_t data);

// Check if IRQ line should currently be asserted
static __always_inline bool mb_core_get_irq(void) {
    return via6522_get_irq(&mb_vias[0]) || via6522_get_irq(&mb_vias[1]);
}

// Update IRQ state and return true if IRQ line assertion changed
static __always_inline bool mb_core_update_irq(void) {
    bool assert_irq = mb_core_get_irq();

    mb_diag.last_ifr[0] = mb_vias[0].ifr;
    mb_diag.last_ier[0] = mb_vias[0].ier;
    mb_diag.last_ifr[1] = mb_vias[1].ifr;
    mb_diag.last_ier[1] = mb_vias[1].ier;

    if (assert_irq != mb_diag.irq_line_asserted) {
        if (assert_irq) {
            mb_diag.irq_assert_count++;
        } else {
            mb_diag.irq_deassert_count++;
        }
        mb_diag.irq_line_asserted = assert_irq;
        return true;
    }
    return false;
}

// Underflow handlers for timers (called when carry accumulator signals underflow)
void mb_core_handle_t1_underflow(int via_idx, bool *irq_updated);
void mb_core_handle_t2_underflow(int via_idx, bool *irq_updated);

// Phi0 1 MHz clock tick handler for 2 VIAs (invoked on Core 1)
// Returns true if IRQ line state changed (caller should update physical IRQ pin)
static __always_inline bool mb_core_tick_phi0(void) {
    uint32_t ev1;
    uint32_t ev2;
    uint32_t tmp;
    uint32_t cnt;
    uint32_t mask = TIMER_T1_RUNNING;
    via6522_t *ptr = mb_vias;

    // Timer 1: Branchless Carry Accumulator
    __asm__ volatile(
        "movs   %[ev], #0\n\t"
        // --- VIA 0 ---
        "ldrb   %[tmp], [%[ptr], #17]\n\t"
        "ands   %[tmp], %[mask]\n\t"
        "ldrh   %[cnt], [%[ptr], #14]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #14]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- VIA 1 ---
        "adds   %[ptr], #24\n\t"
        "ldrb   %[tmp], [%[ptr], #17]\n\t"
        "ands   %[tmp], %[mask]\n\t"
        "ldrh   %[cnt], [%[ptr], #14]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #14]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- Restore ptr ---
        "subs   %[ptr], #24\n\t"
        : [ev] "=&l"(ev1), [tmp] "=&l"(tmp), [cnt] "=&l"(cnt), [ptr] "+l"(ptr)
        : [mask] "l"(mask)
        : "cc", "memory");

    // Timer 2: Branchless Carry Accumulator
    __asm__ volatile(
        "movs   %[ev], #0\n\t"
        // --- VIA 0 ---
        "ldrh   %[cnt], [%[ptr], #20]\n\t"
        "subs   %[cnt], %[cnt], #1\n\t"
        "strh   %[cnt], [%[ptr], #20]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- VIA 1 ---
        "adds   %[ptr], #24\n\t"
        "ldrh   %[cnt], [%[ptr], #20]\n\t"
        "subs   %[cnt], %[cnt], #1\n\t"
        "strh   %[cnt], [%[ptr], #20]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- Restore ptr ---
        "subs   %[ptr], #24\n\t"
        : [ev] "=&l"(ev2), [cnt] "=&l"(cnt), [ptr] "+l"(ptr)
        :
        : "cc", "memory");

    bool irq_updated = false;
    if (__builtin_expect(ev1 != 0x03, 0)) {
        uint32_t uf1 = (~ev1) & 0x03;
        if (uf1 & 0x02) {
            mb_core_handle_t1_underflow(0, &irq_updated);
        }
        if (uf1 & 0x01) {
            mb_core_handle_t1_underflow(1, &irq_updated);
        }
    }
    if (__builtin_expect(ev2 != 0x03, 0)) {
        uint32_t uf2 = (~ev2) & 0x03;
        if (uf2 & 0x02) {
            mb_core_handle_t2_underflow(0, &irq_updated);
        }
        if (uf2 & 0x01) {
            mb_core_handle_t2_underflow(1, &irq_updated);
        }
    }

    if (irq_updated) {
        return mb_core_update_irq();
    }
    return false;
}

// Queue operations (internal inline helpers)
static inline void audio_queue_init(audio_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->overflow_count = 0;
    q->high_water_mark = 0;
}

static __always_inline bool audio_queue_push(audio_queue_t *q, uint8_t reg, uint8_t value) {
    if ((q->head - q->tail) >= AUDIO_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].type = AY_EVENT_REGISTER_WRITE;
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].reg = reg;
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].value = value;
    __dmb();
    q->head++;
    return true;
}

static __always_inline bool audio_queue_push_reset(audio_queue_t *q) {
    if ((q->head - q->tail) >= AUDIO_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].type = AY_EVENT_RESET;
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].reg = 0;
    q->buffer[q->head & (AUDIO_QUEUE_CAPACITY - 1)].value = 0;
    __dmb();
    q->head++;
    return true;
}

static inline bool audio_queue_pop(audio_queue_t *q, ay_event_t *ev) {
    if (q->head == q->tail) {
        return false;
    }
    *ev = q->buffer[q->tail & (AUDIO_QUEUE_CAPACITY - 1)];
    __dmb();
    q->tail++;
    return true;
}

// ============================================================================
// Internal Audio Synthesis Prototypes (ay_audio.c)
// ============================================================================
void ay_audio_init(void);
void ay_audio_produce_pcm(int16_t *pcm_buffer, int num_samples);
void ay_audio_write_reg(uint8_t chip_index, uint8_t reg, uint8_t value);
void ay_audio_reset(uint8_t chip_index);
void ay_audio_handle_event(uint8_t chip_index, const ay_event_t *ev);
uint32_t ay_audio_get_pcm_calls(void);
int16_t ay_audio_get_last_max_amp(void);
uint8_t ay_audio_get_reg(uint8_t chip_index, uint8_t reg);
void ay_audio_get_and_reset_pcm_stats(uint32_t *pcm_samples, int16_t *min_val, int16_t *max_val,
                                      uint32_t *silent_samples, uint32_t *max_silent_run);
uint32_t ay_audio_get_last_pcm_us(void);
void ay_audio_trigger_timing_sample(void);

// ============================================================================
// Core 0 (Audio Synthesis & Task) APIs
// ============================================================================

// Initialize Core 0 audio synthesis subsystem
void mb_core_audio_init(void);

// Periodic task for Core 0: drains event queues and updates AYUMI state
void mb_core_task(void);

// Produce 16-bit stereo PCM audio samples (44.1 kHz)
void mb_core_produce_pcm(int16_t *pcm_buffer, int num_samples);

// Diagnostic getters
uint32_t mb_core_get_pcm_calls(void);
uint8_t mb_core_get_ay_reg(uint8_t chip_index, uint8_t reg);
void mb_core_trigger_timing_sample(void);
uint32_t mb_core_get_last_pcm_us(void);
void mb_core_get_and_reset_pcm_stats(uint32_t *pcm_samples, int16_t *min_val, int16_t *max_val,
                                     uint32_t *silent_samples, uint32_t *max_silent_run);
uint32_t mb_core_get_audio_event_count(uint8_t queue_idx);

#ifdef __cplusplus
}
#endif

#endif // _MB_CORE_H

