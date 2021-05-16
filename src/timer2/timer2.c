/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * timer2.c - timer of MINOS
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2017-2021 Frank Meyer - frank(at)fli4l.de
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#include <stdlib.h>
#include <stdio.h>

#include "stm32f4xx_conf.h"

#include "timer2.h"

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * timer definitions:
 *
 *      F_INTERRUPTS    = TIM_CLK / (TIM_PRESCALER + 1) / (TIM_PERIOD + 1)
 * <==> TIM_PRESCALER   = TIM_CLK / F_INTERRUPTS / (TIM_PERIOD + 1) - 1
 *
 * STM32F407:
 *      TIM_PERIOD      =  168 - 1 = 167
 *      TIM_PRESCALER   = 1000 - 1 = 999
 *      F_INTERRUPTS    = 168000000 / 1000 / 168 = 1000 (0.00% error)
 * STM32F401:
 *      TIM_PERIOD      =   84 - 1 =  83
 *      TIM_PRESCALER   = 1000 - 1 = 999
 *      F_INTERRUPTS    =  84000000 / 1000 /  84 = 1000 (0.00% error)
 * STM32F411:
 *      TIM_PERIOD      =  100 - 1 =   99
 *      TIM_PRESCALER   = 1000 - 1 =  999
 *      F_INTERRUPTS    = 100000000 / 1000 / 100 = 1000 (0.00% error)
 * STM32F446:
 *      TIM_PERIOD      =  180 - 1 = 179
 *      TIM_PRESCALER   = 1000 - 1 = 999
 *      F_INTERRUPTS    = 180000000 / 1000 / 179 = 1000 (0.00% error)
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define F_INTERRUPTS            1000                                        // 1000 interrupts per second

#if   defined (STM32F407VG)                                                 // STM32F407VG Discovery Board
#define TIM_PERIOD              (84-1)                                      // APB2 clock: 84 MHz

#elif defined (STM32F407VE)                                                 // STM32F407VE Black Board
#define TIM_PERIOD              (84-1)                                      // APB2 clock: 84 MHz

#elif defined (STM32F401RE)                                                 // STM32F401 Nucleo Board
#define TIM_PERIOD              (84-1)                                      // APB2 clock: 84 MHz

#elif defined (STM32F411RE)                                                 // STM32F411 Nucleo Board
#define TIM_PERIOD              (100-1)                                     // APB2 clock: 100 MHz

#elif defined (STM32F446RE)                                                 // STM32F446 Nucleo Board
#define TIM_PERIOD              (90-1)                                      // APB2 clock: 90 MHz

#else
#error STM32 unknown
#endif

#define TIM_PRESCALER           ((RCC_Clocks.PCLK2_Frequency / F_INTERRUPTS) / (TIM_PERIOD + 1) - 1)

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * initialize timer2
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
void
timer2_init (void)
{
    TIM_TimeBaseInitTypeDef     tim;
    NVIC_InitTypeDef            nvic;
    RCC_ClocksTypeDef           RCC_Clocks;

    RCC_GetClocksFreq(&RCC_Clocks);

    TIM_TimeBaseStructInit (&tim);
    RCC_APB1PeriphClockCmd (RCC_APB1Periph_TIM2, ENABLE);

    tim.TIM_ClockDivision   = TIM_CKD_DIV1;
    tim.TIM_CounterMode     = TIM_CounterMode_Up;
    tim.TIM_Period          = TIM_PERIOD;
    tim.TIM_Prescaler       = TIM_PRESCALER;
    TIM_TimeBaseInit (TIM2, &tim);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel                    = TIM2_IRQn;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    nvic.NVIC_IRQChannelPreemptionPriority  = 0x0F;
    nvic.NVIC_IRQChannelSubPriority         = 0x0F;
    NVIC_Init (&nvic);

    TIM_Cmd(TIM2, ENABLE);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * timer2 IRQ handler
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void TIM2_IRQHandler (void);                                     // keep compiler happy
volatile uint32_t       milliseconds;                                   // used by time.start() and time.stop()
volatile uint32_t       alarm_millis;                                   // used by alarm.set() and alarm.check()

void
TIM2_IRQHandler (void)
{
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    milliseconds++;
    alarm_millis++;

#if 0
    if (buttons_used)
    {
        GPIO_TypeDef *  portp;
        int             port;
        int             pin;
        int             mask;

        for (i = 0; i < buttons_used; i++)
        {
            port = buttons[i].port;
            pin  = buttons[i].pin;

            mask = 1 << pin;

            portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
            state = GPIO_ReadInputDataBit(portp, mask);                           // GPIO_ReadInputDataBit(GPIOA, mask);

            if (buttons[i].active_low)
            {
                if (! state)
                {
                    if (buttons[i].pressed_cnt < MAX_BUTTON_CNT)
                    {
                        buttons[i].pressed_cnt++;
                    }
                    else
                    {
                        buttons[i].pressed = TRUE;
                    }

                    buttons[i].released_cnt = 0;
                }
                else
                {
                    buttons[i].pressed_cnt = 0;

                    if (buttons[i].released_cnt < MAX_BUTTON_CNT)
                    {
                        buttons[i].released_cnt++;
                    }
                    else
                    {
                        buttons[i].pressed = FALSE;
                    }
                }
            }
            else
            {
                if (state)
                {
                    if (buttons[i].pressed_cnt < MAX_BUTTON_CNT)
                    {
                        buttons[i].pressed_cnt++;
                    }
                    else
                    {
                        buttons[i].pressed = TRUE;
                    }

                    buttons[i].released_cnt = 0;
                }
                else
                {
                    buttons[i].pressed_cnt = 0;

                    if (buttons[i].released_cnt < MAX_BUTTON_CNT)
                    {
                        buttons[i].released_cnt++;
                    }
                    else
                    {
                        buttons[i].pressed = FALSE;
                    }
                }
            }
        }
    }
#endif
}
