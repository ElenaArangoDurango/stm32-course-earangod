/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co

Etapa 4: recepción ping-pong, transmisión por DMA y comandos.
Ahora hay dos bloques, A y B. El DMA llena uno mientras app_loop lee el otro y se van turnando,
entonces ya no se pisan como en la etapa 3. Si los dos están ocupados la recepción se pausa y se
avisa por la terminal que se perdieron datos.
Para enviar también se usa DMA. uart2_send_text ya no espera, solo copia el texto a un buffer, y
uart2_flush lo manda al final de cada vuelta de app_loop.
Un bloque no es una línea, por eso cada byte pasa por line_add_byte, que arma la línea hasta el Enter,
y después cmd_parser y commands hacen lo mismo que en la etapa 2.
Los relojes, pines, los dos DMA y el NVIC se configuran en HAL_UART_MspInit.
*/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"
#include "cmd_parser.h"	// el analizador y los comandos son los mismos de la etapa 2
#include "commands.h"
#include "pwm.h"	// para pwm_init, etapa 5

#define RX_BLOCK_SIZE  32U	// tamaño de cada bloque, pequeño para que una ráfaga larga se parta en dos
#define TX_BUF_SIZE    256U	// tamaño de los dos buffers de envío
#define LINE_MAX_LEN   63U	// largo máximo de la línea sin contar el '\0'

static UART_HandleTypeDef huart2; // ficha de la UART
static DMA_HandleTypeDef hdma_usart2_rx; // ficha del DMA de recepción, va afuera de las funciones porque
// __HAL_LINKDMA guarda su dirección en huart2 y tiene que seguir existiendo
static DMA_HandleTypeDef hdma_usart2_tx;	// lo mismo pero para enviar

// Cada bloque tiene sus bytes, cuántos llegaron y a quién le pertenece en ese momento
typedef struct {
    uint8_t data[RX_BLOCK_SIZE];	// aquí escribe el DMA
    volatile uint16_t size;	// cuántos bytes llegaron, solo sirve cuando ready es true
    volatile bool ready;	// true: lo tiene app_loop, false: lo tiene el DMA
} rx_block_t;

static rx_block_t rx_block[2];	// A es el 0 y B es el 1
static volatile uint8_t rx_dma_index = 0U;	// el bloque que está llenando el DMA
static volatile bool rx_paused = false;	// true si no había bloque libre y la recepción quedó parada
static volatile uint32_t rx_pause_count = 0U;	// para que app_loop sepa que hubo una pausa nueva
// errores de la UART, igual que en la etapa 2
static volatile uint32_t rx_error_count = 0U;
static volatile uint32_t rx_last_error = 0U;

static uint8_t tx_fill[TX_BUF_SIZE];	// aquí se junta el texto que falta por enviar
static uint16_t tx_fill_len = 0U;
static uint8_t tx_dma_buf[TX_BUF_SIZE];	// de aquí lee el DMA, no se toca mientras está enviando
static volatile bool tx_busy = false;	// true mientras el DMA está enviando

// La línea que se va armando, igual que en la etapa 2
static char line[LINE_MAX_LEN + 1U];
static uint16_t line_len = 0U;
static bool line_too_long = false;

static void uart2_init(void);
static void uart2_start_rx(void);
static void uart2_send_bytes(const uint8_t *data, uint16_t length);
static void uart2_send_text(const char *text);
static void uart2_flush(void);
static void line_add_byte(uint8_t new_byte);
static void line_execute(void);

// Configura la UART y el PWM, manda el banner y arranca la primera recepción
void app_init(void)
{
    uart2_init();
    pwm_init();	// el LED arranca apagado, 0 %
    uart2_send_text("\r\nStage 4: ping-pong reception.\r\n> ");
    uart2_start_rx(); // sin esta línea el DMA nunca empezaría
}

