/* Autor: earangod@unal.edu.co
Salida PWM en PB7 con el canal 2 del TIM4, a 1 kHz y con ciclo de trabajo de 0 a 100 %.
El temporizador cuenta a 1 MHz de 0 a 999, o sea un periodo dura 1 ms. El pin queda en alto mientras el
contador está por debajo del valor de comparación, así que un valor de 250 da 25 %. El temporizador maneja
el pin solo, no hace falta ninguna interrupción. */
#include "stm32f4xx_hal.h"
#include "pwm.h"
#include "system.h"

#define PWM_TICK_HZ       1000000U	// el contador corre a 1 MHz, un tick dura 1 microsegundo
#define PWM_PERIOD_TICKS  1000U	// 1000 ticks son 1 ms, o sea 1 kHz

static TIM_HandleTypeDef htim_pwm;	// es static porque solo este archivo la toca
static bool pwm_started = false;	// se vuelve true cuando pwm_init terminó
static uint32_t pwm_duty = 0U;	// el último ciclo de trabajo en porcentaje

static uint32_t pwm_timer_clock_hz(void);

// Configura el TIM4 canal 2 como salida PWM de 1 kHz en PB7 y la deja en 0 %.
// Sigue el mismo orden de siempre, relojes, pin, temporizador, canal y arranque
void pwm_init(void)
{
    GPIO_InitTypeDef gpio = {0};	// campos en cero antes de llenarlos
    TIM_OC_InitTypeDef channel = {0};	// la configuración del canal, también en cero

    // Enciende el reloj del puerto B para el pin y el del TIM4 para el temporizador
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    // PB7 se entrega al canal 2 del TIM4 con la función alternativa AF2
    gpio.Pin       = GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;	// función alternativa, push pull
    gpio.Pull      = GPIO_NOPULL;	// sin resistencia interna, el pin lo maneja el temporizador
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;	// 1 kHz es lento, no hace falta velocidad
    gpio.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &gpio);

    // Base de tiempo, cuenta a 1 MHz hasta 999 y por eso un periodo dura 1 ms
    htim_pwm.Instance               = TIM4;
    htim_pwm.Init.Prescaler         = (pwm_timer_clock_hz() / PWM_TICK_HZ) - 1U;	// divide el reloj del temporizador hasta llegar a 1 MHz
    htim_pwm.Init.CounterMode       = TIM_COUNTERMODE_UP;	// cuenta hacia arriba
    htim_pwm.Init.Period            = PWM_PERIOD_TICKS - 1U;	// cuenta de 0 a 999
    htim_pwm.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim_pwm.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;	// un cambio de periodo se aplica al final del periodo actual
    if (HAL_TIM_PWM_Init(&htim_pwm) != HAL_OK) {	// escribe los valores en el temporizador
        Error_Handler();
    }

    // Canal 2, la salida queda en alto mientras el contador esté por debajo del valor de comparación
    channel.OCMode     = TIM_OCMODE_PWM1;	// modo PWM, alto mientras el contador es menor que la comparación
    channel.Pulse      = 0U;	// empieza en 0 %
    channel.OCPolarity = TIM_OCPOLARITY_HIGH;	// el nivel activo es alto
    channel.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim_pwm, &channel, TIM_CHANNEL_2) != HAL_OK) {
        Error_Handler();
    }

    // Arranque, no se necesita interrupción porque el temporizador maneja el pin solo
    if (HAL_TIM_PWM_Start(&htim_pwm, TIM_CHANNEL_2) != HAL_OK) {
        Error_Handler();
    }
    pwm_started = true;	// desde ahora pwm_set_duty puede escribir en el temporizador
}

// Cambia el ciclo de trabajo. Devuelve false si el valor está fuera de rango o si la salida nunca
// se inició, o sea si esta aplicación no llamó a pwm_init
bool pwm_set_duty(uint32_t percent)
{
    if ((!pwm_started) || (percent > 100U)) {	// no se inició, o el valor no es válido
        return false;	// no se escribe nada en un temporizador que nadie configuró
    }
    pwm_duty = percent;	// recuerda el valor para pwm_get_duty
    // Un porcentaje equivale a 10 ticks, y el valor nuevo se aplica al final del periodo actual
    __HAL_TIM_SET_COMPARE(&htim_pwm, TIM_CHANNEL_2, percent * (PWM_PERIOD_TICKS / 100U));
    return true;
}

// Devuelve el último ciclo de trabajo que se puso, en porcentaje
uint32_t pwm_get_duty(void)
{
    return pwm_duty;
}

// El TIM4 está en el bus APB1, su reloj es PCLK1, o el doble de PCLK1 cuando el prescaler de APB1 no es 1
static uint32_t pwm_timer_clock_hz(void)
{
    uint32_t pclk1 = HAL_RCC_GetPCLK1Freq();	// frecuencia del bus APB1

    if ((RCC->CFGR & RCC_CFGR_PPRE1) == RCC_CFGR_PPRE1_DIV1) {	// el prescaler de APB1 es 1
        return pclk1;	// el temporizador usa el mismo reloj del bus
    }
    return 2U * pclk1;	// con otro prescaler el hardware duplica el reloj del temporizador
}
