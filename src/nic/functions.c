/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * functions.c - internal functions of nic runtime system
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2017-2021 Frank Meyer - frank(at)fli4l.de
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
#include <time.h>
#include <math.h>
#include "nicstrings.h"
#include "nic-common.h"
#include "nic.h"
#include "functions.h"
#include "mcurses.h"
#include "nic-base.h"

#if defined (unix)
#include <time.h>
#include <sys/time.h>
#elif defined (WIN32)
#include <windows.h>
#include <time.h>
#else
#include "console.h"
#include "tft.h"
#include "delay.h"
#include "ws2812.h"
#include "i2c.h"
#include "i2c-lcd.h"
#include "i2c-at24c32.h"
#include "i2c-ds3231.h"
#include "stm32f4-rtc.h"
#include "w25qxx.h"
#include "base.h"
#include "timer2.h"
#endif

#include "font.h"

#ifdef __GNUC__
#  define UNUSED(x)         UNUSED_ ## x __attribute__((__unused__))
#else
#  define UNUSED(x)         UNUSED_ ## x
#endif

#define BIN     2
#define DEC     10
#define HEX     16

static int
printbin (uint32_t number, int tabulate)
{
    int     i;
    char    buf[32 + 1];
    int     len = 0;
    int     first_1 = 32;
    int     rtc = 0;

    buf[32] = '\0';

    for (i = 0; i < 32; i++)
    {
        if (number & 0x01)
        {
            buf[31 - i] = '1';
            first_1 = 31 - i;
            len = i + 1;
        }
        else
        {
            buf[31 - i] = '0';
        }

        number >>= 1;
    }

    while (tabulate > len)
    {
        console_putc ('0');
        tabulate--;
        rtc++;
    }

    console_puts (buf + first_1);
    rtc += 32 - first_1;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * CONSOLE routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_console_putc ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_console_putc (FIP_RUN * fip)
{
    int ch = get_argument_int (fip, 0);

    console_putc (ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_console_print ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_console_print (FIP_RUN * fip)
{
    int             result = 0;
    int             format;
    int             tabulation = 0;
    unsigned char * resultstr = (unsigned char *) NULL;
    int             type = get_argument (fip, 0, &resultstr, &result);

    if (type == RESULT_INT)                                             // argument is integer
    {
        format = DEC_FORMAT;                                            // default format is decimal
    }
    else if (type == RESULT_BYTE_ARRAY)
    {
        format = DEC_FORMAT;                                            // default format is decimal
    }
    else                                                                // argument is string
    {
        format = STR_FORMAT;                                            // default format is string
    }

    if (fip->argc >= 2)
    {
        format = get_argument_int (fip, 1);                             // overwrite format

        if (fip->argc >= 3)
        {
            tabulation = get_argument_int (fip, 2);                     // overwrite tabulation
        }
    }

    if (format == STR_FORMAT && type == RESULT_INT)                     // print an integer as string
    {
        resultstr = get_argument_string (fip, 0);                       // get int argument as string
        type = RESULT_CSTRING;                                          // patch type
    }
    else if (format != STR_FORMAT && type == RESULT_CSTRING)            // print a string as integer
    {
        result = get_argument_int (fip, 0);                             // get string argument as int
        type = RESULT_INT;                                              // patch type
    }

    if (type == RESULT_INT)
    {
        char    fmt[32];

        if (format == DEC_FORMAT)
        {
            sprintf (fmt, "%%%dd", tabulation);
            fip->reti = console_printf (fmt, result);
        }
        else if (format == DEC0_FORMAT)
        {
            sprintf (fmt, "%%0%dd", tabulation);
            fip->reti = console_printf (fmt, result);
        }
        else if (format == HEX_FORMAT)
        {
            sprintf (fmt, "%%0%dX", tabulation);
            fip->reti = console_printf (fmt, result);
        }
        else // if (format == BIN_FORMAT)
        {
            fip->reti = printbin (result, tabulation);
        }
    }
    else if (type == RESULT_BYTE_ARRAY)
    {
        char        fmt[32];
        uint8_t *   ptr;
        int         idx;

        if (format == DEC_FORMAT)
        {
            sprintf (fmt, "%%%dd", tabulation);

            for (idx = 0, ptr = resultstr; idx < result; idx++, ptr++)
            {
                fip->reti = console_printf (fmt, *ptr);

                if (idx < result - 1)
                {
                    console_putc (' ');
                }
            }
        }
        else if (format == DEC0_FORMAT)
        {
            sprintf (fmt, "%%0%dd", tabulation);

            for (idx = 0, ptr = resultstr; idx < result; idx++, ptr++)
            {
                fip->reti = console_printf (fmt, *ptr);

                if (idx < result - 1)
                {
                    console_putc (' ');
                }
            }
        }
        else if (format == HEX_FORMAT)
        {
            sprintf (fmt, "%%0%dX", tabulation);

            for (idx = 0, ptr = resultstr; idx < result; idx++, ptr++)
            {
                fip->reti = console_printf (fmt, *ptr);

                if (idx < result - 1)
                {
                    console_putc (' ');
                }
            }
        }
        else // if (format == BIN_FORMAT)
        {
            for (idx = 0, ptr = resultstr; idx < result; idx++, ptr++)
            {
                fip->reti = printbin (result, tabulation);

                if (idx < result - 1)
                {
                    console_putc (' ');
                }
            }
        }
    }
    else if (type == RESULT_CSTRING)
    {
        if (fip->argc >= 2)
        {
            format = get_argument_int (fip, 1);
        }

        if (fip->argc >= 3)
        {
            char    fmt[32];
            int     tabulation = get_argument_int (fip, 2);

            sprintf (fmt, "%%%ds", tabulation);
            fip->reti = console_printf (fmt, resultstr);
        }
        else
        {
            fip->reti = console_printf ("%s", resultstr);
        }
    }

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_console_println ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_console_println (FIP_RUN * fip)
{
    int     rtc;

    rtc = nici_console_print (fip);
    console_puts ("\r\n");
    fip->reti += 2;

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * STRING routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_string_substring ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_string_substring (FIP_RUN * fip)
{
    unsigned char * str = get_argument_string (fip, 0);
    int             pos = get_argument_int (fip, 1);
    int             slot;
    int             len;
    unsigned char * s;

    len = ustrlen (str);

    if (pos < 0)
    {
        pos = len + pos;                                        // pos < 0: position from right!
    }

    if (pos < len)
    {
        s = str + pos;

        if (fip->argc == 3)
        {
            int n = get_argument_int (fip, 2);

            len = ustrlen (s);

            if (n < 0)                                          // n < 0: cut n chars from end
            {
                n = len + n;
            }

            if (n < 0)
            {
                slot = new_tmp_stringslot ((unsigned char *) "");
            }
            else if (n < len)
            {
                unsigned char ch = s[n];
                s[n] = '\0';
                slot = new_tmp_stringslot (s);
                s[n] = ch;
            }
            else
            {
                slot = new_tmp_stringslot (s);
            }
        }
        else
        {
            slot = new_tmp_stringslot (s);
        }
    }
    else
    {
        slot = new_tmp_stringslot ((unsigned char *) "");
    }

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_string_tokens ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_string_tokens (FIP_RUN * fip)
{
    unsigned char * str             = get_argument_string (fip, 0);
    unsigned char * delim           = get_argument_string (fip, 1);
    int             lstr            = ustrlen (str);
    int             ldelim          = ustrlen (delim);
    int             cnt             = 0;
    int             idx;

    for (idx = 0; idx < lstr; idx++)
    {
        if (! ustrncmp (str + idx, delim, ldelim))
        {
            idx += ldelim - 1;
            cnt++;
        }
    }

    fip->reti = cnt + 1;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_string_get_token ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_string_get_token (FIP_RUN * fip)
{
    unsigned char * str             = get_argument_string (fip, 0);
    unsigned char * delim           = get_argument_string (fip, 1);
    int             pos             = get_argument_int (fip, 2);
    int             lstr            = ustrlen (str);
    int             ldelim          = ustrlen (delim);
    int             cnt             = 0;
    int             last_match_idx  = 0;
    int             idx;
    int             slot            = -1;

    for (idx = 0; idx < lstr; idx++)
    {
        if (! ustrncmp (str + idx, delim, ldelim))
        {
            if (cnt == pos)
            {
                unsigned char   tmp[idx - last_match_idx + 1];
                ustrncpy (tmp, str + last_match_idx, idx - last_match_idx);
                tmp[idx - last_match_idx] = '\0';
                slot = new_tmp_stringslot (tmp);
                break;
            }

            last_match_idx = idx + ldelim;
            idx += ldelim - 1;
            cnt++;
        }
    }

    if (idx == lstr && cnt == pos)
    {
        unsigned char   tmp[idx - last_match_idx + 1];
        ustrncpy (tmp, str + last_match_idx, idx - last_match_idx);
        tmp[idx - last_match_idx] = '\0';
        slot = new_tmp_stringslot (tmp);
    }
    else if (slot < 0)
    {
        slot = new_tmp_stringslot ((unsigned char *) "");
    }

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_int_tochar ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_int_tochar (FIP_RUN * fip)
{
    unsigned char   str[2];
    int             slot;
    int             ch = get_argument_int (fip, 0);

    str[0] = ch;
    str[1] = '\0';

    slot = new_tmp_stringslot (str);
    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_polar_to_x ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
nici_polar_to_x (FIP_RUN * fip)
{
    int     radius  = get_argument_int (fip, 0);
    int     angle   = get_argument_int (fip, 1);
    int     x;

    x = (double) radius * cos (((double) angle * 2 * M_PI) / 360.0) + 0.5;

    fip->reti = x;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_polar_to_y ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
nici_polar_to_y (FIP_RUN * fip)
{
    int     radius  = get_argument_int (fip, 0);
    int     angle   = get_argument_int (fip, 1);
    int     y;

    y = (double) (-radius) * sin (((double) angle * 2 * M_PI) / 360.0) + 0.5;

    fip->reti = y;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_string_length ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_string_length (FIP_RUN * fip)
{
    int             len = 0;
    unsigned char * str = get_argument_string (fip, 0);

    len = ustrlen (str);

    fip->reti = len;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * TIME routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MAX_ALARM_SLOTS     8
static int alarm_slots[MAX_ALARM_SLOTS];
static int alarm_start[MAX_ALARM_SLOTS];
static int alarm_functions[MAX_ALARM_SLOTS];
static int alarm_cnt[MAX_ALARM_SLOTS];
int alarm_slots_used = 0;

#if defined (unix) || defined (WIN32)

static unsigned long
get_millis (void)
{
    unsigned long msec;

#if defined (unix)

    struct timeval tv;

    if (gettimeofday(&tv, NULL) != 0)
    {
        return 0;
    }

    msec = (unsigned long) ((tv.tv_sec * 1000ul) + (tv.tv_usec / 1000ul));

#else // if defined (WIN32)

    SYSTEMTIME time;
    GetSystemTime(&time);
    msec = (time.wSecond * 1000) + time.wMilliseconds;

#endif
    return msec;
}

static unsigned long millis_start;

static void
start_millis (void)
{
    millis_start = get_millis ();
}

static unsigned long
stop_millis (void)
{
    unsigned long millis_stop;

    millis_stop = get_millis ();
    return millis_stop - millis_start;
}

#else // STM32

static uint32_t millis_start;

#endif // unix or win32

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_time_start ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_time_start (FIP_RUN * UNUSED(fip))
{
#if defined (unix) || defined (WIN32)
    start_millis ();
#else
    millis_start = milliseconds;
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_time_stop ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_time_stop (FIP_RUN * fip)
{
    int millis;

#if defined (unix) || defined (WIN32)
    millis = stop_millis ();
#else
    millis = milliseconds - millis_start;
#endif

    fip->reti = millis;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_time_delay ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_time_delay (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    unsigned int msec = (unsigned int) get_argument_int (fip, 0);

    start_millis ();

    while (stop_millis () < msec)
    {
        ;
    }
#else
    int msec = get_argument_int (fip, 0);
    delay_msec (msec);
#endif
    return FUNCTION_TYPE_VOID;
}

void
nici_alarm_reset_all (void)
{
    alarm_slots_used = 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_alarm_set ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_alarm_set (FIP_RUN * fip)
{
    int     slot = -1;

    if (alarm_slots_used < MAX_ALARM_SLOTS - 1)
    {
        int msec = get_argument_int (fip, 0);
        slot = alarm_slots_used;
        alarm_slots[slot] = msec;
#if defined (unix) || defined (WIN32)
        alarm_start[slot] = get_millis ();
#else
        alarm_start[slot] = alarm_millis;
#endif
        alarm_slots_used++;
    }

    if (fip->argc == 2)
    {
        alarm_functions[slot] = get_argument_int (fip, 1) + 1;
    }
    else
    {
        alarm_functions[slot] = 0;
    }

    fip->reti = slot;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * check_alarms ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
check_alarms (int slot)
{
    int rtc = 0;

    if (alarm_cnt[slot] > alarm_slots[slot])
    {
        alarm_cnt[slot] -= alarm_slots[slot];
#if defined (unix) || defined (WIN32)
        alarm_start[slot] = get_millis ();
#else
        alarm_start[slot] = alarm_millis;
#endif
        rtc = 1;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * update_alarm_timers ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
update_alarm_timers (void)
{
    int slot;

    if (alarm_slots_used)
    {
#if defined (unix) || defined (WIN32)
        unsigned long m = get_millis ();
#else
        unsigned long m = alarm_millis;
#endif

        for (slot = 0; slot < alarm_slots_used; slot++)
        {
            alarm_cnt[slot] = m - alarm_start[slot];

            if (alarm_functions[slot] > 0 && check_alarms (slot))           // execute only check_alarms(), if we have an alarm function in NIC script!
            {
                nici (alarm_functions[slot] - 1, (FIP_RUN *) NULL);
            }
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_alarm ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_alarm_check (FIP_RUN * fip)
{
    int     rtc     = 0;
    int     slot    = get_argument_int (fip, 0);

    if (slot < alarm_slots_used)
    {
        if (alarm_functions[slot] == 0)                             // execute only check_alarms(), if we have NOT an alarm function in NIC script!
        {
            rtc = check_alarms (slot);
        }
    }

    fip->reti = rtc;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * DATE routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_date_datetime ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_date_datetime (FIP_RUN * fip)
{
    int         slot;
    char        buf[32];

#if unix
    struct tm * tmp;
    time_t sec = time ((time_t *) 0);

    tmp = localtime (&sec);

    if (tmp)
    {
        sprintf (buf, "%d-%02d-%02d %02d:%02d:%02d", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday,
                 tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
        slot = new_tmp_stringslot ((unsigned char *) buf);
    }
#else
    struct tm   tm;

    if (stm32f4_rtc_get (&tm) == SUCCESS)
    {
        sprintf (buf, "%d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
        slot = new_tmp_stringslot ((unsigned char *) buf);
    }
#endif
    else
    {
        slot = new_tmp_stringslot ((unsigned char *) "");
    }

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_rtc_calibrate ()
 *
 *  Return values:
 *      ERROR   (0) - error
 *      SUCCESS (1) - success
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_rtc_calibrate (FIP_RUN * fip)
{
    int     pulses  = get_argument_int (fip, 0);
    int     period  = get_argument_int (fip, 1);

#if unix
    console_printf ("rtc_calibrate: pulses=%d period=%d\n", pulses, period);
    fip->reti = 1;
#else
    fip->reti = stm32f4_rtc_calibrate (pulses, period);
#endif
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * GPIO routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define INPUT_MODE      0
#define OUTPUT_MODE     1

#define IN_NOPULL       0
#define IN_PULLUP       1
#define IN_PULLDOWN     2

#define OUT_PUSHPULL    0
#define OUT_OPENDRAIN   1

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_gpio_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_gpio_init (FIP_RUN * fip)
{
    int port;
    int pin;
    int mode;
    int pull;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);
    mode    = get_argument_int (fip, 2);

    if (fip->argc == 4)
    {
        pull = get_argument_int (fip, 3);
    }
    else
    {
        pull = 0;
    }

#if defined (unix) || defined (WIN32)
    console_printf ("gpio_init: GPIO=%d PIN=%d MODE=%d PULL=%d\n", port, pin, mode, pull);
#else
    GPIO_InitTypeDef gpio;
    GPIO_StructInit (&gpio);
    gpio.GPIO_Pin   = 1 << pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    if (mode == OUTPUT_MODE)
    {
        if (pull == OUT_OPENDRAIN)
        {
            gpio.GPIO_Mode  = GPIO_Mode_OUT;
            gpio.GPIO_OType = GPIO_OType_OD;
            gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
        }
        else // (pull == OUT_PUSHPULL)                              // output push-pull
        {
            gpio.GPIO_Mode  = GPIO_Mode_OUT;
            gpio.GPIO_OType = GPIO_OType_PP;
            gpio.GPIO_PuPd  = GPIO_PuPd_NOPULL;
        }
    }
    else // mode == INPUT_MODE
    {
        if (pull == IN_PULLUP)                                      // input with internal pullup
        {
            gpio.GPIO_Mode = GPIO_Mode_IN;
            gpio.GPIO_PuPd = GPIO_PuPd_UP;
        }
        else if (pull == IN_PULLDOWN)                               // input with internal pulldown
        {
            gpio.GPIO_Mode = GPIO_Mode_IN;
            gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
        }
        else // if (pull == IN_NOPULL)                              // input floating
        {
            gpio.GPIO_Mode = GPIO_Mode_IN;
            gpio.GPIO_PuPd = GPIO_PuPd_NOPULL;
        }
    }

    RCC_AHB1PeriphClockCmd (1 << port, ENABLE);             // RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_TypeDef * portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    GPIO_Init(portp, &gpio);                                // GPIO_Init(GPIOA, &gpio);
#endif // unix

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_gpio_set ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_gpio_set (FIP_RUN * fip)
{
    int             port;
    int             pin;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);

#if defined (unix) || defined (WIN32)
    console_printf ("gpio_set: PORT=%d PIN=%d\n", port, pin);
#else
    int mask = 1 << pin;

    GPIO_TypeDef *  portp;
    portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    portp->BSRRL = mask;                                            // GPIOA->BSRRL = mask;
#endif // unix

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_gpio_reset ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_gpio_reset (FIP_RUN * fip)
{
    int             port;
    int             pin;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);

#if defined (unix) || defined (WIN32)
    console_printf ("gpio_reset: PORT=%d PIN=%d\n", port, pin);
#else
    int mask = 1 << pin;

    GPIO_TypeDef *  portp;
    portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    portp->BSRRH = mask;                                            // GPIOA->BSRRH = mask;
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_gpio_toggle ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_gpio_toggle (FIP_RUN * fip)
{
    int             port;
    int             pin;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);

#if defined (unix) || defined (WIN32)
    console_printf ("gpio_toggle: PORT=%d PIN=%d\n", port, pin);
#else
    int mask = 1 << pin;

    GPIO_TypeDef *  portp;
    portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    portp->ODR ^= mask;                                            // GPIOA->ODR ^= mask;
#endif // unix
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_gpio_get ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_gpio_get (FIP_RUN * fip)
{
    int rtc = 0;
#if defined (unix) || defined (WIN32)
    static int last_rtc;
#endif

    int             port;
    int             pin;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);

#if defined (unix) || defined (WIN32)
    console_printf ("gpio_get: PORT=%d PIN=%d\n", port, pin);
#else
    int mask = 1 << pin;

    GPIO_TypeDef *  portp;
    portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    rtc = GPIO_ReadInputDataBit(portp, mask);                           // rtc = GPIO_ReadInputDataBit(GPIOA, mask);
#endif // unix

#if defined (unix) || defined (WIN32)
    if (last_rtc)
    {
        last_rtc = 0;
    }
    else
    {
        last_rtc = 1;
    }

    rtc = last_rtc;
    fip->reti = rtc;
#else
    if (rtc == Bit_SET)
    {
        fip->reti = 1;
    }
    else
    {
        fip->reti = 0;
    }
#endif

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * BIT routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bit_set ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bit_set (FIP_RUN * fip)
{
    int     value = 0;

    int     bit;

    value   = get_argument_int (fip, 0);
    bit     = get_argument_int (fip, 1);

    value |= 1 << bit;

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bit_reset ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bit_reset (FIP_RUN * fip)
{
    int     value = 0;

    int     bit;

    value   = get_argument_int (fip, 0);
    bit     = get_argument_int (fip, 1);

    value &= ~(1 << bit);

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bit_toggle ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bit_toggle (FIP_RUN * fip)
{
    int     value = 0;
    int     bit;

    value   = get_argument_int (fip, 0);
    bit     = get_argument_int (fip, 1);

    value ^= (1 << bit);

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bit_isset ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bit_isset (FIP_RUN * fip)
{
    int     value = 0;
    int     bit;

    value   = get_argument_int (fip, 0);
    bit     = get_argument_int (fip, 1);

    if (value & (1 << bit))
    {
        value = 1;
    }
    else
    {
        value = 0;
    }

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * BITMASK routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_and ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_and (FIP_RUN * fip)
{
    int     value = 0;
    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = mask1 & mask2;

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_nand ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_nand (FIP_RUN * fip)
{
    int     value = 0;

    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = ~(mask1 & mask2);

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_or ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_or (FIP_RUN * fip)
{
    int     value = 0;
    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = mask1 | mask2;

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_nor ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_nor (FIP_RUN * fip)
{
    int     value = 0;
    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = ~(mask1 | mask2);

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_xor ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_xor (FIP_RUN * fip)
{
    int     value = 0;
    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = mask1 ^ mask2;

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_bitmask_xnor ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_bitmask_xnor (FIP_RUN * fip)
{
    int     value = 0;
    int     mask1;
    int     mask2;

    mask1 = get_argument_int (fip, 0);
    mask2 = get_argument_int (fip, 1);

    value   = ~(mask1 ^ mask2);

    fip->reti = value;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * UART routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_init (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    int     alternate   = get_argument_int (fip, 1);
    int     baud        = get_argument_int (fip, 2);

    uart_init (uart_number, alternate, baud);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_getc ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_getc (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    int     ch          = uart_getc (uart_number);
    fip->reti = ch;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_rxchars ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_rxchars (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    int     rxchars     = uart_get_rxsize (uart_number);
    fip->reti = rxchars;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_putc ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_putc (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    int     ch          = get_argument_int (fip, 1);

    uart_putc (uart_number, ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_print ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_print (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    char *  str         = (char *) get_argument_string (fip, 1);

    uart_puts (uart_number, str);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_uart_println ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_uart_println (FIP_RUN * fip)
{
    int     uart_number = get_argument_int (fip, 0);
    char * str = (char *) get_argument_string (fip, 1);

    uart_puts (uart_number, str);
    uart_putc (uart_number, '\r');
    uart_putc (uart_number, '\n');
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * WS2812 routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int n_leds;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_ws2812_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_ws2812_init (FIP_RUN * fip)
{
    if (fip->argc == 1)
    {
        n_leds = get_argument_int (fip, 0);
#if defined (unix) || defined (WIN32)
        console_printf ("ws2812_init: n_leds=%d\n", n_leds);
#else
        ws2812_init (n_leds);
#endif
    }

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_ws2812_set ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_ws2812_set (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    int         n = get_argument_int (fip, 0);
    int         r = get_argument_int (fip, 1);
    int         g = get_argument_int (fip, 2);
    int         b = get_argument_int (fip, 3);
    console_printf ("ws2812_set: n=%d r=%d g=%d b=%d\n", n, r, g, b);
#else
    WS2812_RGB  rgb;
    int         n;

    n           = get_argument_int (fip, 0);
    rgb.red     = get_argument_int (fip, 1);
    rgb.green   = get_argument_int (fip, 2);
    rgb.blue    = get_argument_int (fip, 3);

    if (n < n_leds)
    {
        ws2812_set_led (n, &rgb);
    }
    else
    {
        ws2812_set_all (&rgb, n_leds, 0);
    }
#endif // unix

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_ws2812_clear ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_ws2812_clear (FIP_RUN * fip)
{
    int n = get_argument_int (fip, 0);
#if defined (unix) || defined (WIN32)
    console_printf ("ws2812_clear: n=%d\n", n);
#else
    ws2812_clear_all (n);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_ws2812_refresh ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_ws2812_refresh (FIP_RUN * fip)
{
    int n = get_argument_int (fip, 0);
#if defined (unix) || defined (WIN32)
    console_printf ("ws2812_refresh: n=%d\n", n);
#else
    ws2812_refresh (n);
#endif

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * BUTTON routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MAX_BUTTONS                 8

#define BUTTON_STATE_NOT_PRESSED    0
#define BUTTON_STATE_PRESSED        1

volatile                            BUTTON buttons[MAX_BUTTONS];
volatile int                        buttons_used;

#define BUTTON_PULLUP               1
#define BUTTON_PULLDOWN             2
#define BUTTON_NOPULLUP             3
#define BUTTON_NOPULLDOWN           4

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_button_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_button_init (FIP_RUN * fip)
{
    int button;

    if (buttons_used >= MAX_BUTTONS)
    {
        fip->reti = 0;
        return FUNCTION_TYPE_INT;
    }

    int port;
    int pin;
    int mode;

    port    = get_argument_int (fip, 0);
    pin     = get_argument_int (fip, 1);
    mode    = get_argument_int (fip, 2);

    button = buttons_used;
    buttons_used++;

#if defined (unix) || defined (WIN32)
    console_printf ("button_init: GPIO=%d PIN=%d MODE=%d\n", port, pin, mode);
#else
    GPIO_InitTypeDef gpio;
    GPIO_StructInit (&gpio);
    gpio.GPIO_Pin   = 1 << pin;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;

    if (mode == BUTTON_PULLUP)                                  // input with internal pullup
    {
        gpio.GPIO_Mode = GPIO_Mode_IN;
        gpio.GPIO_PuPd = GPIO_PuPd_UP;
        buttons[button].active_low = 1;                         // active low
    }
    else if (mode == BUTTON_PULLDOWN)                           // input with internal pulldown
    {
        gpio.GPIO_Mode = GPIO_Mode_IN;
        gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
        buttons[button].active_low = 0;                         // active high
    }
    else if (mode == BUTTON_PULLUP)                             // input with external pullup
    {
        gpio.GPIO_Mode = GPIO_Mode_IN;
        gpio.GPIO_PuPd = GPIO_PuPd_UP;
        buttons[button].active_low = 1;                         // active low
    }
    else if (mode == BUTTON_PULLDOWN)                           // input with external pulldown
    {
        gpio.GPIO_Mode = GPIO_Mode_IN;
        gpio.GPIO_PuPd = GPIO_PuPd_DOWN;
        buttons[button].active_low = 0;                         // active high
    }

    RCC_AHB1PeriphClockCmd (1 << port, ENABLE);             // RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_TypeDef * portp = (GPIO_TypeDef *) (AHB1PERIPH_BASE + (port << 10));
    GPIO_Init(portp, &gpio);                                // GPIO_Init(GPIOA, &gpio);

    buttons[button].port    = port;
    buttons[button].pin     = pin;
#endif // unix
    fip->reti = button + 1;

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_button_pressed ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_button_pressed (FIP_RUN * fip)
{
    int     button  = get_argument_int (fip, 0);
    int     pressed = 0;

    if (button < buttons_used)
    {
#if defined (unix) || defined (WIN32)
        console_printf ("button_pressed: button=%d\n", button);
#endif // unix
        pressed = buttons[button].pressed;
    }

    fip->reti = pressed;
    return FUNCTION_TYPE_INT;
}

#define I2C1_CHANNEL        1
#define I2C2_CHANNEL        2
#define I2C3_CHANNEL        3

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * I2C routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_init (FIP_RUN * fip)
{
    int     channel     = get_argument_int (fip, 0);
    int     alt         = get_argument_int (fip, 1);
    int     clockspeed  = get_argument_int (fip, 2);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    i2c_init (i2c_channel, alt, clockspeed);

    fip->reti = TRUE;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_read ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_read (FIP_RUN * fip)
{
    int         channel                 = get_argument_int (fip, 0);
    int         addr                    = get_argument_int (fip, 1);
    uint8_t *   bufp                    = get_argument_byte_ptr (fip, 2);
    int         bytes                   = get_argument_int (fip, 3);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    i2c_read (i2c_channel, addr, bufp, bytes);

    fip->reti = TRUE;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_write ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_write (FIP_RUN * fip)
{
    int         channel                 = get_argument_int (fip, 0);
    int         addr                    = get_argument_int (fip, 1);
    uint8_t *   bufp                    = get_argument_byte_ptr (fip, 2);
    int         bytes                   = get_argument_int (fip, 3);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    i2c_write (i2c_channel, addr, bufp, bytes);
    fip->reti = TRUE;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * I2C LCD routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_init (FIP_RUN * fip)
{
    int     channel     = get_argument_int (fip, 0);
    int     alt         = get_argument_int (fip, 1);
    int     addr        = get_argument_int (fip, 2);
    int     lines       = get_argument_int (fip, 3);
    int     columns     = get_argument_int (fip, 4);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    fip->reti = i2c_lcd_init (i2c_channel, alt, addr, lines, columns);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_clear ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_clear (FIP_RUN * fip)
{
    fip->reti = i2c_lcd_clear ();
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_home ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_home (FIP_RUN * fip)
{
    fip->reti = i2c_lcd_home ();
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_move ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_move (FIP_RUN * fip)
{
    uint8_t     y   = get_argument_int (fip, 0);
    uint8_t     x   = get_argument_int (fip, 1);

    fip->reti = i2c_lcd_move (y, x);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_backlight ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_backlight (FIP_RUN * fip)
{
    int     on  = get_argument_int (fip, 0);

    fip->reti = i2c_lcd_backlight (on);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_define_char ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_define_char (FIP_RUN * fip)
{
    uint8_t     n_char  = get_argument_int (fip, 0);
    uint8_t *   data    = get_argument_byte_ptr (fip, 1);

    fip->reti = i2c_lcd_define_char (n_char, data);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_print ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_print (FIP_RUN * fip)
{
    int             result      = 0;
    unsigned char * resultstr   = (unsigned char *) NULL;
    int             type        = get_argument (fip, 0, &resultstr, &result);

    if (type == RESULT_INT)                                             // argument is integer
    {
        uint8_t ch = get_argument_int (fip, 0);
        fip->reti = i2c_lcd_putc (ch);
    }
    else                                                                // argument is string
    {
        unsigned char * str = get_argument_string (fip, 0);
        fip->reti = i2c_lcd_puts ((const char *) str);
    }

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_mvprint ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_mvprint (FIP_RUN * fip)
{
    uint8_t         y           = get_argument_int (fip, 0);
    uint8_t         x           = get_argument_int (fip, 1);
    int             result      = 0;
    unsigned char * resultstr   = (unsigned char *) NULL;
    int             type        = get_argument (fip, 2, &resultstr, &result);

    if (type == RESULT_INT)                                             // argument is integer
    {
        uint8_t ch = get_argument_int (fip, 2);

        if (i2c_lcd_move (y, x))
        {
            fip->reti = i2c_lcd_putc (ch);
        }
        else
        {
            fip->reti = FALSE;
        }
    }
    else                                                                // argument is string
    {
        unsigned char * str = get_argument_string (fip, 2);
        fip->reti = i2c_lcd_mvputs (y, x, (const char *) str);
    }

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_lcd_clrtoeol ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_lcd_clrtoeol (FIP_RUN * fip)
{
    fip->reti = i2c_lcd_clrtoeol ();
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * I2C DS3231 routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_ds3231_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_ds3231_init (FIP_RUN * fip)
{
    int     channel     = get_argument_int (fip, 0);
    int     alt         = get_argument_int (fip, 1);
    int     addr        = get_argument_int (fip, 2);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    fip->reti = i2c_ds3231_init (i2c_channel, alt, addr);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_ds3231_set_date_time ()
 *
 * Format:
 *  0123456789012345678
 *  YYYY-MM-DD hh:mm:ss
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_ds3231_set_date_time (FIP_RUN * fip)
{
    static struct tm    tm;
    unsigned char *     datetime    = get_argument_string (fip, 0);

    tm.tm_year  = u_atoi (datetime + 0) - 1900;
    tm.tm_mon   = u_atoi (datetime + 5) - 1;
    tm.tm_mday  = u_atoi (datetime + 8);
    tm.tm_hour  = u_atoi (datetime + 11);
    tm.tm_min   = u_atoi (datetime + 14);
    tm.tm_sec   = u_atoi (datetime + 17);
    tm.tm_wday  = dayofweek (tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);

    fip->reti = i2c_ds3231_set_date_time (&tm);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_ds3231_get_date_time ()
 *
 * Format:
 *  0123456789012345678
 *  YYYY-MM-DD hh:mm:ss
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_ds3231_get_date_time (FIP_RUN * fip)
{
    static struct tm    tm;
    unsigned char       buf[32];
    int                 slot;

    fip->reti = i2c_ds3231_get_date_time (&tm);

    if (fip->reti)
    {
        sprintf ((char *) buf, "%04d-%02d-%02d %02d:%02d:%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        slot = new_tmp_stringslot (buf);
    }
    else
    {
        slot = new_tmp_stringslot ((unsigned char *) "");
    }

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * I2C AT24C32 routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_at24c32_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_at24c32_init (FIP_RUN * fip)
{
    int     channel     = get_argument_int (fip, 0);
    int     alt         = get_argument_int (fip, 1);
    int     addr        = get_argument_int (fip, 2);

    I2C_TypeDef *   i2c_channel;

    switch (channel)
    {
        case I2C1_CHANNEL:
            i2c_channel = I2C1;
            break;
        case I2C2_CHANNEL:
            i2c_channel = I2C2;
            break;
        case I2C3_CHANNEL:
            i2c_channel = I2C3;
            break;
        default:
            fprintf (stderr, "invalid I2C channel\n");
            fip->reti = FALSE;
            return FUNCTION_TYPE_INT;
    }

    fip->reti = i2c_at24c32_init (i2c_channel, alt, addr);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_at24c32_write ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_at24c32_write (FIP_RUN * fip)
{
    uint_fast16_t   addr    = get_argument_int (fip, 0);
    uint8_t *       bufp    = get_argument_byte_ptr (fip, 1);
    uint_fast16_t   bytes   = get_argument_int (fip, 2);

    fip->reti = i2c_at24c32_write (addr, bufp, bytes);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_i2c_at24c32_read ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_i2c_at24c32_read (FIP_RUN * fip)
{
    uint_fast16_t   addr    = get_argument_int (fip, 0);
    uint8_t *       bufp    = get_argument_byte_ptr (fip, 1);
    uint_fast16_t   bytes   = get_argument_int (fip, 2);

    fip->reti = i2c_at24c32_read (addr, bufp, bytes);
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * FILE routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MAX_OPEN_FILES 8
static FILE * openfp[MAX_OPEN_FILES];

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_open ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_open (FIP_RUN * fip)
{
    unsigned char * fname   = get_argument_string (fip, 0);
    unsigned char * mode    = get_argument_string (fip, 1);
    FILE *          fp;
    int             idx;
    int             rtc     = -1;

    for (idx = 0; idx < MAX_OPEN_FILES; idx++)
    {
        if (openfp[idx] == (FILE *) 0)
        {
            break;
        }
    }

    if (idx < MAX_OPEN_FILES)
    {
        fp = fopen ((char *) fname, (char *) mode);

        if (fp)
        {
            openfp[idx] = fp;
            rtc = idx;
        }
    }

    fip->reti = rtc;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_getc ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_getc (FIP_RUN * fip)
{
    int hdl = get_argument_int (fip, 0);

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        int ch = fgetc (openfp[hdl]);

        fip->reti = ch;
    }
    else
    {
        fip->reti = 0x00;
    }

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_putc ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_putc (FIP_RUN * fip)
{
    int hdl = get_argument_int (fip, 0);
    int ch  = get_argument_int (fip, 1);

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        fputc (ch, openfp[hdl]);
    }

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_readln ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_readln (FIP_RUN * fip)
{
    int hdl = get_argument_int (fip, 0);
    int slot;

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        unsigned char buf[256];

        if (fgets ((char *) buf, 256, openfp[hdl]))
        {
            char * p = strchr ((char *) buf, '\r');

            if (p)
            {
                *p = '\0';
            }

            p = strchr ((char *) buf, '\n');

            if (p)
            {
                *p = '\0';
            }
            slot = new_tmp_stringslot (buf);
        }
        else
        {
            slot = new_tmp_stringslot ((unsigned char *) "");
        }
    }
    else
    {
        slot = new_tmp_stringslot ((unsigned char *) "");
    }

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_write ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_write (FIP_RUN * fip)
{
    int             hdl = get_argument_int (fip, 0);
    unsigned char * str = get_argument_string (fip, 1);

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        fputs ((char *) str, openfp[hdl]);
    }

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_writeln ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_writeln (FIP_RUN * fip)
{
    int             hdl = get_argument_int (fip, 0);
    unsigned char * str = get_argument_string (fip, 1);

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        fputs ((char *) str, openfp[hdl]);
        fputc ('\n', openfp[hdl]);
    }

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_eof ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_eof (FIP_RUN * fip)
{
    int     hdl = get_argument_int (fip, 0);
    int     rtc = 1;

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        rtc = feof (openfp[hdl]);
    }

    fip->reti = rtc;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_tell ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_tell (FIP_RUN * fip)
{
    int     hdl = get_argument_int (fip, 0);
    int     rtc = -1;

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        rtc = ftell (openfp[hdl]);
    }

    fip->reti = rtc;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_seek ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_seek (FIP_RUN * fip)
{
    int     hdl     = get_argument_int (fip, 0);
    int     offset  = get_argument_int (fip, 1);
    int     whence  = get_argument_int (fip, 2);
    int     rtc = -1;

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        rtc = fseek (openfp[hdl], offset, whence);
    }

    fip->reti = rtc;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_close ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_file_close (FIP_RUN * fip)
{
    int     hdl = get_argument_int (fip, 0);

    if (hdl >= 0 && hdl < MAX_OPEN_FILES && openfp[hdl])
    {
        fclose (openfp[hdl]);
        openfp[hdl] = (FILE *) NULL;
    }

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_file_close_all_open_files ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
nici_file_close_all_open_files (void)
{
    int idx;

    for (idx = 0; idx < MAX_OPEN_FILES; idx++)
    {
        if (openfp[idx])
        {
            fprintf (stderr, "file #%d automatically closed\n", idx);
            fclose (openfp[idx]);
            openfp[idx] = (FILE *) NULL;
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_initscr ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_initscr (FIP_RUN * UNUSED(fip))
{
    initscr ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_move ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_move (FIP_RUN * fip)
{
    int y;
    int x;

    y = get_argument_int (fip, 0);
    x = get_argument_int (fip, 1);
    move (y, x);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_attrset ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_attrset (FIP_RUN * fip)
{
    int attr;

    attr = get_argument_int (fip, 0);
    attrset (attr);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_addch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_addch (FIP_RUN * fip)
{
    int ch;

    ch = get_argument_int (fip, 0);
    addch (ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvaddch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvaddch (FIP_RUN * fip)
{
    int y;
    int x;
    int ch;

    y   = get_argument_int (fip, 0);
    x   = get_argument_int (fip, 1);
    ch  = get_argument_int (fip, 2);
    mvaddch (y, x, ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_addstr ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_addstr (FIP_RUN * fip)
{
    unsigned char * s;

    s =  get_argument_string (fip, 0);
    addstr ((char *) s);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvaddstr ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvaddstr (FIP_RUN * fip)
{
    int             y;
    int             x;
    unsigned char * s;

    y   = get_argument_int (fip, 0);
    x   = get_argument_int (fip, 1);
    s =  get_argument_string (fip, 0);

    mvaddstr (y, x, (char *) s);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_printw ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_printw (FIP_RUN * fip)
{
    unsigned char * fmt     = get_argument_string (fip, 0);

    addstr ((char *) fmt);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvprintw ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvprintw (FIP_RUN * fip)
{
    int             y       = get_argument_int (fip, 0);
    int             x       = get_argument_int (fip, 1);
    unsigned char * fmt     = get_argument_string (fip, 2);

    move (y, x);
    addstr ((char *) fmt);

    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_getnstr ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_getnstr (FIP_RUN * fip)
{
    unsigned char * str = get_argument_string (fip, 0);
    int             maxlen = get_argument_int (fip, 1);
    char            buf[maxlen];
    int             slot;

    if (*str && maxlen > 1)
    {
        ustrncpy (buf, str, maxlen - 1);
        buf[maxlen - 1] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }

    getnstr (buf, maxlen);
    slot = new_tmp_stringslot ((unsigned char *) buf);

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvgetnstr ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvgetnstr (FIP_RUN * fip)
{
    int             y           = get_argument_int (fip, 0);
    int             x           = get_argument_int (fip, 1);
    unsigned char * str         = get_argument_string (fip, 2);
    int             maxlen      = get_argument_int (fip, 3);
    char            buf[maxlen];
    int             slot;

    if (*str && maxlen > 1)
    {
        ustrncpy (buf, str, maxlen - 1);
        buf[maxlen - 1] = '\0';
    }
    else
    {
        buf[0] = '\0';
    }

    mvgetnstr (y, x, buf, maxlen);
    slot = new_tmp_stringslot ((unsigned char *) buf);

    fip->reti = slot;
    return FUNCTION_TYPE_STRING;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_setscrreg ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_setscrreg (FIP_RUN * fip)
{
    int     top     = get_argument_int (fip, 0);
    int     bottom  = get_argument_int (fip, 1);

    setscrreg (top, bottom);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_deleteln ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_deleteln (FIP_RUN * UNUSED(fip))
{
    deleteln ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_insertln ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_insertln (FIP_RUN * UNUSED(fip))
{
    insertln ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_scroll ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_scroll (FIP_RUN * UNUSED(fip))
{
    scroll ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_clear ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_clear (FIP_RUN * UNUSED(fip))
{
    clear ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_erase ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_erase (FIP_RUN * UNUSED(fip))
{
    erase ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_clrtobot ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_clrtobot (FIP_RUN * UNUSED(fip))
{
    clrtobot ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_clrtoeol ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_clrtoeol (FIP_RUN * UNUSED(fip))
{
    clrtoeol ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_delch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_delch (FIP_RUN * UNUSED(fip))
{
    delch ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvdelch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvdelch (FIP_RUN * fip)
{
    int     y       = get_argument_int (fip, 0);
    int     x       = get_argument_int (fip, 1);

    mvdelch (y, x);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_insch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_insch (FIP_RUN * fip)
{
    int     ch      = get_argument_int (fip, 0);

    insch (ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_mvinsch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_mvinsch (FIP_RUN * fip)
{
    int     y       = get_argument_int (fip, 0);
    int     x       = get_argument_int (fip, 1);
    int     ch      = get_argument_int (fip, 2);

    mvinsch (y, x, ch);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_nodelay ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_nodelay (FIP_RUN * fip)
{
    int     value   = get_argument_int (fip, 0);

    nodelay (value);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_halfdelay ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_halfdelay (FIP_RUN * fip)
{
    int     value   = get_argument_int (fip, 0);

    halfdelay (value);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_getch ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_getch (FIP_RUN * fip)
{
    int     ch;

    ch = getch ();

    fip->reti = ch;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_curs_set ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_curs_set (FIP_RUN * fip)
{
    int     value   = get_argument_int (fip, 0);

    curs_set (value);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_refresh ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_refresh (FIP_RUN * UNUSED(fip))
{
    refresh ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_endwin ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_endwin (FIP_RUN * UNUSED(fip))
{
    endwin ();
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_gety ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_gety (FIP_RUN * fip)
{
    int y;

    y = gety ();
    fip->reti = y;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_mcurses_getx ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_mcurses_getx (FIP_RUN * fip)
{
    int x;

    x = getx ();
    fip->reti = x;
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * TFT routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_init ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_init (FIP_RUN * fip)
{
    uint_fast8_t    flags = get_argument_int (fip, 0);
    tft_init (flags);
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_rgb64_to_color565 () - convert RGB64 to 16 bit value: 5 bits of red, 6 bits of green, 5 bits of blue
 *
 * bits:   15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * format: R5 R4 R3 R2 R1 G5 G4 G3 G2 G1 G0 B5 B4 B3 B2 B1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_rgb64_to_color565 (FIP_RUN * fip)
{
    int     r = get_argument_int (fip, 0);
    int     g = get_argument_int (fip, 1);
    int     b = get_argument_int (fip, 2);

#if defined (unix) || defined (WIN32)
    printf ("tft_rgb64_to_color565 (%d, %d, %d)\n", r, g, b);
    fip->reti = 0;
#else
    fip->reti = tft_rgb64_to_color565 (r, g, b);
#endif
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_rgb256_to_color565 () - convert RGB256 to 16 bit value: 5 bits of red, 6 bits of green, 5 bits of blue
 *
 * bits:   15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
 * format: R5 R4 R3 R2 R1 G5 G4 G3 G2 G1 G0 B5 B4 B3 B2 B1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_rgb256_to_color565 (FIP_RUN * fip)
{
    int     r = get_argument_int (fip, 0);
    int     g = get_argument_int (fip, 1);
    int     b = get_argument_int (fip, 2);

#if defined (unix) || defined (WIN32)
    printf ("tft_rgb256_to_color565 (%d, %d, %d)\n", r, g, b);
    fip->reti = 0;
#else
    fip->reti = tft_rgb256_to_color565 (r, g, b);
#endif
    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * tft_fadein_backlight ()  - fade in backlight
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_fadein_backlight (FIP_RUN * fip)
{
    uint32_t delay_ms = get_argument_int (fip, 0);

#if defined (unix) || defined (WIN32)
    printf ("tft_fadein_backlight (%d)\n", delay_ms);
#else
    tft_fadein_backlight (delay_ms);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_fadeout_backlight () - fade out backlight
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_fadeout_backlight (FIP_RUN * fip)
{
    uint32_t delay_ms = get_argument_int (fip, 0);

#if defined (unix) || defined (WIN32)
    printf ("tft_fadeout_backlight (%d)\n", delay_ms);
#else
    tft_fadeout_backlight (delay_ms);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_pixel () - draw pixel
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_pixel (FIP_RUN * fip)
{
    uint_fast16_t   x           = get_argument_int (fip, 0);
    uint_fast16_t   y           = get_argument_int (fip, 1);
    uint_fast16_t   color565    = get_argument_int (fip, 3);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_pixel (%d, %d, 0x%04x)\n", x, y, color565);
#else
    tft_draw_pixel (x, y, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_horizontal_line () - draw horizontal line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_horizontal_line (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   len         = get_argument_int (fip, 2);
    uint_fast16_t   color565    = get_argument_int (fip, 3);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_horizontal_line (%d, %d, %d, 0x%04x)\n", x0, y0, len, color565);
#else
    tft_draw_horizontal_line (x0, y0, len, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_vertical_line () - draw vertical line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_vertical_line (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   height      = get_argument_int (fip, 2);
    uint_fast16_t   color565    = get_argument_int (fip, 3);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_vertical_line (%d, %d, %d, 0x%04x)\n", x0, y0, height, color565);
#else
    tft_draw_vertical_line (x0, y0, height, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_rectangle  () - draw rectangle
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_rectangle (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   x1          = get_argument_int (fip, 2);
    uint_fast16_t   y1          = get_argument_int (fip, 3);
    uint_fast16_t   color565    = get_argument_int (fip, 4);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_rectangle (%d, %d, %d, %d, 0x%04x)\n", x0, x1, y0, y1, color565);
#else
    tft_draw_rectangle (x0, x1, y0, y1, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_fill_rectangle () - fill rectangle
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_fill_rectangle (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   x1          = get_argument_int (fip, 2);
    uint_fast16_t   y1          = get_argument_int (fip, 3);
    uint_fast16_t   color565    = get_argument_int (fip, 4);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_rectangle (%d, %d, %d, %d, 0x%04x)\n", x0, x1, y0, y1, color565);
#else
    tft_fill_rectangle (x0, x1, y0, y1, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_fill_screen () - clear total screen
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_fill_screen (FIP_RUN * fip)
{
    uint_fast16_t   color565    = get_argument_int (fip, 0);

#if defined (unix) || defined (WIN32)
    printf ("tft_fill_screen (0x%04x)\n", color565);
#else
    tft_fill_screen (color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_line () - draw line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_line (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   x1          = get_argument_int (fip, 2);
    uint_fast16_t   y1          = get_argument_int (fip, 3);
    uint_fast16_t   color565    = get_argument_int (fip, 4);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_line (%3d, %3d, %3d, %3d, 0x%04x)\n", x0, y0, x1, y1, color565);
#else
    tft_draw_line (x0, y0, x1, y1, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_line () - draw line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_thick_line (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   x1          = get_argument_int (fip, 2);
    uint_fast16_t   y1          = get_argument_int (fip, 3);
    uint_fast16_t   color565    = get_argument_int (fip, 4);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_thick_line (%3d, %3d, %3d, %3d, 0x%04x)\n", x0, y0, x1, y1, color565);
#else
    tft_draw_thick_line (x0, y0, x1, y1, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_circle () - draw circle
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_circle (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   radius      = get_argument_int (fip, 2);
    uint_fast16_t   color565    = get_argument_int (fip, 3);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_circle (%3d, %3d, %3d, 0x%04x)\n", x0, y0, radius, color565);
#else
    tft_draw_circle (x0, y0, radius, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_thick_circle () - draw circle
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_thick_circle (FIP_RUN * fip)
{
    uint_fast16_t   x0          = get_argument_int (fip, 0);
    uint_fast16_t   y0          = get_argument_int (fip, 1);
    uint_fast16_t   radius      = get_argument_int (fip, 2);
    uint_fast16_t   color565    = get_argument_int (fip, 3);

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_thick_circle (%3d, %3d, %3d, 0x%04x)\n", x0, y0, radius, color565);
#else
    tft_draw_thick_circle (x0, y0, radius, color565);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_image () - draw an image
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_image (FIP_RUN * fip)
{
    uint_fast16_t   x           = get_argument_int (fip, 0);
    uint_fast16_t   y           = get_argument_int (fip, 1);
    uint_fast16_t   l           = get_argument_int (fip, 2);
    uint_fast16_t   h           = get_argument_int (fip, 3);
    unsigned char * imagefile   = get_argument_string (fip, 4);

    uint16_t *      image       = (uint16_t *) imagefile;           // TODO: open file, read image

#if defined (unix) || defined (WIN32)
    printf ("tft_draw_image (%d, %d, %d, %d, %ld)\n", x, y, l, h, (long) image);
#else
    tft_draw_image (x, y, l, h, image);
#endif
    return FUNCTION_TYPE_VOID;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_set_font () - set font
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_set_font (FIP_RUN * fip)
{
    uint_fast16_t   font        = get_argument_int (fip, 0);

    set_font (font);
    return FUNCTION_TYPE_VOID;
}

void
tft_reset_font (void)
{
    set_font (0);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_fonts () - get number of font
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_fonts (FIP_RUN * fip)
{
    fip->reti = number_of_fonts ();
    return FUNCTION_TYPE_INT;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_font_height () - get font height
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_font_height (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    printf ("nici_tft_font_height ()\n");
#endif

    fip->reti = font_height ();

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_font_width () - get font width
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_font_width (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    printf ("nici_tft_font_width ()\n");
#endif

    fip->reti = font_width ();

    return FUNCTION_TYPE_INT;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nici_tft_draw_string () - draw a string
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
nici_tft_draw_string (FIP_RUN * fip)
{
    uint_fast16_t   x           = get_argument_int (fip, 0);
    uint_fast16_t   y           = get_argument_int (fip, 1);
    unsigned char * s           = get_argument_string (fip, 2);
    uint_fast16_t   fcolor565   = get_argument_int (fip, 3);
    uint_fast16_t   bcolor565   = get_argument_int (fip, 4);

#if defined (unix) || defined (WIN32)
    printf ("draw_string (%d, %d, \"%s\", 0x%04x, 0x%04x)\n", x, y, s, fcolor565, bcolor565);
#else
    draw_string (s, y, x, fcolor565, bcolor565);
#endif
    return FUNCTION_TYPE_VOID;
}

static int
flash_device_id (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    fputs ("flash.device_id()\n", stdout);
    fip->reti = 0;
#else
    fip->reti = w25qxx_device_id ();
#endif
    return FUNCTION_TYPE_INT;
}

static int
flash_statusreg1 (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    fputs ("flash.statusreg1()\n", stdout);
    fip->reti = 0;
#else
    fip->reti = w25qxx_statusreg1 ();
#endif
    return FUNCTION_TYPE_INT;
}

static int
flash_statusreg2 (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    fputs ("flash.statusreg2()\n", stdout);
    fip->reti = 0;
#else
    fip->reti = w25qxx_statusreg2 ();
#endif
    return FUNCTION_TYPE_INT;
}

static int
flash_unique_id (FIP_RUN * fip)
{
#if defined (unix) || defined (WIN32)
    fputs ("flash.unique_id()\n", stdout);
    fip->reti = new_tmp_stringslot ((unsigned char *) "4711");
#else
    fip->reti = new_tmp_stringslot ((unsigned char *) w25qxx_unique_id ());
#endif
    return FUNCTION_TYPE_STRING;
}

#define DEFINE_FUNCTIONS    1
#include "funclist.h"                                           // should be at least line
