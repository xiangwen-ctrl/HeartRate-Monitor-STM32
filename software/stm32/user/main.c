#include "stm32f10x.h"
#include "delay.h"
#include "led.h"
#include "buzzer.h"
#include "lcd.h"
#include "adc.h"
#include <stdio.h>

int BPM_Threshold = 100;

void JTAG_SW_Config(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
}

int main(void)
{
    uint8_t ledflag = 0, key;
    uint32_t last_display, last_key, now;
    char buf[30];

    JTAG_SW_Config();
    SystemCoreClockUpdate();
    Systick_Init(SystemCoreClock / 1000U);
    LED_Config();
    BUZZER_Config();
    KEY_Config();
    LCD_Init();
    LCD_Clear(BLACK);
    LCD_ShowChinese(30, 5, (u8*)"心率检测器", BLUE, BLACK, 16, 1);
    PulseSensor_Config();
    last_display = last_key = Millis();

    while (1) {
        /* Acquisition continues in TIM3/ADC/DMA while the LCD is busy. */
        get_BPM();
        if (BPM > 0 && BPM > BPM_Threshold) GPIO_SetBits(GPIOA, GPIO_Pin_6);
        else GPIO_ResetBits(GPIOA, GPIO_Pin_6);

        now = Millis();
        if ((uint32_t)(now - last_key) >= 10U) {
            last_key = now;
            key = KEY_Scan();
            if (key == 1 && BPM_Threshold < 180) BPM_Threshold += 5;
            if (key == 2 && BPM_Threshold > 40) BPM_Threshold -= 5;
        }
        if ((uint32_t)(now - last_display) >= 200U) {
            last_display = now;
            ledflag = (uint8_t)!ledflag;
            if (ledflag) GPIO_ResetBits(GPIOC, GPIO_Pin_0);
            else GPIO_SetBits(GPIOC, GPIO_Pin_0);
            sprintf(buf, "BPM: %3u   ", (unsigned int)BPM);
            LCD_ShowString(30, 60, (u8*)buf, WHITE, BLACK, 16, 0);
            sprintf(buf, "Limit: %3d ", BPM_Threshold);
            LCD_ShowString(30, 90, (u8*)buf, YELLOW, BLACK, 16, 0);
        }
    }
}
