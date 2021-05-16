/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * i2c-lcd.c - I2C LCD routines
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
#include "i2c.h"
#include "i2c-lcd.h"
#include "delay.h"

#define LCD_CLOCKSPEED      100000

#define MAX_LCD_LINES       4

#define LCD_FONT_HEIGHT     8
#define LCD_ENTRYMODESET    0x06
#define LCD_FUNCTIONSET     0x28

#define RS_PIN	            0x00
#define RW_PIN	            0x01
#define	E_PIN	            0x02
#define BL_PIN	            0x03

#define LCD_CLEARDISPLAY 	0x01
#define LCD_RETURNHOME 		0x02
#define LCD_DISPLAYON       0x0C
#define LCD_SETCGRAMADDR	0x40
#define LCD_SETDDRAMADDR    0x80

#define HIGH_NIBBLE(x)      ((uint8_t)((x) & 0xf0))
#define LOW_NIBBLE(x)       ((uint8_t)((x) << 4))

static I2C_TypeDef *        i2c_lcd_channel;
static uint_fast8_t         i2c_lcd_addr;
static uint_fast8_t         i2c_lcd_lines;
static uint_fast8_t         i2c_lcd_columns;

static uint_fast8_t         start_addresses[MAX_LCD_LINES];
static uint8_t              port_state = 0;
static uint8_t              cursor_x;
static uint8_t              cursor_y;

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_send_nibble() - send nibble
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
i2c_lcd_send_nibble (uint8_t nibble)
{
	uint8_t         cmd[2];
	uint_fast8_t    rtc = 0;

	port_state = (port_state & 0x0f) | nibble;
	cmd[0] = port_state | (1 << E_PIN);
	cmd[1] = port_state;

    if (i2c_write (i2c_lcd_channel, i2c_lcd_addr, cmd, 2) == I2C_OK)
    {
        rtc = 1;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_send_byte() - send byte
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
i2c_lcd_send_byte (uint8_t byte)
{
	uint8_t         cmd[4];
	uint_fast8_t    rtc = 0;

	port_state = ((port_state & 0x0F) | HIGH_NIBBLE(byte));
	cmd[0] = port_state | (1 << E_PIN);
	cmd[1] = port_state;
	port_state = ((port_state & 0x0F) | LOW_NIBBLE(byte));
	cmd[2] = port_state | (1 << E_PIN);
	cmd[3] = port_state;

    if (i2c_write (i2c_lcd_channel, i2c_lcd_addr, cmd, 4) == I2C_OK)
    {
        rtc = 1;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_send_cmd () - send command
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
i2c_lcd_send_cmd (uint8_t cmd)
{
    uint_fast8_t    rtc;

	port_state &= ~(1 << RS_PIN);	                                                                        // clear RS
	rtc = i2c_lcd_send_byte (cmd);
	return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_send_data () - send data
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
i2c_lcd_send_data (uint8_t cmd)
{
    uint_fast8_t    rtc;
	port_state |= (1 << RS_PIN);                                                                        	// set RS
	rtc = i2c_lcd_send_byte(cmd);
	return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_clear () - clear display, set cursor to home position
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_clear (void)
{
    uint_fast8_t    rtc;
	rtc = i2c_lcd_send_cmd (LCD_CLEARDISPLAY);

	if (rtc)
    {
        cursor_x = 0;
        cursor_y = 0;
        delay_msec (2);
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_home () - set cursor to home position
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_home (void)
{
    uint_fast8_t    rtc;

	rtc = i2c_lcd_send_cmd (LCD_RETURNHOME);

	if (rtc)
    {
        cursor_x = 0;
        cursor_y = 0;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_move () - set cursor position
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_move (uint8_t y, uint8_t x)
{
    uint_fast8_t    rtc = 0;

	if (y < i2c_lcd_lines && x < i2c_lcd_columns)
	{
	    uint8_t addr = start_addresses[y] + x;

		rtc = i2c_lcd_send_cmd (LCD_SETDDRAMADDR | addr);

		if (rtc)
        {
            cursor_x = x;
            cursor_y = y;
        }
	}
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_backlight () - switch off/on backlight
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_backlight (uint8_t on)
{
	uint8_t         cmd[1];
    uint_fast8_t    rtc = 0;

    if (on)
    {
        port_state |= 1 << BL_PIN;
    }
    else
    {
        port_state &= ~(1 << BL_PIN);
    }

	cmd[0] = port_state;

    if (i2c_write (i2c_lcd_channel, i2c_lcd_addr, cmd, 1) == I2C_OK)
    {
        rtc = 1;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_define_char () - define character
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_define_char (uint8_t n_char, uint8_t * data)
{
    uint_fast8_t idx;
    uint_fast8_t rtc = 1;

	i2c_lcd_send_cmd ((n_char << 3) | LCD_SETCGRAMADDR);

	for (idx = 0; idx < LCD_FONT_HEIGHT; idx++)
	{
		if (! i2c_lcd_send_data (data[idx]))
        {
            rtc = 0;
            break;
        }
	}
	return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_putc () - print character
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_putc (uint8_t ch)
{
    uint_fast8_t    rtc = 0;

    if (cursor_x < i2c_lcd_columns)
    {
        if (i2c_lcd_send_data (ch))
        {
            cursor_x++;
            rtc = 1;
        }
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_puts () - print string
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_puts (const char * str)
{
    uint_fast8_t    rtc = 1;

	while (*str)
    {
		if (! i2c_lcd_putc (*str))
        {
            rtc = 0;
            break;
        }
        str++;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_mvputs () - move cursor, print string
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_mvputs (uint8_t y, uint8_t x, const char * str)
{
    uint_fast8_t    rtc = 0;

    if (i2c_lcd_move (y, x))
    {
        rtc = 1;

        while (*str)
        {
            if (! i2c_lcd_putc (*str))
            {
                rtc = 0;
                break;
            }

            str++;
        }
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_clrtoeol () - clear to end of line
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_clrtoeol (void)
{
    uint_fast8_t    rtc = 1;

    while (cursor_x < i2c_lcd_columns)
    {
        if (! i2c_lcd_send_data (' '))
        {
            rtc = 0;
            break;
        }
        cursor_x++;
    }
    return rtc;
}

/*-------------------------------------------------------------------------------------------------------------------------------------------
 * i2c_lcd_init () - init LCD
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
i2c_lcd_init (I2C_TypeDef * i2c_channel, uint_fast8_t alt, uint_fast8_t i2c_addr, uint_fast8_t lines, uint_fast8_t columns)
{
    uint_fast8_t    rtc = 0;

    i2c_lcd_channel = i2c_channel;
    i2c_lcd_addr    = i2c_addr << 1;
    i2c_lcd_lines   = lines;
    i2c_lcd_columns = columns;

    start_addresses[0] = 0x00;                              // DDRAM address of first char of line 1
    start_addresses[1] = 0x40;                              // DDRAM address of first char of line 2

    if (lines == 4)
    {
        if (columns == 16)
        {
            start_addresses[2] = 0x10;                      // DDRAM address of first char of line 3
            start_addresses[3] = 0x50;                      // DDRAM address of first char of line 4

        }
        else if (columns == 20)
        {
            start_addresses[2] = 0x14;                      // DDRAM address of first char of line 3
            start_addresses[3] = 0x54;                      // DDRAM address of first char of line 4
        }
    }

    i2c_init (i2c_channel, alt, LCD_CLOCKSPEED);

    delay_msec (350);

    if (! i2c_lcd_send_nibble (0x30))
    {
        return rtc;
    }

    delay_msec (40);

    if (! i2c_lcd_send_nibble (0x30))
    {
        return rtc;
    }

    delay_msec (2);

    if (! i2c_lcd_send_nibble (0x30))
    {
        return rtc;
    }

    delay_msec (2);

    if (i2c_lcd_send_nibble (0x20) &&
        i2c_lcd_send_cmd (LCD_FUNCTIONSET) &&
        i2c_lcd_send_cmd (LCD_ENTRYMODESET) &&
        i2c_lcd_send_cmd (LCD_DISPLAYON) &&
        i2c_lcd_backlight (0) &&
        i2c_lcd_clear ())
    {
        rtc = 1;
    }

    return rtc;
}
