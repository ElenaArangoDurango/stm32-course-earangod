/* Author: earangod@unal.edu.co

Etapa 4, recepción ping-pong, transmisión por DMA y comandos.
La recepción usa dos bloques, A y B, que se turnan. Mientras el DMA llena uno, app_loop lee el otro,
así nunca trabajan sobre el mismo. Si los dos están ocupados la recepción se pausa y se avisa que
se perdieron datos, en vez de dañarlos en silencio como pasaba en la etapa 3.
La transmisión también usa DMA, enviar un texto solo lo copia a un buffer y el programa sigue,
uart2_flush lo manda al final de cada vuelta de app_loop.
Los bloques no son líneas, por eso cada byte recibido pasa por line_add_byte, que arma la línea
hasta Enter, y luego cmd_parser y commands la ejecutan igual que en la etapa 2.
Todo el hardware de USART2 (relojes, pines, dos canales de DMA e interrupciones) se reúne en HAL_UART_MspInit.
*/

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "stm32f4xx_hal.h"
#include "app.h"
#include "system.h"
#include "cmd_parser.h"	// traen el analizador y los comandos de la etapa 2
#include "commands.h"
#include "pwm.h"	// la salida PWM que controla el comando pwm

#define RX_BLOCK_SIZE  32U	// Tamaño de cada bloque de recepción, pequeño para que una ráfaga larga se parta en dos
#define TX_BUF_SIZE    256U	// Tamaño de los buffers de transmisión
#define LINE_MAX_LEN   63U	// La línea más larga que se acepta, sin contar el '\0'

static UART_HandleTypeDef huart2; // Maneja la UART
static DMA_HandleTypeDef hdma_usart2_rx; // Maneja el DMA, es static a nivel del archivo
// porque __HAL_LINKDMA guarda su dirección dentro de huart2. Es la ficha con la configuración de este canal
static DMA_HandleTypeDef hdma_usart2_tx;	// igual que el de recepción pero para el canal de transmisión

// Un bloque de recepción agrupa sus bytes, cuántos llegaron y de quién es en este momento
typedef struct {
    uint8_t data[RX_BLOCK_SIZE];	// aquí el DMA deja los bytes recibidos
    volatile uint16_t size;	// cuántos bytes llegaron, vale cuando ready es true
    volatile bool ready;	// si es true el bloque es de app_loop, si es false es del DMA
} rx_block_t;

static rx_block_t rx_block[2];	// los dos bloques, A es el 0 y B es el 1
static volatile uint8_t rx_dma_index = 0U;	// el bloque que el DMA está llenando ahora
static volatile bool rx_paused = false;	// true si los dos bloques estaban ocupados y no hay recepción
static volatile uint32_t rx_pause_count = 0U;	// cuenta las pausas para que app_loop las avise
// Cuentan los errores de la UART y guardan el último código, igual que en la etapa 2
static volatile uint32_t rx_error_count = 0U;
static volatile uint32_t rx_last_error = 0U;

static uint8_t tx_fill[TX_BUF_SIZE];	// aquí se va juntando el texto que espera para salir
static uint16_t tx_fill_len = 0U;	// cuántos bytes hay esperando en tx_fill
static uint8_t tx_dma_buf[TX_BUF_SIZE];	// de aquí lee el DMA mientras envía, nadie lo toca hasta que termine
static volatile bool tx_busy = false;	// true mientras el DMA está enviando

// Variables de la línea que se va armando, igual que en la etapa 2
static char line[LINE_MAX_LEN + 1U];	// la línea, con un espacio extra para el '\0'
static uint16_t line_len = 0U;	// cuántos caracteres lleva
static bool line_too_long = false;	// se activa si la línea pasó el máximo

static void uart2_init(void);
static void uart2_start_rx(void);
static void uart2_send_bytes(const uint8_t *data, uint16_t length);
static void uart2_send_text(const char *text);
static void uart2_flush(void);
static void line_add_byte(uint8_t new_byte);
static void line_execute(void);

