/* Regression tests compile the SAME C sources used by the firmware. */
#include "pulse_detect.h"
#include "pulse_buffer.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

static uint32_t seed;
static uint16_t Ppg(uint32_t t, unsigned bpm, int double_peak, int noise)
{
    double phase, value;
    if (double_peak) {
        phase = (double)(t % 1000U) / 1000.0;
        value = 2048.0 + 400.0 * exp(-pow((phase - .2) / .045, 2.0) / 2.0)
                      + 300.0 * exp(-pow((phase - .55) / .045, 2.0) / 2.0);
    } else if (bpm) {
        double period = 60000.0 / bpm;
        phase = fmod((double)t, period) / period;
        value = 2048.0 + 400.0 * exp(-pow((phase - .25) / .08, 2.0) / 2.0);
    } else value = 2048.0;
    seed = (seed * 1103515245U + 12345U) & 0x7fffffffU;
    value += (((double)((seed >> 16) % 1000U) / 1000.0) - .5) * 2.0 * noise;
    if (value < 0) value = 0;
    if (value > 4095) value = 4095;
    return (uint16_t)value;
}

static void TestRange(void)
{
    unsigned bpm;
    double worst = 0;
    for (bpm = 30; bpm <= 200; ++bpm) {
        PulseDetector s;
        uint32_t t, sum = 0, n = 0, updates = 0, last_update = 0;
        volatile uint16_t raw[10];
        unsigned j;
        PulseDetector_Init(&s);
        seed = 1;
        for (t = 0; t < 60000U; t += 10U) {
            /* Match TIM3's 1kHz stream and the firmware's 10-point block filter. */
            for (j = 0; j < 10; ++j) raw[j] = Ppg(t + j, bpm, 0, 3);
            PulseDetector_Process(&s, PulseSamples_TrimmedMean(raw), t + 9U);
            if (t >= 30000U) {
                CHECK(s.bpm >= 30 && s.bpm <= 200);
                sum += s.bpm;
                ++n;
                if (s.last_valid_ms != last_update) ++updates;
                last_update = s.last_valid_ms;
            }
        }
        {
            double error = fabs((double)sum / n - bpm);
            if (error > worst) worst = error;
            if (error >= 1.0) fprintf(stderr, "BPM %u mean %.3f\n", bpm, (double)sum / n);
            CHECK(error < 1.0);
            CHECK(updates >= bpm / 3U); /* A held/stale value cannot pass. */
        }
    }
    printf("PASS: all 171 integer BPMs, max steady mean error %.3f BPM\n", worst);
}

static void TestInvalidAndRecovery(void)
{
    PulseDetector s;
    uint32_t t;
    PulseDetector_Init(&s);
    seed = 1;
    for (t = 0; t < 45000U; t += 10U) {
        PulseDetector_Process(&s, Ppg(t < 15000U ? t : t - 15000U,
                                     t < 15000U ? 60U : 240U, 0, 3), t);
        if (t >= 10000U && t < 15000U) CHECK(s.bpm == 60);
        if (t >= 18000U) CHECK(s.bpm == 0);
    }
    /* Recover after invalid input, then lose contact and recover again. */
    for (; t < 60000U; t += 10U) {
        PulseDetector_Process(&s, Ppg(t, 72, 0, 3), t);
        if (t >= 55000U) CHECK(s.bpm >= 71 && s.bpm <= 73);
    }
    for (; t < 65000U; t += 10U) {
        PulseDetector_Process(&s, Ppg(t, 0, 0, 3), t);
        if (t >= 63000U) CHECK(s.bpm == 0);
    }
    for (; t < 80000U; t += 10U) {
        PulseDetector_Process(&s, Ppg(t, 120, 0, 3), t);
        if (t >= 75000U) CHECK(s.bpm == 120);
    }
    PulseDetector_Expire(&s, t + 101U);
    CHECK(s.bpm == 0 && s.ibi == 0);
    puts("PASS: sustained 240 BPM expires, contact loss and recovery, stopped ADC");
}

