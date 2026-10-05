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

#ifndef _VIA6522_H
#define _VIA6522_H

#include <stdint.h>
#include <stdbool.h>

// 6522 Register Offsets
#define REG_ORB    0x00
#define REG_ORA    0x01
#define REG_DDRB   0x02
#define REG_DDRA   0x03
#define REG_T1CL   0x04
#define REG_T1CH   0x05
#define REG_T1LL   0x06
#define REG_T1LH   0x07
#define REG_T2CL   0x08
#define REG_T2CH   0x09
#define REG_SR     0x0A
#define REG_ACR    0x0B
#define REG_PCR    0x0C
#define REG_IFR    0x0D
#define REG_IER    0x0E
#define REG_ORA_NH 0x0F

#define TIMER_T1_RUNNING 0x01
#define TIMER_T2_RUNNING 0x02

typedef struct {
    // 6522 register state
    uint8_t ora;       // Output register A (REG_ORA, REG_ORA_NH)
    uint8_t orb;       // Output register B (REG_ORB)
    uint8_t ddra;      // Data direction register A (1=output, 0=input)
    uint8_t ddrb;      // Data direction register B (1=output, 0=input)
    uint8_t acr;       // Auxiliary control register
    uint8_t pcr;       // Peripheral control register
    uint8_t ifr;       // Interrupt flag register (bits 0-6)
    uint8_t ier;       // Interrupt enable register (bits 0-6)
    uint8_t sr;        // Shift register

    // Port input levels (pin inputs when DDR bit is 0; default 0xFF)
    uint8_t in_a;
    uint8_t in_b;

    // Timer 1 state
    uint16_t t1_latch;
    uint16_t t1_counter;
    bool     t1_underflowed;
    uint8_t  timer_running; // bit 0: T1 running, bit 1: T2 running

    // Timer 2 state
    uint16_t t2_latch;
    uint16_t t2_counter;
    bool     t2_underflowed;
    uint8_t  t2_running; // 0 or 1: actual step per tick
} via6522_t;

typedef via6522_t via_t;

void via6522_init(via6522_t *via);
void via6522_reset(via6522_t *via);
uint8_t via6522_read(via6522_t *via, uint8_t reg);
void via6522_write(via6522_t *via, uint8_t reg, uint8_t data);
bool via6522_sync_tick(via6522_t *via);
static inline bool via6522_get_irq(const via6522_t *via) {
    return (via->ifr & via->ier & 0x7F) != 0;
}

#endif // _VIA6522_H
