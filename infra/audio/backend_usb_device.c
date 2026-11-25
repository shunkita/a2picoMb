/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "audio_sink.h"
#include "audio_ringbuf.h"
#include "mb_core.h"
#include "tusb.h"
#include "usb_dev_telemetry.h"
#ifdef USB_DEV_SELFTEST
#include "usb_dev_selftest.h"
#endif

#include <hardware/irq.h>
#include <hardware/structs/m33.h>
#include <hardware/sync.h>
#include <pico/time.h>
#include <string.h>

/* 48 kHz UAC2 stereo output parameters */
#define USB_DEV_SAMPLE_RATE_HZ     48000u
#define USB_DEV_SRC_RATE_HZ        44100u

/* 4-byte aligned stereo ring buffer (power of 2 frames, ~42.6 ms @ 48 kHz) */
#define USB_DEV_RINGBUF_FRAMES     2048u

/* Adaptive sample rate pacing thresholds (in stereo frames) */
#define USB_DEV_TARGET_FRAMES      512u
#define USB_DEV_LOW_WATERMARK      384u
#define USB_DEV_HIGH_WATERMARK     640u

#define USB_DEV_PACKET_FRAMES_MIN  47u
#define USB_DEV_PACKET_FRAMES_NOM  48u
#define USB_DEV_PACKET_FRAMES_MAX  49u

/* Resampling chunk size (44.1 kHz input -> 48 kHz output) */
#define USB_DEV_SRC_CHUNK_FRAMES   64u
#define USB_DEV_OUT_CHUNK_FRAMES   80u

/* Q16.16 phase step: 44100 / 48000 = 0.91875 -> 60211 */
#define USB_DEV_PHASE_STEP_Q16     ((uint32_t)(((uint64_t)USB_DEV_SRC_RATE_HZ << 16) / USB_DEV_SAMPLE_RATE_HZ))

/*
 * Real-time Core 0 priority hierarchy (lower numerical value = higher priority):
 *   Tier 1 (0x40): USBCTRL_IRQ       - Hardware USB controller ISR (~5 us)
 *   Tier 2 (0x60): usb_task_irq      - TinyUSB task + 1 ms ISO IN packet feeder (~15 us)
 *   Tier 3 (0x80): wakeup_timer_cb   - Lightweight 500 us wakeup & telemetry tick (< 1 us)
 *   Thread Mode  : backend_usb_device_task() - Heavy AYUMI/SSI-263A DSP & resampling
 *
 * Keeping Tier 2 strictly above Thread Mode DSP guarantees tud_audio_tx_done_pre_load_cb()
 * preempts synthesis on every 1 ms USB SOF without missing ISO IN transfers or livelocking.
 */
#define USB_DEV_USBCTRL_IRQ_PRIO   0x40u
#define USB_DEV_USBTASK_IRQ_PRIO   0x60u
#define USB_DEV_WAKEUP_PERIOD_US   500

static audio_stereo_frame_t usb_dev_storage[USB_DEV_RINGBUF_FRAMES];
static audio_ringbuf_t usb_dev_ringbuf;

static volatile bool usb_dev_initialized = false;
static volatile bool usb_dev_mounted = false;
static volatile bool usb_dev_streaming = false;
static volatile bool usb_dev_reset_requested = false;

static uint8_t usb_dev_clock_valid = 1;
static uint32_t usb_dev_sample_rate_hz = USB_DEV_SAMPLE_RATE_HZ;
static audio_control_range_4_n_t(1) usb_dev_sample_rate_range;

/* Resampler state (owned exclusively by Core 0 Thread Mode) */
static audio_stereo_frame_t resampler_prev_frame = {0, 0};
static uint32_t resampler_phase_q16 = 0;

/* Tier 3 Core 0 alarm pool and repeating wakeup timer */
static alarm_pool_t *usb_dev_alarm_pool = NULL;
static repeating_timer_t usb_dev_timer;

/* Tier 2 claimed user IRQ for TinyUSB event draining and 1 ms ISO IN feeding */
static uint8_t usb_task_irq = 0;
static volatile bool usb_task_irq_claimed = false;

static void backend_usb_device_deinit(void);

static void reset_stream_state(void) {
    audio_ringbuf_reset(&usb_dev_ringbuf);
    resampler_prev_frame.left = 0;
    resampler_prev_frame.right = 0;
    resampler_phase_q16 = 0;
}

static void reset_stream_state_atomic(void) {
    if (usb_task_irq_claimed) {
        irq_set_enabled(usb_task_irq, false);
        reset_stream_state();
        irq_set_enabled(usb_task_irq, true);
    } else {
        reset_stream_state();
    }
}

