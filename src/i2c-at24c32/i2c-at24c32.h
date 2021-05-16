/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * at24c32.c - AT24C32 EEPROM routines
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
#ifndef AT24C32_H
#define AT24C32_H

#include <stdint.h>
#include "stm32f4xx.h"

extern uint_fast8_t i2c_at24c32_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint_fast8_t i2c_addr);
extern uint_fast8_t i2c_at24c32_write (uint_fast16_t addr, uint8_t * bufp, uint_fast16_t cnt);
extern uint_fast8_t i2c_at24c32_read (uint_fast16_t addr, uint8_t * bufp, uint_fast16_t cnt);

#endif // AT24C32_H