// Configura la UART, manda el banner con el prompt y arranca la primera recepción
void app_init(void)
{
    uart2_init();
    pwm_init();	// arranca la salida PWM en 0 %
    uart2_send_text("\r\nStage 4: ping-pong reception.\r\n> ");
    uart2_start_rx(); // sin esta línea el DMA nunca empezaría
}

// Se repite sin parar. Lee el bloque que le toca, lo devuelve al DMA, avisa lo que vieron las
// interrupciones y al final manda el texto que quedó esperando
void app_loop(void)
{
    static uint8_t read_index = 0U;	// el bloque que le toca leer, los bloques se usan en turno A, B, A
    static uint32_t reported_errors = 0U;	// errores ya avisados
    static uint32_t reported_pauses = 0U;	// pausas ya avisadas
    char msg[64];	// espacio para armar el aviso de error

    if (rx_block[read_index].ready) {	// el DMA ya terminó este bloque y es de app_loop
        bool restart_aux;	// recuerda si la recepción estaba en pausa
        // Esto se usó en el experimento 1 del Break it de la etapa 4 y se dejó comentado. Esperaba 2 segundos
        // antes de procesar el bloque. Al pegar hello y presionar x y y, salió hellox con el aviso de pausa
        // y después y. No hubo xello, los datos no se dañaron y la pausa se avisó.
        // HAL_Delay(2000U);

        // Entrega cada byte del bloque a la línea, un bloque no es una línea
        for (uint16_t i = 0U; i < rx_block[read_index].size; i++) {
            line_add_byte(rx_block[read_index].data[i]);
        }

        // Se devuelve el bloque con las interrupciones apagadas un momento. Si el callback corriera
        // en medio, vería el bloque ocupado y pausaría justo después de que aquí se decidió no reiniciar
        __disable_irq();	// apaga las interrupciones
        rx_block[read_index].ready = false;	// devuelve el bloque al DMA
        restart_aux = rx_paused;	// lee si había pausa
        rx_paused = false;	// la pausa se termina
        __enable_irq();	// vuelve a encender las interrupciones

        if (restart_aux) {	// la recepción esperaba que se liberara un bloque
            rx_dma_index = read_index;	// el DMA usa el bloque que se acaba de liberar
            uart2_start_rx();	// reinicia la recepción
        }
        read_index = (uint8_t)(1U - read_index);	// pasa al otro bloque, 1 menos el índice cambia 0 por 1 y 1 por 0
    }

    if (rx_error_count != reported_errors) {	// el contador del callback aumentó, hay un error sin avisar
        reported_errors = rx_error_count;	// se anota como avisado
        // muestra el código del error en hexadecimal
        (void)snprintf(msg, sizeof msg, "\r\n[uart error 0x%02lX, reception restarted]\r\n",
                       (unsigned long)rx_last_error);
        uart2_send_text(msg);	// deja el aviso esperando para salir
    }
    if (rx_pause_count != reported_pauses) {	// hubo una pausa nueva
        reported_pauses = rx_pause_count;	// se anota como avisada
        uart2_send_text("\r\n[reception paused: both blocks busy, data lost]\r\n");	// los bytes de la pausa se perdieron
    }

    uart2_flush();	// empieza a enviar el texto que quedó esperando
}

// Salta cuando la UART ve la línea en reposo (IDLE), un error o el final de una transmisión, solo le pasa el trabajo al HAL
void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&huart2);
}

// Salta cuando el buffer se llenó (TC) o cuando hay un error del DMA de recepción, también se lo pasa al HAL.
// El nombre debe coincidir exactamente con la tabla de vectores. En el experimento 3 del Break it de la etapa 3
// se le quitó la r final, compiló sin errores pero al llenarse el buffer la placa se colgó y LD2 se quedó quieto,
// porque la interrupción fue a Default_Handler, un bucle infinito
void DMA1_Stream5_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_rx);
}

