#ifndef PULSE_BUFFER_H
#define PULSE_BUFFER_H

#include <stdint.h>

#define PULSE_BUFFER_SIZE 32U
typedef struct {
    uint16_t value;
    uint32_t time_ms;
} PulseSample;

typedef struct {
    volatile PulseSample samples[PULSE_BUFFER_SIZE];
    volatile uint8_t read_index, write_index, fault;
} PulseBuffer;

/* Single ISR producer; the foreground consumer must mask interrupts around
   Pop. On data loss Pop discards the partial history and returns -1. */
void PulseBuffer_Init(PulseBuffer *buffer);
void PulseBuffer_Push(PulseBuffer *buffer, uint16_t value, uint32_t time_ms);
int PulseBuffer_Pop(PulseBuffer *buffer, PulseSample *sample);
uint16_t PulseSamples_TrimmedMean(const volatile uint16_t *samples);

#endif
