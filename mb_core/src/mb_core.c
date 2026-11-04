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

#include "mb_core.h"
#include <string.h>

// Diagnostic state for Mockingboard state & IRQ inspection
mb_diag_t mb_diag = {0};

// 2 VIA instances (Mockingboard #0):
// mb_vias[0]: VIA #0 ($Cn00-$Cn0F)
// mb_vias[1]: VIA #1 ($Cn80-$Cn8F)
via6522_t mb_vias[MB_NUM_VIAS];

_Static_assert(sizeof(via6522_t) == 24, "via6522_t size must be 24 bytes");
_Static_assert(offsetof(via6522_t, t1_counter) == 14, "t1_counter offset must be 14");
_Static_assert(offsetof(via6522_t, timer_running) == 17, "timer_running offset must be 17");
_Static_assert(offsetof(via6522_t, t2_counter) == 20, "t2_counter offset must be 20");
_Static_assert(offsetof(via6522_t, t2_armed) == 23, "t2_armed offset must be 23");

// Audio Event Queues (Core 1 -> Core 0)
// Currently MB#0 uses queues 0 and 1; 2 and 3 reserved for expansion
audio_queue_t mb_audio_queues[MB_NUM_QUEUES];

static uint32_t audio_event_count[MB_NUM_QUEUES] = {0};

// AY register address latch state for each VIA (R0-R15)
static uint8_t ay_selected_reg[MB_NUM_VIAS];

// AY-3-8913 register bit masks (R0-R15)
static const uint8_t ay_reg_mask[16] = {
    0xFF, // R0: Channel A Tone Period Fine (8-bit)
    0x0F, // R1: Channel A Tone Period Coarse (4-bit)
    0xFF, // R2: Channel B Tone Period Fine (8-bit)
    0x0F, // R3: Channel B Tone Period Coarse (4-bit)
    0xFF, // R4: Channel C Tone Period Fine (8-bit)
    0x0F, // R5: Channel C Tone Period Coarse (4-bit)
    0x1F, // R6: Noise Period (5-bit)
    0xFF, // R7: Enable / Mixer (8-bit)
    0x1F, // R8: Channel A Amplitude / Mode (5-bit)
    0x1F, // R9: Channel B Amplitude / Mode (5-bit)
    0x1F, // R10: Channel C Amplitude / Mode (5-bit)
    0xFF, // R11: Envelope Period Fine (8-bit)
    0xFF, // R12: Envelope Period Coarse (8-bit)
    0x0F, // R13: Envelope Shape / Cycle (4-bit)
    0x00, // R14: I/O Port A (not connected on AY-3-8913)
    0x00  // R15: I/O Port B (not connected on AY-3-8913)
};

// AY register shadow state for each VIA (2 VIAs x 16 registers)
static uint8_t ay_regs[MB_NUM_VIAS][16];

static __always_inline void handle_ay_orb_write(via6522_t *target_via, uint8_t data) {
    uint8_t ctrl = data & 0x07;
    int via_idx = target_via - mb_vias;

    if (ctrl == 0x07) { // LATCH
        ay_selected_reg[via_idx] = target_via->ora & 0x0F;
    } else if (ctrl == 0x06) { // WRITE
        uint8_t reg = ay_selected_reg[via_idx];
        ay_regs[via_idx][reg] = target_via->ora & ay_reg_mask[reg];
        audio_queue_push(&mb_audio_queues[via_idx], reg, target_via->ora);
    } else if (ctrl == 0x05) { // READ
        uint8_t reg = ay_selected_reg[via_idx];
        target_via->in_a = ay_regs[via_idx][reg];
    } else if (ctrl == 0x04) { // INACTIVE
        target_via->in_a = 0xFF;
    } else if (ctrl == 0x00) { // RESET
        for (int r = 0; r < 16; r++) {
            ay_regs[via_idx][r] = 0;
        }
        target_via->in_a = 0xFF;
        audio_queue_push_reset(&mb_audio_queues[via_idx]);
    }
}

