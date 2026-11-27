/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
PWM en PB7 con TIM4 canal 2, a 1 kHz. Este es el único archivo que toca el timer,
commands.c solo llama a estas funciones. */
#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

void     pwm_init(void);	// arranca la salida en 0 %
bool     pwm_set_duty(uint32_t percent);	// de 0 a 100, devuelve false si está fuera de rango o no se inició
uint32_t pwm_get_duty(void);	// devuelve el último ciclo de trabajo en porcentaje

#endif
