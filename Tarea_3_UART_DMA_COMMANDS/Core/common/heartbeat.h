/* Core/common/heartbeat.h
El LED externo en PC0 parpadea indicando que todo funciona adecuadamente (antes era LD2 en PA5).
 * Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
 */
#ifndef HEARTBEAT_H
#define HEARTBEAT_H

#include <stdbool.h> // Es un dato de tipo booleano, true o false 
#include <stdint.h> // Determina el tamaño de los datos 


#define HEARTBEAT_MIN_MS    50U // Define el periodo mínimo de parpadeo, no parpadea más rápido que esto
#define HEARTBEAT_MAX_MS  5000U // Define el periodo máximo permitido 

// Aquí se le dice al programa que puede pedirle al heartbeat 
void     heartbeat_init(uint32_t toggle_ms); // Se le pide que arranque el blinky con este periodo
bool     heartbeat_set_period(uint32_t toggle_ms);  // Cambia el periodo a este valor, devuelve true o false 
uint32_t heartbeat_get_period(void); // pregunta por el periodo actual y devuelve el valor en ms,

#endif
