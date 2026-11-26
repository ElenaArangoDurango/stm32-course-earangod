/* Autor: earangod@unal.edu.co
Una salida PWM, TIM4 canal 2 en PB7 a 1 kHz.
commands.c decide qué significa pwm 25 y este módulo es el único que toca el temporizador. */
#ifndef PWM_H
#define PWM_H

#include <stdbool.h>
#include <stdint.h>

void     pwm_init(void);	// arranca la salida en 0 %
bool     pwm_set_duty(uint32_t percent);	// de 0 a 100, devuelve false si está fuera de rango o no se inició
uint32_t pwm_get_duty(void);	// devuelve el último ciclo de trabajo en porcentaje

#endif
