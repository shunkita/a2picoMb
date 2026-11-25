/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "usb_dev_selftest.h"
#include "mb_core.h"

#include <hardware/gpio.h>
#include <hardware/pio.h>
#include <hardware/structs/busctrl.h>
#include <pico/multicore.h>
#include <pico/time.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Duration of each self-test step (~2 seconds) and Step 1C L/R toggle period */
#define SELFTEST_STEP_DURATION_MS   2000u
#define SELFTEST_ALT_PERIOD_MS      250u
#define SELFTEST_STAGE2A_GATE_MS    1850u

/* Direct 48 kHz tone parameters for Stage 0 (bypassing mb_core) */
#define SELFTEST_USB_RATE_HZ        48000u
#define SELFTEST_DIRECT_CHUNK       80u
#define SELFTEST_TONE_AMP           10000.0f
#define SELFTEST_PHASE_440HZ        ((uint32_t)(((uint64_t)440u << 32) / SELFTEST_USB_RATE_HZ))
#define SELFTEST_PHASE_880HZ        ((uint32_t)(((uint64_t)880u << 32) / SELFTEST_USB_RATE_HZ))

/* Mockingboard slot I/O offsets ($Cn00-$CnFF) */
#define MB_VIA0_BASE                0x00u
#define MB_VIA1_BASE                0x80u
#define MB_SSI_BASE                 0x40u

/* AY-3-8913 tone periods for 1.022727 MHz clock: period = round(Fclk / (16 * F)) */
#define AY_PERIOD_440HZ             0x91u /* 145 -> 440.8 Hz */
#define AY_PERIOD_660HZ             0x61u /*  97 -> 659.0 Hz */

/* Lock-free SPSC log ring buffer (Thread Mode producer -> Tier 2 USB IRQ consumer) */
#define SELFTEST_LOG_SLOTS          8u
#define SELFTEST_LOG_LINE_MAX       144u

typedef enum {
    SELFTEST_STEP_0A_DIRECT_USB = 0,
    SELFTEST_STEP_1A_VIA0_LEFT,
    SELFTEST_STEP_1B_VIA1_RIGHT,
    SELFTEST_STEP_1C_VIA_ALT_LR,
    SELFTEST_STEP_2A_SSI_VOWEL,
    SELFTEST_STEP_2B_SSI_HELLO,
    SELFTEST_STEP_COUNT
} selftest_step_t;

typedef struct {
    uint8_t durphon;
    uint8_t inflect;
} selftest_phoneme_t;

/* 5-phoneme "HELLO" sequence (HF - EH - L - OU - U) at Rate=7 (~36 ms/frame) */
static const selftest_phoneme_t s_hello_phonemes[5] = {
    { 0xACu, 0x6Eu }, /* 0: HF (0x2C, DUR=2 -> 2 frames) */
    { 0x4Au, 0x6Eu }, /* 1: EH (0x0A, DUR=1 -> 3 frames) */
    { 0x60u, 0x76u }, /* 2: L  (0x20, DUR=1 -> 3 frames) */
    { 0x12u, 0x76u }, /* 3: OU (0x12, DUR=0 -> 4 frames) */
    { 0x56u, 0x66u }, /* 4: U  (0x16, DUR=1 -> 3 frames) */
};

static int16_t s_sine_lut[256];

/* State owned exclusively by Core 0 Thread Mode */
static bool s_started = false;
static volatile selftest_step_t s_step = SELFTEST_STEP_0A_DIRECT_USB;
static uint32_t s_cycle = 0;
static uint32_t s_step_start_ms = 0;
static uint32_t s_phase_l = 0;
static uint32_t s_phase_r = 0;
static uint32_t s_alt_slot = 0;
static bool s_stage2a_gated = false;
static uint8_t s_hello_idx = 0;
static bool s_hello_done = false;
static uint32_t s_hello_ca1_polls = 0;

/* Static scratch line avoids Thread Mode stack pressure */
static char s_fmt_buf[SELFTEST_LOG_LINE_MAX];

/* Lock-free SPSC log ring buffer */
static char s_log_ring[SELFTEST_LOG_SLOTS][SELFTEST_LOG_LINE_MAX];
static volatile uint32_t s_log_head = 0;
static volatile uint32_t s_log_tail = 0;

