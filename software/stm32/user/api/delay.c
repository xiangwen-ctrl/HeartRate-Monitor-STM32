#include "delay.h"

static volatile uint32_t system_ms;

void SysTick_Handler(void)
{
    ++system_ms;
}

uint32_t Millis(void)
{
    /* An aligned 32-bit read is atomic on Cortex-M3. The foreground never
       writes this ISR-owned counter, so ticks cannot be lost by clearing it. */
    return system_ms;
}

void Systick_Init(uint32_t load)
{
    system_ms = 0;
    if (SysTick_Config(load) != 0) while (1);
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void Delay_us(uint32_t time)
{
    while (time--) { delay_1us(); }
}

void Delay_ms(uint32_t time)
{
    uint32_t start = Millis();
    while ((uint32_t)(Millis() - start) < time);
}
