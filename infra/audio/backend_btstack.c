/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "audio_sink.h"
#include "bt_a2dp_source.h"
#include "mb_core.h"
#include <a2pico.h>
#include <pico/stdlib.h>
#include <pico/cyw43_arch.h>
#include <stdio.h>

/* Helper for A2Pico LED control (direct GPIO or CYW43 wireless GPIO) */
static inline void backend_btstack_set_led(bool on) {
    if (a2pico_led() >= 0) {
        gpio_put(a2pico_led(), on ? 1 : 0);
    } else if (a2pico_radio()) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
    }
}

static bool backend_btstack_init(void) {
    printf("[AUDIO] Initializing Bluetooth A2DP Source (BTstack)...\n");

    /* Fail-safe radio check */
    if (!a2pico_radio()) {
        printf("[AUDIO] No onboard radio module detected\n");
        return false;
    }

    /* Initialize Bluetooth A2DP Source with PCM callback from mb_core */
    if (!bt_a2dp_source_init(mb_core_produce_pcm)) {
        printf("[AUDIO] Failed to initialize Bluetooth A2DP Source\n");
        return false;
    }

    return true;
}

static void backend_btstack_task(void) {
    /* 1. Poll Bluetooth A2DP stack */
    bt_a2dp_source_poll();

#ifdef A2PICOMB_ENABLE_AUDIO_STATS_LOG
    /* 2. Minimal periodic summary report (every 1 second) */
    static uint32_t last_diag_report_time = 0;
    static uint32_t last_pcm_calls = 0;
    static uint32_t last_pkts = 0;
    static uint32_t last_pkts_gen = 0;
    static uint32_t last_tx_wait = 0;

    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_diag_report_time >= 1000) {
        uint32_t elapsed_ms = now - last_diag_report_time;
        last_diag_report_time = now;

        uint32_t cur_pcm_calls = mb_core_get_pcm_calls();
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
        int16_t min_val = 0, max_val = 0;
        uint32_t silent_samples = 0, max_silent_run = 0;
        mb_core_get_and_reset_pcm_stats(&pcm_samples, &min_val, &max_val, &silent_samples, &max_silent_run);

        uint32_t pcm_rate = (elapsed_ms > 0) ? (pcm_samples * 1000) / elapsed_ms : pcm_samples;
        uint32_t calls_rate = (elapsed_ms > 0) ? (delta_pcm_calls * 1000) / elapsed_ms : delta_pcm_calls;
        uint32_t pkts_rate = (elapsed_ms > 0) ? (delta_pkts * 1000) / elapsed_ms : delta_pkts;
        uint32_t gen_rate = (elapsed_ms > 0) ? (delta_gen * 1000) / elapsed_ms : delta_gen;

        uint32_t pcm_us = mb_core_get_last_pcm_us();
        mb_core_trigger_timing_sample();

        printf("[AUDIO] pcm_samples=%lu/sec (raw=%lu), pcm_calls=%lu/sec, pkts=%lu/sec (gen=%lu wait=%lu, stream=%d) [pcm=%luus]\n",
               pcm_rate, pcm_samples, calls_rate, pkts_rate,
               gen_rate, delta_wait,
               bt_a2dp_source_is_streaming() ? 1 : 0,
               pcm_us);
    }
#endif
}

static bool backend_btstack_can_accept_samples(uint32_t count) {
    (void)count;
    /* BTstack A2DP operates via callback pull mechanism */
    return bt_a2dp_source_is_streaming();
}

static uint32_t backend_btstack_write_samples(const int16_t *samples, uint32_t count) {
    (void)samples;
    (void)count;
    /* PCM samples are produced on-demand by callback from mb_core */
    return 0;
}

static void backend_btstack_deinit(void) {
    /* Deinitialization stub */
}

static bool backend_btstack_is_ready(void) {
    return bt_a2dp_source_is_streaming();
}

const audio_sink_backend_t audio_sink_backend_btstack = {
    .name = "BTSTACK",
    .init = backend_btstack_init,
    .task = backend_btstack_task,
    .can_accept_samples = backend_btstack_can_accept_samples,
    .write_samples = backend_btstack_write_samples,
    .deinit = backend_btstack_deinit,
    .is_ready = backend_btstack_is_ready,
    .get_buffer_level = NULL,
};

