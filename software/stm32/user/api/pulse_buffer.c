#include "pulse_buffer.h"

uint16_t PulseSamples_TrimmedMean(const volatile uint16_t *samples)
{
    uint32_t sum = 0;
    uint16_t min = 4095U, max = 0, value;
    uint8_t i;
    for (i = 0; i < 10U; ++i) {
        value = samples[i];
        sum += value;
        if (value < min) min = value;
        if (value > max) max = value;
    }
    return (uint16_t)((sum - min - max) / 8U);
}

void PulseBuffer_Init(PulseBuffer *b)
{
    b->read_index = 0;
    b->write_index = 0;
    b->fault = 0;
}

void PulseBuffer_Push(PulseBuffer *b, uint16_t value, uint32_t time_ms)
{
    uint8_t next = (uint8_t)((b->write_index + 1U) % PULSE_BUFFER_SIZE);
    if (b->fault || next == b->read_index) {
        b->fault = 1;
        return;
    }
    b->samples[b->write_index].value = value;
    b->samples[b->write_index].time_ms = time_ms;
    b->write_index = next;
}

int PulseBuffer_Pop(PulseBuffer *b, PulseSample *sample)
{
    if (b->fault) {
        b->read_index = b->write_index;
        b->fault = 0;
        return -1;
    }
    if (b->read_index == b->write_index) return 0;
    sample->value = b->samples[b->read_index].value;
    sample->time_ms = b->samples[b->read_index].time_ms;
    b->read_index = (uint8_t)((b->read_index + 1U) % PULSE_BUFFER_SIZE);
    return 1;
}
