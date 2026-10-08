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

#include "audio.h"
#include "audio_event.h"
#include "ay_audio.h"
#include "bt_a2dp_source.h"
#include <pico/stdlib.h>
#include <stdio.h>

#define QUEUE_DRAIN_DELAY_US 10

static uint32_t audio_event_count[NUM_AUDIO_QUEUES] = {0};

bool audio_init(void) {
  // 1. Initialize AYUMI instances
  ay_audio_init();

  // 2. Initialize Bluetooth A2DP Source
  printf("[AUDIO] Initializing Bluetooth A2DP Source...\n");
  if (!bt_a2dp_source_init(ay_audio_produce_pcm)) {
    printf("[AUDIO] Failed to initialize Bluetooth A2DP Source\n");
    return false;
  }

  return true;
}

void audio_service(void) {
  // 1. Poll Bluetooth A2DP stack
  bt_a2dp_source_poll();

  // 2. Drain Audio Event Queues (Core 1 -> Core 0)
  for (int q = 0; q < NUM_AUDIO_QUEUES; q++) {
    audio_queue_t *queue = &audio_queues[q];
    uint32_t used = queue->head - queue->tail;
    if (used > queue->high_water_mark) {
      queue->high_water_mark = used;
    }

    ay_event_t ev;
    while (audio_queue_pop(queue, &ev)) {
      audio_event_count[q]++;
      if (ev.type == AY_EVENT_RESET) {
        printf("[AUDIO EVT] Q%d RESET\n", q);
      }
      ay_audio_handle_event(q, &ev);
#if QUEUE_DRAIN_DELAY_US > 0
      sleep_us(QUEUE_DRAIN_DELAY_US);
#endif
    }
  }

  // 3. Minimal periodic summary report (every 1 second)
  static uint32_t last_diag_report_time = 0;
  static uint32_t last_pcm_calls = 0;
  static uint32_t last_pkts = 0;
  static uint32_t last_pkts_gen = 0;
  static uint32_t last_tx_wait = 0;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (now - last_diag_report_time >= 1000) {
    uint32_t elapsed_ms = now - last_diag_report_time;
    last_diag_report_time = now;

    uint32_t cur_pcm_calls = ay_audio_get_pcm_calls();
    uint32_t delta_pcm_calls = cur_pcm_calls - last_pcm_calls;
    last_pcm_calls = cur_pcm_calls;

    uint32_t cur_pkts = bt_a2dp_get_packets_sent();
    uint32_t delta_pkts = cur_pkts - last_pkts;
    last_pkts = cur_pkts;

    uint32_t cur_pkts_gen = 0, cur_tx_wait = 0;
    bt_a2dp_get_pacing_stats(&cur_pkts_gen, &cur_tx_wait);
    uint32_t delta_gen = cur_pkts_gen - last_pkts_gen;
    uint32_t delta_wait = cur_tx_wait - last_tx_wait;
    last_pkts_gen = cur_pkts_gen;
    last_tx_wait = cur_tx_wait;

    uint32_t pcm_samples = 0;
    ay_audio_get_and_reset_pcm_stats(&pcm_samples, NULL, NULL, NULL, NULL);

    uint32_t pcm_rate = (elapsed_ms > 0) ? (pcm_samples * 1000) / elapsed_ms : pcm_samples;
    uint32_t calls_rate = (elapsed_ms > 0) ? (delta_pcm_calls * 1000) / elapsed_ms : delta_pcm_calls;
    uint32_t pkts_rate = (elapsed_ms > 0) ? (delta_pkts * 1000) / elapsed_ms : delta_pkts;
    uint32_t gen_rate = (elapsed_ms > 0) ? (delta_gen * 1000) / elapsed_ms : delta_gen;

    uint32_t pcm_tot = ay_audio_get_last_pcm_us();
    ay_audio_trigger_timing_sample();

    printf("[AUDIO] pcm_samples=%lu/sec (raw=%lu), pcm_calls=%lu/sec, pkts=%lu/sec (gen=%lu wait=%lu, stream=%d) [pcm=%luus]\n",
           pcm_rate, pcm_samples, calls_rate, pkts_rate,
           gen_rate, delta_wait,
           bt_a2dp_source_is_streaming() ? 1 : 0,
           pcm_tot);
  }
}
