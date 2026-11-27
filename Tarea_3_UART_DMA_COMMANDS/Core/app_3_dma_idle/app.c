/* Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co

Etapa 3: recepción por DMA que termina con la línea IDLE.
Aquí el DMA es el que copia los bytes al buffer y la CPU no hace nada mientras tanto. Cuando la línea
se queda quieta (IDLE) o el buffer se llena (TC), el HAL avisa una sola vez por todo el bloque y no por
cada byte como en la etapa 2.
Por ahora solo se imprime cada bloque con su tamaño y el evento que lo cerró, los comandos vuelven en la etapa 4.
El problema que queda es que hay un solo buffer, si app_loop se demora el DMA escribe encima.
Ahora los relojes, pines, DMA y NVIC de la USART2 se configuran en HAL_UART_MspInit.
*/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"

#define TX_TIMEOUT_MS  100U // Tiempo máximo para enviar un texto
#define RX_DMA_SIZE    32U // Tamaño del buffer, se configura pequeño para que una ráfaga larga
// se parta en dos

static UART_HandleTypeDef huart2; // ficha de la UART
static DMA_HandleTypeDef hdma_usart2_rx; // ficha del DMA de recepción, va afuera de las funciones porque
// __HAL_LINKDMA guarda su dirección en huart2 y tiene que seguir existiendo
static uint8_t rx_dma_buf[RX_DMA_SIZE]; // aquí el DMA deja los bytes que llegan
// Con estas tres el callback le avisa a app_loop que llegó un bloque, de cuántos bytes y qué lo cerró.
// Son volatile porque las cambia una interrupción
static volatile bool rx_event_ready = false;
static volatile uint16_t rx_event_size = 0U;
static volatile uint32_t rx_event_type = 0U;
// Cuentan los errores de la UART y guardan el último código, igual que en la etapa 2
static volatile uint32_t rx_error_count = 0U;
static volatile uint32_t rx_last_error = 0U;

static void uart2_init(void);
static void uart2_start_rx(void);
static void uart2_send_text(const char *text);
static void print_block(uint32_t event_type, uint16_t size);
static void event_name(uint32_t event_type, char *name, size_t name_size);

// Configura la UART, manda el banner y arranca la primera recepción
void app_init(void)
{
    uart2_init();
    uart2_send_text("\r\nStage 3: DMA reception, ended by IDLE.\r\n");
    uart2_start_rx(); // sin esta línea el DMA nunca empezaría
}

// Se repite sin parar. Si hay un bloque nuevo lo reclama y lo imprime, y avisa si hubo errores
void app_loop(void)
{
    static uint32_t reported_errors = 0U;	// errores ya avisados, es static para recordarlos entre vueltas
    char msg[64];	// espacio para armar el aviso de error

    if (rx_event_ready) {	// si es verdadero, hay un bloque nuevo
        uint16_t size_aux;
        uint32_t type_aux;

        // Se copian los datos con las interrupciones apagadas, así otro bloque no
        // cambia el tamaño en medio de la copia
        __disable_irq();
        size_aux = rx_event_size;
        type_aux = rx_event_type;
        rx_event_ready = false;	// ya se reclamó el aviso
        __enable_irq();
        // Esto se usó en el experimento 1 del Break it de la etapa 3 y se dejó comentado. Esperaba 2 segundos
        // antes de imprimir. Mientras tanto el DMA ya llenaba el mismo buffer desde la posición 0, así que
        // al pegar hello y presionar x dentro de los 2 segundos salió 'xello', la x pisó la h.
        // HAL_Delay(2000U);

        print_block(type_aux, size_aux);	// se imprime con las copias
    }

    if (rx_error_count != reported_errors) {	// hay un error que no se ha avisado
        reported_errors = rx_error_count;
        (void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
                       (unsigned long)rx_last_error);
        uart2_send_text(msg);
    }
}

// Salta con el IDLE o con un error de la UART, solo le pasa el trabajo al HAL
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

/* Salta cuando el buffer se llenó (TC) o hay un error del DMA, y también se lo pasa al HAL.
El nombre tiene que ser igual al de la tabla de vectores. En el experimento 3 del Break it le quité
la r del final, compiló sin errores pero cuando se llenó el buffer la placa se colgó y LD2 se quedó quieto,
porque la interrupción se fue a Default_Handler, que es un bucle infinito. */
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

