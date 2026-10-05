/*

MIT License

Copyright (c) 2026 Shunichi Kitahara

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

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
