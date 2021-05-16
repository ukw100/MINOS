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
#include <stdio.h>
#include "delay.h"
#include "i2c.h"
#include "i2c-at24c32.h"

static I2C_TypeDef *            i2c_at24c32_channel;
static uint_fast8_t             i2c_at24c32_addr;                // normally 0x50 - 0x57

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * initialize I2C and RTC
 *
 * Return values:
 *  0   Failed
 *  1   Successful
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_at24c32_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint_fast8_t i2c_addr)
{
    uint32_t        clockspeed  = 100000;

    i2c_at24c32_channel  = i2c_channel;
    i2c_at24c32_addr     = i2c_addr << 1;
    i2c_init (i2c_channel, alt, clockspeed);

    return 1;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * write data
 *
 * Return values:
 *  0   Failed
 *  1   Successful
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_at24c32_write (uint_fast16_t addr, uint8_t * bufp, uint_fast16_t cnt)
{
    uint8_t         buffer[3];
    uint_fast8_t    rtc = 1;

    while (cnt)
    {
        buffer[0] = (addr >> 8) & 0x00FF;
        buffer[1] = addr & 0x00FF;
        buffer[2] = *bufp++;

        if (i2c_write (i2c_at24c32_channel, i2c_at24c32_addr, buffer, 3) != I2C_OK)
        {
            rtc = 0;
            break;
        }
        delay_msec (15);
        cnt--;
        addr++;
    }

    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * get date & time
 *
 * Return values:
 *  0   Failed
 *  1   Successful
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_at24c32_read (uint_fast16_t addr, uint8_t * bufp, uint_fast16_t cnt)
{
    uint8_t         buffer[2];
    uint_fast8_t    rtc = 0;

    buffer[0] = (addr >> 8) & 0x00FF;
    buffer[1] = addr & 0x00FF;

    if (i2c_write (i2c_at24c32_channel, i2c_at24c32_addr, buffer, 2) == I2C_OK)
    {
        if (i2c_read (i2c_at24c32_channel, i2c_at24c32_addr, bufp, cnt) == I2C_OK)
        {
            rtc = 1;
        }
    }

    return rtc;
}
