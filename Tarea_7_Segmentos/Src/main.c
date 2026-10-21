/**
 ******************************************************************************
 * @file           : Tarea 1
 * @author         : Elena Arango Durango
 * @brief          : Main program body
 ******************************************************************************
 */

#include <stdint.h>
#include <stdlib.h>
#include "stm32f411xe.h"
#include "stm32f4xx.h"

/*
DESCRIPCIÓN DEL HARDWARE

Lo primero que se hizo fue la identificación y correcta distribución de los pines del 7 segmentos. 
Los pines asociados a los dígitos corresponden al 6, 8, 9 y 12, y los asociados a los segmentos 
se distribuyen de la siguiente manera: el segmento a está asociado al pin 11, el segmento b al pin 7, 
el segmento c al pin 4, el d al pin 2, el e al pin 1, el f se enciende con el 10, el g se asocia con el pin 5 
y el pin 3 queda sin conectar porque corresponde al punto decimal.

Teniendo esto en cuenta, se asocia cada pin del 7 segmentos a un pin en el microcontrolador:

PINES DÍGITOS
Pin 12 (D1) al PB10
Pin 9  (D2) al PC3
Pin 8  (D3) al PC2
Pin 6  (D4) al PB5

PINES SEGMENTOS
Pin 11 (a) al PA1
Pin 7  (b) al PB0
Pin 4  (c) al PB15
Pin 2  (d) al PB1
Pin 1  (e) al PB2
Pin 10 (f) al PA4
Pin 5  (g) al PB14

Tras realizar esta asignación, la base de cada transistor PNP (Al ser PNP se activan cuando llega una señal de 0 lógico del microntrolador) se conecta a través de una resistencia 
hacia su pin del microcontrolador, el emisor se conecta a la alimentación de 3.3 V y el colector va hacia 
el pin del ánodo común del dígito en el display. Posteriormente, cada pin correspondiente a los segmentos 
se conecta a través de una resistencia limitadora hacia su pin asociado del microcontrolador.

Para la conexión de las fotocompuertas, se configuró la fotocompuerta 1 (PC12) para contar en flanco de subida 
y sumar en el contador, y la fotocompuerta 2 (PC10) para contar en flanco de bajada y restar en el contador. 
Ambas generan interrupciones en el microcontrolador para modificar el valor visible en el display de 7 segmentos.

Rastreo de pines foto compuertas
Foto compuerta 1 (Rising edge) al PC12
Foto compuerta 2 (Falling edge) al PC10

DESCRIPCIÓN DEL SOFTWARE 

El código se construye dividiendo las tareas en funciones específicas.
Primero se realiza la activación de los relojes en el RCC y la configuración de los pines GPIO 
(modo entrada/salida, salida push-pull, velocidad baja y sin resistencias pull-up/pull-down internas).

Posteriormente, el programa se divide en 4 partes concretas:

Led Blinky: 
   Se encarga del parpadeo del LED en PB7 cada 500 ms, usando una interrupción periódica 
   con el Timer 11, en este punto debido a que los pines PH1 Y PH0 se encontraban malos, fue necesario utilizar el PB7
   conectado a un LED externo.

Refresh del display: 
   Usamos el Timer 2 para generar una interrupción cada 4 ms; esto pone en 1 la bandera refresco_display 
   para que el main sepa cuándo cambiar de dígito.

Visualización en el display: 
   Se divide en dos secciones. Por un lado, la máquina de estados (refrescar_digitos) apaga los transistores, 
   selecciona cuál dígito sigue y lleva la base del transistor a 0 V para activarlo. Por el otro, graficar_numero 
   pone en bajo (0 V) los segmentos específicos para dibujar el número. En el bucle principal se descomponen las 
   cifras del contador (millares, centenas, decenas y unidades).

Interrupciones externas (EXTI) para las Fotocompuertas: 
   Configuradas en las líneas 10 y 12 para leer las fotocompuertas. La fotocompuerta 1 en PC12 suma en flanco de 
   subida y la fotocompuerta 2 en PC10 resta en flanco de bajada, con desbordamiento natural a 65535 si se resta 
   cuando el contador está en 0.
*/

// Definición de variables
// Estas son volátiles para indicar que su valor puede modificarse en cualquier instante 
// dentro de las rutinas de interrupción (ISR)
 
