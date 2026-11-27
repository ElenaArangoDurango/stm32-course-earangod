/* app.c, etapa 1: UART por polling
USART2 bloqueante, la CPU espera dentro de cada llamada HAL.
Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co

Picocom es el programa del computador que hace de pantalla y permite enviar y recibir caracteres
se configura a una velocidad de 115200 baudios y a 8N1 ( Que indica que son ocho bits de datos, No hay paridad
y el bit de parada es 1). Se conecta a /dev/ttyACM0, que es el puerto virtual que crea el USB. 
*/

#include <stdio.h>            
#include <string.h>           
#include "stm32f4xx_hal.h"    
#include "app.h"              
#include "system.h"           

#define TX_TIMEOUT_MS   100U   //  Tiempo máximo de espera para enviar un texto
#define RX_TIMEOUT_MS    10U   // cuánto espera app_loop a que llegue una tecla
// Para el experimento 1 se cambió el tiempo máximo de espera para entender qué
// pasa cuando nos quedamos esperando para siempre una tecla. Resultado: los tick
// dejaron de salir, LD2 siguió parpadeando y, al escribir 'a', salió rx: 'a' 0x61
// seguido del tick 1 a 100136 ms (el ciclo principal estuvo bloqueado todo ese tiempo).
// #define RX_TIMEOUT_MS    HAL_MAX_DELAY
#define TICK_PERIOD_MS 1000U   // cada cuánto se imprime el mensaje tick

static UART_HandleTypeDef huart2;   // Se configura el USART2 como static porque solo este archivo lo toca


static void uart2_init(void);
static void uart2_send_text(const char *text);
// Esta es una estructura que guarda tres cosas, el periferico que se usa
// en el campo instance, la configuración en el campo Init y el estado interno de HAL en el campo gState.
// El HAL la recibe cada vez que se llama a una función HAL_UART_ y la devuelve cuando termina.

// Se ejecuta una vez, después del reloj y del heartbeat 
void app_init(void)
// Se ejecuta una sola vez al arrancar. Configura la UART con uart2_init()
//  y manda el mensaje de bienvenida con uart2_send_text().
{
    uart2_init();
    uart2_send_text("\r\nStage 1: polling. Type something.\r\n");
    //  empieza con \r\n para que, si hay algo escrito en esa línea, el mensaje salga en una línea limpia. 
    // Termina con \r\n para dejar el cursor listo.
}

// Se repite en el loop

// Se repite sin parar. Avisa que está viva con un tick cada segundo
// y revisa si llegó alguna tecla.
void app_loop(void)
{
    static uint32_t last_tick_ms = 0U; // Hora del último tick; static para que no se reinicie
    static uint32_t tick_count   = 0U; // Cuántos tick se han enviado
    uint8_t  byte_aux;  // Aquí llega la tecla recibida
    char     msg[40];   // Texto armado antes de enviarlo
    char     shown;     // Carácter que se muestra en pantalla

    /* HAL_GetTick() da la hora actual en ms, contada desde que arrancó la placa.
    Se le resta la hora del último tick y, si la diferencia llegó a 1000 ms,
    toca imprimir otro. Así no se necesita un temporizador, se compara la hora
    en cada vuelta. */
    if ((HAL_GetTick() - last_tick_ms) >= TICK_PERIOD_MS) {
        last_tick_ms = HAL_GetTick(); // Anota la hora de este tick, el siguiente cuenta desde aquí
        tick_count++;          // Suma uno al contador
        // snprintf arma el texto dentro de msg sin pasarse de su tamaño.
        // %lu pide unsigned long, por eso el cast de los uint32_t.
        (void)snprintf(msg, sizeof(msg), "tick %lu at %lu ms\r\n",
                       (unsigned long)tick_count, (unsigned long)last_tick_ms); // last_tick_ms es la hora de este tick
        uart2_send_text(msg); // Envía el texto por la UART
    }

    // Espera una tecla hasta RX_TIMEOUT_MS y, si llega, la describe.
    // Le pide al HAL un byte y espera 10 ms a que llegue. Si llega, lo guarda
    // en byte_aux, devuelve HAL_OK y entra al if. Si no llega, devuelve
    // HAL_TIMEOUT y se salta el bloque.
    if (HAL_UART_Receive(&huart2, &byte_aux, 1U, RX_TIMEOUT_MS) == HAL_OK) {
        // shown es un if en una sola línea. Los códigos del 0x20 al 0x7E son
        // los caracteres que se pueden ver, como letras, números y símbolos.
        // Si el byte está en ese rango, se muestra tal cual. Si no, como
        // Enter (0x0D), se muestra un punto.
        shown = ((byte_aux >= 0x20U) && (byte_aux <= 0x7EU)) ? (char)byte_aux : '.';
        // %c muestra el carácter y %02X el valor en hexadecimal con 2 dígitos
        (void)snprintf(msg, sizeof(msg), "rx: '%c' 0x%02X\r\n",
                       shown, byte_aux);
        uart2_send_text(msg); // Envía la descripción de la tecla por la UART
    }
}


// Configura USART2 a 115200 8N1 en PA2 y PA3, con el mismo orden del heartbeat 
static void uart2_init(void)
{
    GPIO_InitTypeDef gpio = {0}; // Se ponen todos los campos en cero 
    // Enciende el reloj y los pines

    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // Pone PA2 es TX y PA3 es RX, ambos entregados a USART2 por AF7 
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP; // Alternative function, push pull
    gpio.Pull      = GPIO_PULLUP;            // una línea UART en reposo está en alto 
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2; // Se pone el AF7 
    HAL_GPIO_Init(GPIOA, &gpio); // Aplica todo 

    // Periférico 115200 baudios, 8N1, transmite y recibe, sin control de flujo 
    huart2.Instance          = USART2; // Elige el periferico
    huart2.Init.BaudRate     = 115200U; // son 115200 bits por segundo
    huart2.Init.WordLength   = UART_WORDLENGTH_8B; // Son 8 bits por byte 
    huart2.Init.StopBits     = UART_STOPBITS_1; // Define cuantos bits de para hay al final de cada byte 
    huart2.Init.Parity       = UART_PARITY_NONE; // no se agrega bit de paridad 
    huart2.Init.Mode         = UART_MODE_TX_RX; // Activa el transmisor o el receptor
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE; 
    huart2.Init.OverSampling = UART_OVERSAMPLING_16; // Cuantas veces se revisa el bit 
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        // Escribe esos valores en los registros de USART2 y la enciende. Si falla, devuelve un error 
        // y el código llama a Error_Handler().
        Error_Handler();
    }

    
}

 /* uart2_send_text Envía un texto por la UART y espera a que salga completo.
 */
static void uart2_send_text(const char *text)
{
    (void)HAL_UART_Transmit(&huart2,  // UART por la que se envía 
                            (const uint8_t *)text,   // el texto visto como bytes 
                           (uint16_t)strlen(text),  // cantidad de caracteres a enviar 
                            TX_TIMEOUT_MS);   // tiempo máximo de espera: 100 ms 
}