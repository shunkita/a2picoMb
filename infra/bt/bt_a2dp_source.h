/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _BT_A2DP_SOURCE_H
#define _BT_A2DP_SOURCE_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*bt_a2dp_pcm_callback_t)(int16_t *buffer, int num_samples);

// Initialize Bluetooth CYW43 and BTstack A2DP Source
bool bt_a2dp_source_init(bt_a2dp_pcm_callback_t callback);

// Non-blocking poll for Core 0 loop (calls cyw43_arch_poll)
void bt_a2dp_source_poll(void);

// Returns true if A2DP stream is actively connected and streaming
bool bt_a2dp_source_is_streaming(void);

// Diagnostic getters
uint32_t bt_a2dp_get_packets_sent(void);
uint8_t bt_a2dp_get_last_send_status(void);
uint32_t bt_a2dp_get_timer_ticks(void);
void bt_a2dp_get_frame_breakdown(uint32_t *total_us, uint32_t *pcm_us, uint32_t *sbc_us);
void bt_a2dp_get_pacing_stats(uint32_t *pkts_gen, uint32_t *tx_wait);

#endif // _BT_A2DP_SOURCE_H