static void produce_and_push_48k_chunk(void) {
    /* Static buffers keep Core 0 stack usage minimal during DSP/resampling */
    static int16_t src_pcm[USB_DEV_SRC_CHUNK_FRAMES * 2u];
    static audio_stereo_frame_t out_frames[USB_DEV_OUT_CHUNK_FRAMES];

    mb_core_produce_pcm(src_pcm, (int)USB_DEV_SRC_CHUNK_FRAMES);

    const audio_stereo_frame_t *in_frames = (const audio_stereo_frame_t *)src_pcm;
    uint32_t phase = resampler_phase_q16;
    uint32_t out_count = 0;

    while (out_count < USB_DEV_OUT_CHUNK_FRAMES) {
        uint32_t idx = phase >> 16;
        if (idx >= USB_DEV_SRC_CHUNK_FRAMES) {
            break;
        }

        uint32_t frac = phase & 0xFFFFu;
        audio_stereo_frame_t f0 = (idx == 0u) ? resampler_prev_frame : in_frames[idx - 1u];
        audio_stereo_frame_t f1 = in_frames[idx];

        int32_t dl = (int32_t)f1.left - (int32_t)f0.left;
        int32_t dr = (int32_t)f1.right - (int32_t)f0.right;

        out_frames[out_count].left = (int16_t)((int32_t)f0.left + ((dl * (int32_t)frac) >> 16));
        out_frames[out_count].right = (int16_t)((int32_t)f0.right + ((dr * (int32_t)frac) >> 16));
        out_count++;

        phase += USB_DEV_PHASE_STEP_Q16;
    }

    resampler_phase_q16 = phase - (USB_DEV_SRC_CHUNK_FRAMES << 16);
    resampler_prev_frame = in_frames[USB_DEV_SRC_CHUNK_FRAMES - 1u];

    if (out_count > 0u) {
        (void)audio_ringbuf_write(&usb_dev_ringbuf, out_frames, out_count);
    }
}

static void prefill_ringbuf_to_target(void) {
    while (usb_dev_streaming &&
           audio_ringbuf_level(&usb_dev_ringbuf) < USB_DEV_TARGET_FRAMES &&
           audio_ringbuf_free(&usb_dev_ringbuf) >= USB_DEV_OUT_CHUNK_FRAMES) {
        /* Keep draining SSI-263A/AY queues and advancing CA1 timers between chunks */
        mb_core_task();
        produce_and_push_48k_chunk();
    }
}

/* Tier 1 hook: wake Tier 2 user IRQ whenever TinyUSB queues a hardware event */
void tud_event_hook_cb(uint8_t rhport, uint32_t eventid, bool in_isr) {
    (void)rhport;
    (void)eventid;
    (void)in_isr;
    if (usb_task_irq_claimed) {
        irq_set_pending(usb_task_irq);
    }
}

/* Tier 2 (0x60): Preempts Thread Mode DSP to service 1 ms USB ISO IN packets on time */
static void usb_task_irq_handler(void) {
    if (!tud_inited()) {
        return;
    }

    tud_task_ext(0, false);
    usb_dev_mounted = tud_mounted();

    usb_dev_telemetry_poll(to_ms_since_boot(get_absolute_time()),
                           usb_dev_streaming,
                           audio_ringbuf_level(&usb_dev_ringbuf));
}

/* Tier 3 (0x80): Ultra-lightweight 500 us wakeup timer (< 1 us, zero DSP) */
static bool wakeup_timer_cb(repeating_timer_t *rt) {
    (void)rt;
    if (!usb_dev_initialized) {
        return false;
    }

    if (usb_task_irq_claimed && usb_dev_mounted) {
        if (usb_dev_telemetry_is_due(to_ms_since_boot(get_absolute_time()))) {
            irq_set_pending(usb_task_irq);
        }
    }

    /* Exception return automatically sets Cortex-M33 event latch to wake __wfe() */
    return true;
}

