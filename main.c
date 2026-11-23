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

#include <pico/stdlib.h>
#include <pico/multicore.h>
#include <stdio.h>

#include <a2pico.h>

#include "board.h"
#include "mb_core.h"
#include "audio_sink.h"
#if SSI263_ENABLE_MOCK
#include "ssi263_mock.h"
#endif

int main(void) {
    stdio_usb_init();

    a2pico_init(board);

    printf("\n\nA2Pico Mockingboard\n\n");

    mb_core_audio_init();
    mb_core_set_time_source(time_us_64);

    if (!audio_sink_init()) {
        printf("[AUDIO] Failed to initialize audio sink backend\n");
    }

#if SSI263_ENABLE_MOCK
    ssi263_mock_print_help();
#endif

    while (true) {
        audio_sink_task();

#if SSI263_ENABLE_MOCK
        int ch = getchar_timeout_us(0);
        if (ch != PICO_ERROR_TIMEOUT) {
            ssi263_mock_handle_char((char)ch, audio_sink_can_accept_samples(1));
        }
        ssi263_mock_task(time_us_64());
#endif

        mb_core_task();

        if (reset) {
            reset = false;
            printf(" RESET ");
        }
    }

    return 0;
}
