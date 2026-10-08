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

#include <stddef.h>
#include <a2pico.h>
#include "a2pico2.pio.h"

#include "board.h"
#include "mockingboard.h"

#define SM_SYNC 3

#define PHI0_CYCLES_PER_TICK 1

volatile bool reset;

// Flat 2 VIA instances (MockingBoard #0 only):
// vias[0]: VIA #0 ($Cn00-$Cn0F)
// vias[1]: VIA #1 ($Cn80-$Cn8F)
static via6522_t vias[2];

// 4 Audio Event Queues (Core 1 -> Core 0)
// Reserved for 2 Mockingboards (4 AY chips); currently MB#0 uses queues 0 and 1
audio_queue_t audio_queues[NUM_AUDIO_QUEUES];

// AY register address latch state for each VIA (R0-R15)
static uint8_t ay_selected_reg[2];

static __always_inline void update_irq(void) {
    bool assert_irq = false;
    for (int i = 0; i < 2; i++) {
        if (via6522_get_irq(&vias[i])) {
            assert_irq = true;
            break;
        }
    }
    a2pico_irq(assert_irq);
}

static void __time_critical_func(handler)(bool asserted) {
    if (asserted) {
        a2pico_irq(false);
        for (int i = 0; i < 2; i++) {
            via6522_reset(&vias[i]);
            ay_selected_reg[i] = 0;
        }
    } else {
        reset = true;
    }
}

_Static_assert(sizeof(via6522_t) == 24, "via6522_t size must be 24 bytes");
_Static_assert(offsetof(via6522_t, t1_counter) == 14, "t1_counter offset must be 14");
_Static_assert(offsetof(via6522_t, timer_running) == 17, "timer_running offset must be 17");
_Static_assert(offsetof(via6522_t, t2_counter) == 20, "t2_counter offset must be 20");
_Static_assert(offsetof(via6522_t, t2_running) == 23, "t2_running offset must be 23");

static __always_inline uint32_t process_2via_t1_tick(via6522_t *v) {
    uint32_t ev;
    uint32_t tmp;
    uint32_t cnt;
    uint32_t mask = TIMER_T1_RUNNING;
    via6522_t *ptr = v;

    __asm__ volatile (
        "movs   %[ev], #0\n\t"
        // --- VIA 0 ---
        "ldrb   %[tmp], [%[ptr], #17]\n\t"
        "ands   %[tmp], %[mask]\n\t"
        "ldrh   %[cnt], [%[ptr], #14]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #14]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- VIA 1 ---
        "adds   %[ptr], #24\n\t"
        "ldrb   %[tmp], [%[ptr], #17]\n\t"
        "ands   %[tmp], %[mask]\n\t"
        "ldrh   %[cnt], [%[ptr], #14]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #14]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- Restore ptr ---
        "subs   %[ptr], #24\n\t"
        : [ev] "=&l" (ev),
          [tmp] "=&l" (tmp),
          [cnt] "=&l" (cnt),
          [ptr] "+l" (ptr)
        : [mask] "l" (mask)
        : "cc", "memory"
    );

    return ev;
}

static __always_inline void handle_t1_via_underflow(via6522_t *via, bool *irq_updated) {
    if (via->acr & 0x40) {
        // Free-running mode: reload counter from latch and set IFR6
        via->ifr |= 0x40;
        via->t1_counter = via->t1_latch;
        *irq_updated = true;
    } else {
        // One-shot mode: set IFR6 only on first underflow
        if (!via->t1_underflowed) {
            via->ifr |= 0x40;
            via->t1_underflowed = true;
            *irq_updated = true;
        }
        // Counter is already wrapped to 0xFFFF by subs/strh
    }
}

static __always_inline void handle_t1_events(uint32_t ev) {
    bool irq_updated = false;
    uint32_t uf = (~ev) & 0x03;

    if (uf & 0x02) {
        handle_t1_via_underflow(&vias[0], &irq_updated);
    }
    if (uf & 0x01) {
        handle_t1_via_underflow(&vias[1], &irq_updated);
    }

    if (irq_updated) {
        update_irq();
    }
}

static __always_inline uint32_t process_2via_t2_tick(via6522_t *v) {
    uint32_t ev;
    uint32_t tmp;
    uint32_t cnt;
    via6522_t *ptr = v;

    __asm__ volatile (
        "movs   %[ev], #0\n\t"
        // --- VIA 0 ---
        "ldrb   %[tmp], [%[ptr], #23]\n\t"
        "ldrh   %[cnt], [%[ptr], #20]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #20]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- VIA 1 ---
        "adds   %[ptr], #24\n\t"
        "ldrb   %[tmp], [%[ptr], #23]\n\t"
        "ldrh   %[cnt], [%[ptr], #20]\n\t"
        "subs   %[cnt], %[cnt], %[tmp]\n\t"
        "strh   %[cnt], [%[ptr], #20]\n\t"
        "adcs   %[ev], %[ev]\n\t"
        // --- Restore ptr ---
        "subs   %[ptr], #24\n\t"
        : [ev] "=&l" (ev),
          [tmp] "=&l" (tmp),
          [cnt] "=&l" (cnt),
          [ptr] "+l" (ptr)
        :
        : "cc", "memory"
    );

    return ev;
}

