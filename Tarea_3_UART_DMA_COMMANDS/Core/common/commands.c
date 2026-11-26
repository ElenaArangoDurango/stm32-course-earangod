/* Autor: earangod@unal.edu.co
Este archivo decide qué responde cada comando. Recibe el comando ya analizado
por cmd_parser y escribe la respuesta en un texto, sin tocar la UART.
Quien lo llama (app.c) es el que envía esa respuesta por el puerto serie. */
#include <stdio.h>	// snprintf, para armar las respuestas
#include <stdlib.h>	// strtol, para convertir texto a número
#include "stm32f4xx_hal.h"	// HAL_GetTick, el tiempo desde el arranque
#include "commands.h"	// la interfaz de este archivo y cmd_t
#include "heartbeat.h"	// leer y cambiar el periodo del LED
#include "pwm.h"	// cambiar el ciclo de trabajo de la salida PWM

static void cmd_blink(const cmd_t *cmd, char *reply, size_t size);	// responde el comando blink
static void cmd_echo(const cmd_t *cmd, char *reply, size_t size);	// responde el comando echo
static void cmd_pwm(const cmd_t *cmd, char *reply, size_t size);	// responde el comando pwm

// Ejecuta un comando ya analizado y deja la respuesta en reply, en una sola línea.
// size es el espacio que tiene reply, así snprintf no se pasa.
void commands_execute(const cmd_t *cmd, char *reply, size_t size)
{
    if (cmd->too_many_args) {	// el analizador encontró más argumentos de los permitidos
        (void)snprintf(reply, size, "error: too many arguments");	// el (void) descarta el resultado a propósito
        return;	// no se ejecuta ningún comando
    }
    switch (cmd->id) {	// elige qué hacer según el identificador del comando
    case CMD_HELP:	// help
        (void)snprintf(reply, size, "commands: help, info, blink <ms>, echo <text>, pwm <0..100>");	// texto fijo con la lista
        break;	// sale del switch, sin esto seguiría con el siguiente case
    case CMD_INFO:	// info
        (void)snprintf(reply, size, "uptime %lu ms, blink %lu ms",	// tiempo encendida y periodo del LED
                       (unsigned long)HAL_GetTick(), (unsigned long)heartbeat_get_period());	// unsigned long para que coincida con %lu
        break;
    case CMD_BLINK:	// blink <ms>
        cmd_blink(cmd, reply, size); // es más largo, tiene su propia función
        break;
    case CMD_ECHO:	// echo <texto>
        cmd_echo(cmd, reply, size);	// es más largo, tiene su propia función
        break;
    case CMD_PWM:	// pwm <porcentaje>
        cmd_pwm(cmd, reply, size);	// es más largo, tiene su propia función
        break;
    case CMD_UNKNOWN: // el nombre no estaba en la tabla
    default:	// y cualquier otro valor inesperado
        (void)snprintf(reply, size, "error: unknown command '%s' (type help)", cmd->name);	// repite lo que se escribió
        break;
    }
}

// blink, revisa que el argumento sea un número entero dentro del rango
// permitido y entonces cambia el periodo del LED.
static void cmd_blink(const cmd_t *cmd, char *reply, size_t size)
{
    char *end = NULL;	// strtol deja aquí el lugar donde dejó de leer
    long period_ms;	// el número ya convertido

    if (cmd->argc != 1U) {	// blink necesita exactamente un argumento
        (void)snprintf(reply, size, "usage: blink <ms>");	// explica cómo se usa
        return;
    }
    period_ms = strtol(cmd->argv[0], &end, 10);	// convierte el texto a número en base 10
    if ((end == cmd->argv[0]) || (*end != '\0')) {	// no se leyó ningún dígito, o sobró texto después del número
        (void)snprintf(reply, size, "error: '%s' is not a number", cmd->argv[0]);	// por ejemplo abc o 12abc
    } else if ((period_ms < 0L) || !heartbeat_set_period((uint32_t)period_ms)) {	// negativo, o fuera de 50 a 5000
        (void)snprintf(reply, size, "error: blink accepts %u to %u ms",	// dice cuáles son los límites
                       HEARTBEAT_MIN_MS, HEARTBEAT_MAX_MS);
    } else {	// el número es válido y el periodo ya se cambió
        (void)snprintf(reply, size, "ok: blink %ld ms", period_ms);	// confirma el nuevo periodo
    }
}

// echo <texto> repite los argumentos separados por un espacio.
// Si reply se llena, se detiene, y el texto siempre termina en '\0'.
static void cmd_echo(const cmd_t *cmd, char *reply, size_t size)
{
    size_t used = 0U;	// cuántos caracteres ya hay escritos en reply

    reply[0] = '\0';	// si no hay argumentos, la respuesta queda vacía
    for (uint8_t i = 0U; i < cmd->argc; i++) {	// recorre los argumentos
        int written = snprintf(&reply[used], size - used, "%s%s",	// escribe en la primera posición libre
                               (i == 0U) ? "" : " ", cmd->argv[i]);	// un espacio antes de cada palabra, menos la primera
        if ((written < 0) || ((size_t)written >= (size - used))) {	// hubo un error, o la palabra no cupo
            break;	// se detiene, reply sigue siendo un texto completo
        }
        used += (size_t)written;	// la siguiente palabra va justo después de esta
    }
}

// pwm <porcentaje>, revisa que el argumento sea un número entero de 0 a 100 y entonces cambia
// el ciclo de trabajo de la salida PWM. No toca el temporizador, de eso se encarga pwm.c
static void cmd_pwm(const cmd_t *cmd, char *reply, size_t size)
{
    char *end = NULL;	// strtol deja aquí el lugar donde dejó de leer
    long percent;	// el número ya convertido

    if (cmd->argc == 0U) {	// pwm solo, sin argumento
        (void)snprintf(reply, size, "pwm is %lu %%", (unsigned long)pwm_get_duty());	// responde el ciclo de trabajo actual
        return;
    }
    if (cmd->argc != 1U) {	// con dos o más argumentos no se entiende
        (void)snprintf(reply, size, "usage: pwm <0..100>");	// explica cómo se usa
        return;
    }
    percent = strtol(cmd->argv[0], &end, 10);	// convierte el texto a número en base 10
    if ((end == cmd->argv[0]) || (*end != '\0')) {	// no se leyó ningún dígito, o sobró texto después del número
        (void)snprintf(reply, size, "error: '%s' is not a number", cmd->argv[0]);	// por ejemplo abc o 12abc
    } else if ((percent < 0L) || (percent > 100L)) {	// fuera de rango
        (void)snprintf(reply, size, "error: pwm accepts 0 to 100 %%");	// %% imprime un solo %, porque % empieza una conversión
    } else if (!pwm_set_duty((uint32_t)percent)) {	// pwm_set_duty devuelve false si la salida nunca se inició
        (void)snprintf(reply, size, "error: pwm is not started in this application");	// pasa en las etapas que no llaman a pwm_init
    } else {	// el número es válido y el ciclo de trabajo ya se cambió
        (void)snprintf(reply, size, "ok: pwm %ld %%", percent);	// confirma el nuevo valor
    }
}
