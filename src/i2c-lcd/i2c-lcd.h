/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * i2c-lcd.h - I2C LCD routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2019-2021 Frank Meyer - frank(at)fli4l.de
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
#ifndef I2C_LCD_H
#define I2C_LCD_H

#include "stm32f4xx.h"

extern uint_fast8_t i2c_lcd_clear(void);
extern uint_fast8_t i2c_lcd_home(void);
extern uint_fast8_t i2c_lcd_move (uint8_t y, uint8_t x);
extern uint_fast8_t i2c_lcd_backlight (uint8_t on);
extern uint_fast8_t i2c_lcd_define_char (uint8_t n_char, uint8_t * data);
extern uint_fast8_t i2c_lcd_putc (uint8_t ch);
extern uint_fast8_t i2c_lcd_puts (const char * str);
extern uint_fast8_t i2c_lcd_mvputs (uint8_t y, uint8_t x, const char * str);
extern uint_fast8_t i2c_lcd_clrtoeol (void);
extern uint_fast8_t i2c_lcd_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint_fast8_t i2c_addr, uint_fast8_t lines, uint_fast8_t columns);

#endif