// Lee el bloque que le toca, se lo devuelve al DMA, avisa errores o pausas y al final manda lo que falta por enviar
void app_loop(void)
{
    static uint8_t read_index = 0U;	// el bloque que toca leer, van en turno A, B, A...
    static uint32_t reported_errors = 0U;
    static uint32_t reported_pauses = 0U;
    char msg[64];

    if (rx_block[read_index].ready) {	// el DMA ya terminó este bloque
        bool restart_aux;
        /* Experimento 1 del Break it de la etapa 4, lo dejé comentado. Esperaba 2 segundos antes de
        procesar el bloque. Pegué hello y presioné x y y, salió hellox con el aviso de pausa y después la y.
        Esta vez no salió xello, la pausa se avisó y no se dañó nada. */
        // HAL_Delay(2000U);

        // cada byte va a la línea, porque un bloque no es una línea
        for (uint16_t i = 0U; i < rx_block[read_index].size; i++) {
            line_add_byte(rx_block[read_index].data[i]);
        }

        /* Se devuelve el bloque con las interrupciones apagadas. Si el callback se metiera justo en medio,
        podría pausar la recepción después de que aquí ya se revisó la pausa, y quedaría parada para siempre. */
        __disable_irq();
        rx_block[read_index].ready = false;	// el bloque vuelve a ser del DMA
        restart_aux = rx_paused;
        rx_paused = false;
        __enable_irq();

        if (restart_aux) {	// la recepción estaba esperando un bloque libre
            rx_dma_index = read_index;
            uart2_start_rx();
        }
        read_index = (uint8_t)(1U - read_index);	// 1 - 0 = 1 y 1 - 1 = 0, así se alterna
    }

    if (rx_error_count != reported_errors) {	// hay un error sin avisar
        reported_errors = rx_error_count;
        (void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
                       (unsigned long)rx_last_error);
        uart2_send_text(msg);
    }
    if (rx_pause_count != reported_pauses) {	// hubo una pausa nueva
        reported_pauses = rx_pause_count;
        uart2_send_text("\r\n[reception paused: both blocks busy, data lost]\r\n");
    }

    uart2_flush();	// sin esto no sale nada, todo se queda en tx_fill
}

// Salta con el IDLE, con un error o cuando termina un envío, solo le pasa el trabajo al HAL
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

// DMA de recepción: salta cuando se llena el bloque (TC) o hay un error, y se lo pasa al HAL.
// El nombre tiene que ser igual al de la tabla de vectores (ver el experimento 3 de la etapa 3)
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

// DMA de envío: avisa aquí cómo va el envío y el HAL hace el resto
void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_tx);
}

// Igual que en la etapa 3, pero ahora con un segundo DMA para enviar
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance != USART2) { // si es otra UART no se hace nada
        return;
    }
    // Relojes, el del DMA tiene que ir antes de HAL_DMA_Init
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    // PA2 y PA3 en función alternativa AF7
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    // DMA de recepción, igual que en la etapa 3: stream 5 canal 4 (USART2_RX en la tabla del manual)
    hdma_usart2_rx.Instance                 = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;	// de la UART al bloque
    hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode                = DMA_NORMAL;
    hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx);

    // DMA de envío: stream 6 canal 4, que es el de USART2_TX en la tabla del manual
    hdma_usart2_tx.Instance                 = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;	// al revés, de la memoria a la UART
    hdma_usart2_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode                = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority            = DMA_PRIORITY_LOW;	// más baja que la de recibir, para no perder bytes que llegan
    hdma_usart2_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(huart, hdmatx, hdma_usart2_tx);	// si falta, HAL_UART_Transmit_DMA usa un puntero vacío y la CPU se cuelga

    // NVIC: los dos DMA y la USART2, todos con prioridad 2
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

// El HAL la llama cuando se cierra un bloque (IDLE o lleno). Le pasa el bloque a app_loop
// y si el otro está libre sigue recibiendo ahí
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if ((huart->Instance == USART2) &&
        (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_HT)) {	// el HT no cierra el bloque, se ignora
        uint8_t filled_index = rx_dma_index;	// el que se acaba de llenar
        uint8_t other_index = (uint8_t)(1U - filled_index);

        rx_block[filled_index].size = Size;
        rx_block[filled_index].ready = true;	// ahora es de app_loop
        if (!rx_block[other_index].ready) {	// el otro está libre
            rx_dma_index = other_index;
            uart2_start_rx();
        } else {	// los dos ocupados, no hay dónde escribir
            rx_paused = true;	// app_loop la vuelve a arrancar cuando libere uno
            rx_pause_count++;
        }
    }
}

/* El HAL la llama cuando sale el último byte del envío, y así uart2_flush puede mandar lo siguiente.
En el experimento 2 del Break it de la etapa 4 la renombré HAL_UART_TxCompleteCallback. Compiló sin errores,
salió el banner y nada más, porque el HAL usó su versión vacía y tx_busy se quedó en true. Pero blink sí
cambiaba el LED, o sea que recibir y ejecutar comandos funcionaba, solo se trabó la salida. */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        tx_busy = false;	// ya se puede enviar otra cosa
    }
}

