/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co */
#ifndef CMD_PARSER_H
#define CMD_PARSER_H

#include <stdbool.h>
#include <stdint.h> // da uint8_t , que se usa para argc

#define CMD_MAX_ARGS  4U // es el max de argumentos que acepta un comando 

typedef enum { // Es una lista de nombres que valen 0,1,2,3,4 en orden,
    // Cada comando tiene un identificador, hay uno extra para los que no se conocen

    CMD_HELP,
    CMD_INFO,
    CMD_BLINK,
    CMD_ECHO,
    CMD_PWM,	// nuevo, la identidad del comando pwm
    CMD_UNKNOWN
} cmd_id_t;

typedef struct {
    cmd_id_t    id; // Elige cuál comando es, lo usa en el switch de commands_execute 
    const char *name; // La primera palabra tal y como se escribió sirve para el mensaje de reboot
    uint8_t     argc; // Nos dice cuantos argumentos vienen después del nombre, para help es 0 y para
    // blink 500 es 1
    const char *argv[CMD_MAX_ARGS]; // Lista de hasta 4 punteros, uno por argumento
    bool        too_many_args; // Es true si la línea tiene mas argumentos de los permitidos
} cmd_t;

bool cmd_parse(char *line, cmd_t *cmd);

#endif // evita que el contenido se lea dos veces si el archivo se incluye desde varios lugares
