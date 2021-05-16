/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * i2c.h - declarations of I2C routines
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
#ifndef I2C_H
#define I2C_H

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_i2c.h"

#define I2C_OK                  (0)
#define I2C_ERROR_NO_FLAG_SB    (-1)
#define I2C_ERROR_NO_FLAG_ADDR  (-2)
#define I2C_ERROR_NO_FLAG_TXE   (-3)
#define I2C_ERROR_NO_TXE_OR_BTF (-4)
#define I2C_ERROR_NO_FLAG_SB2   (-5)
#define I2C_ERROR_NO_FLAG_ADDR2 (-6)
#define I2C_ERROR_NO_FLAG_RXNE  (-7)

extern void             i2c_init (I2C_TypeDef *, uint_fast8_t, uint32_t);
extern int_fast16_t     i2c_read (I2C_TypeDef * i2c_channel, uint_fast8_t slave_addr, uint8_t * data, uint_fast16_t cnt);
extern int_fast16_t     i2c_write (I2C_TypeDef * i2c_channel, uint_fast8_t slave_addr, uint8_t * data, uint_fast16_t cnt);

#endif
