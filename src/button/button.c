/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * button.c - read user button of STM32F4 Discovery / STM32F4xx Nucleo / STM32F4 BlackBoard
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2014-2021 Frank Meyer - frank(at)fli4l.de
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
#include "button.h"

#if defined (STM32F407VG)                                               // STM32F4 Discovery Board PA0
#define BUTTON_PERIPH_CLOCK_CMD     RCC_AHB1PeriphClockCmd
#define BUTTON_PERIPH               RCC_AHB1Periph_GPIOA
#define BUTTON_PORT                 GPIOA
#define BUTTON_PIN                  GPIO_Pin_0
#define BUTTON_PRESSED              Bit_SET                             // pressed if high

#elif defined (STM32F407VE)                                             // STM32F407 Black Board Key0 = PE4, Key1 = PE3
#define BUTTON_PERIPH_CLOCK_CMD     RCC_AHB1PeriphClockCmd
#define BUTTON_PERIPH               RCC_AHB1Periph_GPIOE
#define BUTTON_PORT                 GPIOE
#define BUTTON_PIN                  GPIO_Pin_4
#define BUTTON_PRESSED              Bit_RESET                           // pressed if low

#elif defined (STM32F4XX)                                               // STM32F4xx Nucleo Board PC13
#define BUTTON_PERIPH_CLOCK_CMD     RCC_AHB1PeriphClockCmd
#define BUTTON_PERIPH               RCC_AHB1Periph_GPIOC
#define BUTTON_PORT                 GPIOC
#define BUTTON_PIN                  GPIO_Pin_13
#define BUTTON_PRESSED              Bit_RESET                           // pressed if low

#else
#error STM32 unknown
#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * initialize button port
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
button_init (void)
{
    GPIO_InitTypeDef gpio;

    GPIO_StructInit (&gpio);
    BUTTON_PERIPH_CLOCK_CMD (BUTTON_PERIPH, ENABLE);

    gpio.GPIO_Pin = BUTTON_PIN;

#if defined (STM32F407VG)                                   // STM32F4 Discovery has already an external pullup
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
#elif defined (STM32F407VE)                                 // STM32F407 Black Board with internal pullup
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_UP;
#elif defined (STM32F4XX)                                   // STM32F4XX Nucleo board has already an external pullup
    gpio.GPIO_Mode = GPIO_Mode_IN;
    gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
#endif

    GPIO_Init(BUTTON_PORT, &gpio);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check if button pressed
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
button_pressed (void)
{
    if (GPIO_ReadInputDataBit(BUTTON_PORT, BUTTON_PIN) == BUTTON_PRESSED)
    {
        return 1;
    }
    return 0;
}
