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

int main(void)
{
    ejercicio_1_2();

    while(1)
   {}

}
