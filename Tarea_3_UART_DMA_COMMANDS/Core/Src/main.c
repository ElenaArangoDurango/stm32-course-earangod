/* Core/Src/main.c — the same for every stage; never edited again
 * Author: earangod@unal.edu.co
 */

 /* Es necesario que el LD2 esté parapadeando todo el tiempo, si deja de hacerlo está indicandonos un problema 
 de estancamiento de la CPU, si parpadea pero la placa no responde, el problema está en la UART
 Otro punto importante de entender es que cada driver HAL necesita dos piezas, una es el 
 #define y la otra es el archivo .c en el CMake,
 además el main.c no cambia, el CMake elige que app.c compilar según lo que encuentre en la variable APP  */
/* Registro de checkpoints:

 0.A Compilar el driver de UART: compila sin errores y LD2 sigue parpadeando.
 
 0.B Selector de aplicación: CMake muestra ">>> Building APP = ..." con la app elegida.

 0.C Heartbeat en módulos compartidos: LD2 parpadea a 2 Hz y el cambio de periodo funciona.
 
 0.D Terminal serie: picocom conecta a 115200 8N1 en /dev/ttyACM0 sin errores.
 Enter envía \r (0x0D): confirmado en 1.C con "rx: '.' 0x0D".
 
 1.A Etapa agregada al build: APP = polling compila sin errores.
 
 1.B USART2 y mensaje de bienvenida: aparece en picocom al presionar RESET y LD2 sigue parpadeando.
 
 1.C Eco de teclas y tick cada segundo: sale un tick por segundo y cada tecla responde "rx: ..." con su valor.
 Break it 1 (RX_TIMEOUT_MS = HAL_MAX_DELAY): los tick dejaron de salir y LD2 siguió parpadeando.
 Al escribir 'a' salió rx: 'a' 0x61 y el tick 1 a 100136 ms. Valor devuelto a 10U.
 Break it 2 (pegar 0123456789): solo regresaron rx: '0' 0x30 y rx: '1' 0x31; los otros 8 se perdieron
 por overrun y después siguió el tick 36 a 36080 ms. Lo resuelven las interrupciones (etapa 2).
 
 2.A Etapa agregada al build: APP = interrupt compila y la placa se comporta igual que en la etapa 1
 (banner, tick y rx).
 
 2.B Recepción por interrupción: en la placa aparece "Stage 2: interrupt reception." y ya no salen tick.
 Falta anotar el resultado de pegar 0123456789 (se esperan las 10 líneas rx: en orden).
 
 2.C Línea de comandos: cada tecla se ve al escribirla, Enter muestra line: '...' y un nuevo prompt,
 un Enter solo muestra solo el prompt y una línea de más de 63 caracteres da error: line too long.
 
 2.D Comandos: help, info, blink, echo y los casos de error responden como indica la guía y LD2 cambia con blink.
 
 Break it 1 de la etapa 2 (apagar las interrupciones cada 5 segundos con una tecla presionada):
 cada 5 segundos salió [uart error 0x08, reception restarted] (0x08 es el overrun), faltaron algunos
 caracteres, se pudo seguir escribiendo porque el callback reinició la recepción y LD2 titubeó en cada pausa.
 El bloque quedó comentado en app_2_interrupt/app.c.
 Break it 2 de la etapa 2 (quitar uart2_start_rx del callback de error): después del primer error la terminal
 no dejó escribir más comandos y la UART quedó sorda, mientras LD2 siguió parpadeando. Se devolvió la llamada a su lugar.

 3.A Etapa agregada al build: APP = dma_idle compila sin errores, CMake muestra >>> Building APP = dma_idle
 y la placa se comporta igual que en la etapa 2 (banner, help, blink, echo).

 3.B Recepción por DMA y línea IDLE: aparece "Stage 3: DMA reception, ended by IDLE.", cada tecla despacio sale como su propio
 bloque [IDLE  1 bytes] y el texto hello world pegado llega como un solo bloque [IDLE 11 bytes], con Enter como '.'.

 3.C Ráfagas largas y recuperación de errores: el texto de 40 caracteres llegó partido en dos bloques porque el buffer
 de 32 bytes se llenó antes de que la línea quedara en reposo. Con la terminal a 9600 baudios salieron caracteres
 raros (la placa sigue a 115200, así que las tramas llegan con el tiempo equivocado y lo que ella envía tampoco se lee bien).
 Al volver a 115200 la recepción funcionó normal, porque HAL_UART_ErrorCallback reinició la recepción.

 Break it 1 de la etapa 3 (HAL_Delay de 2 segundos en app_loop): al pegar hello y presionar x dentro de los 2 segundos
 salió [IDLE  5 bytes] 'xello', porque el DMA ya llenaba el mismo buffer desde la posición 0 y la x pisó la h.
 El HAL_Delay quedó comentado en app_3_dma_idle/app.c.
 Break it 2 de la etapa 3 (comentar __HAL_DMA_DISABLE_IT del HT): al pegar las 26 letras salieron [HT 16 bytes] y luego
 [IDLE 26 bytes], la primera mitad se imprimió dos veces. La línea quedó en su lugar.
 Break it 3 de la etapa 3 (DMA1_Stream5_IRQHandle sin la r final): compiló sin errores y escribir seguía funcionando,
 pero al pegar 40 caracteres la placa se colgó y LD2 se quedó quieto (CPU atrapada en Default_Handler). Se corrigió el nombre.
 Los tres experimentos del Break it de la etapa 3 están hechos.

 4.A Etapa agregada al build: APP = pingpong compila sin errores, CMake muestra >>> Building APP = pingpong
 y la placa se comporta igual que en la etapa 3.

 4.B Dos bloques de recepción: las teclas escritas una a una se alternan entre los bloques,
 [A  1 bytes] 'a', [B  1 bytes] 's', [A  1 bytes] 'd'. hello world pegado llega en un solo bloque y el texto
 de 40 caracteres llega como [A 32 bytes] y [B  8 bytes], el primero completo y correcto.
 Los tres se probaron juntos en el código final, que muestra el banner con el prompt y se comporta como se esperaba.
 Esta salida de bloques ya no existe en el código final, porque en el 4.D los bloques dejan de imprimirse.

 4.C Transmisión por DMA, la salida es la misma que en el 4.B, banner, bloques A y B y el texto de 40
 caracteres. La diferencia no se ve, app_loop ya no espera unos 3 ms por cada línea que imprime, solo copia el texto y sigue.

 4.D Líneas y comandos otra vez, cada línea de la tabla de comandos del 2.D responde igual que en la etapa 2,
 y el comando echo 0123456789 abcdefghijklmnopqrstuvwxyz pegado de una vez responde 0123456789 abcdefghijklmnopqrstuvwxyz,
 aunque llegó en dos bloques de 32 y 10 bytes, porque el armado de línea los juntó.

 Break it 1 de la etapa 4 (HAL_Delay de 2 segundos al procesar un bloque): al pegar hello y presionar x y y dentro
 de los 2 segundos salió hellox con [reception paused: both blocks busy, data lost] y después apareció y.
 No hubo xello, los datos no se dañaron y la pausa se avisó. La guía esperaba que la y se perdiera, no se perdió.
 El HAL_Delay quedó comentado en app_4_pingpong/app.c.
 Repetido con tres teclas, x, y y z, dentro de los 2 segundos: se reportó que funciona bien. No se anotó si la z
 se perdió o no, así que la explicación del registro de datos de la UART (guarda un byte durante la pausa) sigue sin comprobarse.

 Break it 2 de la etapa 4 (HAL_UART_TxCompleteCallback en vez de HAL_UART_TxCpltCallback): compiló sin errores. Con el comando
 blink pegado desde la terminal LD2 sí cambió de ritmo, o sea la placa recibía, analizaba y ejecutaba los comandos, mientras
 la salida de texto estaba trabada, porque el HAL usaba su callback vacío y tx_busy nunca volvía a false. Se corrigió el nombre.
 Los dos experimentos del Break it de la etapa 4 están hechos.

 Nota sobre el pin de la etapa 5, el LED externo va en PB7 y no en PA6 (D12) como dice la guía. Por eso pwm.c usa
 TIM4 canal 2 con AF2 y el reloj del puerto B, en vez de TIM3 canal 1 con el reloj del puerto A. Solo se tocaron
 pwm.c y pwm.h, el resto del comando quedó igual. PB7 también sirve para I2C1 SDA y USART1 RX, hoy no hay conflicto.

 5.A Teach the parser a new word: help lista pwm <0..100>, pwm 150 no deja subir de 100 (da error de rango),
 pwm abc responde que no es un número y pwm sin argumento responde usage: pwm <0..100>.
 No se anotó el texto exacto de cada respuesta, se reportó que salieron como se esperaba.

 5.B Módulo PWM: pwm.c y pwm.h agregados al build y pwm_init llamado en app_init, el proyecto compila sin errores.
 Al arrancar el LED en PB7 queda apagado (0 %).

 5.C Comando conectado a la salida: pwm cambia la intensidad del LED externo en PB7.
 No se anotó el valor exacto de cada prueba (pwm 100, 50, 10 y 0).

 Break it 1 de la etapa 5 (APP = interrupt, la etapa 2 comparte commands.c pero nunca llama a pwm_init): salió el banner
 Stage 2, help sigue listando pwm <0..100> y pwm 50 responde error: pwm is not started in this application.
 pwm_set_duty revisa pwm_started y no escribe en un temporizador que nadie configuró. Se volvió a APP = pingpong.

 Break it 2 de la etapa 5 (comentar { "pwm", CMD_PWM } en la tabla de cmd_parser.c): compiló sin errores ni advertencias,
 help sigue listando pwm <0..100> y pwm 50 responde error: unknown command 'pwm' (type help). La tabla y el case
 de commands.c están en archivos distintos y nada obliga a que coincidan. La fila volvió a su lugar.
 Los dos experimentos del Break it de la etapa 5 están hechos.

 Ejercicio opcional de la etapa 5: pwm sin argumento responde el ciclo de trabajo actual, por ejemplo pwm is 30 %,
 con pwm_get_duty(), y con dos o más argumentos sigue respondiendo usage. Solo se tocó commands.c.
 Probado en la placa, las tres pruebas (pwm 30, pwm y pwm 10 20) salieron bien.
 */

 /* PREGUNTAS DE REPASO
 ETAPA 1: 
1. Primero se activa el reloj de USART2 porque un periférico sin reloj ignora 
lo que se escribe en sus registros. Si pusiera HAL_UART_Init() antes, el HAL escribiría la configuración y no pasaría nada,
 y la UART no funcionaría sin mostrar ningún error.

2. HAL_UART_Init() llama a HAL_UART_MspInit(), que es una función del HAL pensada para activar relojes y pines, 
pero por defecto está vacía. Como ya se activaron los relojes y se configuraron los pines en uart2_init() 
antes de HAL_UART_Ini falta y la UART funciona igual.

3. Si no llega ninguna tecla en RX_TIMEOUT_MS, HAL_UART_Receive devuelve HAL_TIMEOUT.
En app_loop se pregunta si el resultado es HAL_OK, y como no lo es, se salta el bloque, 
no imprime nada y la función termina para volver a empezar. 

4. Con HAL_MAX_DELAY, el app_loop se queda esperando una tecla para siempre dentro de HAL_UART_Receive, y nunca llega al bloque de los tick,
por eso dejan de salir. LD2 lo maneja la interrupción de TIM2, entonces sigue parpadeando.

5. Cada byte ocupa 10 datos y uno de parada. A 115200 baudios eso son 10 ÷ 115200 = 87 microsegundos. 
La línea rx: '0' 0x30\r\e tiene 14 caracteres, entonces se demora 14 × 87 µs = 1200 µs en salir. 
En ese tiempo pueden llegar 14 bytes nuevos, Los diez caracteres pegados llegan en un tiempo menor a 1 ms entonce la UART Solo guarda uno
entonces el resto se pierde y por eso en la prueba solo se vió el 0 y el 1 

6. huart2 es static para que solo se pueda usar dentro de este archivo, si se compilan dos app.c a la vez no chocan los static, pero si app_init y app_loop,
el APP_H no ayuda aquí porque solo evita inclui el mismo .h dos veces dentro de un mismo archivo 


ETAPA 2: PENDIENTE
Faltan responder las 8 preguntas de repaso de la etapa 2. El Break it de esa etapa ya se hizo.

ETAPA 3: PENDIENTE
Faltan responder las 8 preguntas de repaso de la etapa 3. Los checkpoints y el Break it de esa etapa ya se hicieron.

ETAPA 4: PENDIENTE
Faltan responder las preguntas de repaso de la etapa 4. Los checkpoints y el Break it de esa etapa ya se hicieron.

ETAPA 5: PENDIENTE
Faltan responder las 7 preguntas de repaso de la etapa 5. Los checkpoints, el Break it y el ejercicio opcional de esa etapa ya se hicieron.
 */
 #include "stm32f4xx_hal.h"
#include "system.h"
#include "heartbeat.h"
#include "app.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    heartbeat_init(250U);   /* toggle every 250 ms = 2 Hz blink */
    app_init();             /* provided by the selected stage */

    while (1) {
        app_loop();         /* provided by the selected stage */
    }
}