static bool backend_usb_device_init(void) {
    /* Enable automatic + lazy hardware FPU context stacking on RP2350 Cortex-M33 */
    arm_cpu_hw->fpccr |= (M33_FPCCR_ASPEN_BITS | M33_FPCCR_LSPEN_BITS);

    audio_ringbuf_init(&usb_dev_ringbuf, usb_dev_storage, USB_DEV_RINGBUF_FRAMES);
    reset_stream_state();
    usb_dev_telemetry_reset();
#ifdef USB_DEV_SELFTEST
    usb_dev_selftest_init();
#endif

    usb_dev_mounted = false;
    usb_dev_streaming = false;
    usb_dev_reset_requested = false;

    usb_dev_sample_rate_range.wNumSubRanges = 1;
    usb_dev_sample_rate_range.subrange[0].bMin = USB_DEV_SAMPLE_RATE_HZ;
    usb_dev_sample_rate_range.subrange[0].bMax = USB_DEV_SAMPLE_RATE_HZ;
    usb_dev_sample_rate_range.subrange[0].bRes = 0;

    /* Tier 2: Claim user IRQ at 0x60 (above 0x80 timer and Thread Mode DSP) */
    usb_task_irq = (uint8_t)user_irq_claim_unused(true);
    usb_task_irq_claimed = true;
    irq_set_exclusive_handler(usb_task_irq, usb_task_irq_handler);
    irq_set_priority(usb_task_irq, USB_DEV_USBTASK_IRQ_PRIO);
    irq_set_enabled(usb_task_irq, true);

    /* Tier 3: Create Core 0 alarm pool at default priority (0x80) for 500 us wakeup */
    usb_dev_alarm_pool = alarm_pool_create_with_unused_hardware_alarm(4);
    if (!usb_dev_alarm_pool) {
        backend_usb_device_deinit();
        return false;
    }

    /* Tier 1: Elevate USBCTRL_IRQ (0x40) above Tier 2 (0x60) and Tier 3 (0x80) */
    irq_set_priority(USBCTRL_IRQ, USB_DEV_USBCTRL_IRQ_PRIO);
    usb_dev_initialized = true;

    if (!alarm_pool_add_repeating_timer_us(usb_dev_alarm_pool, USB_DEV_WAKEUP_PERIOD_US,
                                           wakeup_timer_cb, NULL, &usb_dev_timer)) {
        backend_usb_device_deinit();
        return false;
    }

    tusb_rhport_init_t dev_init = { .role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO };
    if (!tusb_init(0, &dev_init)) {
        backend_usb_device_deinit();
        return false;
    }

    return true;
}

static void backend_usb_device_task(void) {
    if (!usb_dev_initialized) {
        return;
    }

    if (usb_dev_reset_requested) {
        usb_dev_reset_requested = false;
        reset_stream_state_atomic();
    }

    mb_core_task();

#ifdef USB_DEV_SELFTEST
    if (usb_dev_mounted) {
        if (usb_dev_selftest_tick(to_ms_since_boot(get_absolute_time())) && usb_task_irq_claimed) {
            irq_set_pending(usb_task_irq);
        }
    }
#endif

    if (usb_dev_mounted && usb_dev_streaming) {
#ifdef USB_DEV_SELFTEST
        if (usb_dev_selftest_is_direct_mode()) {
            resampler_prev_frame.left = 0;
            resampler_prev_frame.right = 0;
            resampler_phase_q16 = 0;
            usb_dev_selftest_fill_direct_tone(&usb_dev_ringbuf, USB_DEV_TARGET_FRAMES);
        } else {
            prefill_ringbuf_to_target();
        }
#else
        prefill_ringbuf_to_target();
#endif
    } else if (audio_ringbuf_level(&usb_dev_ringbuf) > 0u || resampler_phase_q16 != 0u) {
        reset_stream_state_atomic();
    }

    /* Sleep until next 1 ms USB IRQ or 500 us wakeup timer once ring buffer is topped up */
    if (!usb_dev_streaming || audio_ringbuf_level(&usb_dev_ringbuf) >= USB_DEV_TARGET_FRAMES) {
        __wfe();
    }
}

static bool backend_usb_device_can_accept_samples(uint32_t count) {
    if (!usb_dev_initialized) {
        return false;
    }
    return audio_ringbuf_free(&usb_dev_ringbuf) >= count;
}

static uint32_t backend_usb_device_write_samples(const int16_t *samples, uint32_t count) {
    if (!usb_dev_initialized) {
        return 0;
    }
    return audio_ringbuf_write_interleaved(&usb_dev_ringbuf, samples, count);
}

static void backend_usb_device_deinit(void) {
    usb_dev_initialized = false;
    usb_dev_mounted = false;
    usb_dev_streaming = false;

    (void)cancel_repeating_timer(&usb_dev_timer);
    if (usb_dev_alarm_pool) {
        alarm_pool_destroy(usb_dev_alarm_pool);
        usb_dev_alarm_pool = NULL;
    }
    if (usb_task_irq_claimed) {
        irq_set_enabled(usb_task_irq, false);
        irq_remove_handler(usb_task_irq, usb_task_irq_handler);
        user_irq_unclaim(usb_task_irq);
        usb_task_irq_claimed = false;
    }

    reset_stream_state();
    usb_dev_telemetry_reset();
}

static bool backend_usb_device_is_ready(void) {
    return usb_dev_initialized && usb_dev_mounted && usb_dev_streaming;
}

static uint32_t backend_usb_device_get_buffer_level(void) {
    return audio_ringbuf_level(&usb_dev_ringbuf);
}

static bool backend_usb_device_handles_core_task(void) {
    return true;
}

/* TinyUSB Device & UAC2 Callbacks (invoked inside Tier 2 usb_task_irq_handler) */

