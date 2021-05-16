/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ds3231.c - DS3231 RTC routines
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
#include "i2c.h"
#include "i2c-ds3231.h"

static I2C_TypeDef *            i2c_ds3231_channel;
static uint_fast8_t             i2c_ds3231_addr;                // normally 0xD0

#define FIRST_TIME_REG          0x00                            // address of first time register

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * DS1307 addresses and control registers
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define DS1307_LAST_RAM_ADDR    0x3F                            // Last ram address of DS1307

#define DS1307_CTRL_OUT         0x80                            // Output Level
#define DS1307_CTRL_SQWE        0x10                            // Square Wave Enable
#define DS1307_CTRL_RS1         0x02                            // Rate Select RS2/RS1: 00 =    1Hz, 01 = 1024Hz,
#define DS1307_CTRL_RS2         0x01                            // Rate Select RS1/RS1: 10 = 4096Hz, 11 = 8182Hz

#define DS1307_CTRL_REG         0x07                            // address of control register
#define DS1307_CTRL_DEFAULT     0x00                            // default value: all bits reset

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * DS3231 addresses and control registers
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
#define DS3231_CTRL_EOSC        0x80                            // Enable Oscillator, active low
#define DS3231_CTRL_BBSQW       0x40                            // Battery-Backed Square-Wave Enable
#define DS3231_CTRL_CONV        0x20                            // Convert Temperature
#define DS3231_CTRL_RS2         0x10                            // Rate Select RS2/RS1: 00 =    1Hz, 01 = 1024Hz,
#define DS3231_CTRL_RS1         0x08                            // Rate Select RS1/RS1: 10 = 4096Hz, 11 = 8182Hz
#define DS3231_CTRL_INTCN       0x04                            // Interrupt Control
#define DS3231_CTRL_A2IE        0x02                            // Alarm 2 Interrupt Enable
#define DS3231_CTRL_A1IE        0x01                            // Alarm 1 Interrupt Enable

#define DS3231_CTRL_REG         0x0E                            // address of control register
#define DS3231_CTRL_DEFAULT     0x00                            // default value: all bits reset

#define DS3231_TEMP_REG_HI      0x11                            // 8 upper bytes: integer part
#define DS3231_TEMP_REG_LO      0x12                            // 2 lower bytes: fractional part 0x00=0.00°, 0x01=0.25°, ... 0x03=0.75°

#define INT_TO_BCD(x)           (((x) / 10) << 4) + ((x) % 10)
#define BCD_TO_INT(x)           (10 * ((x) >> 4) + ((x) & 0x0F))

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * initialize I2C and RTC
 *
 * Return values:
 *  0   Failed
 *  1   Successful
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_ds3231_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint_fast8_t i2c_addr)
{
    uint8_t         buf[2];
    uint_fast8_t    rtc         = 0;
    uint32_t        clockspeed  = 100000;

    i2c_ds3231_channel  = i2c_channel;
    i2c_ds3231_addr     = i2c_addr << 1;

    i2c_init (i2c_channel, alt, clockspeed);

    buf[0] = DS3231_CTRL_REG;

    if (i2c_write (i2c_ds3231_channel, i2c_ds3231_addr, buf, 1) == I2C_OK)
    {
        if (i2c_read (i2c_ds3231_channel, i2c_ds3231_addr, buf, 1) == I2C_OK)   // test read of control register of DS3231
        {
            if (buf[0] != DS3231_CTRL_DEFAULT)
            {
                buf[0] = DS3231_CTRL_REG;
                buf[1] = DS3231_CTRL_DEFAULT;

                if (i2c_write (i2c_ds3231_channel, i2c_ds3231_addr, buf, 1) == I2C_OK)
                {
                    rtc = 1;
                }
            }
            else
            {
                rtc = 1;
            }
        }
    }

    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * set date & time
 *
 * Return values:
 *  0   Failed
 *  1   Successful
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_ds3231_set_date_time (struct tm * tmp)
{
    uint8_t         buffer[8];
    uint_fast8_t    rtc = 0;

    buffer[0] = FIRST_TIME_REG;
    buffer[1] = INT_TO_BCD(tmp->tm_sec);
    buffer[2] = INT_TO_BCD(tmp->tm_min);
    buffer[3] = INT_TO_BCD(tmp->tm_hour);
    buffer[4] = tmp->tm_wday + 1;
    buffer[5] = INT_TO_BCD(tmp->tm_mday);
    buffer[6] = INT_TO_BCD(tmp->tm_mon + 1);
    buffer[7] = INT_TO_BCD(tmp->tm_year - 100);

    if (i2c_write (i2c_ds3231_channel, i2c_ds3231_addr, buffer, 8) == I2C_OK)
    {
        rtc = 1;
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
i2c_ds3231_get_date_time (struct tm * tmp)
{
    uint8_t         buffer[8];
    uint_fast8_t    rtc;

    buffer[0] = FIRST_TIME_REG;

    if (i2c_write (i2c_ds3231_channel, i2c_ds3231_addr, buffer, 1) == I2C_OK)
    {
        if (i2c_read (i2c_ds3231_channel, i2c_ds3231_addr, buffer + 1, 7) == I2C_OK)
        {
            tmp->tm_sec  = BCD_TO_INT(buffer[1]);
            tmp->tm_min  = BCD_TO_INT(buffer[2]);
            tmp->tm_hour = BCD_TO_INT(buffer[3]);
            tmp->tm_wday = buffer[4] - 1;
            tmp->tm_mday = BCD_TO_INT(buffer[5]);
            tmp->tm_mon  = BCD_TO_INT(buffer[6]) - 1;
            tmp->tm_year = BCD_TO_INT(buffer[7]) + 100;

            rtc = 1;
        }
    }

    return rtc;
}
