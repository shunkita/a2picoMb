/*
 * Copyright (c) 2024-2026 Robert Elmer
 * Copyright (c) 2026 a2picoMb
 *
 * Based on https://github.com/relmer/Casso
 *
 * SPDX-License-Identifier: MIT
 */

#include "ssi263_internal.h"
#include <math.h>
#include <string.h>

#define SSI263_PI                3.14159265358979323846f
#define SSI263_NOMINAL_FILTER_HZ 20000.0f

#define SSI263_VOICED_GAIN       0.90f
#define SSI263_NOISE_GAIN        0.14f
#define SSI263_NOISE_LP_COEF     0.20f
#define SSI263_OUTPUT_LP_COEF    0.52f
#define SSI263_FRIC_LP_COEF      0.23f
#define SSI263_OUTPUT_GAIN       14.50f
#define SSI263_SOURCE_BREAK_HZ   50.0f
#define SSI263_NASAL_F2_BW_HZ    200.0f
#define SSI263_FRIC_BW_HZ        450.0f
#define SSI263_FRIC_F5_HZ        2800.0f
#define SSI263_FRIC_F5_BW_HZ     800.0f
#define SSI263_LEVEL_TAU_SEC     0.0025f
#define SSI263_ATTACK_TAU_SEC    0.002f
#define SSI263_RELEASE_TAU_SEC   0.004f

static const float s_bandwidth_hz[3] = { 50.0f, 80.0f, 120.0f };

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Fast inline minimax cosine on [0, 2*pi] (max error < 2.1e-7), avoiding slow
// XIP Flash libm cosf() calls inside the 44.1 kHz formant biquad inner loop.
static inline float ssi263_fast_cosf(float x) {
    const float two_pi = 2.0f * SSI263_PI;
    const float half_pi = 0.5f * SSI263_PI;
    if (x < 0.0f) x = -x;
    if (x >= two_pi) x -= two_pi * (float)((int)(x * (1.0f / (2.0f * SSI263_PI))));
    if (x > SSI263_PI) x = two_pi - x;

    float sign = 1.0f;
    if (x > half_pi) {
        x = SSI263_PI - x;
        sign = -1.0f;
    }

    float t = x * x;
    float c = 1.0f - t * (0.4999999963f - t * (0.0416666418f - t * (0.0013888397f - 0.0000247609f * t)));
    return sign * c;
}

void ssi263_set_sample_rate(ssi263_t *ssi, uint32_t sample_rate) {
    ssi->sample_rate = sample_rate;
    if (sample_rate == 0) {
        return;
    }

    float fs = (float)sample_rate;
    ssi->source_pole = 1.0f - expf(-2.0f * SSI263_PI * SSI263_SOURCE_BREAK_HZ / fs);
    ssi->level_coef = 1.0f - expf(-1.0f / (SSI263_LEVEL_TAU_SEC * fs));
    ssi->attack_coef = 1.0f - expf(-1.0f / (SSI263_ATTACK_TAU_SEC * fs));
    ssi->release_coef = 1.0f - expf(-1.0f / (SSI263_RELEASE_TAU_SEC * fs));

    for (int art = 0; art < 8; art++) {
        // Map ART=0..7 to 32 ms..4 ms exponential glide tau so 1-2 frame
        // phonemes reach target formants while preserving smooth coarticulation.
        float tau = (float)(8 - art) * 0.004f;
        ssi->art_coef[art] = 1.0f - expf(-1.0f / (tau * fs));
    }

    for (int stage = 0; stage < 3; stage++) {
        ssi->res_r[stage] = expf(-SSI263_PI * s_bandwidth_hz[stage] / fs);
    }
    ssi->nasal_f2_r = expf(-SSI263_PI * SSI263_NASAL_F2_BW_HZ / fs);
    ssi->fric_r = expf(-SSI263_PI * SSI263_FRIC_BW_HZ / fs);
    ssi->fric_f5_r = expf(-SSI263_PI * SSI263_FRIC_F5_BW_HZ / fs);
}