void mb_core_init(void) {
    for (int i = 0; i < MB_NUM_VIAS; i++) {
        via6522_init(&mb_vias[i]);
        ay_selected_reg[i] = 0;
        for (int r = 0; r < 16; r++) {
            ay_regs[i][r] = 0;
        }
    }
    for (int i = 0; i < MB_NUM_QUEUES; i++) {
        audio_queue_init(&mb_audio_queues[i]);
    }
}

void mb_core_reset(void) {
    mb_diag.irq_line_asserted = false;
    for (int i = 0; i < MB_NUM_VIAS; i++) {
        via6522_reset(&mb_vias[i]);
        ay_selected_reg[i] = 0;
        for (int r = 0; r < 16; r++) {
            ay_regs[i][r] = 0;
        }
        audio_queue_push_reset(&mb_audio_queues[i]);
    }
}

void mb_core_bus_write(uint8_t offset, uint8_t data) {
    int via_idx = (offset >> 7) & 1;
    via6522_t *target_via = &mb_vias[via_idx];
    uint8_t reg = offset & 0x0F;

    if (reg == REG_T1CH && (target_via->ifr & 0x40)) {
        mb_diag.t1_clear_write_t1ch[via_idx]++;
    } else if (reg == REG_T1LH && (target_via->ifr & 0x40)) {
        mb_diag.t1_clear_write_t1lh[via_idx]++;
    } else if (reg == REG_IFR && (data & 0x40) && (target_via->ifr & 0x40)) {
        mb_diag.t1_clear_write_ifr[via_idx]++;
    }

    via6522_write(target_via, reg, data);

    if (reg == REG_ORB) {
        handle_ay_orb_write(target_via, data);
    }
}

void mb_core_handle_t1_underflow(int via_idx, bool *irq_updated) {
    via6522_t *via = &mb_vias[via_idx];
    mb_diag.t1_underflow_count[via_idx]++;

    if (via->acr & 0x40) {
        via->ifr |= 0x40;
        *irq_updated = true;
    } else {
        if (!via->t1_underflowed) {
            via->ifr |= 0x40;
            via->t1_underflowed = true;
            *irq_updated = true;
        }
    }
    via->t1_counter = via->t1_latch;
}

void mb_core_handle_t2_underflow(int via_idx, bool *irq_updated) {
    via6522_t *via = &mb_vias[via_idx];
    if (via->t2_armed) {
        via->ifr |= 0x20;
        via->t2_armed = 0;
        *irq_updated = true;
    }
}

// ============================================================================
// Core 0 (Audio & Task) Implementations
// ============================================================================

void mb_core_audio_init(void) {
    ay_audio_init();
}

void mb_core_task(void) {
    for (int q = 0; q < MB_NUM_QUEUES; q++) {
        audio_queue_t *queue = &mb_audio_queues[q];
        uint32_t used = queue->head - queue->tail;
        if (used > queue->high_water_mark) {
            queue->high_water_mark = used;
        }

        ay_event_t ev;
        while (audio_queue_pop(queue, &ev)) {
            audio_event_count[q]++;
            ay_audio_handle_event(q, &ev);
        }
    }
}

void mb_core_produce_pcm(int16_t *pcm_buffer, int num_samples) {
    ay_audio_produce_pcm(pcm_buffer, num_samples);
}

uint32_t mb_core_get_pcm_calls(void) {
    return ay_audio_get_pcm_calls();
}

uint8_t mb_core_get_ay_reg(uint8_t chip_index, uint8_t reg) {
    return ay_audio_get_reg(chip_index, reg);
}

void mb_core_trigger_timing_sample(void) {
    ay_audio_trigger_timing_sample();
}

uint32_t mb_core_get_last_pcm_us(void) {
    return ay_audio_get_last_pcm_us();
}

void mb_core_get_and_reset_pcm_stats(uint32_t *pcm_samples, int16_t *min_val, int16_t *max_val,
                                     uint32_t *silent_samples, uint32_t *max_silent_run) {
    ay_audio_get_and_reset_pcm_stats(pcm_samples, min_val, max_val, silent_samples, max_silent_run);
}

uint32_t mb_core_get_audio_event_count(uint8_t queue_idx) {
    if (queue_idx >= MB_NUM_QUEUES) return 0;
    return audio_event_count[queue_idx];
}
