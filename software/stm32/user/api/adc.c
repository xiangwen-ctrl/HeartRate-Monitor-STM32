#include "adc.h"
#include "delay.h"
#include "pulse_detect.h"
#include "pulse_buffer.h"

uint16_t IBI = 0;
uint16_t BPM = 0;

static PulseDetector detector;
static PulseBuffer pending;
static volatile uint16_t dma_samples[20];

void PulseSensor_Config(void)
{
    GPIO_InitTypeDef gpio;
    ADC_InitTypeDef adc;
    DMA_InitTypeDef dma;
    TIM_TimeBaseInitTypeDef timer;
    RCC_ClocksTypeDef clocks;
    uint32_t timer_clock;

    PulseDetector_Init(&detector);
    PulseBuffer_Init(&pending);
    BPM = IBI = 0;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC | RCC_APB2Periph_ADC1, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    GPIO_StructInit(&gpio);
    gpio.GPIO_Pin = GPIO_Pin_3;
    gpio.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOC, &gpio);

    DMA_DeInit(DMA1_Channel1);
    DMA_StructInit(&dma);
    dma.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    dma.DMA_MemoryBaseAddr = (uint32_t)dma_samples;
    dma.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma.DMA_BufferSize = 20;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_High;
    DMA_Init(DMA1_Channel1, &dma);
    DMA_ClearITPendingBit(DMA1_IT_GL1);
    DMA_ITConfig(DMA1_Channel1, DMA_IT_HT | DMA_IT_TC | DMA_IT_TE, ENABLE);
    NVIC_SetPriority(DMA1_Channel1_IRQn, 1);
    NVIC_EnableIRQ(DMA1_Channel1_IRQn);

    ADC_StructInit(&adc);
    adc.ADC_Mode = ADC_Mode_Independent;
    adc.ADC_ScanConvMode = DISABLE;
    adc.ADC_ContinuousConvMode = DISABLE;
    adc.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
    adc.ADC_DataAlign = ADC_DataAlign_Right;
    adc.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &adc);
    ADC_RegularChannelConfig(ADC1, ADC_Channel_13, 1, ADC_SampleTime_55Cycles5);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));

    /* TIM3 update TRGO: 1 kHz ADC; each DMA half is 10 samples / 10 ms.
       Derive the prescaler from the actual APB timer clock, including x2. */
    RCC_GetClocksFreq(&clocks);
    timer_clock = clocks.PCLK1_Frequency;
    if (clocks.PCLK1_Frequency != clocks.HCLK_Frequency) timer_clock *= 2U;
    TIM_TimeBaseStructInit(&timer);
    timer.TIM_Prescaler = (uint16_t)(timer_clock / 1000000U - 1U);
    timer.TIM_Period = 999;
    TIM_TimeBaseInit(TIM3, &timer);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
}

void DMA1_Channel1_IRQHandler(void)
{
    uint8_t half = (uint8_t)(DMA_GetITStatus(DMA1_IT_HT1) != RESET);
    uint8_t full = (uint8_t)(DMA_GetITStatus(DMA1_IT_TC1) != RESET);
    if (DMA_GetITStatus(DMA1_IT_TE1) != RESET || (half && full)) {
        /* Both halves pending means the ISR missed a buffer boundary. Never
           publish overwritten data or invent timestamps for a missing block. */
        DMA_ClearITPendingBit(DMA1_IT_GL1);
        pending.fault = 1;
        return;
    }
    if (half) {
        DMA_ClearITPendingBit(DMA1_IT_HT1);
        PulseBuffer_Push(&pending, PulseSamples_TrimmedMean(dma_samples), Millis());
    }
    if (full) {
        DMA_ClearITPendingBit(DMA1_IT_TC1);
        PulseBuffer_Push(&pending, PulseSamples_TrimmedMean(dma_samples + 10), Millis());
    }
}

void get_BPM(void)
{
    PulseSample sample;
    int status;
    uint32_t mask;
    do {
        /* Preserve the caller's IRQ state. Only the queue pop is protected;
           the algorithm and LCD never run with interrupts disabled. */
        mask = __get_PRIMASK();
        __disable_irq();
        status = PulseBuffer_Pop(&pending, &sample);
        __set_PRIMASK(mask);
        if (status < 0) PulseDetector_Init(&detector);
        else if (status > 0) PulseDetector_Process(&detector, sample.value, sample.time_ms);
    } while (status != 0);
    PulseDetector_Expire(&detector, Millis());
    BPM = detector.bpm;
    IBI = detector.ibi;
}