volatile uint8_t refresco_display = 0; 
// Esta es la bandera de sincronización, Cuando ocurre la interruppción del TIM 2, este la pone en 1 cada 4 ms para 
// avisarle al main que cambie de dígito

volatile uint16_t cnt_refresco = 0; 
// Es la variable que se incrementa o decrementa según las interrupciones externas de las fotocompuertas

// Cabecera de funciones
void init_rcc(void); // Se habilitan las señales de reloj del los puertos A, B, C y de los timer 2 y 11
void init_gpio_display(void); // Configura los pines de segmentos (PA, PB) y dígitos (PB, PC) como salidas
void init_timer_display(void); // COnfigura TIM2 para que genere el cambio de dígito en el display cada 4 ms
void init_gpio_fotocompuertas(void); // Configura PC10 y PC12 como entradas digitales
void init_timer_blinky(void); // Configura TIM11 y el pin PB7 para el parpadeo del LED cada 500 ms
void init_EXTI_Fotocom(void); // Configura interrupciones externas (EXTI10 y EXTI12) asociadas a PC10 y PC12
void apagar_DS(void); // Apaga los 4 transistores asociados a los dígitos 
void graficar_numero(uint8_t num); //Dibuja los numeros (0 al 9) encendiendo los segmentos correspondientes
void refrescar_digitos(uint8_t valores[4]); // Alterna el encendido de los 4 dígitos, uno a la vez en cada llamada

// Definicion Maquinas de estado
// Se enumeran los 4 estados posibles para mostrar en el display

typedef enum
{
    Primer_DIG = 0, //Correpsonde a los millares (PB10)
    Segundo_DIG, // Corresponde a las centenas (PC3)
    Tercer_DIG, //Corresponde a las decenas (PC2)
    Cuarto_DIG //Corresponde a las unidades (PB5)
} DigitoActivo_t;

//Esta es la variable que guarda la información acerca de que dígito debe encenderse, iniciando en el primero
DigitoActivo_t activar_digito = Primer_DIG;

int main(void)
{   // Se hace el llamado a cada función
    init_rcc();
    init_gpio_display();
    init_timer_blinky();
    init_timer_display();
    init_gpio_fotocompuertas();
    init_EXTI_Fotocom();

     // Se almacena el valor que quiero mostrar en la pantalla, separado en dígitos
    uint8_t digitos[4] = {0, 0, 0, 0};


    while (1)
    {
        // Descomponer el número actual en los 4 dígitos.
        // Por ejemplo con 1723: al dividir entre 100 me da 17, y con el módulo 10 (% 10) 
        // me quedo con el residuo que es 7 (la centena). Así saco cada dígito individual.
            digitos[0] = (cnt_refresco / 1000) % 10; 
            digitos[1] = (cnt_refresco / 100)  % 10; 
            digitos[2] = (cnt_refresco / 10)   % 10; 
            digitos[3] = (cnt_refresco)        % 10; 

        // Atender el refresco cuando la interrupción active la bandera
        if (refresco_display)
        {
            refresco_display = 0; // Limplia la bandera para esperar los siguientes 4 ms
            refrescar_digitos(digitos); // Pasa al siguiente dígito del display
        }
    }
}


// CONFIGURACION INICIAL (RCC Y PINES GPIO)

// Habilitar las señales del reloj de cada puerto con RCC AHB1 peripheral clock enable register
void init_rcc(void)
{
    RCC->AHB1ENR &= ~(RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | 
    RCC_AHB1ENR_GPIOCEN); // Limpio todos los bits correspondientes a la activación del reloj
    RCC->AHB1ENR |=  (RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | 
    RCC_AHB1ENR_GPIOCEN); // Activo el reloj para los puertos A, B y C

    // Habilitar periféricos en APB1 (TIM2) y APB2 (TIM11)
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;         // Habilita el reloj de TIM2 (pone bit TIM2EN en 1)
    RCC->APB2ENR &= ~(RCC_APB2ENR_TIM11EN);     // Limpia el bit de TIM11 
    RCC->APB2ENR |=  (RCC_APB2ENR_TIM11EN);     // Habilita el reloj de TIM11 en APB2 (pone el bit en 1)
   
}

