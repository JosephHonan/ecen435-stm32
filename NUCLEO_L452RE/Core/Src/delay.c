/* delay.c - microsecond delays from TIM2 (ECEN-435 Lab 5 handout version,
 * header changed from stm32f0xx_hal.h to stm32l4xx_hal.h for the L452).
 * TIM2 runs at 1 MHz: prescaler 79 on the 80 MHz timer clock. */
#include "delay.h"
#include "stm32l4xx_hal.h"

extern TIM_HandleTypeDef htim2;

void delay_init(void)
{
    HAL_TIM_Base_Start(&htim2);
}

void delay_us(u32 nus)
{
    __HAL_TIM_SET_COUNTER(&htim2, 0);
    while (__HAL_TIM_GET_COUNTER(&htim2) < nus);
}

void delay_ms(u16 nms)
{
    HAL_Delay(nms);
}
