/*
 * Copyright (c) 2024-2026 Robert Elmer
 * Copyright (c) 2026 a2picoMb
 *
 * Based on https://github.com/relmer/Casso
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef SSI263_INTERNAL_H
#define SSI263_INTERNAL_H

#include "ssi263.h"

typedef struct {
    uint16_t f1;
    uint16_t f2;
    uint16_t f3;
    uint8_t  voiced;
    uint8_t  fricative;
    uint8_t  closure;
    uint8_t  bright_fric;
    uint8_t  nasal;
    float    voiced_level;
    float    fric_level;
} ssi263_phoneme_spec_t;

extern const ssi263_phoneme_spec_t ssi263_phonemes[SSI263_PHONEME_COUNT];

void ssi263_reset_dsp(ssi263_t *ssi);

#endif /* SSI263_INTERNAL_H */