//Configuración de pines para segmentos (PA, PB) y dígitos (PB, PC)
void init_gpio_display(void)
{
    // PUERTO A: PA1 (Seg a), PA4 (Seg f)
    GPIOA->MODER &= ~(GPIO_MODER_MODER1 | GPIO_MODER_MODER4);               // Limpia los pines del puerto A
    GPIOA->MODER |=  (GPIO_MODER_MODER1_0 | GPIO_MODER_MODER4_0);           //Configura los pines del puerto A como salida general
    GPIOA->OTYPER &= ~(GPIO_OTYPER_OT_1 | GPIO_OTYPER_OT_4);                // Configura el tipo de salida como push pull (poniendo un cero)
    GPIOA->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR1 | GPIO_OSPEEDER_OSPEEDR4);   // COnfigura la velocidad en  low speed poniendo 00
    GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPDR1 | GPIO_PUPDR_PUPDR4);               // Deshabilita las resistencias internas de pull up y pull down 

    // PUERTO B: Segs PB0, PB1, PB2, PB14, PB15. Dígitos PB5, PB10
    GPIOB->MODER &= ~(GPIO_MODER_MODER0  | GPIO_MODER_MODER1  | GPIO_MODER_MODER2  |
                    GPIO_MODER_MODER5  | GPIO_MODER_MODER10 | GPIO_MODER_MODER14 | GPIO_MODER_MODER15);
        // se limpian todos los pines del puerto B

    GPIOB->MODER |=  (GPIO_MODER_MODER0_0  | GPIO_MODER_MODER1_0  | GPIO_MODER_MODER2_0  |
         GPIO_MODER_MODER5_0  | GPIO_MODER_MODER10_0 | GPIO_MODER_MODER14_0 | GPIO_MODER_MODER15_0);
         // Se configuran como salida general poniendo 01

    GPIOB->OTYPER &= ~(GPIO_OTYPER_OT_0  | GPIO_OTYPER_OT_1  | GPIO_OTYPER_OT_2  |
        GPIO_OTYPER_OT_5  | GPIO_OTYPER_OT_10 | GPIO_OTYPER_OT_14 | GPIO_OTYPER_OT_15);
        // Se configura como salida push pull poniendo un 0

    GPIOB->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR0  | GPIO_OSPEEDER_OSPEEDR1  | GPIO_OSPEEDER_OSPEEDR2  |
        GPIO_OSPEEDER_OSPEEDR5  | GPIO_OSPEEDER_OSPEEDR10 | GPIO_OSPEEDER_OSPEEDR14 |GPIO_OSPEEDER_OSPEEDR15);
        // Se configura en low speed poniendo 00

    GPIOB->PUPDR &= ~(GPIO_PUPDR_PUPDR0  | GPIO_PUPDR_PUPDR1  | GPIO_PUPDR_PUPDR2  |
         GPIO_PUPDR_PUPDR5  | GPIO_PUPDR_PUPDR10 | GPIO_PUPDR_PUPDR14 | GPIO_PUPDR_PUPDR15);
        // Deshabilita resistencias internas de pull-up y pull-down

    // PUERTO C: Dígitos PC2 (D3) y PC3 (D2)
    GPIOC->MODER &= ~(GPIO_MODER_MODER2 | GPIO_MODER_MODER3);             // Limpia los pines
    GPIOC->MODER |=  (GPIO_MODER_MODER2_0 | GPIO_MODER_MODER3_0);         // Salida general (01)
    GPIOC->OTYPER &= ~(GPIO_OTYPER_OT_2 | GPIO_OTYPER_OT_3);              // Salida push-pull (0)
    GPIOC->OSPEEDR &= ~(GPIO_OSPEEDER_OSPEEDR2 | GPIO_OSPEEDER_OSPEEDR3); // Velocidad baja (00)
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPDR2 | GPIO_PUPDR_PUPDR3);             // Sin resistencias internas de pull
}

// Entradas para las Fotocompuertas: PC10 y PC12
void init_gpio_fotocompuertas(void)
{
    // Configura PC10 y PC12 en Modo Entrada (00)
    GPIOC->MODER &= ~(GPIO_MODER_MODER10 | GPIO_MODER_MODER12);
    // Sin pull interno (00) ya que cuentan con resistencia externa de pull-up
    GPIOC->PUPDR &= ~(GPIO_PUPDR_PUPDR10 | GPIO_PUPDR_PUPDR12);
}



