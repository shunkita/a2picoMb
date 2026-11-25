/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "audio_ringbuf.h"
#include <string.h>

#if defined(__arm__) || defined(__thumb__)
#include <hardware/sync.h>
#define AUDIO_RINGBUF_DMB() __dmb()
#else
#define AUDIO_RINGBUF_DMB() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

static inline bool is_power_of_two(uint32_t v) {
    return v >= 2u && (v & (v - 1u)) == 0u;
}

bool audio_ringbuf_init(audio_ringbuf_t *rb, audio_stereo_frame_t *storage, uint32_t capacity_frames) {
    if (!rb || !storage || !is_power_of_two(capacity_frames)) {
        return false;
    }
    rb->buffer = storage;
    rb->capacity = capacity_frames;
    rb->mask = capacity_frames - 1u;
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
    rb->underflow_count = 0;
    return true;
}

void audio_ringbuf_reset(audio_ringbuf_t *rb) {
    if (!rb) {
        return;
    }
    rb->head = 0;
    rb->tail = 0;
    rb->overflow_count = 0;
    rb->underflow_count = 0;
    AUDIO_RINGBUF_DMB();
}

uint32_t audio_ringbuf_level(const audio_ringbuf_t *rb) {
    if (!rb || rb->capacity == 0) {
        return 0;
    }
    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    return head - tail;
}

uint32_t audio_ringbuf_free(const audio_ringbuf_t *rb) {
    if (!rb || rb->capacity == 0) {
        return 0;
    }
    uint32_t used = audio_ringbuf_level(rb);
    return (used <= rb->capacity) ? (rb->capacity - used) : 0u;
}

uint32_t audio_ringbuf_write(audio_ringbuf_t *rb, const audio_stereo_frame_t *frames, uint32_t frame_count) {
    if (!rb || !rb->buffer || !frames || frame_count == 0) {
        return 0;
    }

    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    uint32_t free_frames = rb->capacity - (head - tail);
    uint32_t to_write = (frame_count <= free_frames) ? frame_count : free_frames;

    if (to_write < frame_count) {
        rb->overflow_count++;
    }
    if (to_write == 0) {
        return 0;
    }

    uint32_t idx = head & rb->mask;
    uint32_t first = rb->capacity - idx;
    if (first > to_write) {
        first = to_write;
    }
    memcpy(&rb->buffer[idx], frames, first * sizeof(audio_stereo_frame_t));

    uint32_t second = to_write - first;
    if (second > 0) {
        memcpy(&rb->buffer[0], &frames[first], second * sizeof(audio_stereo_frame_t));
    }

    AUDIO_RINGBUF_DMB();
    rb->head = head + to_write;
    return to_write;
}

uint32_t audio_ringbuf_read(audio_ringbuf_t *rb, audio_stereo_frame_t *frames, uint32_t frame_count) {
    if (!rb || !rb->buffer || !frames || frame_count == 0) {
        return 0;
    }

    uint32_t head = rb->head;
    uint32_t tail = rb->tail;
    AUDIO_RINGBUF_DMB();

    uint32_t avail = head - tail;
    uint32_t to_read = (frame_count <= avail) ? frame_count : avail;

    if (to_read < frame_count) {
        rb->underflow_count++;
    }
    if (to_read == 0) {
        return 0;
    }

    uint32_t idx = tail & rb->mask;
    uint32_t first = rb->capacity - idx;
    if (first > to_read) {
        first = to_read;
    }
    memcpy(frames, &rb->buffer[idx], first * sizeof(audio_stereo_frame_t));

    uint32_t second = to_read - first;
    if (second > 0) {
        memcpy(&frames[first], &rb->buffer[0], second * sizeof(audio_stereo_frame_t));
    }

    AUDIO_RINGBUF_DMB();
    rb->tail = tail + to_read;
    return to_read;
}

uint32_t audio_ringbuf_write_interleaved(audio_ringbuf_t *rb, const int16_t *samples, uint32_t frame_count) {
    return audio_ringbuf_write(rb, (const audio_stereo_frame_t *)samples, frame_count);
}

uint32_t audio_ringbuf_read_interleaved(audio_ringbuf_t *rb, int16_t *samples, uint32_t frame_count) {
    return audio_ringbuf_read(rb, (audio_stereo_frame_t *)samples, frame_count);
}