void ssi263_reset_dsp(ssi263_t *ssi) {
    for (int i = 0; i < 3; i++) {
        ssi->f_cur[i] = 0.0f;
        ssi->res_y1[i] = 0.0f;
        ssi->res_y2[i] = 0.0f;
    }
    ssi->env_level = 0.0f;
    ssi->glottal_phase = 0.0f;
    ssi->pitch_trans_cur = 0.0f;
    ssi->pitch_trans_target = 0;
    ssi->exc_lp1 = 0.0f;
    ssi->exc_lp2 = 0.0f;
    ssi->lfsr = 0xACE1u;
    ssi->noise_lp = 0.0f;
    ssi->va_cur = 0.0f;
    ssi->fa_cur = 0.0f;
    ssi->fric_lp = 0.0f;
    ssi->fric_lp2 = 0.0f;
    ssi->fric_lp3 = 0.0f;
    ssi->fric_y1 = 0.0f;
    ssi->fric_y2 = 0.0f;
    ssi->fric_f5_y1 = 0.0f;
    ssi->fric_f5_y2 = 0.0f;
    ssi->rad_prev = 0.0f;
    ssi->out_lp = 0.0f;
    ssi->out_lp2 = 0.0f;
}

static inline float ssi263_get_inflection_hz(ssi263_t *ssi, float fs) {
    static const float s_glide_mult[8] = {
        1.0f, 0.962f, 1.226f, 2.462f, 2.092f, 2.462f, 2.462f, 2.462f
    };
    uint8_t r1 = ssi->regs[SSI263_REG_INFLECT];
    uint8_t r2 = ssi->regs[SSI263_REG_RATEINF];
    float imm = (float)(((r2 & 0x08) ? 2048u : 0u) | (r2 & 0x07u));

    if (ssi->mode == 3) {
        if (ssi->env_level < 0.005f && ssi->pitch_trans_cur <= 0.0f) {
            ssi->pitch_trans_cur = (float)ssi->pitch_trans_target;
        }
        float diff = (float)ssi->pitch_trans_target - ssi->pitch_trans_cur;
        if (diff != 0.0f) {
            uint32_t rate = (r2 >> 4) & 0x0Fu;
            float gstep = (s_glide_mult[r1 & 0x07u] * SSI263_XCK_HZ) /
                          (128.0f * (float)(16u - rate) * fs);
            ssi->pitch_trans_cur = (fabsf(diff) <= gstep)
                                       ? (float)ssi->pitch_trans_target
                                       : (ssi->pitch_trans_cur + ((diff > 0.0f) ? gstep : -gstep));
        }
    } else {
        ssi->pitch_trans_cur = (float)ssi->pitch_trans_target;
    }

    float div = 8.0f * (4096.0f - (imm + ssi->pitch_trans_cur));
    return (div > 0.0f) ? (SSI263_XCK_HZ / div) : 0.0f;
}

static inline float ssi263_get_filter_hz(const ssi263_t *ssi) {
    float div = 2.0f * (float)(256u - ssi->regs[SSI263_REG_FILFREQ]);
    return (div > 0.0f) ? (SSI263_XCK_HZ / div) : 0.0f;
}

static inline const ssi263_phoneme_spec_t *ssi263_get_active_spec(const ssi263_t *ssi) {
    uint8_t idx = ssi->regs[SSI263_REG_DURPHON] & 0x3F;
    return &ssi263_phonemes[idx];
}

static void ssi263_glide_formants(ssi263_t *ssi, const ssi263_phoneme_spec_t *spec) {
    if (spec->f1 == 0) {
        return; // Hold tract position through pauses and closures
    }

    float target[3] = { (float)spec->f1, (float)spec->f2, (float)spec->f3 };
    if (ssi->f_cur[0] <= 0.0f || ssi->env_level < 0.005f) {
        ssi->f_cur[0] = target[0];
        ssi->f_cur[1] = target[1];
        ssi->f_cur[2] = target[2];
        return;
    }

    uint8_t art = (ssi->regs[SSI263_REG_CTRLAMP] >> 4) & 0x07;
    float base_coef = ssi->art_coef[art];
    float f1_coef = clampf(base_coef * 2.25f, 0.0f, 1.0f);
    ssi->f_cur[0] += f1_coef * (target[0] - ssi->f_cur[0]);
    ssi->f_cur[1] += base_coef * (target[1] - ssi->f_cur[1]);
    ssi->f_cur[2] += base_coef * (target[2] - ssi->f_cur[2]);
}

