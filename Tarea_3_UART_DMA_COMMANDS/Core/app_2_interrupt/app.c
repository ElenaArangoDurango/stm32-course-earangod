/* app.c, etapa 2: recepción por interrupción
USART2 recibe con interrupción, cada byte que llega se guarda en un buffer circular y el app_loop lo saca cuando puede
esta étapa arma lineas 
Autor: earangod@unal.edu.co

*/
#include <stdbool.h> // Booleano, true y false 
#include <stdio.h>            
#include <string.h>           
#include "stm32f4xx_hal.h"    
#include "app.h"              
#include "system.h"           
#include "cmd_parser.h"
#include "commands.h"

#define TX_TIMEOUT_MS   100U //  Tiempo máximo de espera para enviar un texto
#define RX_RING_SIZE   64U // Tamaño del buffer circular que guarda hasta 63 bytes, debe ser una potencia de 2
#define LINE_MAX_LEN  63U // Largo máximo de una línea, sin contar el '\0'


// huart2 es una estructura que guarda tres cosas, el periférico que se usa
// en el campo Instance, la configuración en el campo Init y el estado interno del HAL en gState.
// El HAL la recibe en cada llamada a HAL_UART_ y la devuelve cuando termina.
static UART_HandleTypeDef huart2;   // static porque solo este archivo la toca
static uint8_t rx_byte;  // Aquí el HAL deja cada byte recibido
static uint8_t rx_ring[RX_RING_SIZE]; // Buffer circular donde se guardan los bytes, la interrupción guarda los bytes aquí y el loop los saca
static volatile uint16_t rx_head = 0U; // Es la cabeza. Dónde escribe la interrupción
static volatile uint16_t rx_tail = 0U;  // Dónde lee app_loop()
static volatile uint32_t rx_error_count = 0U;  // Cuántos errores de recepción ha habido
static volatile uint32_t rx_last_error = 0U;   // Código del último error

static char line[LINE_MAX_LEN + 1U]; // Línea que se va armando, con un espacio extra para el '\0'
static uint16_t line_len = 0U;  // Cuántos caracteres lleva la línea
static bool line_too_long = false;  // Se pone en true si la línea pasó el máximo

static void line_add_byte(uint8_t new_byte);   // Agrega un byte a la línea, lo muestra y atiende el enter
static void line_execute(void);                // Usa la línea terminada y la vacía
static void uart2_init(void);
static void uart2_start_rx(void);                    // Pide al HAL recibir un byte por interrupción
static void uart2_send_text(const char *text);
static void rx_ring_put(uint8_t byte_to_put);        // Guarda un byte en el buffer (lado interrupción)
static bool rx_ring_get(uint8_t *byte_to_get);       // Saca un byte del buffer (lado app_loop)


// Se ejecuta una vez al arrancar, después del reloj y del heartbeat.
// Configura USART2, envía el mensaje de bienvenida y pide el primer byte.
void app_init(void)
{
    uart2_init(); // Inicializa la UART, los relojes, pines, configuración
    // y en esta nueva etapa inicializa el NVIC también
    uart2_send_text("\r\nStage 2: interrupt reception.\r\n> ");
    // Envía el mensaje de bienvenida, la recepción aún no está activa entonces es bloqueante
    uart2_start_rx();   
    // Pide el primer byte por interrupción y vuelve, desde aquí la interrupción recibe sola
}

// Se repite sin parar. Saca del buffer los bytes que guardó la interrupción,
// los entrega a la linea que se está armando y avisa si hubo algún error de recepción.
// Ya no hay tick, ahora LD2 es lo único que muestra que la CPU está viva.
void app_loop(void)
{
    static uint32_t reported_errors = 0U;   // Es static para recordar cuántos errores ya avisó entre una vuelta y la otra,
    // se compara con rx_error_count para saber si hay errores nuevos
    uint8_t byte_aux; // Aquí llega el byte que se saca del buffer
    char    msg[64]; // Es el espacio para construir el texto, pasa de 40 a 64 porque el mensaje de error es más largo
    
    // Esto se usó en los experimentos 1 y 2 del Break it de la etapa 2 y se dejó comentado.
    // Cada 5 segundos apagaba todas las interrupciones durante un momento. Con una tecla
    // presionada llegaban varios bytes y nadie los leía, así que la UART marcaba un overrun
    // y aparecía [uart error 0x08, reception restarted], faltaban algunos caracteres y
    // LD2 titubeaba, pero se podía seguir escribiendo porque el callback reiniciaba la recepción.
    // static uint32_t last_stall_ms = 0U;	// hora de la última pausa
    // if ((HAL_GetTick() - last_stall_ms) >= 5000U) {	// pasaron 5 segundos desde la última pausa
    //     last_stall_ms = HAL_GetTick();	// anota la hora de esta pausa
    //     __disable_irq();	// ninguna interrupción puede correr desde aquí
    //     for (volatile uint32_t i = 0U; i < 1000000U; i++) {	// espera un rato con las interrupciones apagadas
    //     }
    //     __enable_irq();	// las interrupciones vuelven a funcionar
    // }

    // Entrega cada byte a la línea en lugar de imprimirlo
    while (rx_ring_get(&byte_aux)) {
        line_add_byte(byte_aux);
    }


    // Avisa si la interrupción vio errores de recepción
    if (rx_error_count != reported_errors) {   // si el contador del callback aumentó quiere decir que hay un
        // error que no se avisó
        reported_errors = rx_error_count;  // Se anotan como ya avisados
        // Muestra el código del error en hexadecimal, unsigned long hace que el tipo coincida con %lX
        (void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
                       (unsigned long)rx_last_error);
        uart2_send_text(msg);
    }
}

