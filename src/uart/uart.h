/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart.h - UART driver routines for STM32F4XX
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2021 Frank Meyer - frank(at)fli4l.de
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
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_rcc.h"
#include "misc.h"

#include <stdarg.h>

#define UART_NUMBER_1   0
#define UART_NUMBER_2   1
#define UART_NUMBER_3   2
#define UART_NUMBER_4   3
#define UART_NUMBER_5   4
#define UART_NUMBER_6   5
#define N_UARTS         6

extern void             uart_init           (uint_fast8_t uart_number, uint_fast8_t alternate, uint32_t baudrate);
extern void             uart_putc           (uint_fast8_t, uint_fast8_t);
extern void             uart_puts           (uint_fast8_t, const char *);
extern int              uart_vprintf        (uint_fast8_t, const char *, va_list);
extern int              uart_printf         (uint_fast8_t, const char *, ...);
extern uint_fast8_t     uart_getc           (uint_fast8_t);
extern uint_fast8_t     uart_poll           (uint_fast8_t, uint_fast8_t *);
extern uint_fast8_t     uart_interrupted    (uint_fast8_t);
extern void             uart_set_rawmode    (uint_fast8_t, uint_fast8_t);
extern uint_fast16_t    uart_get_rxsize     (uint_fast8_t);
extern void             uart_flush          (uint_fast8_t);
extern uint_fast16_t    uart_read           (uint_fast8_t, char *, uint_fast16_t);
extern uint_fast16_t    uart_write          (uint_fast8_t, char *, uint_fast16_t);
