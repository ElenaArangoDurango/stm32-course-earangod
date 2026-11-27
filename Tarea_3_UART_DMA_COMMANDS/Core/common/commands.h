/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co 
Interfaz */
#ifndef COMMANDS_H
#define COMMANDS_H

#include <stddef.h> // es el size_t 
#include "cmd_parser.h" // da cmd_t


//recibe el comando analizado, el espacio donde escribir la respuesta y el tamaño 
// de ese espacio
void commands_execute(const cmd_t *cmd, char *reply, size_t reply_size);
// Guarda las líneas 2, 3 y 10, evitando incluir el archivo 2 veces 
// const cmd_t *cmd indica que la función solo lee el comando y no lo modifica.
// deja la respuesta en reply, como una línea sin final de línea
#endif
