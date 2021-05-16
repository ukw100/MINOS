/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * main.c - main module of MINOS
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

#include "board-led.h"
#include "button.h"
#include "console.h"
#include "mcurses.h"
#include "delay.h"
#include "base.h"
#include "stm32f4-rtc.h"
#include "w25qxx.h"
#include "stm32_sdcard.h"
#include "cmd.h"
#include "timer2.h"

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * MINOS main function
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int
main(void)
{
    RCC_ClocksTypeDef RCC_Clocks;

    SystemInit ();
    SystemCoreClockUpdate();

    delay_init (DELAY_RESOLUTION_10_US);
    board_led_init ();                                                      // initialize GPIO for board LED
    button_init ();
    stm32f4_rtc_init ();
    sdcard_init ();
    console_init (115200);
    initscr ();
    timer2_init ();                                                         // initialize timer2
    w25qxx_init ();

    setvbuf(stdin, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf ("LINES = %d, COLS = %d\r\n", LINES, COLS);

    RCC_GetClocksFreq(&RCC_Clocks);
    printf ("SYS:%lu H:%lu, P1:%lu, P2:%lu\r\n",
                      RCC_Clocks.SYSCLK_Frequency,
                      RCC_Clocks.HCLK_Frequency,   // AHB
                      RCC_Clocks.PCLK1_Frequency,  // APB1
                      RCC_Clocks.PCLK2_Frequency); // APB2

    while (1)
    {
        cmd ((char *) NULL);
    }
}