// Error en la UART o el DMA. Se guarda y se vuelve a arrancar la recepción, menos cuando está en pausa
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        rx_last_error = HAL_UART_GetError(huart);
        rx_error_count++;
        if (huart->gState == HAL_UART_STATE_READY) {	// el error también cortó el envío
            tx_busy = false;	// si no, tx_busy se quedaría en true para siempre
        }
        if (!rx_paused) {	// si está en pausa la arranca app_loop
            uart2_start_rx();
        }
    }
}

// Igual que en la etapa 3, llena la ficha y HAL_UART_Init llama a HAL_UART_MspInit
static void uart2_init(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200U;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

// Arranca la recepción por DMA en el bloque rx_dma_index, termina con IDLE o cuando se llena
static void uart2_start_rx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_block[rx_dma_index].data,
                                     RX_BLOCK_SIZE) == HAL_OK) {
        // se apaga el HT cada vez, igual que en la etapa 3
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}

// Pone los bytes al final de tx_fill y vuelve de una, no espera nada.
// Si no caben se botan completos, para no mandar un pedazo cortado
static void uart2_send_bytes(const uint8_t *data, uint16_t length)
{
    if (length > (TX_BUF_SIZE - tx_fill_len)) {	// no hay espacio
        return;
    }
    memcpy(&tx_fill[tx_fill_len], data, length);	// memcpy copia bytes sin buscar el '\0'
    tx_fill_len = (uint16_t)(tx_fill_len + length);
}

// Lo mismo pero para un texto, ya no es bloqueante como en las etapas 1 a 3
static void uart2_send_text(const char *text)
{
    uart2_send_bytes((const uint8_t *)text, (uint16_t)strlen(text));
}

/* Va al final de app_loop. Si el DMA está libre y hay algo en tx_fill, lo pasa a tx_dma_buf y arranca el envío.
Se usan dos buffers para que app_loop pueda seguir escribiendo en tx_fill mientras el DMA lee el otro. */
static void uart2_flush(void)
{
    if ((!tx_busy) && (tx_fill_len > 0U)) {
        memcpy(tx_dma_buf, tx_fill, tx_fill_len);
        tx_busy = true;	// va antes de arrancar, por si el callback llega muy rápido
        if (HAL_UART_Transmit_DMA(&huart2, tx_dma_buf, tx_fill_len) == HAL_OK) {
            tx_fill_len = 0U;	// tx_fill queda vacío
        } else {
            tx_busy = false;	// no arrancó, se intenta en la siguiente vuelta
        }
    }
}

// Igual que en la etapa 2, lo único que cambia es que el eco ahora va por uart2_send_bytes
static void line_add_byte(uint8_t new_byte)
{
    static uint8_t previous_byte = 0U;	// static para recordarlo entre llamadas

    if ((new_byte == '\n') && (previous_byte == '\r')) {	// el \n de un \r\n
        previous_byte = new_byte;
        return;	// ya se atendió con el \r
    }
    previous_byte = new_byte;

    if ((new_byte == '\r') || (new_byte == '\n')) {	// Enter
        uart2_send_text("\r\n");
        line_execute();
        uart2_send_text("> ");
    } else if (line_len < LINE_MAX_LEN) {	// todavía cabe
        line[line_len] = (char)new_byte;
        line_len++;
        uart2_send_bytes(&new_byte, 1U);	// eco
    } else {
        line_too_long = true;	// sigue leyendo pero la línea se va a rechazar
    }
}

// Igual que en la etapa 2: analiza la línea, ejecuta el comando y manda la respuesta
static void line_execute(void)
{
    cmd_t cmd;
    char reply[96];

    if (line_too_long) {
        uart2_send_text("error: line too long\r\n");
    } else {
        line[line_len] = '\0';	// cmd_parse necesita el '\0' para saber dónde termina
        if (cmd_parse(line, &cmd)) {	// false si la línea estaba vacía
            commands_execute(&cmd, reply, sizeof reply);
            uart2_send_text(reply);
            uart2_send_text("\r\n");	// los comandos no ponen el salto de línea, lo pone esta función
        }
    }
    line_len = 0U;	// estas dos van por fuera del if para que siempre se ejecuten
    line_too_long = false;
}