// Es el del canal de transmisión, el DMA avisa aquí cómo va el envío y el HAL se encarga del resto
void DMA1_Stream6_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&hdma_usart2_tx);
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

    // Segundo DMA para transmitir, stream 6 canal 4 es el par fijo de USART2_TX en la tabla del manual
    hdma_usart2_tx.Instance                 = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel             = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;	// ahora los datos van de la memoria a la UART
    hdma_usart2_tx.Init.PeriphInc           = DMA_PINC_DISABLE;	// el registro de la UART es siempre el mismo
    hdma_usart2_tx.Init.MemInc              = DMA_MINC_ENABLE;	// cada byte sale de una posición más adelante del buffer
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode                = DMA_NORMAL;	// envía el buffer una vez y se detiene
    hdma_usart2_tx.Init.Priority            = DMA_PRIORITY_LOW;	// la transmisión puede esperar, la recepción no
    hdma_usart2_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(huart, hdmatx, hdma_usart2_tx);	// sin esta línea HAL_UART_Transmit_DMA usaría un puntero nulo y la CPU fallaría

    // Habilita las interrupciones de los dos DMA y de USART2, todas con prioridad 2
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(USART2_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

// El HAL la llama dentro de una interrupción cuando termina un bloque, por IDLE o porque se llenó.
// Entrega el bloque lleno a app_loop y recibe en el otro si está libre. El nombre debe ser exacto,
// si no el HAL usa su versión vacía
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if ((huart->Instance == USART2) &&	// solo si es la USART2
        (HAL_UARTEx_GetRxEventType(huart) != HAL_UART_RXEVENT_HT)) {	// el evento de la mitad no cuenta, no detiene la recepción
        uint8_t filled_index = rx_dma_index;	// el bloque que se acaba de llenar
        uint8_t other_index = (uint8_t)(1U - filled_index);	// el otro bloque

        rx_block[filled_index].size = Size;	// guarda cuántos bytes llegaron
        rx_block[filled_index].ready = true;	// desde ahora el bloque es de app_loop
        if (!rx_block[other_index].ready) {	// el otro bloque está libre
            rx_dma_index = other_index;	// el DMA pasa a usarlo
            uart2_start_rx();	// reinicia la recepción en el otro bloque
        } else {	// los dos están ocupados, no hay dónde escribir
            rx_paused = true;	// app_loop reiniciará la recepción cuando libere un bloque
            rx_pause_count++;	// cuenta la pausa para avisarla
        }
    }
}

// El HAL la llama cuando termina de salir el último byte de la transmisión. Libera la transmisión
// para que uart2_flush pueda mandar el siguiente texto. El nombre debe ser exacto. En el experimento 2 del
// Break it de la etapa 4 se renombró a HAL_UART_TxCompleteCallback, compiló sin errores pero salió el banner y nada
// más, porque el HAL usó su versión vacía y tx_busy nunca volvió a false. blink siguió funcionando, solo la salida se trabó
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {	// solo si es la USART2
        tx_busy = false;	// el DMA terminó, se puede enviar otro texto
    }
}

// El HAL la llama cuando la UART o el DMA reportan un error. En DMA todo error detiene la recepción,
// por eso se guarda el error y se reinicia, salvo que la recepción esté en pausa a propósito
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {	// solo si es la USART2
        rx_last_error = HAL_UART_GetError(huart);	// guarda el código del error
        rx_error_count++;	// cuenta un error más
        if (huart->gState == HAL_UART_STATE_READY) {	// la transmisión también terminó por el error
            tx_busy = false;	// se libera para no quedar esperando para siempre
        }
        if (!rx_paused) {	// si hay pausa no se reinicia, lo hará app_loop
            uart2_start_rx();	// reinicia la recepción
        }
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

// Arranca una recepción por DMA hacia el bloque rx_dma_index que termina cuando la línea queda en reposo
// o cuando el bloque se llena. Se llama en app_init, en los callbacks y en app_loop
static void uart2_start_rx(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_block[rx_dma_index].data,	// arranca el DMA y el aviso de IDLE
                                     RX_BLOCK_SIZE) == HAL_OK) {
        // El HAL enciende el aviso de mitad del bloque (HT) cada vez que arranca, por eso se apaga aquí
        // cada vez, queremos bloques completos
        __HAL_DMA_DISABLE_IT(&hdma_usart2_rx, DMA_IT_HT);
    }
}

