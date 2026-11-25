/*
 * Copyright (c) 2022 Oliver Schmidt (https://a2retro.de/)
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _BOARD_H
#define _BOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "mb_core.h"

extern volatile bool reset;

void board(void);

#endif // _BOARD_H