// CONTROL DEL LED BLINKY


// Pin PB7 como salida para LED externo (reemplazo por pines dañados en la placa)
void init_timer_blinky(void)
{
    // Configuración del pin PB7 como salida push-pull
    GPIOB->MODER &= ~GPIO_MODER_MODER7;   // Limpia los bits del pin 7
    GPIOB->MODER |=  GPIO_MODER_MODER7_0; // Escribe 1 para configurarlo como salida general
    GPIOB->OTYPER &= ~GPIO_OTYPER_OT_7;   // Salida tipo push-pull (pone 0)
    GPIOB->OSPEEDR &= ~GPIO_OSPEEDER_OSPEEDR7; // Velocidad baja (00)
    GPIOB->PUPDR &= ~GPIO_PUPDR_PUPDR7;   // Sin pull-up ni pull-down interno (el LED ya tiene su resistencia externa)

    // Base de tiempo con reloj interno a 16 MHz:
    // El prescaler divide los 16 MHz entre 16000 para que el contador avance cada 1 ms (1 kHz)
    TIM11->PSC = 16000 - 1; 

    // Auto-reload: cuenta hasta 500 para generar el desborde cada 500 ms (0.5 s)
    TIM11->ARR = 500 - 1; 
    TIM11->CNT = 0; // Asegura iniciar la cuenta en cero

    // NOTA: A PSC y ARR se les resta 1 porque el contador arranca desde 0

    // Habilitación de interrupción y encendido del temporizador:
    TIM11->DIER |= TIM_DIER_UIE;            // Habilita la interrupción por desborde (UIE)
    NVIC_EnableIRQ(TIM1_TRG_COM_TIM11_IRQn); // Habilita la línea de interrupción en el controlador NVIC
    TIM11->CR1 |= TIM_CR1_CEN;              // Enciende el contador (CEN = 1)
}

// Rutina que se ejecuta durante la interrupción de TIM11
void TIM1_TRG_COM_TIM11_IRQHandler(void)
{
    // Comprueba si la bandera de actualización (UIF) está en 1 por desborde
    if (TIM11->SR & TIM_SR_UIF)
    {
        TIM11->SR &= ~TIM_SR_UIF;     // Limpia la bandera escribiendo 0 (bit tipo rc_w0)
        GPIOB->ODR ^= GPIO_ODR_ODR_7; // Conmuta el estado de PB7 con XOR para hacer parpadear el LED
    }
}


// REFRESCO DE LA PANTALLA

void init_timer_display(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN; // Habilitar reloj de TIM2 en el bus APB1

    TIM2->PSC = 16000 - 1;   // Frecuencia del contador = 1 kHz (1 ms por conteo)
    TIM2->ARR = 4 - 1;     // Genera un desborde cada 4 ms por dígito 
    TIM2->CNT = 0;

    TIM2->SR &= ~TIM_SR_UIF; // Aquí se limpia la bandera por desborde previo
    TIM2->DIER |= TIM_DIER_UIE; // se habilita la interrupción

    NVIC_EnableIRQ(TIM2_IRQn); 
  
    TIM2->CR1 |= TIM_CR1_CEN;   // Encender el temporizador
}

void TIM2_IRQHandler(void)
{
    // Verifica si la interrupción proviene de un evento de desborde/actualización (UIF = 1) [RM0383 Sec. 13.4.5]
    if (TIM2->SR & TIM_SR_UIF)
    {
        // Limpia la bandera de interrupción en TIM2 escribiendo 0 para no reingresar indefinidamente
        TIM2->SR &= ~TIM_SR_UIF;

        // Avisa al bucle principal (main) que transcurrieron 4 ms para cambiar de dígito
        refresco_display = 1;
    }
}

//FUNCIONAMIENTO Y CONTROL DEL DISPLAY 

// Apaga los 4 transistores de los dígitos (corta la corriente en los ánodos comunes)
void apagar_DS(void)
{
    GPIOB->BSRR = (GPIO_BSRR_BS5 | GPIO_BSRR_BS10); // Desactiva los transistores de las unidades y los millares
    GPIOC->BSRR = (GPIO_BSRR_BS2 | GPIO_BSRR_BS3);  // Desactiva los transistores de las decenas y las centenas
}

