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

#include "board.h"
#include "audio.h"

void main(void) {
    set_sys_clock_khz(200000, false);

    stdio_usb_init();
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_PROC1_BITS;
    multicore_launch_core1(board);

    printf("\n\nA2Pico Mockingboard\n");
#if DIAG_TEST_A_DISABLE_IRQ
    printf("[DIAG MODE] Test A: Physical IRQ drive DISABLED (force Hi-Z)\n");
#endif
#if DIAG_TEST_B_WRITE_T1CL_CLEARS_IFR
    printf("[DIAG MODE] Test B: Write to T1C-L clears IFR Timer 1 flag ENABLED\n");
#endif
    printf("\n");

    // Initialize Audio Subsystem (AYUMI & Bluetooth A2DP)
    if (!audio_init()) {
        printf("[AUDIO] Failed to initialize Audio Subsystem\n");
    }

    uint32_t last_diag_time = 0;
    uint32_t prev_uf0 = 0, prev_uf1 = 0, prev_assert = 0, prev_deassert = 0;

    while (true) {
        // Service Audio Subsystem (poll Bluetooth, drain Core 1 queues, update AYUMI)
        audio_service();

        if (reset) {
            reset = false;
            printf(" RESET ");
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_diag_time >= 1000) {
            last_diag_time = now;
            if (mb_diag.t1_underflow_count[0] != prev_uf0 ||
                mb_diag.t1_underflow_count[1] != prev_uf1 ||
                mb_diag.irq_assert_count != prev_assert ||
                mb_diag.irq_deassert_count != prev_deassert) {
                prev_uf0 = mb_diag.t1_underflow_count[0];
                prev_uf1 = mb_diag.t1_underflow_count[1];
                prev_assert = mb_diag.irq_assert_count;
                prev_deassert = mb_diag.irq_deassert_count;
                printf("[DIAG] IRQ_pin:%s (asserts:%lu, deasserts:%lu) | V0(UF:%lu, clrR:%lu, clrW:%lu, IFR:%02x, IER:%02x) | V1(UF:%lu, clrR:%lu, clrW:%lu, IFR:%02x, IER:%02x)\n",
                       mb_diag.irq_line_asserted ? "LOW(Asserted)" : "Hi-Z",
                       (unsigned long)mb_diag.irq_assert_count, (unsigned long)mb_diag.irq_deassert_count,
                       (unsigned long)mb_diag.t1_underflow_count[0], (unsigned long)mb_diag.t1_clear_read_t1cl[0],
                       (unsigned long)(mb_diag.t1_clear_write_t1ch[0] + mb_diag.t1_clear_write_ifr[0] + mb_diag.t1_clear_write_t1cl[0]),
                       mb_diag.last_ifr[0], mb_diag.last_ier[0],
                       (unsigned long)mb_diag.t1_underflow_count[1], (unsigned long)mb_diag.t1_clear_read_t1cl[1],
                       (unsigned long)(mb_diag.t1_clear_write_t1ch[1] + mb_diag.t1_clear_write_ifr[1] + mb_diag.t1_clear_write_t1cl[1]),
                       mb_diag.last_ifr[1], mb_diag.last_ier[1]);
            }
        }
    }
}

