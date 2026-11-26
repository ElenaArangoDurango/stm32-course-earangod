/* Core/app_0_blinky/app.c — Stage 0: no UART yet
 * Author: earangod@unal.edu.co
 
 */
#include "app.h"
#include "stm32f4xx_hal.h"
#include "heartbeat.h"

void app_init(void)
{
    /*
     Prueba de la etapa 0: estas dos líneas se usaron para comprobar el cambio
     de frecuencia del blinky. Después de 5 segundos, heartbeat_set_period(1000U)
     pasa el parpadeo de 2 Hz (250 ms) a 0,5 Hz (1000 ms). 
     */
     // HAL_Delay(5000U);
     // heartbeat_set_period(1000U);
     
  // En la etapa 0 no hay código porque el LED se maneja desde una interrpción
}

void app_loop(void)
{
    // Main la llama, pero no se hace nada porque el LED se maneja desde una interrupción
}