static void queue_log_line(const char *line) {
    uint32_t head = s_log_head;
    uint32_t tail = s_log_tail;
    if ((uint32_t)(head - tail) >= SELFTEST_LOG_SLOTS) {
        /* Drop oldest check only on consumer side; skip push if ring is full */
        return;
    }

    uint32_t slot = head & (SELFTEST_LOG_SLOTS - 1u);
    strncpy(s_log_ring[slot], line, SELFTEST_LOG_LINE_MAX - 1u);
    s_log_ring[slot][SELFTEST_LOG_LINE_MAX - 1u] = '\0';
    __dmb();
    s_log_head = head + 1u;
}

static inline int16_t lut_sine_sample(uint32_t phase) {
    uint32_t idx = phase >> 24;
    uint32_t next = (idx + 1u) & 0xFFu;
    int32_t frac = (int32_t)((phase >> 16) & 0xFFu);
    int32_t s0 = s_sine_lut[idx];
    int32_t s1 = s_sine_lut[next];
    return (int16_t)(s0 + (((s1 - s0) * frac) >> 8));
}

/* Simulated Mockingboard VIA6522 -> AY-3-8913 bus helpers */
static void sim_via_ay_write_reg(uint8_t base, uint8_t reg, uint8_t val) {
    mb_core_bus_write((uint8_t)(base + REG_DDRB), 0xFFu);
    mb_core_bus_write((uint8_t)(base + REG_DDRA), 0xFFu);
    mb_core_bus_write((uint8_t)(base + REG_ORA), reg);
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x07u); /* LATCH */
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x04u); /* INACTIVE */
    mb_core_bus_write((uint8_t)(base + REG_ORA), val);
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x06u); /* WRITE */
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x04u); /* INACTIVE */
}

static uint8_t sim_via_ay_read_reg(uint8_t base, uint8_t reg) {
    mb_core_bus_write((uint8_t)(base + REG_DDRB), 0xFFu);
    mb_core_bus_write((uint8_t)(base + REG_DDRA), 0xFFu);
    mb_core_bus_write((uint8_t)(base + REG_ORA), reg);
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x07u); /* LATCH */
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x04u); /* INACTIVE */
    mb_core_bus_write((uint8_t)(base + REG_DDRA), 0x00u); /* Port A input */
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x05u); /* READ */
    uint8_t val = mb_core_bus_read((uint8_t)(base + REG_ORA));
    mb_core_bus_write((uint8_t)(base + REG_ORB), 0x04u); /* INACTIVE */
    mb_core_bus_write((uint8_t)(base + REG_DDRA), 0xFFu); /* Restore Port A output */
    return val;
}

static void sim_silence_ay(uint8_t base) {
    sim_via_ay_write_reg(base, 8u, 0x00u);
    sim_via_ay_write_reg(base, 9u, 0x00u);
    sim_via_ay_write_reg(base, 10u, 0x00u);
    sim_via_ay_write_reg(base, 7u, 0x3Fu);
}

static void sim_silence_all(void) {
    sim_silence_ay(MB_VIA0_BASE);
    sim_silence_ay(MB_VIA1_BASE);
    ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0xC0u);
    ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x80u);
    mb_core_task();
    (void)mb_core_ssi263_sync_ca1();
}

static void sim_setup_via1_ca1(void) {
    mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_PCR), 0x00u); /* CA1 negative active edge */
    mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_IER), 0x7Fu); /* Disable all VIA #1 IRQs */
    mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_IER), 0x82u); /* Enable CA1 in IER */
    mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_IFR), 0x02u); /* Clear pending CA1 flag */
}