// Agrega bytes al texto que espera para salir y vuelve al instante. Si no caben se descarta
// el pedazo completo, nada espera y nada se corta a la mitad
static void uart2_send_bytes(const uint8_t *data, uint16_t length)
{
    if (length > (TX_BUF_SIZE - tx_fill_len)) {	// no cabe en lo que queda del buffer
        return;	// se descarta
    }
    memcpy(&tx_fill[tx_fill_len], data, length);	// copia los bytes justo después de lo que ya hay, memcpy copia sin buscar un '\0'
    tx_fill_len = (uint16_t)(tx_fill_len + length);	// ahora hay más bytes esperando
}

// Agrega un texto de C al texto que espera para salir, usa uart2_send_bytes
static void uart2_send_text(const char *text)
{
    uart2_send_bytes((const uint8_t *)text, (uint16_t)strlen(text));	// el texto visto como bytes y su largo
}

// Se llama al final de cada vuelta de app_loop. Si el DMA está libre y hay texto esperando, lo copia
// a tx_dma_buf y arranca el DMA, así app_loop puede seguir agregando texto en tx_fill
static void uart2_flush(void)
{
    if ((!tx_busy) && (tx_fill_len > 0U)) {	// el DMA está libre y hay algo que enviar
        memcpy(tx_dma_buf, tx_fill, tx_fill_len);	// pasa el texto al buffer que lee el DMA
        tx_busy = true;	// se marca antes de arrancar porque el callback puede ser muy rápido
        if (HAL_UART_Transmit_DMA(&huart2, tx_dma_buf, tx_fill_len) == HAL_OK) {	// arranca el envío y vuelve al instante
            tx_fill_len = 0U;	// el texto ya salió hacia el DMA, tx_fill queda vacío
        } else {	// no arrancó
            tx_busy = false;	// se libera y se intenta de nuevo en la próxima vuelta
        }
    }
}

// Agrega un byte recibido a la línea que se está escribiendo y lo devuelve como eco.
// Enter (\r, \n o \r\n) termina la línea y llama a line_execute
static void line_add_byte(uint8_t new_byte)
{
    static uint8_t previous_byte = 0U;	// el byte anterior, es static para recordarlo entre llamadas

    if ((new_byte == '\n') && (previous_byte == '\r')) {	// es la segunda mitad de un \r\n
        previous_byte = new_byte;	// se anota
        return;	// ya se atendió con el \r, no se cuenta otra vez
    }
    previous_byte = new_byte;	// se anota para la próxima llamada

    if ((new_byte == '\r') || (new_byte == '\n')) {	// fin de línea
        uart2_send_text("\r\n");	// baja el cursor a la línea siguiente
        line_execute();	// ejecuta la línea que se armó
        uart2_send_text("> ");	// vuelve a mostrar el prompt
    } else if (line_len < LINE_MAX_LEN) {	// todavía cabe en la línea
        line[line_len] = (char)new_byte;	// guarda el carácter
        line_len++;	// la línea es un carácter más larga
        uart2_send_bytes(&new_byte, 1U);	// eco, se agrega al texto que espera para salir
    } else {	// la línea ya está llena
        line_too_long = true;	// se sigue leyendo pero la línea se va a rechazar
    }
}

// Se llama cuando llega Enter. Toma la línea que armó line_add_byte, la analiza con
// cmd_parse, ejecuta el comando con commands_execute y envía la respuesta por la UART.
// Al final vacía la línea para la siguiente. Una línea vacía se ignora
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
            uart2_send_text(reply);	// deja la respuesta esperando para salir
            uart2_send_text("\r\n");	// el final de línea lo pone esta función, los comandos no lo escriben
        }
    }
    line_len = 0U;	// vacía la línea para la siguiente
    line_too_long = false;	// borra la marca de error, estas dos van fuera del if para que corran en todos los casos
}
