/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AUDIO_RINGBUF_H
#define AUDIO_RINGBUF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Single stereo PCM frame (L + R = 4 bytes).
 * All ring buffer operations operate strictly in whole stereo frame units
 * to structurally prevent L/R channel misalignment.
 */
typedef struct {
    int16_t left;
    int16_t right;
} audio_stereo_frame_t;

_Static_assert(sizeof(audio_stereo_frame_t) == 4, "Stereo frame must be 4 bytes");

/*
 * Lock-free Single-Producer Single-Consumer (SPSC) stereo frame ring buffer.
 * Capacity must be a power of 2.
 */
typedef struct {
    audio_stereo_frame_t *buffer;
    uint32_t capacity;
    uint32_t mask;
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t underflow_count;
} audio_ringbuf_t;

/* Initialize ring buffer with caller-provided stereo frame storage (capacity_frames must be power of 2) */
bool audio_ringbuf_init(audio_ringbuf_t *rb, audio_stereo_frame_t *storage, uint32_t capacity_frames);

/* Reset read/write indices and counters (call only when producer and consumer are idle) */
void audio_ringbuf_reset(audio_ringbuf_t *rb);

/* Return number of stereo frames currently stored in the ring buffer */
uint32_t audio_ringbuf_level(const audio_ringbuf_t *rb);

/* Return number of free stereo frames available for writing */
uint32_t audio_ringbuf_free(const audio_ringbuf_t *rb);

/* Write up to frame_count whole stereo frames; returns number of frames actually written */
uint32_t audio_ringbuf_write(audio_ringbuf_t *rb, const audio_stereo_frame_t *frames, uint32_t frame_count);

/* Read up to frame_count whole stereo frames; returns number of frames actually read */
uint32_t audio_ringbuf_read(audio_ringbuf_t *rb, audio_stereo_frame_t *frames, uint32_t frame_count);

/* Convenience wrappers for interleaved int16_t stereo buffers (2 * frame_count int16_t elements) */
uint32_t audio_ringbuf_write_interleaved(audio_ringbuf_t *rb, const int16_t *samples, uint32_t frame_count);
uint32_t audio_ringbuf_read_interleaved(audio_ringbuf_t *rb, int16_t *samples, uint32_t frame_count);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_RINGBUF_H */
