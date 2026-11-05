#ifndef __ADC_H
#define __ADC_H
#include "stm32f10x.h"

extern uint16_t IBI, BPM;
void PulseSensor_Config(void);
/* Drain timestamped DMA blocks and expire stale output; call every main loop. */
void get_BPM(void);
#endif
