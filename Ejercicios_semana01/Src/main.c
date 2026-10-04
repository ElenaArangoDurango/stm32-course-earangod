/**
 ******************************************************************************
 * @file           : Ejercicios semana 01
 * @author         : Elena Arango Durango
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2025 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include <stdint.h>


void ejercicio_1_1 (void)
{
    uint8_t a          = 17;
    uint8_t b          = 5;
    uint8_t div_result = a / b; // suposición : 3
    uint8_t mod_result = a % b; // suposición : 2
    uint8_t mul_result = a * b; // suposición : 85

    uint8_t check = div_result * b + mod_result; // suposición : 17


}

void ejercicio_1_2(void)
{
    // PREDICCIÓN: sum = 44
    uint8_t x   = 200;
    uint8_t y   = 100;
    uint8_t sum = x + y;
}  

void ejercicio_1_3(void)
{
    uint8_t val = 3;

    uint8_t left1  = val << 1; // va a mover ese valor de 3 (0000 0011) a la izquierda una posición ( 0000 0110) resultando en 6
    uint8_t left2  = val << 2; // va a moverlo 2 veces resultando en 12 (0000 1100)
    uint8_t left3  = val << 3; // va a moverlo 3 veces resultando en 24 (0001 1000)
    uint8_t right1 = val >> 1; // va a moverlo 1 vez a la derecha resultando en 1 (0000 0001)

    uint8_t var = 128; // 1000 0000

    uint8_t left1_var  = var << 1; // va a moverlo 1 vez a la izquierda resultando en 0 (0000 0000) ya que se pierde el bit más significativo

}

int main(void)
{
    ejercicio_1_3();

    while(1)
   {}

}