// Interrupción
// Rutina de interrupción de USART2. Su nombre debe coincidir con la tabla de vectores.
// Solo le pasa el evento al HAL, que averigua qué ocurrió.
void USART2_IRQHandler(void) // El archivo de arranque lo busca en la tabla de vectores
{
    HAL_UART_IRQHandler(&huart2); // el HAL lee las banderas del USART2, verifica si 
    // fue un byte recibido o un error
}

// El HAL la llama, dentro de la interrupción, cuando llega el byte pedido.
// Es __weak en el HAL (vacía), la escribimos aquí y el enlazador usa la nuestra.
// Guarda el byte en el buffer y pide el siguiente.
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {   // Confirma que el aviso es de USART2
        rx_ring_put(rx_byte);  // Guarda el byte en el buffer
        uart2_start_rx(); // Pide el siguiente byte, sin esto solo llegaría uno
    }
}

// El HAL la llama, dentro de la interrupción, cuando USART2 reporta un error.
// Anota el error y reinicia la recepción.
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        rx_last_error = HAL_UART_GetError(huart);   // Guarda el código del error, el overrun es 0x08
        rx_error_count++; // Cuenta un error más
        // En el experimento 2 del Break it se comentó la línea de abajo para ver qué pasa sin la recuperación.
        // Después del primer error la UART quedó sorda para siempre, la terminal no dejó escribir más
        // comandos, pero LD2 siguió parpadeando, o sea la CPU vivía y fallaba el camino de la UART.
        uart2_start_rx(); // Un overrun detiene la recepción, se reinicia
    }
}


// Configura USART2 a 115200 8N1 en PA2 y PA3, con el mismo orden del heartbeat 
static void uart2_init(void)
{
    GPIO_InitTypeDef gpio = {0}; // Se ponen todos los campos en cero 
    // Enciende los relojes de USART2 y de GPIOA
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    // PA2 es TX y PA3 es RX, y se entregan a USART2 con AF7 
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP; // Alternative function, push pull
    gpio.Pull      = GPIO_PULLUP;            // una línea UART en reposo está en alto 
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2; // Se pone el AF7 
    HAL_GPIO_Init(GPIOA, &gpio); // Aplica todo 

    // Configura USART2 a 115200 baudios, 8N1, con transmisor y receptor, sin control de flujo 
    huart2.Instance          = USART2; // Elige el periférico
    huart2.Init.BaudRate     = 115200U; // son 115200 bits por segundo
    huart2.Init.WordLength   = UART_WORDLENGTH_8B; // Son 8 bits por byte 
    huart2.Init.StopBits     = UART_STOPBITS_1; // Define cuántos bits de parada hay al final de cada byte 
    huart2.Init.Parity       = UART_PARITY_NONE; // no se agrega bit de paridad 
    huart2.Init.Mode         = UART_MODE_TX_RX; // Activa el transmisor y el receptor
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE; 
    huart2.Init.OverSampling = UART_OVERSAMPLING_16; // Cuántas veces se revisa cada bit 
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        // Escribe esos valores en los registros de USART2 y la enciende. Si falla, devuelve un error 
        // y el código llama a Error_Handler().
        Error_Handler();
    }

    // Deja que la CPU acepte las interrupciones de USART2
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);   // Prioridad 2, más urgente que el heartbeat que tiene 3
    HAL_NVIC_EnableIRQ(USART2_IRQn);           // Activa el aviso de USART2 hacia la CPU
}

 /* uart2_send_text Envía un texto por la UART y espera a que salga completo.
 */