static float ssi263_generate_excitation(ssi263_t *ssi, float fs) {
    float pitch = clampf(ssi263_get_inflection_hz(ssi, fs), 30.0f, 400.0f);
    ssi->glottal_phase += pitch / fs;
    if (ssi->glottal_phase >= 1.0f) {
        ssi->glottal_phase -= 1.0f;
    }

    float ph = ssi->glottal_phase;
    float pulse = 0.0f;
    if (ph < 0.24f) {
        float t = ph / 0.24f;
        pulse = t * t * (3.0f - 2.0f * t);
    } else if (ph < 0.282f) {
        float t = (ph - 0.24f) / 0.042f;
        pulse = 1.0f - t * t;
    }

    return pulse * SSI263_VOICED_GAIN * ssi->va_cur;
}

static float ssi263_generate_noise(ssi263_t *ssi) {
    uint32_t bit = ssi->lfsr & 1u;
    ssi->lfsr >>= 1;
    if (bit != 0) {
        ssi->lfsr ^= 0xB400u;
    }
    float raw = (bit != 0) ? 1.0f : -1.0f;
    ssi->noise_lp += SSI263_NOISE_LP_COEF * (raw - ssi->noise_lp);
    return ssi->noise_lp;
}

static inline float ssi263_biquad_resonate(float input, float center_hz, float r, float fs,
                                           float *y1, float *y2) {
    float fc = clampf(center_hz, 50.0f, fs * 0.45f);
    float b = 2.0f * r * ssi263_fast_cosf(2.0f * SSI263_PI * fc / fs);
    float c = -(r * r);
    float y = (1.0f - b - c) * input + b * (*y1) + c * (*y2);
    *y2 = *y1;
    *y1 = y;
    return y;
}

