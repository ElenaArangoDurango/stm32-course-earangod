/* Core/common/heartbeat.c
 * Los códigos fueron proporcionados por el profesor Nerio Montoya como material de clase, pero los comentarios fueron hechos por earangod@unal.edu.co
 */
#include "stm32f4xx_hal.h" 
#include "heartbeat.h"
#include "system.h"

#define HB_TIM       TIM2 
#define HB_TICK_HZ   10000U        // Es la velocidad de conteo del timer, 10 kHz = 0.1 ms por tick
#define HB_LED_PORT  GPIOC // Puerto del LED externo (antes GPIOA para LD2)
#define HB_LED_PIN   GPIO_PIN_0    // PC0, LED externo con resistencia (antes PA5 = LD2)

static TIM_HandleTypeDef htim_hb;  // Configura el TIM2 como static, solo este archivo lo toca
static volatile uint32_t hb_period_ms; //Es el periodo en ms, se lee de memoria

// El reloj es PCLK1, pero si hay un prescaler diferente a 1 el reloj del timer es el doble del PCLK1

static uint32_t hb_timer_clock_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq(); // Frecuencia del bus APB1 

    if ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) {
        // Pregunta si el prescaler es 1
        return pclk1; // Si es 1, el timer recibe la frecuencia del bus
    }
    return 2U * pclk1; // Si no es q el hardware duplica la frecuencia
}

void heartbeat_init(uint32_t toggle_ms)
{
    GPIO_InitTypeDef gpio = {0}; 
    // Son los parámetros del pin

    // Los relojes se configuran primero 
    __HAL_RCC_GPIOC_CLK_ENABLE(); // reloj del puerto C, en bare metal sería RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; sin esto PC0 no responde
    __HAL_RCC_TIM2_CLK_ENABLE(); // El equivalente para este sería la activación del TIM2


    // LED
    gpio.Pin   = HB_LED_PIN; // Es el pin 0 del puerto C
    gpio.Mode  = GPIO_MODE_OUTPUT_PP; // Salida push pull, MODER 01, OTYPER 0
    gpio.Pull  = GPIO_NOPULL; // Sin resistencia interna
    gpio.Speed = GPIO_SPEED_FREQ_LOW; // Baja velocidad 
    HAL_GPIO_Init(HB_LED_PORT, &gpio); // Corresponde a la activación del GPIOC, con el MODER, OTYPER, PUPDR y OSPEEDR, según el pin que se esté usando.

    // Es la base del tiempo para TIM2 
    hb_period_ms = toggle_ms; // Guarda el periodo en ms
    htim_hb.Instance               = HB_TIM; // Indica que timer es 
    htim_hb.Init.Prescaler         = (hb_timer_clock_hz() / HB_TICK_HZ) - 1U;
    htim_hb.Init.CounterMode       = TIM_COUNTERMODE_UP; // CUenta hacia arriba: DIR del CR1 = 0
    htim_hb.Init.Period            = (toggle_ms * (HB_TICK_HZ / 1000U)) - 1U; //  ARR = 250 x 10 - 1 = 2499
    htim_hb.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1; 
    htim_hb.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;// ARPE = 1, el ARR se carga de nuevo con la actualización
    if (HAL_TIM_Base_Init(&htim_hb) != HAL_OK) { // Aquí se configura el PSC, ARR; CR1 y el ARPE
        Error_Handler();
    }

    // Primero va el NVIC luego el timer
    HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0); // Configura una prioridad de la interrupción
    HAL_NVIC_EnableIRQ(TIM2_IRQn); // Habilita la interrupción del TIM2
    if (HAL_TIM_Base_Start_IT(&htim_hb) != HAL_OK) { // Configura el bit del DIER (UIE) Y del CR1 (CEN)
        Error_Handler();
    }
}

bool heartbeat_set_period(uint32_t toggle_ms)
{
    if ((toggle_ms < HEARTBEAT_MIN_MS) || (toggle_ms > HEARTBEAT_MAX_MS)) {
        return false;   // Fuera del rango de frecuencias se rechaza para que el blinky siga
    }
    hb_period_ms = toggle_ms; // Actualiza la copia que lee heartbeat_get_period
    __HAL_TIM_SET_AUTORELOAD(&htim_hb, (toggle_ms * (HB_TICK_HZ / 1000U)) - 1U); // EScribe el ARR nuevo
    return true; 
}

uint32_t heartbeat_get_period(void)
{
    return hb_period_ms; // única manera de leer desde afuera porque la variable es static
}

// Reemplazan las versiones vacias del HAL y del archivo de arranque
void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim_hb); // Limpia la bandera UIF del TIM2->SR 
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == HB_TIM) { // se comparte entre todos los timers, pero nos interesa solo el TIM2 por ahora
        HAL_GPIO_TogglePin(HB_LED_PORT, HB_LED_PIN); // Invierte PC0
    }
}
