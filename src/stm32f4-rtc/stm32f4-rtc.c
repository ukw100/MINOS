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
#include "stm32f4-rtc.h"
#include "base.h"

#define  RTC_STATUS_REG         RTC_BKP_DR0                                                         // use backup register as status register
#define  RTC_STATUS_CONFIGURED  0x0613                                                              // magic: RTC is already configured

uint_fast8_t                    stm32f4_wakeup_alarm;                                               // flag set if wakeup alarm occured

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * configure RTC the first time
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
rtc_config (void)
{
    RCC_LSEConfig (RCC_LSE_ON);                                                                     // set clock to LSE (extern)

    while (RCC_GetFlagStatus (RCC_FLAG_LSERDY) == RESET)
    {
        ;
    }

    RCC_RTCCLKConfig (RCC_RTCCLKSource_LSE);                                                        // LSE clock enable
    RCC_RTCCLKCmd (ENABLE);                                                                         // enable RTC
    RTC_WaitForSynchro ();                                                                          // wait until synchronized
    RTC_WriteBackupRegister (RTC_STATUS_REG, RTC_STATUS_CONFIGURED);                                // store status in status register
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * init RTC
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ErrorStatus
stm32f4_rtc_init (void)
{
    ErrorStatus     error = SUCCESS;
    uint32_t        status;

    RCC_APB1PeriphClockCmd (RCC_APB1Periph_PWR, ENABLE);                                            // PWR Clock enable: do enable this only once!
    PWR_BackupAccessCmd (ENABLE);                                                                   // enable access to RTC Backup register

    status = RTC_ReadBackupRegister (RTC_STATUS_REG);                                               // get value form status register

    if (status != RTC_STATUS_CONFIGURED)                                                            // rtc already configured?
    {                                                                                               // no....
        struct tm   rtc;

        rtc_config ();

        rtc.tm_hour     = 0;                                                                        // reset RTC to 2000-01-01 00:00:00
        rtc.tm_min      = 0;
        rtc.tm_sec      = 0;
        rtc.tm_mday     = 1;
        rtc.tm_mon      = 0;
        rtc.tm_year     = 0;
        rtc.tm_wday     = dayofweek (rtc.tm_mday, rtc.tm_mon + 1, rtc.tm_year + 1900);

        if (stm32f4_rtc_set (&rtc) == ERROR)
        {
            error = ERROR;
        }
    }
    else
    {                                                                                               // RTC is already configured...
        RTC_WaitForSynchro ();
        RTC_ClearITPendingBit (RTC_IT_WUT);
        EXTI_ClearITPendingBit (EXTI_Line22);
    }

    return error;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * set date/time
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ErrorStatus
stm32f4_rtc_set (struct tm * tmp)
{
    RTC_TimeTypeDef         rtc_time;
    RTC_DateTypeDef         rtc_date;
    RTC_InitTypeDef         rtcinit;

    RTC_TimeStructInit (&rtc_time);
    RTC_DateStructInit (&rtc_date);

    rtcinit.RTC_AsynchPrediv    = 0x7F;                                 // configure the RTC data register and RTC prescaler
    rtcinit.RTC_SynchPrediv     = 0xFF;                                 // ck_spre(1Hz) = RTCCLK(LSI) / (AsynchPrediv + 1) * (SynchPrediv + 1)
    rtcinit.RTC_HourFormat      = RTC_HourFormat_24;
    RTC_Init (&rtcinit);

    rtc_time.RTC_Hours          = tmp->tm_hour;
    rtc_time.RTC_Minutes        = tmp->tm_min;
    rtc_time.RTC_Seconds        = tmp->tm_sec;
    rtc_date.RTC_Date           = tmp->tm_mday;
    rtc_date.RTC_Month          = tmp->tm_mon + 1;
    rtc_date.RTC_Year           = tmp->tm_year - 100;
    rtc_date.RTC_WeekDay        = (tmp->tm_wday == 0) ? 7 : tmp->tm_wday;                           // tm_wday 0...6, RTC_WDay: 1...7 (1 = monday)

    if (RTC_SetTime (RTC_Format_BIN, &rtc_time) == ERROR ||
        RTC_SetDate (RTC_Format_BIN, &rtc_date) == ERROR)
    {
        return ERROR;
    }

    return SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * get date/time
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ErrorStatus
stm32f4_rtc_get (struct tm * tmp)
{
    RTC_TimeTypeDef         rtc_time;
    RTC_DateTypeDef         rtc_date;

    RTC_GetTime (RTC_Format_BIN, &rtc_time);
    RTC_GetDate (RTC_Format_BIN, &rtc_date);

    tmp->tm_hour    = rtc_time.RTC_Hours;
    tmp->tm_min     = rtc_time.RTC_Minutes;
    tmp->tm_sec     = rtc_time.RTC_Seconds;
    tmp->tm_mday    = rtc_date.RTC_Date;
    tmp->tm_mon     = rtc_date.RTC_Month - 1;
    tmp->tm_year    = rtc_date.RTC_Year + 100;
    tmp->tm_wday    = (rtc_date.RTC_WeekDay == 7) ? 0 : rtc_date.RTC_WeekDay;                       // tm_wday 0...6, RTC_WDay: 1...7 (1 = monday)

    return SUCCESS;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * stm32f4_rtc_calibrate () - smooth calibration
 *
 *  parameters:
 *      pulses: pulses to add/mask within given period (positive: add, negative: mask)
 *      period: period, may be 8, 16, or 32
 *
 *  example 1:
 *      clock is 8 seconds too fast per day:
 *          pulses to mask per second:              pulses   = ((86400 + 8) / 86400) * 32768 - 32768    - this is  3.034
 *          pulses to mask per 32 second period:    pulses32 = pulses * 32                              - this is 97.090
 *          call:                                   stm32f4_rtc_calibrate (-97, 32)
 *
 *  example 2:
 *      clock is 1 second too fast per day:
 *          pulses to mask per second:              pulses   = ((86400 + 1) / 86400) * 32768 - 32768    - this is  0.379259259259
 *          pulses to mask per 32 second period:    pulses32 = pulses * 32                              - this is 12.136296296296
 *          call:                                   stm32f4_rtc_calibrate (-49, 32)
 *
 *  example 3:
 *      clock is N seconds too fast per day:        stm32f4_rtc_calibrate (-(N * 12136) / 1000, 32)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ErrorStatus
stm32f4_rtc_calibrate (int pulses, unsigned int period)
{
    uint32_t        rtc_period  = RTC_SmoothCalibPeriod_32sec;
    ErrorStatus     rtc         = ERROR;

    switch (period)
    {
        case  8:    rtc_period = RTC_SmoothCalibPeriod_8sec;        break;
        case 16:    rtc_period = RTC_SmoothCalibPeriod_16sec;       break;
        case 32:    rtc_period = RTC_SmoothCalibPeriod_32sec;       break;
        default:    return ERROR;
    }

    if (pulses >= 0)                                                                                // add N pulses in interval i (8/16/32 sec)
    {
        if (pulses < 512)                                                                           // max value is 511
        {
            rtc = RTC_SmoothCalibConfig (rtc_period, RTC_SmoothCalibPlusPulses_Set, pulses);
        }
    }
    else
    {                                                                                               // mask N pulses in interval i (8/16/32 sec)
        pulses = -pulses;

        if (pulses < 512)                                                                           // max value is 511
        {
            rtc = RTC_SmoothCalibConfig (rtc_period, RTC_SmoothCalibPlusPulses_Reset, pulses);
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * set wakeup interval
 *
 * Possible values:
 *
 *  RTC_WAKEUP_STOP     disable wakeup
 *  RTC_WAKEUP_30s       30 sec
 *  RTC_WAKEUP_10s       10 sec
 *  RTC_WAKEUP_5s         5 sec
 *  RTC_WAKEUP_1s         1 sec
 *  RTC_WAKEUP_500ms    500 msec
 *  RTC_WAKEUP_250ms    250 msec
 *  RTC_WAKEUP_125ms    125 msec
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ErrorStatus
stm32f4_rtc_set_wakeup (uint_fast8_t interval)
{
    NVIC_InitTypeDef    NVIC_InitStructure;
    EXTI_InitTypeDef    EXTI_InitStructure;

    if (interval == RTC_WAKEUP_STOP)
    {                                                                                               // disable wakeup interrupt
        RTC_WakeUpCmd (DISABLE);
        RTC_ITConfig (RTC_IT_WUT, DISABLE);                                                         // disable Interrupt

        NVIC_InitStructure.NVIC_IRQChannel                      = RTC_WKUP_IRQn;                    // NVIC disable
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority    = 1;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority           = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd                   = DISABLE;
        NVIC_Init (&NVIC_InitStructure);

        EXTI_ClearITPendingBit (EXTI_Line22);                                                       // ext Interrupt 22 disable
        EXTI_InitStructure.EXTI_Line                            = EXTI_Line22;
        EXTI_InitStructure.EXTI_Mode                            = EXTI_Mode_Interrupt;
        EXTI_InitStructure.EXTI_Trigger                         = EXTI_Trigger_Rising;
        EXTI_InitStructure.EXTI_LineCmd                         = DISABLE;
        EXTI_Init (&EXTI_InitStructure);
    }
    else
    {                                                                                               // enable wakeup interrupt
        uint32_t    wakeup_time;

        NVIC_InitStructure.NVIC_IRQChannel                      = RTC_WKUP_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority    = 1;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority           = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd                   = ENABLE;
        NVIC_Init (&NVIC_InitStructure);

        EXTI_ClearITPendingBit (EXTI_Line22);                                                       // set ext interrupt 22 (for wakeup)
        EXTI_InitStructure.EXTI_Line                            = EXTI_Line22;
        EXTI_InitStructure.EXTI_Mode                            = EXTI_Mode_Interrupt;
        EXTI_InitStructure.EXTI_Trigger                         = EXTI_Trigger_Rising;
        EXTI_InitStructure.EXTI_LineCmd                         = ENABLE;
        EXTI_Init (&EXTI_InitStructure);

        RTC_WakeUpCmd (DISABLE);                                                                    // disable wakeup here

        switch (interval)
        {
            case RTC_WAKEUP_30s:        wakeup_time = (30 * 2048) - 1;  break;
            case RTC_WAKEUP_10s:        wakeup_time = (10 * 2048) - 1;  break;
            case RTC_WAKEUP_5s:         wakeup_time = ( 5 * 2048) - 1;  break;
            case RTC_WAKEUP_1s:         wakeup_time = ( 1 * 2048) - 1;  break;
            case RTC_WAKEUP_500ms:      wakeup_time = (     1024) - 1;  break;
            case RTC_WAKEUP_250ms:      wakeup_time = (      512) - 1;  break;
            case RTC_WAKEUP_125ms:      wakeup_time = (      256) - 1;  break;
            default:                    return ERROR;
        }

        RTC_WakeUpClockConfig (RTC_WakeUpClock_RTCCLK_Div16);                                       // prescaler 16: 32,768kHz / 16 = 2048 Hz
        RTC_SetWakeUpCounter (wakeup_time);                                                         // set wakeUp counter
        RTC_ITConfig (RTC_IT_WUT, ENABLE);                                                          // enable interrupt
        RTC_WakeUpCmd (ENABLE);                                                                     // enable wakeup
    }

    return SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * rtc wakeup interrupt handler
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
RTC_WKUP_IRQHandler (void)
{
    if (RTC_GetITStatus (RTC_IT_WUT) != RESET)
    {
        RTC_ClearITPendingBit (RTC_IT_WUT);
        EXTI_ClearITPendingBit (EXTI_Line22);

        stm32f4_wakeup_alarm = 1;                                                                   // set wakeup alarm flag
    }
}
