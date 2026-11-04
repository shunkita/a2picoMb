/*
 * MIT License
 *
 * Copyright (c) 2022 Oliver Schmidt (https://a2retro.de/)
 * Copyright (c) 2026 Shunichi Kitahara
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "a2pico2.pio.h"
#include <a2pico.h>
#include <stddef.h>

#include "board.h"
#include "mb_core.h"

#define SM_SYNC 3

#define PHI0_CYCLES_PER_TICK 1

volatile bool reset;

static __always_inline uint32_t mb_getaddr(void) {
  while (pio0->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + SM_ADDR))) {
    if (pio0->irq & 1) {
      pio_interrupt_clear(pio0, 0);

      // Tick 2 VIAs for 1 MHz Phi0 clock tick
      if (mb_core_tick_phi0()) {
        a2pico_irq(mb_diag.irq_line_asserted);
      }
    }

    tight_loop_contents();
  }
  return pio0->rxf[SM_ADDR];
}

static __always_inline uint32_t mb_getdata(void) {
  uint retry = 256;
  while (pio0->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + SM_WRITE)) && --retry) {
    tight_loop_contents();
  }
  return pio0->rxf[SM_WRITE];
}

static void __time_critical_func(handler)(bool asserted) {
  if (asserted) {
    a2pico_irq(false);
    mb_core_reset();
  } else {
    reset = true;
  }
}

static void init_sync_sm(PIO pio, uint sm) {
  uint offset = pio_add_program(pio, &sync_program);
  pio_sm_config config = sync_program_get_default_config(offset);
  pio_sm_init(pio, sm, offset, &config);
  pio_sm_set_enabled(pio, sm, false);
  pio_interrupt_clear(pio, 0);
  pio_sm_put(pio, sm, PHI0_CYCLES_PER_TICK - 1);
  pio_sm_set_enabled(pio, sm, true);
}

void __time_critical_func(board)(void) {
  mb_core_init();
  init_sync_sm(pio0, SM_SYNC);

  a2pico_resethandler(&handler);

  while (true) {
    uint32_t pico = mb_getaddr();

    uint32_t addr = pico & 0x0FFF;
    uint32_t io = pico & 0x0F00;   // IOSTRB or IOSEL
    uint32_t strb = pico & 0x0800; // IOSTRB
    uint32_t read = pico & RW_BIT; // R/W

    // ---- IOSEL ($Cn00-$CnFF, where n = slot) ----
    if (io && !strb) {
      uint8_t offset = addr & 0xFF;

      // Unmapped slot addresses ($Cn10-$Cn7F, $Cn90-$CnFF) when bits 4-6 (0x70)
      // are set
      if ((offset & 0x70) != 0) {
        if (!read) {
          // Drain and discard write data to prevent PIO RX FIFO deadlock
          mb_getdata();
        }
        continue;
      }

      // ---- IOSEL Read ----
      if (read) {
        uint8_t val = mb_core_bus_read(offset);
        a2pico_putdata(val);

        mb_core_bus_read_post(offset);
        if (mb_core_update_irq()) {
          a2pico_irq(mb_diag.irq_line_asserted);
        }
      }
      // ---- IOSEL Write ----
      else {
        uint32_t data = mb_getdata();
        mb_core_bus_write(offset, data);
        if (mb_core_update_irq()) {
          a2pico_irq(mb_diag.irq_line_asserted);
        }
      }
    }
    // ---- DEVSEL ($C080-$C0FF) or IOSTRB ($C800-$CFFE) Read ----
    else if (read) {
      ;
    }
    // DEVSEL or IOSTRB Write
    else {
      mb_getdata();
    }
  }
}
