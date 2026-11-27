/* Core/common/system.c
 * Es el sistema del reloj y el manejo de errores que se comparte en las etapas
 * Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
 */
#include "stm32f4xx_hal.h" // Tipos y funciones del HAL
#include "system.h" // Son los prototipos propios para que el compilador revise si coinciden

/*
 SystemClock_Config
 Usa el oscilador interno HSI a 16 MHz 
 */
void SystemClock_Config(void)
{
    // Son los parametros del oscilador que se encuentran en cero 
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // No se toca el PLL, el HSI está encendido y se usa como reloj del sistema
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
    RCC_OscInitStruct.HSIState            = RCC_HSI_ON;
    RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_NONE;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    // Se configuran los buses y el reloj del sistema para que estén a 16 MHz
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_HCLK   |
                                       RCC_CLOCKTYPE_PCLK1  |
                                       RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_HSI;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;   /* HCLK  = 16 MHz */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;     /* APB1  = 16 MHz */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;     /* APB2  = 16 MHz */

    // Son los parámetros del flash, no hay estados de espera 
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0);
}

// Se llama cuando una función del HAL detecta un error, se apagan las interrupciones

void Error_Handler(void)
{
    __disable_irq();   // Bloquea todas las interrupciones, el heartbeat deja de parpadear y la CPU entra en un bucle
    while (1) {
       // se queda en el bucle y el depurador puede ver que llamada falló
    }
}

// TIM2 y USART2 cuelgan de APB1 