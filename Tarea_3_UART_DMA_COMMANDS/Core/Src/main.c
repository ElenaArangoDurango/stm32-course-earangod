/* Core/Src/main.c, es el mismo para todas las etapas y no se vuelve a cambiar
 * Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
 */

 /* Es necesario que el LD2 esté parapadeando todo el tiempo, si deja de hacerlo está indicandonos un problema 
 de estancamiento de la CPU, si parpadea pero la placa no responde, el problema está en la UART
 Otro punto importante de entender es que cada driver HAL necesita dos piezas, una es el 
 #define y la otra es el archivo .c en el CMake,
 además el main.c no cambia, el CMake elige que app.c compilar según lo que encuentre en la variable APP  */
/* Registro de checkpoints, lo que fui probando en la placa en cada paso:

 0.A Driver de UART: compiló sin errores y LD2 siguió parpadeando.

 0.B Selector de aplicación: en el panel de CMake sale ">>> Building APP = ..." con la app que elegí.

 0.C Heartbeat en common: LD2 parpadea a 2 Hz y cuando probé heartbeat_set_period(1000U) bajó a 0,5 Hz.

 0.D Terminal: picocom se conecta a /dev/ttyACM0 a 115200 8N1 sin errores.
 Mi Enter manda \r (0x0D), lo confirmé en el 1.C porque salió rx: '.' 0x0D.

 1.A Agregué la etapa polling al build y compiló sin errores.

 1.B Al presionar RESET sale el mensaje de bienvenida en picocom y LD2 sigue parpadeando.

 1.C Sale un tick cada segundo y cada tecla que escribo responde con rx: y su valor.
 Break it 1 (RX_TIMEOUT_MS = HAL_MAX_DELAY): los tick dejaron de salir pero LD2 siguió parpadeando.
 Cuando escribí 'a' salió rx: 'a' 0x61 y después tick 1 at 100136 ms. Lo volví a dejar en 10U.
 Break it 2 (pegar 0123456789): solo volvieron rx: '0' 0x30 y rx: '1' 0x31, los otros 8 se perdieron
 por overrun y después siguió el tick 36 at 36080 ms. 

 2.A Con APP = interrupt compiló y la placa hizo lo mismo que en la etapa 1 (banner, tick y rx).

 2.B Recepción por interrupción: sale "Stage 2: interrupt reception." y ya no salen los tick.
 Me faltó anotar qué pasó al pegar 0123456789, deberían salir las 10 líneas rx: en orden.

 2.C Cada tecla se ve mientras escribo, con Enter sale line: '...' y otra vez el prompt.
 Un Enter solo muestra el prompt y si escribo más de 63 caracteres sale error: line too long.

 2.D help, info, blink, echo y los errores responden como dice la guía, y con blink cambia LD2.

 Break it 1 de la etapa 2 (apagar las interrupciones cada 5 segundos con una tecla presionada):
 cada 5 segundos salía [uart error 0x08, reception restarted], 0x08 es el overrun. Faltaron algunos
 caracteres pero pude seguir escribiendo porque el callback vuelve a arrancar la recepción, y LD2 se
 trababa un poquito en cada pausa. Dejé ese bloque comentado en app_2_interrupt/app.c.
 Break it 2 de la etapa 2 (quitar uart2_start_rx del callback de error): después del primer error ya no
 pude escribir más comandos, la UART quedó sorda pero LD2 siguió parpadeando. Volví a poner la línea.

 3.A Con APP = dma_idle compiló, CMake mostró >>> Building APP = dma_idle
 y la placa hizo lo mismo que en la etapa 2 (banner, help, blink, echo).

 3.B Sale "Stage 3: DMA reception, ended by IDLE.". Escribiendo despacio cada tecla llega como su propio
 bloque [IDLE  1 bytes] y hello world pegado llega en un solo bloque [IDLE 11 bytes], con el Enter como '.'.

 3.C El texto de 40 caracteres llegó partido en dos bloques, porque el buffer de 32 se llenó antes de que
 la línea quedara quieta. Con la terminal a 9600 salieron caracteres raros (la placa sigue a 115200, así que
 los tiempos no cuadran y tampoco se lee bien lo que ella manda). Al volver a 115200 funcionó normal
 porque HAL_UART_ErrorCallback reinicia la recepción.

 Break it 1 de la etapa 3 (HAL_Delay de 2 segundos en app_loop): pegué hello y presioné x antes de los
 2 segundos, salió [IDLE  5 bytes] 'xello' porque el DMA ya estaba llenando el mismo buffer desde el
 principio y la x quedó encima de la h. El HAL_Delay quedó comentado en app_3_dma_idle/app.c.
 Break it 2 de la etapa 3 (comentar el __HAL_DMA_DISABLE_IT del HT): con las 26 letras salió [HT 16 bytes]
 y luego [IDLE 26 bytes], o sea la primera mitad salió dos veces. Volví a dejar la línea.
 Break it 3 de la etapa 3 (DMA1_Stream5_IRQHandle sin la r): compiló sin errores y escribir funcionaba,
 pero al pegar 40 caracteres la placa se colgó y LD2 se quedó quieto (la CPU quedó en Default_Handler).
 Corregí el nombre.

 4.A Con APP = pingpong compiló, CMake mostró >>> Building APP = pingpong
 y la placa hizo lo mismo que en la etapa 3.

 4.B Las teclas una por una se van turnando entre los bloques: [A  1 bytes] 'a', [B  1 bytes] 's',
 [A  1 bytes] 'd'. hello world pegado llega en un solo bloque y el texto de 40 caracteres llega como
 [A 32 bytes] y [B  8 bytes], el primero completo y bien.
 Lo probé ya con el código final, que muestra el banner con el prompt. En el código final esta salida
 de bloques ya no aparece porque desde el 4.D los bloques no se imprimen.

 4.C Transmisión por DMA: la salida se ve igual que en el 4.B (banner, bloques A y B y el texto de 40).
 La diferencia no se ve pero está, app_loop ya no se queda unos 3 ms esperando por cada línea, solo copia
 el texto y sigue.

 4.D Todos los comandos de la tabla del 2.D responden igual que en la etapa 2. Pegué de una vez
 echo 0123456789 abcdefghijklmnopqrstuvwxyz y respondió 0123456789 abcdefghijklmnopqrstuvwxyz, aunque
 llegó en dos bloques de 32 y 10 bytes, porque line_add_byte volvió a armar la línea.

 Break it 1 de la etapa 4 (HAL_Delay de 2 segundos al procesar un bloque): pegué hello y presioné x y y antes
 de los 2 segundos. Salió hellox con [reception paused: both blocks busy, data lost] y después apareció la y.
 No salió xello, no se dañó nada y la pausa se avisó. La guía decía que la y se iba a perder, pero no se perdió.
 El HAL_Delay quedó comentado en app_4_pingpong/app.c.
 Lo repetí con tres teclas (x, y y z) y funcionó bien, pero no me fijé si la z se perdió, así que todavía no
 he comprobado lo de que el registro de la UART guarda un byte durante la pausa.

 Break it 2 de la etapa 4 (HAL_UART_TxCompleteCallback en vez de HAL_UART_TxCpltCallback): compiló sin errores.
 Pegando blink desde la terminal LD2 sí cambió de ritmo, o sea que la placa recibía, analizaba y ejecutaba los
 comandos, pero no salía texto porque el HAL usaba su callback vacío y tx_busy nunca volvía a false.
 Corregí el nombre.

 Etapa 5: el LED externo lo tengo en PB7 y no en PA6 (D12) como dice la guía. Por eso pwm.c usa TIM4 canal 2
 con AF2 y el reloj del puerto B, en vez de TIM3 canal 1 con el puerto A. Solo cambié pwm.c y pwm.h, el resto
 del comando quedó igual. PB7 también puede ser I2C1 SDA o USART1 RX, pero no estoy usando ninguno de los dos.

 5.A help ya muestra pwm <0..100>, pwm 150 da error de rango, pwm abc dice que no es un número y pwm solo
 responde usage: pwm <0..100>. No anoté el texto exacto de cada respuesta, pero salieron como esperaba.

 5.B Agregué pwm.c y pwm.h al build y pwm_init en app_init, compiló sin errores.
 Al arrancar el LED en PB7 queda apagado (0 %).

 5.C Con pwm cambia la intensidad del LED en PB7. No anoté qué se veía con cada valor (100, 50, 10 y 0).

 Break it 1 de la etapa 5 (APP = interrupt, que usa el mismo commands.c pero nunca llama a pwm_init): salió
 el banner de la etapa 2, help sigue mostrando pwm <0..100> y pwm 50 responde error: pwm is not started in
 this application, porque pwm_set_duty revisa pwm_started y no escribe en un timer sin configurar.
 Volví a APP = pingpong.

 Break it 2 de la etapa 5 (comentar { "pwm", CMD_PWM } en la tabla de cmd_parser.c): compiló sin errores ni
 advertencias, help sigue mostrando pwm <0..100> pero pwm 50 responde error: unknown command 'pwm' (type help).
 La tabla y el case de commands.c están en archivos distintos y nada obliga a que coincidan. Volví a poner la fila.

 Ejercicio opcional de la etapa 5: pwm sin argumento responde el ciclo de trabajo actual, por ejemplo
 pwm is 30 %, usando pwm_get_duty(), y con dos o más argumentos sigue respondiendo usage. Solo cambié commands.c.
 Lo probé en la placa con pwm 30, pwm y pwm 10 20 y las tres salieron bien.
 */

 /* PREGUNTAS DE REPASO
 ETAPA 1: 
1. Primero se activa el reloj de USART2 porque un periférico sin reloj ignora 
lo que se escribe en sus registros. Si pusiera HAL_UART_Init() antes, el HAL escribiría la configuración y no pasaría nada,
 y la UART no funcionaría sin mostrar ningún error.

2. HAL_UART_Init() llama a HAL_UART_MspInit(), que es una función del HAL pensada para activar relojes y pines, 
pero por defecto está vacía. Como ya se activaron los relojes y se configuraron los pines en uart2_init() 
antes de llamar a HAL_UART_Init(), no hace falta llenar HAL_UART_MspInit() y la UART funciona igual.

3. Si no llega ninguna tecla en RX_TIMEOUT_MS, HAL_UART_Receive devuelve HAL_TIMEOUT.
En app_loop se pregunta si el resultado es HAL_OK, y como no lo es, se salta el bloque, 
no imprime nada y la función termina para volver a empezar. 

4. Con HAL_MAX_DELAY, el app_loop se queda esperando una tecla para siempre dentro de HAL_UART_Receive, y nunca llega al bloque de los tick,
por eso dejan de salir. LD2 lo maneja la interrupción de TIM2, entonces sigue parpadeando.

5. Cada byte son 10 bits: 1 de inicio, 8 de datos y 1 de parada. A 115200 baudios un bit dura 8,68 µs,
entonces un byte tarda 10 ÷ 115200 = 86,8 µs.
La línea rx: '0' 0x30\r\n tiene 14 caracteres, entonces se demora 14 × 86,8 µs ≈ 1,2 ms en salir.
En ese tiempo pueden llegar 14 bytes nuevos, pero la UART solo guarda uno en su registro, entonces
los demás se pierden por overrun y por eso en la prueba solo se vio el 0 y el 1.

6. huart2 es static para que solo se pueda usar dentro de este archivo. Si se compilaran dos app.c a la vez,
los static no chocan porque cada uno es privado de su archivo, pero app_init y app_loop sí chocan porque son
públicos y quedarían definidos dos veces (multiple definition). El APP_H no ayuda aquí porque solo evita
incluir el mismo .h dos veces dentro de un mismo archivo.

 */
 #include "stm32f4xx_hal.h"
#include "system.h"
#include "heartbeat.h"
#include "app.h"

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    heartbeat_init(250U);   // cambia cada 250 ms, o sea parpadea a 2 Hz
    app_init();             // la pone el app.c de la etapa que se eligió en APP

    while (1) {
        app_loop();         // igual, viene del app.c elegido
    }
}