void tud_mount_cb(void) {
    usb_dev_mounted = true;
    usb_dev_reset_requested = true;
}

void tud_umount_cb(void) {
    usb_dev_mounted = false;
    usb_dev_streaming = false;
    usb_dev_reset_requested = true;
}

/*
 * Adaptive sample rate pacing (47 / 48 / 49 stereo frames per 1 ms USB frame):
 * Strictly fast (~15 us) and lock-free; reads 4-byte audio_stereo_frame_t entries
 * from audio_ringbuf in Tier 2 (0x60), preempting Thread Mode DSP seamlessly.
 */
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t func_id,
                                   uint8_t ep_in, uint8_t cur_alt_setting) {
    (void)rhport;

    if (func_id != 0 || ep_in != 0x81 || cur_alt_setting == 0) {
        return true;
    }

    if (!usb_dev_streaming) {
        usb_dev_streaming = true;
    }

    uint32_t level = audio_ringbuf_level(&usb_dev_ringbuf);
    uint32_t target_frames = USB_DEV_PACKET_FRAMES_NOM;

    if (level < USB_DEV_LOW_WATERMARK) {
        target_frames = USB_DEV_PACKET_FRAMES_MIN;
    } else if (level > USB_DEV_HIGH_WATERMARK) {
        target_frames = USB_DEV_PACKET_FRAMES_MAX;
    }

    /* Zero-initialized stereo frame array guarantees 4-byte aligned silence padding */
    audio_stereo_frame_t packet[USB_DEV_PACKET_FRAMES_MAX];
    memset(packet, 0, sizeof(packet));

    uint32_t supplied = audio_ringbuf_read(&usb_dev_ringbuf, packet, target_frames);
    uint32_t fifo_errs = 0;

    uint16_t packet_bytes = (uint16_t)(target_frames * sizeof(audio_stereo_frame_t));
    uint16_t written = tud_audio_write(packet, packet_bytes);
    if (written != packet_bytes) {
        fifo_errs++;
        (void)tud_audio_clear_ep_in_ff();
        written = tud_audio_write(packet, packet_bytes);
    }

    bool tx_ok = (written == packet_bytes);
    if (!tx_ok) {
        fifo_errs++;
        (void)tud_audio_clear_ep_in_ff();
    }

    usb_dev_telemetry_record_packet(packet, supplied, target_frames, tx_ok, fifo_errs);
    return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *request) {
    (void)rhport;
    uint8_t const interface_number = TU_U16_LOW(request->wIndex);
    uint8_t const alternate_setting = TU_U16_LOW(request->wValue);

    if (interface_number == 1) {
        bool enable = (alternate_setting != 0);
        if (enable && !usb_dev_streaming) {
            usb_dev_streaming = true;
            usb_dev_reset_requested = true;
        } else if (!enable) {
            usb_dev_streaming = false;
            usb_dev_reset_requested = true;
        }
    }
    return true;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport,
                                  tusb_control_request_t const *request) {
    (void)rhport;
    uint8_t const interface_number = TU_U16_LOW(request->wIndex);
    if (interface_number == 1) {
        usb_dev_streaming = false;
        usb_dev_reset_requested = true;
    }
    return true;
}

bool tud_audio_get_req_entity_cb(uint8_t rhport,
                                 tusb_control_request_t const *request) {
    uint8_t const control_selector = TU_U16_HIGH(request->wValue);
    uint8_t const entity_id = TU_U16_HIGH(request->wIndex);

    if (entity_id != 4) {
        return false;
    }

    if (control_selector == AUDIO_CS_CTRL_SAM_FREQ) {
        if (request->bRequest == AUDIO_CS_REQ_CUR) {
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &usb_dev_sample_rate_hz, sizeof(usb_dev_sample_rate_hz));
        }
        if (request->bRequest == AUDIO_CS_REQ_RANGE) {
            return tud_audio_buffer_and_schedule_control_xfer(
                rhport, request, &usb_dev_sample_rate_range, sizeof(usb_dev_sample_rate_range));
        }
    } else if (control_selector == AUDIO_CS_CTRL_CLK_VALID &&
               request->bRequest == AUDIO_CS_REQ_CUR) {
        return tud_control_xfer(rhport, request, &usb_dev_clock_valid, sizeof(usb_dev_clock_valid));
    }

    return false;
}

const audio_sink_backend_t audio_sink_backend_usb_device = {
    .name = "USB_DEVICE",
    .init = backend_usb_device_init,
    .task = backend_usb_device_task,
    .can_accept_samples = backend_usb_device_can_accept_samples,
    .write_samples = backend_usb_device_write_samples,
    .deinit = backend_usb_device_deinit,
    .is_ready = backend_usb_device_is_ready,
    .get_buffer_level = backend_usb_device_get_buffer_level,
    .handles_core_task = backend_usb_device_handles_core_task,
};
