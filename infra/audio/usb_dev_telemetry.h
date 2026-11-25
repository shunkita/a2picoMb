/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef USB_DEV_TELEMETRY_H
#define USB_DEV_TELEMETRY_H

#include "audio_ringbuf.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void usb_dev_telemetry_reset(void);
bool usb_dev_telemetry_is_due(uint32_t now_ms);
void usb_dev_telemetry_record_packet(const audio_stereo_frame_t *frames,
                                     uint32_t supplied_frames,
                                     uint32_t target_frames,
                                     bool tx_ok,
                                     uint32_t fifo_err_delta);
void usb_dev_telemetry_log_boot(void);
void usb_dev_telemetry_poll(uint32_t now_ms, bool streaming, uint32_t ringbuf_level);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEV_TELEMETRY_H */
