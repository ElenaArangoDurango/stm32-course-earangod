/* Core/common/system.h
 * Author: earangod@unal.edu.co
 Configuración del reloj y los errores  que se comparten en todas las etapas 
 */
#ifndef SYSTEM_H
#define SYSTEM_H

void SystemClock_Config(void); // Configura el reloj a 16 MHz con el HSI 
void Error_Handler(void);   // Apaga las interrupciones y se queda en un bucle infinito 

#endif
