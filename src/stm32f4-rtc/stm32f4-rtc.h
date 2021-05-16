/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * stm32f4-rtc.h - STM32F4 RTC functions
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2018-2021 Frank Meyer - frank(at)fli4l.de
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
#ifndef STM32F4_RTC_H
#define STM32F4_RTC_H

#include <time.h>
#include "stm32f4xx.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_rtc.h"
#include "stm32f4xx_exti.h"
#include "misc.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * possible values of wakeup interval
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define RTC_WAKEUP_STOP         0                                   // disable wakeup interrupt
#define RTC_WAKEUP_30s          1                                   // timer interval = 30sec
#define RTC_WAKEUP_10s          2                                   // timer interval = 10sec
#define RTC_WAKEUP_5s           3                                   // timer interval = 5sec
#define RTC_WAKEUP_1s           4                                   // timer interval = 1sec
#define RTC_WAKEUP_500ms        5                                   // timer interval = 500msec
#define RTC_WAKEUP_250ms        6                                   // timer interval = 250msec
#define RTC_WAKEUP_125ms        7                                   // timer interval = 125msec

extern uint_fast8_t             stm32f4_wakeup_alarm;               // flag set if wakeup alarm occured

extern ErrorStatus              stm32f4_rtc_init (void);
extern ErrorStatus              stm32f4_rtc_set (struct tm *);
extern ErrorStatus              stm32f4_rtc_get (struct tm *);
extern ErrorStatus              stm32f4_rtc_calibrate (int, unsigned int);
extern ErrorStatus              stm32f4_rtc_set_wakeup (uint_fast8_t);

#endif // STM32F4_RTC_H
