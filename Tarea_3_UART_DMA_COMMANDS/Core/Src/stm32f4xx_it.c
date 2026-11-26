/*
 Author: earangod@unal.edu.co
 */

#include "stm32f4xx_hal.h"

/* SysTick handler — required by HAL for HAL_Delay() and timeouts */
void SysTick_Handler(void)
{
    HAL_IncTick();
}
