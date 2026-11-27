/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co 
Recibe una línea como blink   500, la parte en palabras, busca la primera en una tabla 
y deja el resto como argumentos. No usa memoria extra, trabaja sobre la misma línea.*/
#include <string.h> // trae strcmp, que compara dos textos
#include "cmd_parser.h" // trae cmd_t, el enumerado y el prototipo de cmd_parse.

typedef struct {
    const char *name; // una fila de la tabla, con nombre e identificador
    cmd_id_t    id;
} cmd_entry_t;

static const cmd_entry_t cmd_table[] = { // la tabla completa de las respuestas
    { "help",  CMD_HELP  },
    { "info",  CMD_INFO  },
    { "blink", CMD_BLINK },
    { "echo",  CMD_ECHO  },
    { "pwm",   CMD_PWM   },	// conecta la palabra pwm con su identidad
    // se pueden agregar comandos aquí añadiendo una fila y un case en commands.c
};

#define CMD_TABLE_LEN  (sizeof cmd_table / sizeof cmd_table[0])
// El tamaño de toda la tabla dividido por el tamaño de una fila da cuántas filas hay. 
// Si se agrega una fila, el número se ajusta solo y no hay que actualizarlo.

static bool is_separator(char c);
// Se declara antes porque cmd_parse la usa antes de que esté escrita.
//  Es static, así que solo existe en este archivo.

bool cmd_parse(char *line, cmd_t *cmd)
{
    const char *words[CMD_MAX_ARGS + 1U]; // es una lista de 5 punteros, uno por cada palabra, cuenta también el nombre del comando 
    uint8_t word_count = 0U; // Guarda cuantas palabras lleva encontradas
    char *cursor = line; //un puntero que recorre la línea carácter por carácter 

    cmd->too_many_args = false; // parte sin error, se pone en true solo si sobran palabras

    while (*cursor != '\0') { // el ciclo corre mientras el cursor no apunte al \0
        if (is_separator(*cursor)) {
            *cursor = '\0';
            // Aquí se parte la línea, cada separador se cambia por un '\0'.
            // Por eso line no puede ser const, se escribe encima.
            cursor++;
        } else if (word_count == (CMD_MAX_ARGS + 1U)) { // ya hay demasiadas palabras
            cmd->too_many_args = true;
            break;
        } else {
            // Guarda dónde empieza la palabra, cuenta una más y avanza hasta el final de ella,
            //  o sea hasta un separador o el fin del texto. En la siguiente vuelta del ciclo grande, el separador se vuelve '\0'.
            words[word_count] = cursor; 
            // el caracter empieza una palabra, 
            word_count++;
            while ((*cursor != '\0') && !is_separator(*cursor)) {
                cursor++;
            }
        }
    }
    if (word_count == 0U) {	// si no se encontró ninguna palabra
        return false;	// la línea estaba vacía, no hay comando
    }

    cmd->name = words[0];	// el nombre del comando es la primera palabra
    cmd->id = CMD_UNKNOWN;	// se parte suponiendo que no se conoce
    for (size_t i = 0U; i < CMD_TABLE_LEN; i++) {	// recorre la tabla fila por fila
        if (strcmp(words[0], cmd_table[i].name) == 0) {	// si es 0 los dos textos son iguales
            cmd->id = cmd_table[i].id;	// guarda el identificador de esa fila
            break;	// ya se encontró, no hace falta seguir
        }
    }

    cmd->argc = (uint8_t)(word_count - 1U);	// los argumentos son una palabra menos, porque la primera es el nombre
    for (uint8_t i = 0U; i < cmd->argc; i++) {	// recorre los argumentos
        cmd->argv[i] = words[i + 1U];	// guarda el puntero a la palabra, no una copia del texto
    }
    return true;	// se pudo analizar la línea
}

static bool is_separator(char c)
{
    return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');	// true si es espacio, tabulador o fin de línea
}
