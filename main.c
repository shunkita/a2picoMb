/*

MIT License

Copyright (c) 2022 Oliver Schmidt (https://a2retro.de/)
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

#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <hardware/clocks.h>
#include <hardware/structs/busctrl.h>

#include <a2pico.h>

#include <string.h>
#include "board.h"
#include "audio.h"
#include "pico/cyw43_arch.h"

// LED control: Pico 2 W uses CYW43 on-board LED; classic boards use RP2350 GPIO
static inline void board_led_init(void) {
#if !defined(CYW43_WL_GPIO_LED_PIN)
    if (a2pico_led() >= 0) {
        gpio_init(a2pico_led());
        gpio_set_dir(a2pico_led(), GPIO_OUT);
    }
#endif
}

static inline void board_led_put(bool on) {
#if defined(CYW43_WL_GPIO_LED_PIN)
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#else
    if (a2pico_led() >= 0) {
        gpio_put(a2pico_led(), on);
    }
#endif
}

// Diagnostic states for Core0 inter-core communication
static uint32_t core0_last_rx_value = 0;
static uint32_t core0_rx_count = 0;
static uint32_t core0_last_tx_value = 0;
static uint32_t core0_tx_count = 0;
static uint32_t core0_tx_fail_count = 0;

static inline bool core0_send_to_core1(uint32_t data) {
    if (multicore_fifo_wready()) {
        sio_hw->fifo_wr = data;
        core0_last_tx_value = data;
        core0_tx_count++;
        return true;
    }
    core0_tx_fail_count++;
    return false;
}

void main(void) {
    set_sys_clock_khz(200000, false);

    stdio_usb_init();

    board_led_init();

    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    multicore_launch_core1(board);

    printf("\n\nA2Pico Mockingboard (Core0 / Core1 Inter-Core Communication)\n\n");

    // Initialize Audio Subsystem (AYUMI & Bluetooth A2DP)
    if (!audio_init()) {
        printf("[AUDIO] Failed to initialize Audio Subsystem\n");
    }

    // Send boot-time diagnostic test message from Core 0 to Core 1
    core0_send_to_core1(0xABCDEF01);

    uint32_t last_reported_core1_rx_count = 0;

    while (true) {

        // Service Audio Subsystem (poll Bluetooth, drain Core 1 queues, update AYUMI)
        audio_service();

        if (reset) {
            reset = false;
            printf(" RESET ");
        }

        // Non-blocking Core 1 -> Core 0 receive check
        if (multicore_fifo_rvalid()) {
            core0_last_rx_value = sio_hw->fifo_rd;
            core0_rx_count++;
            printf("CORE0 RX: %08lX (count=%lu)\n", core0_last_rx_value, core0_rx_count);
        }

        // Check if Core 1 received any new data from Core 0
        if (core1_rx_count != last_reported_core1_rx_count) {
            last_reported_core1_rx_count = core1_rx_count;
            printf("CORE1 RX: %08lX (count=%lu)\n", core1_last_rx_value, core1_rx_count);
        }


        // Optional terminal key input to trigger Core 0 -> Core 1 transfer
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            core0_send_to_core1(0xABCDEF01);
            printf("Triggered Core0 -> Core1 send: 0xABCDEF01 (tx_count=%lu)\n", core0_tx_count);
        }
    }
}
