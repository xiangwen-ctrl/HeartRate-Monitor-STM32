#include "pulse_detect.h"
#include <string.h>

static void ClearReading(PulseDetector *s)
{
    s->bpm = 0;
    s->ibi = 0;
    s->have_valid = 0;
    s->interval_count = 0;
    s->interval_index = 0;
}

void PulseDetector_Init(PulseDetector *s)
{
    memset(s, 0, sizeof(*s));
}

void PulseDetector_Expire(PulseDetector *s, uint32_t now_ms)
{
    /* Invalid crossings never refresh last_valid_ms. Unsigned subtraction
       also handles the 32-bit millisecond counter wrapping around. */
    if ((s->have_valid && (uint32_t)(now_ms - s->last_valid_ms) > PULSE_TIMEOUT_MS) ||
        (s->have_sample && (uint32_t)(now_ms - s->last_sample_ms) > PULSE_MAX_SAMPLE_GAP_MS)) {
        ClearReading(s);
    }
    if (s->have_edge && (uint32_t)(now_ms - s->last_edge_ms) > PULSE_TIMEOUT_MS) {
        s->have_edge = 0;
        s->interval_count = 0;
        s->interval_index = 0;
    }
}

static void UpdateThresholds(PulseDetector *s)
{
    uint16_t i, min = 4095U, max = 0;
    uint32_t amplitude;
    for (i = 0; i < PULSE_WINDOW_SIZE; ++i) {
        if (s->samples[i] < min) min = s->samples[i];
        if (s->samples[i] > max) max = s->samples[i];
    }
    amplitude = (uint32_t)max - min;
    s->signal_ok = (uint8_t)(amplitude >= 20U);
    /* High threshold rejects the lower secondary peak in the regression
       waveform; a separate low threshold prevents threshold chatter.
       Equal-height double peaks cannot be distinguished by amplitude alone. */
    s->low = (uint16_t)(min + amplitude / 2U);
    s->high = (uint16_t)(min + amplitude * 4U / 5U);
    if (!s->signal_ok) {
        ClearReading(s);
        s->have_edge = 0;
        s->above = 0;
    }
}

void PulseDetector_Process(PulseDetector *s, uint16_t sample, uint32_t now_ms)
{
    uint32_t interval, sum;
    uint8_t i;

    if (s->have_sample &&
        (uint32_t)(now_ms - s->last_sample_ms) > PULSE_MAX_SAMPLE_GAP_MS) {
        PulseDetector_Init(s); /* Do not bridge a lost section of acquisition. */
    }
    PulseDetector_Expire(s, now_ms);
    s->last_sample_ms = now_ms;
    s->have_sample = 1;
    s->samples[s->index] = sample;
    s->index = (uint16_t)((s->index + 1U) % PULSE_WINDOW_SIZE);
    if (s->count < PULSE_WINDOW_SIZE) {
        ++s->count;
        if (s->count < PULSE_WINDOW_SIZE) return;
        UpdateThresholds(s);
        s->above = (uint8_t)(sample > s->low);
        return; /* Warm-up must not manufacture an edge. */
    }
    if (s->index % 25U == 0U) UpdateThresholds(s);
    if (!s->signal_ok) return;

    if (sample <= s->low) s->above = 0;
    if (s->above || sample <= s->high) return;
    s->above = 1;
    if (!s->have_edge) {
        s->have_edge = 1;
        s->last_edge_ms = now_ms;
        return;
    }
    interval = (uint32_t)(now_ms - s->last_edge_ms);
    s->last_edge_ms = now_ms; /* Candidate interval, NOT output freshness. */
    if (interval < 300U || interval > 2000U) {
        s->interval_count = 0;
        s->interval_index = 0;
        return;
    }
    s->intervals[s->interval_index] = (uint16_t)interval;
    s->interval_index = (uint8_t)((s->interval_index + 1U) % 4U);
    if (s->interval_count < 4U) ++s->interval_count;
    /* Require two consecutive plausible intervals before publishing. */
    if (s->interval_count < 2U) return;
    sum = 0;
    for (i = 0; i < s->interval_count; ++i) sum += s->intervals[i];
    s->ibi = (uint16_t)((sum + s->interval_count / 2U) / s->interval_count);
    s->bpm = (uint16_t)((60000U * s->interval_count + sum / 2U) / sum);
    s->last_valid_ms = now_ms;
    s->have_valid = 1;
}
