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

#include <a2pico.h>
#include "via6522.h"
#include "board.h"

void via6522_init(via6522_t *via) {
    via->ora = 0;
    via->orb = 0;
    via->ddra = 0;
    via->ddrb = 0;
    via->acr = 0;
    via->pcr = 0;
    via->ifr = 0;
    via->ier = 0;
    via->sr = 0;
    via->in_a = 0xFF;
    via->in_b = 0xFF;

    via->t1_latch = 0xFFFF;
    via->t1_counter = 0xFFFF;
    via->t1_underflowed = true;
    via->timer_running = TIMER_T1_RUNNING;

    via->t2_latch = 0xFFFF;
    via->t2_counter = 0xFFFF;
    via->t2_underflowed = true;
    via->t2_armed = 0;
}

void via6522_reset(via6522_t *via) {
    via->ora = 0;
    via->orb = 0;
    via->ddra = 0;
    via->ddrb = 0;
    via->acr = 0;
    via->pcr = 0;
    via->ifr = 0;
    via->ier = 0;
    via->sr = 0;
    via->in_a = 0xFF;
    via->in_b = 0xFF;

    via->t1_latch = 0xFFFF;
    via->t1_counter = 0xFFFF;
    via->t1_underflowed = true;
    via->timer_running = TIMER_T1_RUNNING;

    via->t2_latch = 0xFFFF;
    via->t2_counter = 0xFFFF;
    via->t2_underflowed = true;
    via->t2_armed = 0;
}

void __time_critical_func(via6522_write)(via6522_t *via, uint8_t reg, uint8_t data) {
    switch (reg & 0x0F) {
    case REG_ORB: // $00
        via->orb = data;
        via->ifr &= ~0x18; // clear bit 4 (CB1), bit 3 (CB2)
        break;

    case REG_ORA: // $01
        via->ora = data;
        via->ifr &= ~0x03; // clear bit 1 (CA1), bit 0 (CA2)
        break;

    case REG_DDRB: // $02
        via->ddrb = data;
        break;

    case REG_DDRA: // $03
        via->ddra = data;
        break;

    case REG_T1CL: // $04
        // 6522 spec: writes to T1C-L only update low-order latch
        via->t1_latch = (via->t1_latch & 0xFF00) | data;
#if DIAG_TEST_B_WRITE_T1CL_CLEARS_IFR
        via->ifr &= ~0x40;
#endif
        break;

    case REG_T1CH: // $05
        // 6522 spec: writes to T1C-H write high-order latch, transfer entire latch
        // into counter, clear IFR bit 6, and start Timer 1 counting.
        // Method A: In real 6522 hardware, the first decrement occurs on cycle 2 following the write.
        // Pre-initializing t1_counter to (latch + 1) ensures that after 15 Phi0 ticks,
        // exactly 14 decrements have taken effect, yielding N - 14 ($F1 for N=$FF).
        via->t1_latch = (via->t1_latch & 0x00FF) | ((uint16_t)data << 8);
        via->t1_counter = via->t1_latch + 1;
        via->timer_running |= TIMER_T1_RUNNING;
        via->t1_underflowed = false;
        via->ifr &= ~0x40;
        break;

    case REG_T1LL: // $06
        // 6522 spec: writes to T1L-L update low-order latch
        via->t1_latch = (via->t1_latch & 0xFF00) | data;
        break;

    case REG_T1LH: // $07
        // 6522 spec: writes to T1L-H update high-order latch, clear IFR bit 6, and reset one-shot state
        via->t1_latch = (via->t1_latch & 0x00FF) | ((uint16_t)data << 8);
        via->t1_underflowed = false;
        via->ifr &= ~0x40;
        break;

    case REG_T2CL: // $08
        via->t2_latch = (via->t2_latch & 0xFF00) | data;
        break;

    case REG_T2CH: // $09
        // 6522 spec: writes to T2C-H write high-order latch, transfer entire latch
        // into counter, clear IFR bit 5, and start Timer 2 counting.
        // Method A: In real 6522 hardware, the first decrement occurs on cycle 2 following the write.
        // Pre-initializing t2_counter to (latch + 1) ensures that after 15 Phi0 ticks,
        // exactly 14 decrements have taken effect, yielding N - 14 ($F1 for N=$FF).
        via->t2_latch = (via->t2_latch & 0x00FF) | ((uint16_t)data << 8);
        via->t2_counter = via->t2_latch + 1;
        via->timer_running |= TIMER_T2_RUNNING;
        via->t2_armed = (via->acr & 0x20) ? 0 : 1;
        via->t2_underflowed = false;
        via->ifr &= ~0x20;
        break;

    case REG_SR: // $0A
        via->sr = data;
        via->ifr &= ~0x04; // clear bit 2 (SR)
        break;

    case REG_ACR: // $0B
        via->acr = data;
        if (data & 0x20) {
            via->t2_armed = 0;
        }
        break;

    case REG_PCR: // $0C
        via->pcr = data;
        break;

    case REG_IFR: // $0D
        // Write-1-to-clear for bits 0-6 (bit 7 write ignored)
        via->ifr &= ~(data & 0x7F);
        break;

    case REG_IER: // $0E
        // Bit 7 determines set (1) or clear (0) for bits 0-6
        if (data & 0x80) {
            via->ier |= (data & 0x7F);
        } else {
            via->ier &= ~(data & 0x7F);
        }
        break;

    case REG_ORA_NH: // $0F
        via->ora = data;
        // No handshake flags cleared
        break;

    default:
        break;
    }
}

bool __time_critical_func(via6522_sync_tick)(via6522_t *via) {
    bool irq = false;

    if (via->timer_running & TIMER_T1_RUNNING) {
        if (via->t1_counter == 0) {
            if (via->acr & 0x40) {
                via->ifr |= 0x40;
                irq = true;
            } else {
                if (!via->t1_underflowed) {
                    via->ifr |= 0x40;
                    via->t1_underflowed = true;
                    irq = true;
                }
            }
            via->t1_counter = via->t1_latch;
        } else {
            via->t1_counter--;
        }
    }

    if (!(via->acr & 0x20)) {
        if (via->t2_counter == 0) {
            if (via->t2_armed) {
                via->ifr |= 0x20;
                via->t2_armed = 0;
                irq = true;
            }
            via->t2_counter--;
        } else {
            via->t2_counter--;
        }
    }

    return irq;
}
