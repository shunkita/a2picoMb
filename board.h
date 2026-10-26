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

#ifndef _BOARD_H
#define _BOARD_H

#include <stdint.h>
#include <stdbool.h>

#include "audio_event.h"

extern volatile bool reset;


typedef struct {
    volatile uint32_t t1_underflow_count[2];
    volatile uint32_t t1_clear_read_t1cl[2];
    volatile uint32_t t1_clear_write_t1cl[2];
    volatile uint32_t t1_clear_write_t1ch[2];
    volatile uint32_t t1_clear_write_t1lh[2];
    volatile uint32_t t1_clear_write_ifr[2];
    volatile uint32_t irq_assert_count;
    volatile uint32_t irq_deassert_count;
    volatile uint8_t  last_ifr[2];
    volatile uint8_t  last_ier[2];
    volatile bool     irq_line_asserted;
} mb_diag_t;

extern mb_diag_t mb_diag;

void board(void);

#endif