// Apaga y enciende los segmentos correspondientes 
// Para que los segmentos enciendan, el cátodo debe conectarse a un nivel bajo ( 0V)
// Para permitir que la corriente circule.

/* Dibuja los números del 0 al 9 activando los segmentos correspondientes.
Usamos el registro BSRR en lugar de ODR porque BSRR modifica los pines de forma atómica
(en una sola instrucción).
Escribir en BS pone el pin en 1 (3.3 V) y apaga el segmento.
Escribir en BR pone el pin en 0 (0 V) y enciende el segmento.*/
void graficar_numero(uint8_t num) 
{
    // Apaga primero todos los segmentos poniendo sus pines en 1 (3.3 V) con BS
    GPIOA->BSRR = (GPIO_BSRR_BS1 | GPIO_BSRR_BS4); // Apaga segmento a (PA1) y f (PA4)
    GPIOB->BSRR = (GPIO_BSRR_BS0 | GPIO_BSRR_BS1 | GPIO_BSRR_BS2 | GPIO_BSRR_BS14 | GPIO_BSRR_BS15); 
    // Apaga segmentos b (PB0), d (PB1), e (PB2), g (PB14) y c (PB15)

    // Enciende solo los segmentos necesarios para cada número poniendo sus pines en 0 (0 V) con BR
    switch (num)
    {
        case 0: // Enciende segmentos a, b, c, d, e, f
            GPIOA->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR4); // Enciende a (PA1) y f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR1 | GPIO_BSRR_BR2 | GPIO_BSRR_BR15); // Enciende b (PB0), d (PB1), e (PB2) y c (PB15)
            break;

        case 1: // Enciende segmentos b y c
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR15); // Enciende b (PB0) y c (PB15)
            break;

        case 2: // Enciende segmentos a, b, d, e, g
            GPIOA->BSRR = GPIO_BSRR_BR1; // Enciende a (PA1)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR1 | GPIO_BSRR_BR2 | GPIO_BSRR_BR14); // Enciende b (PB0), d (PB1), e (PB2) y g (PB14)
            break;

        case 3: // Enciende segmentos a, b, c, d, g
            GPIOA->BSRR = GPIO_BSRR_BR1; // Enciende a (PA1)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR1 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende b (PB0), d (PB1), g (PB14) y c (PB15)
            break;

        case 4: // Enciende segmentos b, c, f, g
            GPIOA->BSRR = GPIO_BSRR_BR4; // Enciende f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende b (PB0), g (PB14) y c (PB15)
            break;

        case 5: // Enciende segmentos a, c, d, f, g
            GPIOA->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR4); // Enciende a (PA1) y f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende d (PB1), g (PB14) y c (PB15)
            break;

        case 6: // Enciende segmentos a, c, d, e, f, g
            GPIOA->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR4); // Enciende a (PA1) y f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR2 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende d (PB1), e (PB2), g (PB14) y c (PB15)
            break;

        case 7: // Enciende segmentos a, b, c
            GPIOA->BSRR = GPIO_BSRR_BR1; // Enciende a (PA1)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR15); // Enciende b (PB0) y c (PB15)
            break;

        case 8: // Enciende todos los segmentos (a, b, c, d, e, f, g)
            GPIOA->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR4); // Enciende a (PA1) y f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR1 | GPIO_BSRR_BR2 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende b, d, e, g y c
            break;

        case 9: // Enciende segmentos a, b, c, d, f, g
            GPIOA->BSRR = (GPIO_BSRR_BR1 | GPIO_BSRR_BR4); // Enciende a (PA1) y f (PA4)
            GPIOB->BSRR = (GPIO_BSRR_BR0 | GPIO_BSRR_BR1 | GPIO_BSRR_BR14 | GPIO_BSRR_BR15); // Enciende b (PB0), d (PB1), g (PB14) y c (PB15)
            break;

        default: // Caso de seguridad por si llega un valor fuera de rango
            break;
    }
}

     
//En ésta función nos concentramos en seleccionar el dígito que va a ser encendido y confiurar que hay que activar para que se encienda
// Se encarga de seleccionar qué dígito encender, dibujar su número y pasar el turno al siguiente
void refrescar_digitos(uint8_t valores[4])
{
    apagar_DS(); // Apaga los transistores antes de cambiar de dígito 

    // Máquina de estados: activa el transistor del dígito correspondiente y cambia el turno
    switch (activar_digito)
    {
        case Primer_DIG:
            graficar_numero(valores[0]);  // Dibuja los millares
            GPIOB->BSRR = GPIO_BSRR_BR10; // Enciende el primer dígito llevando PB10 a 0 V
            activar_digito = Segundo_DIG; // Pasa el turno al segundo dígito
            break;

        case Segundo_DIG:
            graficar_numero(valores[1]);  // Dibuja las centenas
            GPIOC->BSRR = GPIO_BSRR_BR3;  // Enciende el segundo dígito llevando PC3 a 0 V
            activar_digito = Tercer_DIG;  // Pasa el turno al tercer dígito
            break;

        case Tercer_DIG:
            graficar_numero(valores[2]);  // Dibuja las decenas
            GPIOC->BSRR = GPIO_BSRR_BR2;  // Enciende el tercer dígito llevando PC2 a 0 V
            activar_digito = Cuarto_DIG;  // Pasa el turno al cuarto dígito
            break;

        case Cuarto_DIG:
            graficar_numero(valores[3]);  // Dibuja las unidades
            GPIOB->BSRR = GPIO_BSRR_BR5;  // Enciende el cuarto dígito llevando PB5 a 0 V
            activar_digito = Primer_DIG;  // Vuelve a empezar el ciclo en el primer dígito
            break;

        default:
            activar_digito = Primer_DIG;  //Por seguridad, si pasa algo raro regresa al primer dígito
            break;
    }
}

 