static void enter_step(selftest_step_t step, uint32_t now_ms) {
    s_step_start_ms = now_ms;

    switch (step) {
    case SELFTEST_STEP_0A_DIRECT_USB:
        sim_silence_all();
        s_phase_l = 0;
        s_phase_r = 0;
        s_step = step;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=0 step=0A mode=DIRECT_USB desc=\"440Hz_L 880Hz_R -> ringbuf\"\r\n",
                 (unsigned long)s_cycle);
        queue_log_line(s_fmt_buf);
        break;

    case SELFTEST_STEP_1A_VIA0_LEFT: {
        sim_silence_all();
        sim_via_ay_write_reg(MB_VIA0_BASE, 0u, AY_PERIOD_440HZ);
        sim_via_ay_write_reg(MB_VIA0_BASE, 1u, 0x00u);
        sim_via_ay_write_reg(MB_VIA0_BASE, 7u, 0x3Eu);
        sim_via_ay_write_reg(MB_VIA0_BASE, 8u, 0x0Fu);
        bool rb_ok = (sim_via_ay_read_reg(MB_VIA0_BASE, 0u) == AY_PERIOD_440HZ) &&
                     (sim_via_ay_read_reg(MB_VIA0_BASE, 8u) == 0x0Fu);
        mb_core_task();
        s_step = step;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=1 step=1A mode=VIA0_PSG0 desc=\"440Hz_L_only\" via0_rb=%u\r\n",
                 (unsigned long)s_cycle, rb_ok ? 1u : 0u);
        queue_log_line(s_fmt_buf);
        break;
    }

    case SELFTEST_STEP_1B_VIA1_RIGHT: {
        sim_silence_all();
        sim_via_ay_write_reg(MB_VIA1_BASE, 0u, AY_PERIOD_660HZ);
        sim_via_ay_write_reg(MB_VIA1_BASE, 1u, 0x00u);
        sim_via_ay_write_reg(MB_VIA1_BASE, 7u, 0x3Eu);
        sim_via_ay_write_reg(MB_VIA1_BASE, 8u, 0x0Fu);
        bool rb_ok = (sim_via_ay_read_reg(MB_VIA1_BASE, 0u) == AY_PERIOD_660HZ) &&
                     (sim_via_ay_read_reg(MB_VIA1_BASE, 8u) == 0x0Fu);
        mb_core_task();
        s_step = step;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=1 step=1B mode=VIA1_PSG1 desc=\"660Hz_R_only\" via1_rb=%u\r\n",
                 (unsigned long)s_cycle, rb_ok ? 1u : 0u);
        queue_log_line(s_fmt_buf);
        break;
    }

    case SELFTEST_STEP_1C_VIA_ALT_LR:
        sim_silence_all();
        sim_via_ay_write_reg(MB_VIA0_BASE, 0u, AY_PERIOD_440HZ);
        sim_via_ay_write_reg(MB_VIA0_BASE, 1u, 0x00u);
        sim_via_ay_write_reg(MB_VIA0_BASE, 7u, 0x3Eu);
        sim_via_ay_write_reg(MB_VIA0_BASE, 8u, 0x0Fu);

        sim_via_ay_write_reg(MB_VIA1_BASE, 0u, AY_PERIOD_660HZ);
        sim_via_ay_write_reg(MB_VIA1_BASE, 1u, 0x00u);
        sim_via_ay_write_reg(MB_VIA1_BASE, 7u, 0x3Fu);
        sim_via_ay_write_reg(MB_VIA1_BASE, 8u, 0x00u);
        s_alt_slot = 0;
        mb_core_task();
        s_step = step;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=1 step=1C mode=VIA_ALT_LR desc=\"alt_250ms_440L_660R\" ay_ev=%lu/%lu\r\n",
                 (unsigned long)s_cycle,
                 (unsigned long)mb_core_get_audio_event_count(0),
                 (unsigned long)mb_core_get_audio_event_count(1));
        queue_log_line(s_fmt_buf);
        break;

    case SELFTEST_STEP_2A_SSI_VOWEL: {
        sim_silence_all();
        sim_setup_via1_ca1();

        /* SSI-263A power-down/init sequence ($43=$80, $40=$C0, $43=$70) */
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x80u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0xC0u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x70u);

        /* Trigger sustained vowel phoneme burst ($44..$40: AH1 "father" centered) */
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_FILFREQ), 0xE6u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x5Fu);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_RATEINF), 0x8Eu);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_INFLECT), 0x6Eu);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0x0Fu);

        s_stage2a_gated = false;
        mb_core_task();
        (void)mb_core_ssi263_sync_ca1();
        s_step = step;

        uint32_t ca1_cnt = __atomic_load_n(&mb_ssi263.ca1_word, __ATOMIC_RELAXED) >> 1;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=2 step=2A mode=SSI263_VOWEL desc=\"init+AH1_center\" ssi_ev=%lu ca1=%lu\r\n",
                 (unsigned long)s_cycle,
                 (unsigned long)mb_ssi263.events_processed,
                 (unsigned long)ca1_cnt);
        queue_log_line(s_fmt_buf);
        break;
    }

    case SELFTEST_STEP_2B_SSI_HELLO:
    default: {
        sim_silence_all();
        sim_setup_via1_ca1();

        /* SSI-263A power-down/init sequence ($43=$80, $40=$C0, $43=$70) */
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x80u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0xC0u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x70u);

        /* Configure speech parameters and prime phoneme 0 of "HELLO" */
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_FILFREQ), 0xE6u);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x5Fu);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_RATEINF), 0x7Eu);
        mb_core_task();
        (void)mb_core_ssi263_sync_ca1();
        mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_IFR), 0x02u);

        s_hello_idx = 0;
        s_hello_done = false;
        s_hello_ca1_polls = 0;
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_INFLECT), s_hello_phonemes[0].inflect);
        ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), s_hello_phonemes[0].durphon);
        mb_core_task();
        (void)mb_core_ssi263_sync_ca1();
        s_step = step;

        uint32_t ca1_cnt = __atomic_load_n(&mb_ssi263.ca1_word, __ATOMIC_RELAXED) >> 1;
        snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                 "[SELFTEST] cycle=%lu stage=2 step=2B mode=SSI263_HELLO desc=\"5phoneme_VIA1_CA1\" ssi_ev=%lu ca1=%lu\r\n",
                 (unsigned long)s_cycle,
                 (unsigned long)mb_ssi263.events_processed,
                 (unsigned long)ca1_cnt);
        queue_log_line(s_fmt_buf);
        break;
    }
    }
}

