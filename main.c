/*
 * Copyright (c) 2022 Oliver Schmidt (https://a2retro.de/)
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
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
    stdio_init_all();

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

        if (!audio_sink_handles_core_task()) {
            mb_core_task();
        }

        if (reset) {
            reset = false;
            printf(" RESET ");
        }
    }

    return 0;
}
