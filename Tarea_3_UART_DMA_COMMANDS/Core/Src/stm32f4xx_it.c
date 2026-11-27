/*
 Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
 */

#include "stm32f4xx_hal.h"

// Salta cada 1 ms y le suma uno al contador del HAL, sin esto no sirven HAL_Delay ni los tiempos de espera
void SysTick_Handler(void)
{
    HAL_IncTick();
}