/* Aquí va todo el hardware de la USART2. HAL_UART_Init la llama sola antes de escribir los registros.
La del HAL es __weak y está vacía, por eso se escribe esta. */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};

    if (huart->Instance != USART2) { // si es otra UART no se hace nada
        return;
    }
    // Relojes, el del DMA tiene que ir antes de HAL_DMA_Init porque sin reloj ignora lo que se le escribe
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    // PA2 y PA3 en función alternativa AF7, igual que en las etapas anteriores
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    // DMA de recepción: stream 5 canal 4, es el que le toca a USART2_RX según la tabla del manual
    hdma_usart2_rx.Instance                 = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;	// de la UART al buffer
    hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE; // siempre se lee el mismo registro de la UART
    hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE; // en el buffer sí avanza una posición por byte
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; // de a un byte
    hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_rx.Init.Mode                = DMA_NORMAL;	// para cuando se llena, no es circular
    hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx);	// así huart2 sabe cuál es su DMA

    // NVIC: interrupciones del DMA y de la USART2, las dos con prioridad 2
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/* El HAL la llama desde la interrupción cuando se cierra un bloque, por IDLE o porque se llenó el buffer.
Size es cuántos bytes hay. Si el nombre está mal escrito el HAL sigue usando su versión vacía. */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {
        rx_event_size = Size;
        rx_event_type = HAL_UARTEx_GetRxEventType(huart);	// IDLE, TC o HT
        rx_event_ready = true;	// aviso para app_loop
        uart2_start_rx();	// IDLE y TC paran la recepción, hay que volver a arrancarla
    }
}

// El HAL la llama cuando hay un error en la UART o en el DMA. Con DMA cualquier error para la recepción,
// por eso se guarda y se vuelve a arrancar
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        rx_last_error = HAL_UART_GetError(huart);
        rx_error_count++;
        uart2_start_rx();
    }
}

// Solo llena la ficha y llama a HAL_UART_Init, los relojes, pines y DMA ahora están en HAL_UART_MspInit
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

// Arranca la recepción por DMA en rx_dma_buf, termina con IDLE o cuando el buffer se llena.
// Se llama en app_init y en los callbacks
static void uart2_start_rx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, RX_DMA_SIZE) == HAL_OK) {
        /* El HAL prende solo el aviso de mitad de buffer (HT) cada vez que arranca, entonces hay que
        apagarlo cada vez porque solo nos interesan los bloques completos.
        En el experimento 2 del Break it comenté esta línea, el HT quedó prendido y el callback trató de
        reiniciar pero el HAL le dijo HAL_BUSY. Con las 26 letras salió [HT 16 bytes] y después
        [IDLE 26 bytes], o sea la primera mitad salió dos veces. */
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}

// Envía un texto y espera a que salga, sigue siendo bloqueante como en la etapa 1
static void uart2_send_text(const char *text)
{
    (void)HAL_UART_Transmit(&huart2, (const uint8_t *)text,
                            (uint16_t)strlen(text), TX_TIMEOUT_MS);
}

// Imprime el bloque así: [EVENTO bytes] 'texto'. Lo que no se puede mostrar, como el Enter, sale como un punto
static void print_block(uint32_t event_type, uint16_t size)
{
    char name[5];	// cabe IDLE y el '\0'
    char text[RX_DMA_SIZE + 1U];	// +1 para el '\0'
    char msg[RX_DMA_SIZE + 32U];

    if (size > RX_DMA_SIZE) {	// para no leer por fuera del buffer
        size = RX_DMA_SIZE;
    }
    for (uint16_t i = 0U; i < size; i++) {
        uint8_t byte_aux = rx_dma_buf[i];
        // de 0x20 a 0x7E se ve normal, lo demás sale como punto
        text[i] = ((byte_aux >= 0x20U) && (byte_aux < 0x7FU)) ? (char)byte_aux : '.';
    }
    text[size] = '\0';
    event_name(event_type, name, sizeof name);
    (void)snprintf(msg, sizeof msg, "[%s %2u bytes] '%s'\r\n",	// %2u deja el número en dos espacios
                   name, (unsigned int)size, text);
    uart2_send_text(msg);
}

// Escribe el nombre del evento en name, que es un arreglo que le presta print_block.
// No se puede devolver un arreglo local porque se borra cuando la función termina
static void event_name(uint32_t event_type, char *name, size_t name_size)
{
    switch (event_type) {
    case HAL_UART_RXEVENT_IDLE:
        (void)snprintf(name, name_size, "IDLE");
        break;
    case HAL_UART_RXEVENT_TC:
        (void)snprintf(name, name_size, "TC");
        break;
    case HAL_UART_RXEVENT_HT:
        (void)snprintf(name, name_size, "HT");
        break;
    default:
        (void)snprintf(name, name_size, "?");
        break;
    }
}
