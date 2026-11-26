/* Author: earangod@unal.edu.co

Etapa 3, recepción por DMA terminada por la línea IDLE.
El DMA copia cada byte de USART2 a un buffer sin que la CPU haga nada, y la UART avisa
cuando la línea queda en reposo, o sea cuando terminó una ráfaga. La CPU ya no atiende una
interrupción por byte sino una por ráfaga. Esta etapa solo imprime cada bloque recibido,
con su tamaño y qué lo terminó (IDLE, TC o HT), los comandos vuelven en la etapa 4.
Con un solo buffer, una ráfaga de más de 32 bytes se parte en dos y un bloque nuevo puede
sobrescribir el anterior si app_loop tarda en imprimirlo, eso lo resuelve la etapa 4.
Todo el hardware de USART2 (relojes, pines, DMA e interrupciones) se reúne en HAL_UART_MspInit.
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

static UART_HandleTypeDef huart2; // Maneja la UART
static DMA_HandleTypeDef hdma_usart2_rx; // Maneja el DMA, es static a nivel del archivo
// porque __HAL_LINKDMA guarda su dirección dentro de huart2. Es la ficha con la configuración de este canal
static uint8_t rx_dma_buf[RX_DMA_SIZE]; // Es el buffer donde el DMA deja los bytes recibidos, sin que la CPU haga nada
// El aviso del callback a app_loop dice que llegó un bloque, cuántos bytes tiene
// y qué lo terminó. Son variables volatile porque las escribe una interrupción
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
        uint16_t size_aux;	// copia local del tamaño
        uint32_t type_aux;	// copia local del tipo de evento

        // Se reclama el evento con las interrupciones apagadas un momento, así uno nuevo no puede
        // cambiar el tamaño después de leerlo y antes de leer el tipo
        __disable_irq();	// apaga las interrupciones
        size_aux = rx_event_size;	// copia el tamaño
        type_aux = rx_event_type;	// copia el tipo
        rx_event_ready = false;	// baja el aviso, ya se reclamó
        __enable_irq();	// vuelve a encender las interrupciones
        // Esto se usó en el experimento 1 del Break it de la etapa 3 y se dejó comentado. Esperaba 2 segundos
        // antes de imprimir. Mientras tanto el DMA ya llenaba el mismo buffer desde la posición 0, así que
        // al pegar hello y presionar x dentro de los 2 segundos salió 'xello', la x pisó la h.
        // HAL_Delay(2000U);

        print_block(type_aux, size_aux);	// imprime con las copias, no con las variables del callback
    }

    if (rx_error_count != reported_errors) {	// el contador del callback aumentó, hay un error sin avisar
        reported_errors = rx_error_count;	// se anota como avisado
        // muestra el código del error en hexadecimal
        (void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
                       (unsigned long)rx_last_error);
        uart2_send_text(msg);	// envía el aviso
    }
}

// Salta cuando la UART ve la línea en reposo (IDLE) o un error, solo le pasa el trabajo al HAL
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

// Salta cuando el buffer se llenó (TC) o cuando hay un error del DMA, también se lo pasa al HAL.
// El nombre debe coincidir exactamente con la tabla de vectores. En el experimento 3 del Break it se le quitó
// la r final, compiló sin errores pero al llenarse el buffer la placa se colgó y LD2 se quedó quieto, porque
// la interrupción fue a Default_Handler, un bucle infinito
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

// Reúne todo el hardware que acompaña a USART2. El HAL la llama dentro de HAL_UART_Init antes de escribir
// los registros de la UART. Se escribe aquí porque la versión del HAL es __weak y está vacía
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio = {0};	// campos en cero antes de llenarlos

    if (huart->Instance != USART2) { // Si la llamada es de otra UART no hace nada
        return;
    }
    // Enciende los relojes, el del DMA va antes de HAL_DMA_Init porque un periférico sin reloj ignora lo que se le escribe
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    // Configura PA2 y PA3 como función alternativa AF7, entregados a USART2
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    // Se configura el DMA. En Instance y Channel se pone stream 5, canal 4, que es
    // el par fijo de USART2_RX en la tabla del manual
    hdma_usart2_rx.Instance                 = DMA1_Stream5;
    hdma_usart2_rx.Init.Channel             = DMA_CHANNEL_4;
    // Del periférico a memoria, los datos van de la UART al buffer
    hdma_usart2_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_usart2_rx.Init.PeriphInc           = DMA_PINC_DISABLE; // Apagado, la dirección del registro de la UART no cambia
    hdma_usart2_rx.Init.MemInc              = DMA_MINC_ENABLE; // Encendido, cada byte se escribe una posición más adelante en el buffer
    hdma_usart2_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; // Las alineaciones son de a un byte
    hdma_usart2_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE; // igual para la memoria
    hdma_usart2_rx.Init.Mode                = DMA_NORMAL;	// modo normal, se detiene cuando el buffer se llena, no es circular
    hdma_usart2_rx.Init.Priority            = DMA_PRIORITY_HIGH;	// prioridad del canal
    hdma_usart2_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;	// sin almacenamiento intermedio
    if (HAL_DMA_Init(&hdma_usart2_rx) != HAL_OK) {	// aplica la configuración del DMA
        Error_Handler();	// si falla, se queda en el manejador de errores
    }
    __HAL_LINKDMA(huart, hdmarx, hdma_usart2_rx);	// conecta los dos manejadores, huart2 conoce su DMA y el DMA conoce su UART

    // Habilita las interrupciones del DMA y de USART2, las dos con prioridad 2
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

// El HAL la llama dentro de una interrupción cuando termina un bloque, por IDLE o porque el buffer se llenó.
// Size es la cantidad de bytes que hay en el buffer. El nombre debe ser exacto, si no el HAL usa su versión vacía
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2) {	// solo si es la USART2
        rx_event_size = Size;	// guarda cuántos bytes llegaron
        rx_event_type = HAL_UARTEx_GetRxEventType(huart);	// guarda qué terminó el bloque, IDLE, TC o HT
        rx_event_ready = true;	// avisa a app_loop que hay un bloque
        uart2_start_rx();	// IDLE y TC detienen la recepción, se reinicia para el siguiente bloque
    }
}

// El HAL la llama cuando la UART o el DMA reportan un error. En DMA todo error detiene la recepción,
// por eso se guarda el error y se reinicia
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {	// solo si es la USART2
        rx_last_error = HAL_UART_GetError(huart);	// guarda el código del error
        rx_error_count++;	// cuenta un error más
        uart2_start_rx();	// reinicia la recepción
    }
}

// Llena la ficha de la UART y llama a HAL_UART_Init, que a su vez llama a HAL_UART_MspInit.
// Los relojes, los pines y el DMA ya no están aquí
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
    if (HAL_UART_Init(&huart2) != HAL_OK) {	// escribe los valores en los registros y enciende la UART
        Error_Handler();
    }
}

// Arranca una recepción por DMA hacia rx_dma_buf que termina cuando la línea queda en reposo
// o cuando el buffer se llena. Se llama en app_init y dentro de los callbacks
static void uart2_start_rx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buf, RX_DMA_SIZE) == HAL_OK) {	// arranca el DMA y el aviso de IDLE
        // El HAL enciende el aviso de mitad del buffer (HT) cada vez que arranca, por eso se apaga aquí
        // cada vez, queremos bloques completos
        // En el experimento 2 del Break it se comentó la línea de abajo. Sin ella el aviso de mitad del buffer (HT)
        // queda encendido, no detiene la recepción y el reinicio del callback es rechazado con HAL_BUSY.
        // Al pegar las 26 letras salió [HT 16 bytes] y luego [IDLE 26 bytes], la primera mitad repetida.
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}

// Envía un texto y espera a que salga completo, es bloqueante como en la etapa 1
static void uart2_send_text(const char *text)
{
    (void)HAL_UART_Transmit(&huart2, (const uint8_t *)text,	// la UART y el texto visto como bytes
                            (uint16_t)strlen(text), TX_TIMEOUT_MS);	// cantidad de caracteres y tiempo máximo
}

// Imprime el bloque recibido con el formato [EVENTO bytes] 'texto', leyendo rx_dma_buf directamente.
// Los bytes que no se pueden mostrar, como Enter, salen como un punto
static void print_block(uint32_t event_type, uint16_t size)
{
    char name[5];	// espacio para IDLE y el '\0'
    char text[RX_DMA_SIZE + 1U];	// el bloque como texto, con un espacio extra para el '\0'
    char msg[RX_DMA_SIZE + 32U];	// el mensaje completo

    if (size > RX_DMA_SIZE) {	// por seguridad nunca se lee más allá del buffer
        size = RX_DMA_SIZE;
    }
    for (uint16_t i = 0U; i < size; i++) {	// recorre los bytes recibidos
        uint8_t byte_aux = rx_dma_buf[i];	// el byte que dejó el DMA
        // si está entre 0x20 y 0x7E se muestra tal cual, si no se muestra un punto
        text[i] = ((byte_aux >= 0x20U) && (byte_aux < 0x7FU)) ? (char)byte_aux : '.';
    }
    text[size] = '\0';	// cierra el texto para poder usarlo con snprintf
    event_name(event_type, name, sizeof name);	// escribe el nombre del evento en name
    (void)snprintf(msg, sizeof msg, "[%s %2u bytes] '%s'\r\n",	// %2u deja el número en dos espacios
                   name, (unsigned int)size, text);
    uart2_send_text(msg);	// envía el mensaje
}

// Escribe el nombre del evento en name. No lo devuelve, lo escribe en el arreglo que le presta quien la llama,
// ese arreglo sigue existiendo cuando la función termina
static void event_name(uint32_t event_type, char *name, size_t name_size)
{
    switch (event_type) {	// compara el tipo con los tres eventos del HAL
    case HAL_UART_RXEVENT_IDLE:
        (void)snprintf(name, name_size, "IDLE");
        break;
    case HAL_UART_RXEVENT_TC:
        (void)snprintf(name, name_size, "TC");
        break;
    case HAL_UART_RXEVENT_HT:
        (void)snprintf(name, name_size, "HT");
        break;
    default:	// cualquier otro valor
        (void)snprintf(name, name_size, "?");
        break;
    }
}