static __always_inline void handle_t2_via_underflow(via6522_t *via, bool *irq_updated) {
    if (!via->t2_underflowed) {
        via->ifr |= 0x20;
        via->t2_underflowed = true;
        *irq_updated = true;
    }
}

static __always_inline void handle_t2_events(uint32_t ev) {
    bool irq_updated = false;
    uint32_t uf = (~ev) & 0x03;

    if (uf & 0x02) {
        handle_t2_via_underflow(&vias[0], &irq_updated);
    }
    if (uf & 0x01) {
        handle_t2_via_underflow(&vias[1], &irq_updated);
    }

    if (irq_updated) {
        update_irq();
    }
}

static __always_inline uint32_t mb_getaddr(void) {
    while (pio0->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + SM_ADDR))) {
        if (pio0->irq & 1) {
            pio_interrupt_clear(pio0, 0);

            // 2 VIA Timer 1: Branchless Carry Accumulator
            uint32_t ev1 = process_2via_t1_tick(vias);
            if (__builtin_expect(ev1 != 0x03, 0)) {
                handle_t1_events(ev1);
            }

            // 2 VIA Timer 2: Branchless Carry Accumulator
            uint32_t ev2 = process_2via_t2_tick(vias);
            if (__builtin_expect(ev2 != 0x03, 0)) {
                handle_t2_events(ev2);
            }
        }

        tight_loop_contents();
    }
    return pio0->rxf[SM_ADDR];
}

static __always_inline void handle_ay_orb_write(via6522_t *target_via, uint8_t data) {
    uint8_t ctrl = data & 0x07;
    int via_idx = target_via - vias;
    if (ctrl == 0x07) {
        ay_selected_reg[via_idx] = target_via->ora & 0x0F;
    } else if (ctrl == 0x06) {
        audio_queue_push(&audio_queues[via_idx], ay_selected_reg[via_idx], target_via->ora);
    } else if (ctrl == 0x00) {
        audio_queue_push_reset(&audio_queues[via_idx]);
    }
}

void __time_critical_func(board)(void) {
    // Initialize 2 VIA instances (MockingBoard #0 only)
    for (int i = 0; i < 2; i++) {
        via6522_init(&vias[i]);
        ay_selected_reg[i] = 0;
    }
    // Initialize 4 audio event queues (MB#0 uses queues 0 and 1, reserved for 2 MBs)
    for (int i = 0; i < NUM_AUDIO_QUEUES; i++) {
        audio_queue_init(&audio_queues[i]);
    }

    a2pico_init();

    // Initialize SM_SYNC (continuously running from boot as in Step 5-A)
    uint offset = pio_add_program(pio0, &sync_program);
    pio_sm_config config = sync_program_get_default_config(offset);
    pio_sm_init(pio0, SM_SYNC, offset, &config);
    pio_sm_set_enabled(pio0, SM_SYNC, false);
    pio_interrupt_clear(pio0, 0);

    // Load counter value (1 Phi0 cycle per tick) and start SM_SYNC
    pio_sm_put(pio0, SM_SYNC, PHI0_CYCLES_PER_TICK - 1);
    pio_sm_set_enabled(pio0, SM_SYNC, true);

    a2pico_resethandler(&handler);

    while (true) {
        uint32_t pico = mb_getaddr();

        uint32_t addr = pico & 0x0FFF;
        uint32_t io   = pico & 0x0F00;  // IOSTRB or IOSEL
        uint32_t strb = pico & 0x0800;  // IOSTRB
        uint32_t read = pico & RW_BIT;  // R/W

        // --------------------------------------------------------------------
        // IOSEL ($Cn00-$CnFF, where n = slot)
        // --------------------------------------------------------------------
        if (io && !strb) {
            uint8_t offset = addr & 0xFF;
            uint8_t reg    = offset & 0x0F;

            // Direct resolution from offset to flat VIA instances
            via6522_t *target_via = NULL;
            if ((offset & 0xF0) == 0x00) {
                target_via = &vias[0];
            } else if ((offset & 0xF0) == 0x80) {
                target_via = &vias[1];
            }

            // ---- IOSEL Read ----
            if (read) {
                if (target_via != NULL) {
                    a2pico_putdata(via6522_read(target_via, reg));
                    update_irq();
                }
                // Unmapped slot addresses ($Cn10-$Cn7F, $Cn90-$CnFF): open bus / Hi-Z
            }
            // ---- IOSEL Write ----
            else {
                uint32_t data = a2pico_getdata();
                if (target_via != NULL) {
                    via6522_write(target_via, reg, data);
                    update_irq();

                    if (reg == REG_ORB) {
                        handle_ay_orb_write(target_via, data);
                    }
                }
            }
        }
        // --------------------------------------------------------------------
        // DEVSEL ($C080-$C0FF) or IOSTRB ($C800-$CFFE) Read
        // --------------------------------------------------------------------
        else if (read) {
            // DEVSEL and IOSTRB ($C800-$CFFE) read: do nothing (open bus / Hi-Z)
        }
        // --------------------------------------------------------------------
        // DEVSEL or IOSTRB Write
        // --------------------------------------------------------------------
        else {
            // DEVSEL or IOSTRB write: consume data from FIFO
            a2pico_getdata();
        }
    }
}