// INTERRUPCIONES EXTERNAS DE LAS FOTOCOMPUERTAS


void init_EXTI_Fotocom(void)
{
    // Habilitar la señal de reloj del SYSCFG
    RCC->APB2ENR &= ~(RCC_APB2ENR_SYSCFGEN); // Limpiamos en APB2
    RCC->APB2ENR |=  (RCC_APB2ENR_SYSCFGEN); // Activamos la señal de reloj

    // Seleccionar qué puertos corresponden para cada EXTI
    // EXTI10 está en EXTICR[2]
    SYSCFG->EXTICR[2] &= ~SYSCFG_EXTICR3_EXTI10;
    SYSCFG->EXTICR[2] |=  SYSCFG_EXTICR3_EXTI10_PC; // Dejar pasar PC10

    // EXTI12 está en EXTICR[3]
    SYSCFG->EXTICR[3] &= ~SYSCFG_EXTICR4_EXTI12;
    SYSCFG->EXTICR[3] |=  SYSCFG_EXTICR4_EXTI12_PC; // Dejamos pasar PC12

    // Selección de flancos:
    // PC12 (Fotocompuerta 1 - Suma): Rising edge para contar cuando la tarjeta entra (se obstruye el haz)
    EXTI->RTSR |=  EXTI_RTSR_TR12; // Flanco de subida 
    EXTI->FTSR &= ~EXTI_FTSR_TR12; // Sin flanco de bajada

    // PC10 (Fotocompuerta 2 - Resta): Falling edge para contar cuando la tarjeta sale (se libera el haz)
    EXTI->RTSR &= ~EXTI_RTSR_TR10; // Sin flanco de subida
    EXTI->FTSR |=  EXTI_FTSR_TR10; // Flanco de bajada

    // Se limpian las banderas pendientes escribiendo un 1 en PR (es de tipo rc_w1)
    EXTI->PR  |= (EXTI_PR_PR10 | EXTI_PR_PR12); 
    EXTI->IMR |= (EXTI_IMR_MR10 | EXTI_IMR_MR12); // Se desenmascaran las interrupciones

    // Se habilita la interrupción compartida en el NVIC
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void EXTI15_10_IRQHandler(void)
{
    // PC12 (Fotocompuerta 1: Flanco de subida, Suma)
    if (EXTI->PR & EXTI_PR_PR12)
    {
        EXTI->PR |= EXTI_PR_PR12; // Bajar bandera escribiendo 1
        cnt_refresco++;           // Incremento natural de 16 bits 
    }

    // PC10 (Fotocompuerta 2: Flanco de bajada, Resta)
    if (EXTI->PR & EXTI_PR_PR10)
    {
        EXTI->PR |= EXTI_PR_PR10; // Bajar bandera escribiendo 1
        cnt_refresco--;           // Desbordamiento natural: 0 - 1 = 65535
    }
}