void usb_dev_selftest_init(void) {
    /*
     * Standalone A2Pico2 is detached from the Apple II slot: floating /RESET
     * (GPIO 27) and /ENBL-/IOSEL (GPIO 26..28) pins on Core 1 would otherwise
     * trigger spurious mb_core_reset() calls and corrupt SPSC audio queues.
     * Park Core 1 and disable bus PIO state machines cleanly for self-test.
     */
    busy_wait_us_32(1000u);
    gpio_set_irq_enabled(27u, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, false);
    multicore_reset_core1();
    pio_set_sm_mask_enabled(pio0, 0x0Fu, false);
    pio_set_sm_mask_enabled(pio1, 0x0Fu, false);
    busctrl_hw->priority = 0u;

    /* Re-initialize mb_core cleanly on Core 0 after isolating Core 1 */
    mb_core_init();
    mb_core_audio_init();

    for (uint32_t i = 0; i < 256u; i++) {
        float rad = (2.0f * 3.14159265358979323846f * (float)i) / 256.0f;
        s_sine_lut[i] = (int16_t)lrintf(SELFTEST_TONE_AMP * sinf(rad));
    }

    s_started = false;
    s_step = SELFTEST_STEP_0A_DIRECT_USB;
    s_cycle = 0;
    s_step_start_ms = 0;
    s_phase_l = 0;
    s_phase_r = 0;
    s_alt_slot = 0;
    s_stage2a_gated = false;
    s_hello_idx = 0;
    s_hello_done = false;
    s_hello_ca1_polls = 0;
    s_log_head = 0;
    s_log_tail = 0;
}

bool usb_dev_selftest_has_pending_log(void) {
    return (s_log_head != s_log_tail);
}

