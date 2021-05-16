/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ws2812.h - WS2812 driver
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
#ifndef WS2812_H
#define WS2812_H

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_tim.h"
#include "stm32f4xx_dma.h"
#include "misc.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * RGB LED color definition
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
typedef struct
{
  uint8_t red;
  uint8_t green;
  uint8_t blue;
} WS2812_RGB;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * WS2812 interface definition
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
extern void ws2812_init (uint_fast16_t);
extern void ws2812_refresh (uint_fast16_t);
extern void ws2812_set_led (uint_fast16_t, WS2812_RGB *);
extern void ws2812_set_all (WS2812_RGB *, uint_fast16_t, uint_fast8_t);
extern void ws2812_clear_all (uint_fast16_t);

#endif
