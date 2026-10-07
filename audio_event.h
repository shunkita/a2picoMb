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

#ifndef _AUDIO_EVENT_H
#define _AUDIO_EVENT_H

#include <stdint.h>
#include <stdbool.h>
#include <hardware/sync.h>

#define AUDIO_QUEUE_CAPACITY 256

typedef enum {
    AY_EVENT_REGISTER_WRITE = 0,
    AY_EVENT_RESET          = 1
} ay_event_type_t;

typedef struct {
    uint8_t type;
    uint8_t reg;
    uint8_t value;
} ay_event_t;

typedef struct {
    ay_event_t buffer[AUDIO_QUEUE_CAPACITY];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t overflow_count;
    volatile uint32_t high_water_mark;
} audio_queue_t;

#define NUM_AUDIO_QUEUES 4
extern audio_queue_t audio_queues[NUM_AUDIO_QUEUES];

static inline void audio_queue_init(audio_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->overflow_count = 0;
    q->high_water_mark = 0;
}

// Push raw event
static __always_inline bool audio_queue_push_event(audio_queue_t *q, const ay_event_t *ev) {
    if ((q->head - q->tail) >= AUDIO_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }
    q->buffer[q->head & 0xFF] = *ev;
    __dmb();
    q->head++;
    return true;
}

// Producer (Core 1): Non-blocking push for register write (existing API). Returns false on full (event dropped).
static __always_inline bool audio_queue_push(audio_queue_t *q, uint8_t reg, uint8_t value) {
    if ((q->head - q->tail) >= AUDIO_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }
    q->buffer[q->head & 0xFF].type = AY_EVENT_REGISTER_WRITE;
    q->buffer[q->head & 0xFF].reg = reg;
    q->buffer[q->head & 0xFF].value = value;
    __dmb();
    q->head++;
    return true;
}

// Producer (Core 1): Non-blocking push for AY RESET. Returns false on full (event dropped).
static __always_inline bool audio_queue_push_reset(audio_queue_t *q) {
    if ((q->head - q->tail) >= AUDIO_QUEUE_CAPACITY) {
        q->overflow_count++;
        return false;
    }
    q->buffer[q->head & 0xFF].type = AY_EVENT_RESET;
    q->buffer[q->head & 0xFF].reg = 0;
    q->buffer[q->head & 0xFF].value = 0;
    __dmb();
    q->head++;
    return true;
}

// Consumer (Core 0): Pop event. Returns false on empty.
static inline bool audio_queue_pop(audio_queue_t *q, ay_event_t *ev) {
    if (q->head == q->tail) {
        return false;
    }
    *ev = q->buffer[q->tail & 0xFF];
    __dmb();
    q->tail++;
    return true;
}

#endif // _AUDIO_EVENT_H