bool usb_dev_selftest_tick(uint32_t now_ms) {
    /* Sync SSI-263A CA1 transitions to VIA #1 IFR when detached from Apple II Phi0 */
    (void)mb_core_ssi263_sync_ca1();

    if (!s_started) {
        s_started = true;
        s_cycle = 0;
        /* Perform one-time hardware VIA/AY reset before starting cyclic steps */
        mb_core_bus_write((uint8_t)(MB_VIA0_BASE + REG_DDRB), 0xFFu);
        mb_core_bus_write((uint8_t)(MB_VIA0_BASE + REG_DDRA), 0xFFu);
        mb_core_bus_write((uint8_t)(MB_VIA0_BASE + REG_ORB), 0x00u);
        mb_core_bus_write((uint8_t)(MB_VIA0_BASE + REG_ORB), 0x04u);
        mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_DDRB), 0xFFu);
        mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_DDRA), 0xFFu);
        mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_ORB), 0x00u);
        mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_ORB), 0x04u);
        mb_core_task();
        enter_step(SELFTEST_STEP_0A_DIRECT_USB, now_ms);
        return true;
    }

    uint32_t elapsed_ms = (uint32_t)(now_ms - s_step_start_ms);
    if (elapsed_ms >= SELFTEST_STEP_DURATION_MS) {
        uint32_t next = (uint32_t)s_step + 1u;
        if (next >= (uint32_t)SELFTEST_STEP_COUNT) {
            next = 0u;
            s_cycle++;
        }
        enter_step((selftest_step_t)next, now_ms);
        return true;
    }

    if (s_step == SELFTEST_STEP_1C_VIA_ALT_LR) {
        uint32_t slot = elapsed_ms / SELFTEST_ALT_PERIOD_MS;
        if (slot != s_alt_slot && slot < 8u) {
            s_alt_slot = slot;
            if ((slot & 1u) == 0u) {
                sim_via_ay_write_reg(MB_VIA0_BASE, 7u, 0x3Eu);
                sim_via_ay_write_reg(MB_VIA0_BASE, 8u, 0x0Fu);
                sim_via_ay_write_reg(MB_VIA1_BASE, 8u, 0x00u);
                sim_via_ay_write_reg(MB_VIA1_BASE, 7u, 0x3Fu);
            } else {
                sim_via_ay_write_reg(MB_VIA0_BASE, 8u, 0x00u);
                sim_via_ay_write_reg(MB_VIA0_BASE, 7u, 0x3Fu);
                sim_via_ay_write_reg(MB_VIA1_BASE, 7u, 0x3Eu);
                sim_via_ay_write_reg(MB_VIA1_BASE, 8u, 0x0Fu);
            }
            mb_core_task();
        }
    } else if (s_step == SELFTEST_STEP_2A_SSI_VOWEL) {
        if (!s_stage2a_gated && elapsed_ms >= SELFTEST_STAGE2A_GATE_MS) {
            s_stage2a_gated = true;
            ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0xC0u);
            ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x80u);
            mb_core_task();
            (void)mb_core_ssi263_sync_ca1();
        }
    } else if (s_step == SELFTEST_STEP_2B_SSI_HELLO && !s_hello_done) {
        s_hello_ca1_polls++;
        uint8_t ifr = mb_core_bus_read((uint8_t)(MB_VIA1_BASE + REG_IFR));
        if ((ifr & 0x02u) != 0u) {
            mb_core_bus_write((uint8_t)(MB_VIA1_BASE + REG_IFR), 0x02u);
            s_hello_idx++;
            if (s_hello_idx < 5u) {
                ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_INFLECT),
                                 s_hello_phonemes[s_hello_idx].inflect);
                ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON),
                                 s_hello_phonemes[s_hello_idx].durphon);
                mb_core_task();
                (void)mb_core_ssi263_sync_ca1();
            } else {
                ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_DURPHON), 0xC0u);
                ssi263_bus_write((uint8_t)(MB_SSI_BASE + SSI263_REG_CTRLAMP), 0x80u);
                mb_core_task();
                (void)mb_core_ssi263_sync_ca1();
                s_hello_done = true;

                uint32_t ca1_cnt = __atomic_load_n(&mb_ssi263.ca1_word, __ATOMIC_RELAXED) >> 1;
                snprintf(s_fmt_buf, sizeof(s_fmt_buf),
                         "[SELFTEST] cycle=%lu stage=2 step=2B_DONE phonemes=5/5 via1_ca1_polls=%lu ssi_ev=%lu ca1=%lu\r\n",
                         (unsigned long)s_cycle,
                         (unsigned long)s_hello_ca1_polls,
                         (unsigned long)mb_ssi263.events_processed,
                         (unsigned long)ca1_cnt);
                queue_log_line(s_fmt_buf);
                return true;
            }
        }
    }

    return (s_log_head != s_log_tail);
}

bool usb_dev_selftest_is_direct_mode(void) {
    return (s_step == SELFTEST_STEP_0A_DIRECT_USB);
}

void usb_dev_selftest_fill_direct_tone(audio_ringbuf_t *rb, uint32_t target_frames) {
    audio_stereo_frame_t frames[SELFTEST_DIRECT_CHUNK];

    while (audio_ringbuf_level(rb) < target_frames &&
           audio_ringbuf_free(rb) >= SELFTEST_DIRECT_CHUNK) {
        for (uint32_t i = 0; i < SELFTEST_DIRECT_CHUNK; i++) {
            frames[i].left = lut_sine_sample(s_phase_l);
            frames[i].right = lut_sine_sample(s_phase_r);
            s_phase_l += SELFTEST_PHASE_440HZ;
            s_phase_r += SELFTEST_PHASE_880HZ;
        }
        (void)audio_ringbuf_write(rb, frames, SELFTEST_DIRECT_CHUNK);
    }
}

void usb_dev_selftest_emit_cdc(usb_dev_cdc_log_fn_t log_fn, uint32_t (*avail_fn)(void)) {
    if (!log_fn || !avail_fn) {
        return;
    }

    while (s_log_tail != s_log_head) {
        __dmb();
        uint32_t slot = s_log_tail & (SELFTEST_LOG_SLOTS - 1u);
        size_t len = strlen(s_log_ring[slot]);
        if (len > 0u) {
            if (avail_fn() < (uint32_t)len) {
                break;
            }
            log_fn("%s", s_log_ring[slot]);
        }
        __dmb();
        s_log_tail++;
    }
}
