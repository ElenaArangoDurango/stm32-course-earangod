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

void ejercicio_1_4(void)
{
    uint8_t x      = 4;   // distinto de cero (verdadero para C)
    uint8_t y      = 0;   // cero (falso para C)
    uint8_t z      = 9;
    uint8_t result = 0;   // empieza en 0 para ver cuándo cambia

    // Bloque 1: un valor distinto de cero como condición directa
    // PREDICCIÓN: result = 10
    if (x)
    {
        result = 10;      // solo se ejecuta si x != 0
    }
    // breakpoint aquí

    // Bloque 2: un valor cero como condición directa, con else
    // PREDICCIÓN: result = 30
    if (y)
    {
        result = 20;      // se ejecuta solo si y != 0
    }
    else
    {
        result = 30;      // se ejecuta si y == 0
    }
    // breakpoint aquí

    // Bloque 3: comparación de igualdad
    // PREDICCIÓN: result = 50
    if (x == y)
    {
        result = 40;      // se ejecuta si x e y son iguales
    }
    else
    {
        result = 50;      // se ejecuta si son distintos
    }
    
}

void ejercicio_1_5(void)
{
    uint8_t x;        // controla el bucle (lo cambia el for)
    uint8_t counter;  // cuenta las vueltas (lo cambia counter++)

    // Versión original: contar de 0 a 9
    // x toma: 0, 1, 2, ..., 9 y termina cuando x vale 10
    // PREDICCIÓN: counter = 9
    counter = 0;
    for (x = 0; x <= 9; x = x + 1)
    {
        counter++;
    }
  

    // Modificación 1: pasos de 2
    // x toma: 0, 2, 4, 6, 8 y termina cuando x vale 10
    // PREDICCIÓN: counter = 10
    counter = 0;
    for (x = 0; x <= 9; x = x + 2)
    {
        counter++;
    }
    

    // Modificación 2: hacia atrás, de 10 a 1
    // x toma: 10, 9, 8, ..., 1 y termina cuando x vale 0
    // PREDICCIÓN: counter = 10
    counter = 0;
    for (x = 10; x >= 1; x = x - 1)
    {
        counter++;
    }
    
    // Modificación 3: detenerse en otro valor
    // x toma: 0, 1, 2, 3, 4 y termina cuando x vale 5
    // PREDICCIÓN: counter = 5
    counter = 0;
    for (x = 0; x < 5; x = x + 1)
    {
        counter++;
    }
    
}

void ejercicio_1_6(void)
{
   

        // PREDICCIÓN 1: sum = 55 PREDICCIÓN 2: sum = 5050
        uint8_t counter = 0;
        uint16_t sum = 0;
        while (counter < 100)
        {
            counter++;
            sum = sum + counter;
        }
    

}


void ejercicio_1_7(void)
{
    uint8_t x = 2;   // distinto de 0, así que la condición (x == 0) es FALSA

    // Pieza 1: while
    // PREDICCIÓN: resultado_while = 0, porque la condición es falsa
    // desde el inicio y el cuerpo nunca se ejecuta
    uint8_t resultado_while = 0;
    while (x == 0)
    {
        resultado_while = 42;
    }

    // Pieza 2: do-while, con LA MISMA condición
    // PREDICCIÓN: resultado_do = 42, porque el cuerpo se ejecuta una vez
    // antes de evaluar la condición
    uint8_t resultado_do = 0;
    do
    {
        resultado_do = 42;
    } while (x == 0);   // este bloque termina con ;

    // Diferencia: el while evalúa la condición ANTES de ejecutar el cuerpo,
    // y puede no ejecutarlo nunca. El do-while evalúa DESPUÉS, así que su
    // cuerpo se ejecuta al menos una vez.
}

int main(void)
{
    ejercicio_1_7();

    while (1)
    {
    }
}