/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "usb_dev_telemetry.h"
#include "mb_core.h"
#include "tusb.h"
#ifdef USB_DEV_SELFTEST
#include "usb_dev_selftest.h"
#endif

#include <stdarg.h>
#include <stdio.h>

#define USB_DEV_CDC_INTERVAL_MS  1000u
#define USB_DEV_CDC_LOG_BUF_SIZE 256u
#define USB_DEV_PKT_FRAMES_MIN   47u
#define USB_DEV_PKT_FRAMES_NOM   48u

static volatile uint32_t s_packets_47 = 0;
static volatile uint32_t s_packets_48 = 0;
static volatile uint32_t s_packets_49 = 0;
static volatile uint32_t s_underruns = 0;
static volatile uint32_t s_fifo_errors = 0;
static uint16_t s_peak_left = 0;
static uint16_t s_peak_right = 0;
static bool s_boot_banner_sent = false;
static volatile uint32_t s_last_log_ms = 0;

#ifdef USB_DEV_SELFTEST
static uint32_t usb_dev_cdc_write_avail(void) {
    return tud_cdc_n_write_available(0);
}
#endif

/* Non-blocking CDC telemetry output (called exclusively inside Tier 2 usb_task_irq_handler) */
static void usb_dev_cdc_logf(const char *fmt, ...) {
    static char buf[USB_DEV_CDC_LOG_BUF_SIZE];
    if (!tud_mounted()) {
        return;
    }

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n <= 0) {
        return;
    }

    uint32_t len = ((uint32_t)n >= sizeof(buf)) ? (uint32_t)(sizeof(buf) - 1u) : (uint32_t)n;
    if (tud_cdc_n_write_available(0) < len) {
        return;
    }

    (void)tud_cdc_n_write(0, buf, len);
    (void)tud_cdc_n_write_flush(0);
}

void usb_dev_telemetry_reset(void) {
    s_packets_47 = 0;
    s_packets_48 = 0;
    s_packets_49 = 0;
    s_underruns = 0;
    s_fifo_errors = 0;
    s_peak_left = 0;
    s_peak_right = 0;
    s_boot_banner_sent = false;
    s_last_log_ms = 0;
}

bool usb_dev_telemetry_is_due(uint32_t now_ms) {
    if ((uint32_t)(now_ms - s_last_log_ms) >= USB_DEV_CDC_INTERVAL_MS) {
        return true;
    }
#ifdef USB_DEV_SELFTEST
    if (usb_dev_selftest_has_pending_log()) {
        return true;
    }
#endif
    return false;
}

void usb_dev_telemetry_record_packet(const audio_stereo_frame_t *frames,
                                     uint32_t supplied_frames,
                                     uint32_t target_frames,
                                     bool tx_ok,
                                     uint32_t fifo_err_delta) {
    if (supplied_frames < target_frames) {
        s_underruns++;
    }

    if (frames) {
        uint16_t pk_l = s_peak_left;
        uint16_t pk_r = s_peak_right;
        for (uint32_t i = 0; i < supplied_frames; i++) {
            int32_t sl = frames[i].left;
            int32_t sr = frames[i].right;
            uint16_t al = (uint16_t)((sl < 0) ? -sl : sl);
            uint16_t ar = (uint16_t)((sr < 0) ? -sr : sr);
            if (al > pk_l) pk_l = al;
            if (ar > pk_r) pk_r = ar;
        }
        s_peak_left = pk_l;
        s_peak_right = pk_r;
    }

    s_fifo_errors += fifo_err_delta;
    if (tx_ok) {
        if (target_frames == USB_DEV_PKT_FRAMES_MIN) {
            s_packets_47++;
        } else if (target_frames == USB_DEV_PKT_FRAMES_NOM) {
            s_packets_48++;
        } else {
            s_packets_49++;
        }
    }
}

void usb_dev_telemetry_log_boot(void) {
    if (!tud_mounted() || s_boot_banner_sent || tud_cdc_n_write_available(0) < 32u) {
        return;
    }
    s_boot_banner_sent = true;
#ifdef USB_DEV_SELFTEST
    usb_dev_cdc_logf("[USB-DEV] boot (SELFTEST)\r\n");
#else
    usb_dev_cdc_logf("[USB-DEV] boot\r\n");
#endif
}

static void usb_dev_emit_telemetry(uint32_t now_ms, bool streaming, uint32_t ringbuf_level) {
    if (!tud_mounted()) {
        return;
    }

    usb_dev_telemetry_log_boot();
#ifdef USB_DEV_SELFTEST
    usb_dev_selftest_emit_cdc(usb_dev_cdc_logf, usb_dev_cdc_write_avail);
#endif

    if ((uint32_t)(now_ms - s_last_log_ms) < USB_DEV_CDC_INTERVAL_MS) {
        return;
    }
    s_last_log_ms = now_ms;

    uint16_t pk_l = s_peak_left;
    uint16_t pk_r = s_peak_right;
    s_peak_left = 0;
    s_peak_right = 0;

    uint32_t ca1_cnt = __atomic_load_n(&mb_ssi263.ca1_word, __ATOMIC_RELAXED) >> 1;
    usb_dev_cdc_logf(
        "[USB-DEV] stream=%d rb=%u pkts=%lu/%lu/%lu ur=%lu peak=%u/%u q_ovf=%lu/%lu ssi_ev=%lu ssi_ovf=%lu ca1=%lu\r\n",
        streaming ? 1 : 0,
        (unsigned)ringbuf_level,
        (unsigned long)s_packets_47,
        (unsigned long)s_packets_48,
        (unsigned long)s_packets_49,
        (unsigned long)s_underruns,
        (unsigned)pk_l,
        (unsigned)pk_r,
        (unsigned long)mb_audio_queues[0].overflow_count,
        (unsigned long)mb_audio_queues[1].overflow_count,
        (unsigned long)mb_ssi263.events_processed,
        (unsigned long)mb_ssi263.overflow_count,
        (unsigned long)ca1_cnt);
}

void usb_dev_telemetry_poll(uint32_t now_ms, bool streaming, uint32_t ringbuf_level) {
    if (tud_cdc_n_available(0)) {
        tud_cdc_n_read_flush(0);
    }
    usb_dev_emit_telemetry(now_ms, streaming, ringbuf_level);
}

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    if ((dtr || rts) && tud_mounted() && tud_cdc_n_write_available(0) >= 32u) {
#ifdef USB_DEV_SELFTEST
        usb_dev_cdc_logf("\r\n[USB-DEV] cdc open (SELFTEST)\r\n");
#else
        usb_dev_cdc_logf("\r\n[USB-DEV] cdc open\r\n");
#endif
    }
}