float ssi263_render_sample(ssi263_t *ssi) {
    if (ssi->sample_rate == 0 || ssi263_is_silent(ssi)) {
        return 0.0f;
    }

    float fs = (float)ssi->sample_rate;
    const ssi263_phoneme_spec_t *spec = ssi263_get_active_spec(ssi);

    ssi263_glide_formants(ssi, spec);

    float voiced_target = spec->voiced_level;
    float fric_target = spec->fric_level;
    if (spec->voiced && spec->fricative && spec != &ssi263_phonemes[0x2F] && voiced_target < 0.18f) {
        voiced_target = 0.18f;
    }
    if (spec->closure && ssi->phoneme_dur_samples > 0) {
        float frac = (float)ssi->phoneme_sample_pos / (float)ssi->phoneme_dur_samples;
        if (!spec->voiced) {
            if (frac < 0.58f) {
                fric_target = 0.0f;
            } else if (frac >= 0.68f) {
                fric_target *= 0.22f;
            }
        } else {
            if (frac < 0.45f) {
                voiced_target *= 0.35f;
            } else if (spec == &ssi263_phonemes[0x25] && frac < 0.56f) {
                fric_target = 0.40f;
            }
        }
    }
    if (ssi->phoneme_active) {
        ssi->phoneme_sample_pos++;
    }

    float va_coef = ssi->level_coef;
    if (spec->closure && spec->voiced && voiced_target > ssi->va_cur) {
        va_coef = clampf(ssi->level_coef * 3.0f, 0.0f, 1.0f);
    }
    float fa_coef = ssi->level_coef;
    if (spec->closure && fric_target > ssi->fa_cur) {
        fa_coef = clampf(ssi->level_coef * 2.5f, 0.0f, 1.0f);
    }
    ssi->va_cur += va_coef * (voiced_target - ssi->va_cur);
    ssi->fa_cur += fa_coef * (fric_target - ssi->fa_cur);

    float scale = clampf(ssi263_get_filter_hz(ssi) / SSI263_NOMINAL_FILTER_HZ, 0.5f, 2.0f);

    float f2_r = spec->nasal ? ssi->nasal_f2_r : ssi->res_r[1];
    float sample = ssi263_biquad_resonate(ssi263_generate_excitation(ssi, fs), ssi->f_cur[0] * scale,
                                          ssi->res_r[0], fs, &ssi->res_y1[0], &ssi->res_y2[0]);
    sample = ssi263_biquad_resonate(sample, ssi->f_cur[1] * scale,
                                    f2_r, fs, &ssi->res_y1[1], &ssi->res_y2[1]);
    sample = ssi263_biquad_resonate(sample, ssi->f_cur[2] * scale,
                                    ssi->res_r[2], fs, &ssi->res_y1[2], &ssi->res_y2[2]);

    if (spec->fricative || ssi->fa_cur > 0.0001f) {
        float noise = ssi263_generate_noise(ssi);
        if (spec->voiced && spec->fricative) {
            if (spec == &ssi263_phonemes[0x2F]) {
                float swell = 1.0f + 0.80f * ssi263_fast_cosf(2.0f * SSI263_PI * ssi->glottal_phase);
                noise *= swell * 0.87f;
            } else {
                float swell = 1.0f + 0.30f * ssi263_fast_cosf(2.0f * SSI263_PI * ssi->glottal_phase);
                noise *= swell * 0.96f;
            }
        }
        float fric_f2 = ssi263_biquad_resonate(noise, ssi->f_cur[1] * scale, ssi->fric_r, fs,
                                               &ssi->fric_y1, &ssi->fric_y2);
        float fric_f5 = ssi263_biquad_resonate(noise, SSI263_FRIC_F5_HZ * scale, ssi->fric_f5_r, fs,
                                               &ssi->fric_f5_y1, &ssi->fric_f5_y2);
        float w_f5 = (spec == &ssi263_phonemes[0x2F]) ? 0.22f : (spec->bright_fric ? 0.05f : 0.00f);
        float fric_mix = fric_f2 + w_f5 * fric_f5;
        ssi->fric_lp += SSI263_FRIC_LP_COEF * (fric_mix - ssi->fric_lp);
        ssi->fric_lp2 += SSI263_FRIC_LP_COEF * (ssi->fric_lp - ssi->fric_lp2);
        sample += ssi->fric_lp2 * SSI263_NOISE_GAIN * ssi->fa_cur;
    }

    float diffed = (sample - ssi->rad_prev) * (fs / 44100.0f);
    ssi->rad_prev = sample;
    sample = diffed;

    ssi->out_lp += SSI263_OUTPUT_LP_COEF * (sample - ssi->out_lp);
    ssi->out_lp2 += SSI263_OUTPUT_LP_COEF * (ssi->out_lp - ssi->out_lp2);
    sample = ssi->out_lp2;

    bool has_source = (spec->voiced_level > 0.0f) || (spec->fric_level > 0.0f);
    uint8_t amp = ssi->regs[SSI263_REG_CTRLAMP] & 0x0F;
    float target = (!ssi->power_down && has_source) ? ((float)amp / 15.0f) : 0.0f;
    float env_coef = (target > ssi->env_level) ? ssi->attack_coef : ssi->release_coef;
    ssi->env_level += env_coef * (target - ssi->env_level);

    sample *= ssi->env_level * SSI263_OUTPUT_GAIN;
    sample = sample / (1.0f + fabsf(sample));
    return clampf(sample, -1.0f, 1.0f);
}

void ssi263_render_samples(ssi263_t *ssi, float *out_buf, int num_samples) {
    if (!out_buf || num_samples <= 0) {
        return;
    }
    if (ssi->sample_rate == 0 || ssi263_is_silent(ssi)) {
        memset(out_buf, 0, (size_t)num_samples * sizeof(float));
        return;
    }
    for (int i = 0; i < num_samples; i++) {
        out_buf[i] = ssi263_render_sample(ssi);
    }
}
