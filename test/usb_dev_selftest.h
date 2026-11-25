/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef USB_DEV_SELFTEST_H
#define USB_DEV_SELFTEST_H

#include "audio_ringbuf.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*usb_dev_cdc_log_fn_t)(const char *fmt, ...);

/* Park Core 1 Apple II bus PIO (detached mode), init mb_core and 48 kHz sine LUT */
void usb_dev_selftest_init(void);

/* Advance self-test state machine from Core 0 Thread Mode.
 * Returns true when one or more CDC log lines are queued for Tier 2 emission. */
bool usb_dev_selftest_tick(uint32_t now_ms);

/* Returns true when log lines are queued for CDC output */
bool usb_dev_selftest_has_pending_log(void);

/* Returns true in Stage 0 (direct 48 kHz USB ringbuf tone bypassing mb_core) */
bool usb_dev_selftest_is_direct_mode(void);

/* Fill ring buffer up to target_frames with 440 Hz Left / 880 Hz Right @ 48 kHz */
void usb_dev_selftest_fill_direct_tone(audio_ringbuf_t *rb, uint32_t target_frames);

/* Drain queued self-test log lines from Tier 2 USB task IRQ context */
void usb_dev_selftest_emit_cdc(usb_dev_cdc_log_fn_t log_fn, uint32_t (*avail_fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* USB_DEV_SELFTEST_H */