static void uart2_send_text(const char *text)
{
    (void)HAL_UART_Transmit(&huart2, // UART por la que se envía 
                            (const uint8_t *)text,   // el texto visto como bytes 
                           (uint16_t)strlen(text),  // cantidad de caracteres a enviar 
                            TX_TIMEOUT_MS);   // tiempo máximo de espera: 100 ms 
}

// Pide al HAL recibir un byte por interrupción y lo deja en rx_byte
// Regresa enseguida y el byte llega después, en HAL_UART_RxCpltCallback()
static void uart2_start_rx(void)
{
    // Si devuelve HAL_BUSY no pasa nada, la recepción ya estaba activa
    (void)HAL_UART_Receive_IT(&huart2, &rx_byte, 1U);
}

// Guarda un byte en el buffer circular, solo la llama la interrupción
// Si el buffer está lleno, el byte se pierde
static void rx_ring_put(uint8_t byte_to_put)
{
    // Calcula la siguiente posición y vuelve a 0 al llegar al final del buffer
    uint16_t next = (uint16_t)((rx_head + 1U) & (RX_RING_SIZE - 1U));

    if (next != rx_tail) {    // Si no se ha topado con la cola, hay espacio
        rx_ring[rx_head] = byte_to_put;  // Guarda el byte donde apunta la cabeza
        rx_head = next; // Mueve la cabeza a la siguiente posición
    }
}

// Saca el byte más viejo del buffer circular, solo la llama app_loop()
// Devuelve false si no hay nada guardado
static bool rx_ring_get(uint8_t *byte_to_get)
{
    if (rx_tail == rx_head) {  // Cola igual a cabeza significa buffer vacío
        return false;
    }
    *byte_to_get = rx_ring[rx_tail];     // Entrega el byte más viejo
    rx_tail = (uint16_t)((rx_tail + 1U) & (RX_RING_SIZE - 1U));   // Mueve la cola a la siguiente posición
    return true;
}

// Agrega un byte a la línea que se está escribiendo y lo muestra en pantalla
// Enter, ya sea \r, \n o \r\n, termina la línea y llama a line_execute()
static void line_add_byte(uint8_t new_byte)
{
    static uint8_t previous_byte = 0U;   // Byte anterior, sirve para detectar el \r\n

    // Un \n justo después de un \r es la segunda mitad de \r\n, y ya se atendió
    if ((new_byte == '\n') && (previous_byte == '\r')) {
        previous_byte = new_byte;
        return;
    }
    previous_byte = new_byte;

    if ((new_byte == '\r') || (new_byte == '\n')) { // Fin de línea
        uart2_send_text("\r\n");
        line_execute();
        uart2_send_text("> ");
    } else if (line_len < LINE_MAX_LEN) {  // Todavía cabe en la línea
        line[line_len] = (char)new_byte;
        line_len++;
        (void)HAL_UART_Transmit(&huart2, &new_byte, 1U, TX_TIMEOUT_MS);   // Eco de la tecla
    } else {
        line_too_long = true;   // Sigue leyendo, pero la línea se rechaza
    }
}

// Se llama cuando llega Enter. Toma la línea que armó line_add_byte, la analiza con
// cmd_parse, ejecuta el comando con commands_execute y envía la respuesta por la UART.
// Al final vacía la línea para la siguiente. Es la unión entre la línea, el analizador y los comandos.
static void line_execute(void)
{
    cmd_t cmd;	// aquí cmd_parse deja el nombre, el identificador y los argumentos
    char reply[96];	// aquí commands_execute escribe la respuesta, 96 alcanzan para la más larga

    if (line_too_long) {	// line_add_byte marcó que la línea pasó el máximo
        uart2_send_text("error: line too long\r\n");	// solo se avisa el error, no se analiza nada
    } else {	// la línea cabe, se puede analizar
        line[line_len] = '\0';	// cierra la línea como un texto de C, cmd_parse necesita saber dónde termina
        if (cmd_parse(line, &cmd)) {	// parte la línea en palabras, devuelve false si estaba vacía
            commands_execute(&cmd, reply, sizeof reply);	// decide qué responder, sizeof reply evita que se pase
            uart2_send_text(reply);	// envía la respuesta por la UART
            uart2_send_text("\r\n");	// el final de línea lo pone esta función, los comandos no lo escriben
        }
    }
    line_len = 0U;	// vacía la línea para la siguiente
    line_too_long = false;	// borra la marca de error, estas dos van fuera del if para que corran en todos los casos
}
