#ifndef PULSE_DETECT_H
#define PULSE_DETECT_H

#include <stdint.h>

#define PULSE_WINDOW_SIZE 250U
#define PULSE_TIMEOUT_MS 2500U
#define PULSE_MAX_SAMPLE_GAP_MS 100U

typedef struct {
    uint16_t samples[PULSE_WINDOW_SIZE];
    uint16_t index, count, low, high;
    uint16_t intervals[4];
    uint16_t bpm, ibi;
    uint8_t interval_count, interval_index;
    uint8_t signal_ok, above, have_edge, have_sample, have_valid;
    uint32_t last_sample_ms, last_edge_ms, last_valid_ms;
} PulseDetector;

void PulseDetector_Init(PulseDetector *state);
void PulseDetector_Process(PulseDetector *state, uint16_t sample, uint32_t now_ms);
void PulseDetector_Expire(PulseDetector *state, uint32_t now_ms);

#endif
