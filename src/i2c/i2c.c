/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * i2c.c - I2C routines
 *
 * Ports/Pins:
 *
 *  +---------+-----+--------------------+
 *  | Channel | alt | STM32F4xx          |
 *  +---------+-----+--------------------+
 *  | I2C1    |  0  | SCL=PB6,  SDA=PB7  |
 *  | I2C1    |  1  | SCL=PB8,  SDA=PB9  |
 *  | I2C2    |  0  | SCL=PB10, SDA=PB11 |
 *  | I2C3    |  0  | SCL=PA8,  SDA=PC9  |
 *  +---------+-----+--------------------+
 *
 *  I2C3 pin PC9 is used by SD Card, so don't use it!
 *
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
#include "i2c.h"
#include "delay.h"

#define I2C_TIMEOUT_CNT             100                                    // timeout counter: 100
#define I2C_TIMEOUT_USEC             50                                    // timeout: 100 * 50 usec = 5 msec

static uint32_t         i2c_clockspeed;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * init i2c bus
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
i2c_init_i2c (I2C_TypeDef * i2c_channel)
{
    I2C_InitTypeDef  i2c;

    I2C_StructInit (&i2c);

    I2C_DeInit(i2c_channel);

    i2c.I2C_Mode                  = I2C_Mode_I2C;
    i2c.I2C_DutyCycle             = I2C_DutyCycle_2;
    i2c.I2C_OwnAddress1           = 0x00;
    i2c.I2C_Ack                   = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress   = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed            = i2c_clockspeed;

    I2C_Init (i2c_channel, &i2c);
    I2C_Cmd (i2c_channel, ENABLE);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * handle timeout
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
i2c_handle_timeout (I2C_TypeDef * i2c_channel)
{
    I2C_GenerateSTOP (i2c_channel, ENABLE);
    I2C_SoftwareResetCmd (i2c_channel, ENABLE);
    I2C_SoftwareResetCmd (i2c_channel, DISABLE);

    I2C_DeInit (i2c_channel);
    i2c_init_i2c (i2c_channel);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * wait for flags
 *
 * return values:
 * == 1  OK, got flag
 * == 0  timeout
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
i2c_wait_for_flags (I2C_TypeDef * i2c_channel, uint32_t flag1, uint32_t flag2)
{
    uint32_t  timeout = I2C_TIMEOUT_CNT;

    if (flag2)
    {
        while ((!I2C_GetFlagStatus(i2c_channel, flag1)) || (!I2C_GetFlagStatus(i2c_channel, flag2)))
        {
            if (timeout > 0)
            {
                delay_usec(I2C_TIMEOUT_USEC);
                timeout--;
            }
            else
            {
                i2c_handle_timeout (i2c_channel);
                return 0;
            }
        }
    }
    else
    {
        while (! I2C_GetFlagStatus(i2c_channel, flag1))
        {
            if (timeout > 0)
            {
                delay_usec(I2C_TIMEOUT_USEC);
                timeout--;
            }
            else
            {
                i2c_handle_timeout (i2c_channel);
                return 0;
            }
        }
    }

    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * initialize I2C
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
i2c_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint32_t clockspeed)
{
    GPIO_InitTypeDef    gpio;

    I2C_DeInit(i2c_channel);
    GPIO_StructInit (&gpio);

    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    gpio.GPIO_OType = GPIO_OType_OD;                                                    // OpenDrain!
    gpio.GPIO_PuPd  = GPIO_PuPd_UP; // GPIO_PuPd_NOPULL;

    if (i2c_channel == I2C1)
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);                           // for SCL & SDA

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, ENABLE);                            // I2C reset
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C1, DISABLE);

        if (alt)                                                                        // alternate function?
        {
            GPIO_PinAFConfig(GPIOB, GPIO_PinSource8, GPIO_AF_I2C1);                     // SCL: PB8
            GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);                     // SDA: PB9

            gpio.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9;                                    // SCL & SDA pin
            GPIO_Init(GPIOB, &gpio);
        }
        else
        {
            GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_I2C1);                     // SCL: PB6
            GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_I2C1);                     // SDA: PB7

            gpio.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;                                    // SCL & SDA pin
            GPIO_Init(GPIOB, &gpio);
        }
    }
    else if (i2c_channel == I2C2)
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);                           // for SCL & SDA

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C2, ENABLE);
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, ENABLE);                            // I2C reset
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C2, DISABLE);

        GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_I2C2);                        // SCL: PB10
        GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_I2C2);                        // SDA: PB11

        gpio.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;                                      // SCL & SDA pin
        GPIO_Init(GPIOB, &gpio);
    }
    else if (i2c_channel == I2C3)
    {
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);                           // for SCL
        RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);                           // for SDA

        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C3, ENABLE);
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C3, ENABLE);                            // I2C reset
        RCC_APB1PeriphResetCmd(RCC_APB1Periph_I2C3, DISABLE);

        GPIO_PinAFConfig(GPIOA, GPIO_PinSource8, GPIO_AF_I2C3);                         // SCL: PA8
        GPIO_PinAFConfig(GPIOC, GPIO_PinSource9, GPIO_AF_I2C3);                         // SDA: PC9

        gpio.GPIO_Pin = GPIO_Pin_8;                                                     // SCL pin
        GPIO_Init(GPIOA, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_9;                                                     // SDA pin
        GPIO_Init(GPIOC, &gpio);
    }
    else
    {
        return;
    }

    i2c_clockspeed = clockspeed;
    i2c_init_i2c (i2c_channel);
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_read - I2C polling read
 *
 * This function waits until I2C peripheral is ready.
 *
 * return values:
 * ==  0 I2C_OK
 *  <  0 Error
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int_fast16_t
i2c_read (I2C_TypeDef * i2c_channel, uint_fast8_t slave_addr, uint8_t * data, uint_fast16_t cnt)
{
    uint_fast16_t   n;

    while (I2C_GetFlagStatus(i2c_channel, I2C_FLAG_BUSY))
    {
        ;
    }

    I2C_GenerateSTART(i2c_channel, ENABLE);                                        // start sequence

    if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_SB, 0))
    {
        return I2C_ERROR_NO_FLAG_SB2;
    }

    I2C_Send7bitAddress(i2c_channel, slave_addr, I2C_Direction_Receiver);          // send slave address (receiver)

    if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_ADDR, 0))
    {
        return I2C_ERROR_NO_FLAG_ADDR2;
    }

    i2c_channel->SR2;                                                               // clear ADDR-Flag

    I2C_AcknowledgeConfig(i2c_channel, ENABLE);                                     // ACK enable

    for (n = 0; n < cnt; n++)                                                       // read all data
    {
        if (n + 1 == cnt)
        {
            I2C_AcknowledgeConfig(i2c_channel, DISABLE);                            // ACK disable
            I2C_GenerateSTOP(i2c_channel, ENABLE);                                  // stop sequence

            while (I2C_GetFlagStatus(i2c_channel, I2C_FLAG_BUSY))
            {
                ;
            }
        }

        if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_RXNE, 0))
        {
            return I2C_ERROR_NO_FLAG_RXNE;
        }

        data[n] = I2C_ReceiveData(i2c_channel);                                     // read data
    }

    I2C_AcknowledgeConfig(i2c_channel, ENABLE);                                     // ACK enable

    return I2C_OK;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_write - I2C polling write
 *
 * This function waits until I2C peripheral is ready.
 *
 * return values:
 * ==  0 I2C_OK
 *  <  0 Error
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
int_fast16_t
i2c_write (I2C_TypeDef * i2c_channel, uint_fast8_t slave_addr, uint8_t * data, uint_fast16_t cnt)
{
    uint8_t         value;
    uint_fast16_t   n;

    while (I2C_GetFlagStatus(i2c_channel, I2C_FLAG_BUSY))
    {
       ;
    }

    I2C_GenerateSTART(i2c_channel, ENABLE);

    if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_SB, 0))
    {
        return I2C_ERROR_NO_FLAG_SB;
    }

    I2C_AcknowledgeConfig(i2c_channel, ENABLE);                                     // ACK enable

    I2C_Send7bitAddress (i2c_channel, slave_addr, I2C_Direction_Transmitter);       // send slave address (transmitter)

    if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_ADDR, 0))
    {
        return I2C_ERROR_NO_FLAG_ADDR;
    }

    i2c_channel->SR2;                                                               // clear ADDR-Flag

    if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_TXE, 0))
    {
        return I2C_ERROR_NO_FLAG_TXE;
    }

    for (n = 0; n < cnt; n++)                                                       // send all data
    {
        value = *data++;                                                            // read data from buffer

        I2C_SendData(i2c_channel, value);                                           // send data

        if (! i2c_wait_for_flags (i2c_channel, I2C_FLAG_TXE, I2C_FLAG_BTF))
        {
            return I2C_ERROR_NO_TXE_OR_BTF;
        }
    }

    I2C_GenerateSTOP(i2c_channel, ENABLE);                                          // stop sequence

    while (I2C_GetFlagStatus(i2c_channel, I2C_FLAG_STOPF))                          // fm: necessary?
    {
        ;
    }

    return I2C_OK;
}