static void TestDoublePeak(void)
{
    PulseDetector s;
    uint32_t t;
    int noise;
    for (noise = 0; noise <= 3; noise += 3) {
        PulseDetector_Init(&s);
        seed = 1;
        for (t = 0; t < 45000U; t += 10U) {
            PulseDetector_Process(&s, Ppg(t, 60, 1, noise), t);
            if (t >= 10000U) CHECK(s.bpm == 60);
        }
    }
    puts("PASS: reported 400/300-amplitude double peak stays at 60 BPM, with/without noise");
}

static void TestTimestamps(void)
{
    unsigned mode;
    for (mode = 0; mode < 3; ++mode) {
        PulseDetector s;
        uint32_t t = 0, sum = 0, n = 0, i = 0;
        uint32_t origin = mode == 2 ? 0xffffff00U : 0U;
        const uint32_t jitter[] = {7, 13, 9, 11};
        PulseDetector_Init(&s);
        seed = 1;
        while (t < 60000U) {
            PulseDetector_Process(&s, Ppg(t, 72, 0, 3), origin + t);
            if (t >= 30000U) { sum += s.bpm; ++n; CHECK(s.bpm > 0); }
            t += mode == 1 ? jitter[i++ % 4U] : 11U;
        }
        CHECK(fabs((double)sum / n - 72.0) < 1.0);
        PulseDetector_Process(&s, 2048, origin + t + 200U);
        CHECK(s.bpm == 0 && s.count == 1);
    }
    puts("PASS: 11ms sampling, irregular timestamps, uint32 wrap, missing samples");
}

static void TestBuffer(void)
{
    PulseBuffer b;
    PulseSample sample;
    PulseDetector s;
    uint32_t t;
    unsigned i;
    volatile uint16_t block[10] = {0, 4095, 2000, 2000, 2000, 2000, 2000, 2000, 2000, 2000};
    CHECK(PulseSamples_TrimmedMean(block) == 2000);
    PulseBuffer_Init(&b);
    CHECK(PulseBuffer_Pop(&b, &sample) == 0);
    for (i = 0; i < 1000; ++i) {
        PulseBuffer_Push(&b, (uint16_t)i, 0xffffff00U + i * 10U);
        CHECK(PulseBuffer_Pop(&b, &sample) == 1);
        CHECK(sample.value == i && sample.time_ms == 0xffffff00U + i * 10U);
    }
    for (i = 0; i < PULSE_BUFFER_SIZE; ++i) PulseBuffer_Push(&b, 2000, i * 10U);
    CHECK(PulseBuffer_Pop(&b, &sample) == -1);
    CHECK(PulseBuffer_Pop(&b, &sample) == 0);
    PulseBuffer_Push(&b, 1234, 999);
    CHECK(PulseBuffer_Pop(&b, &sample) == 1 && sample.value == 1234);

    PulseBuffer_Init(&b);
    PulseDetector_Init(&s);
    seed = 1;
    for (t = 0; t < 60000U; t += 10U) {
        PulseBuffer_Push(&b, Ppg(t, 72, 0, 3), t);
        /* Model foreground LCD pauses without changing acquisition timestamps. */
        if (t % 150U == 0) {
            while (PulseBuffer_Pop(&b, &sample) == 1)
                PulseDetector_Process(&s, sample.value, sample.time_ms);
            CHECK(!b.fault);
            PulseDetector_Expire(&s, t);
            if (t >= 30000U) CHECK(s.bpm >= 71 && s.bpm <= 73);
        }
    }
    puts("PASS: DMA trim filter, queue order/wrap/overflow, 150ms foreground pauses");
}

int main(void)
{
    TestRange();
    TestInvalidAndRecovery();
    TestDoublePeak();
    TestTimestamps();
    TestBuffer();
    printf("PASS: %u assertions against firmware C sources\n", checks);
    return 0;
}
