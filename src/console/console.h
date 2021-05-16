/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * console.h - declarations of serial routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2016-2021 Frank Meyer - frank(at)fli4l.de
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
#include <stdarg.h>

#include "uart.h"

#define console_init(b)             uart_init           (UART_NUMBER_1, 0, (b))
#define console_putc(ch)            uart_putc           (UART_NUMBER_1, (ch))
#define console_puts(s)             uart_puts           (UART_NUMBER_1, (s))
#define console_printf(fmt, ...)    uart_printf         (UART_NUMBER_1, (fmt), __VA_ARGS__)
#define console_getc()              uart_getc           (UART_NUMBER_1)
#define console_poll(p)             uart_poll           (UART_NUMBER_1, (p))
#define console_interrupted()       uart_interrupted    (UART_NUMBER_1)
#define console_set_rawmode(r)      uart_set_rawmode    (UART_NUMBER_1, (r))
#define console_get_rxsize()        uart_get_rxsize     (UART_NUMBER_1)
#define console_flush()             uart_flush          (UART_NUMBER_1)
#define console_read(buf,n)         uart_read           (UART_NUMBER_1, (buf), (n))
#define console_write(buf,n)        uart_write          (UART_NUMBER_1, (buf), (n))

