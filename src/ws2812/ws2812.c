/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ws2812.c - WS2812 driver
 *
 * Timings:
 *          WS2812          WS2812B         Tolerance       Common symmetric(!) values
 *   T0H    350 ns          400 ns          +/- 150 ns      470 ns
 *   T1H    700 ns          800 ns          +/- 150 ns      800 ns
 *   T0L    800 ns          850 ns          +/- 150 ns      800 ns
 *   T1L    600 ns          450 ns          +/- 150 ns      470 ns
 *
 * WS2812 format : (8G 8R 8B)
 *   24bit per LED  (24 * 1.25 = 30us per LED)
 *    8bit per color (MSB first)
 *
 * After each frame of n LEDs there has to be a pause of >= 50us
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
#include <stdlib.h>

#include "ws2812.h"
#include "delay.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * timer calculation:
 *
 *  freq = WS2812_TIM_CLK / (WS2812_TIM_PRESCALER + 1) / (WS2812_TIM_PERIOD + 1)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#if defined (STM32F401RE)                                       // STM32F401 Nucleo Board
#define WS2812_TIM_CLK              84L                         // 84 MHz = 11.90ns
#elif defined (STM32F411RE)                                     // STM32F411 Nucleo Board
#define WS2812_TIM_CLK              100L                        // 100 MHz = 10.00ns
#elif defined (STM32F407VE)                                     // STM32F407VE Black board
#define WS2812_TIM_CLK              84L                         // 72 MHz = 13.89ns
#else
#error STM32 unknown
#endif

#define WS2812_BIT_PER_LED          24                          // 3 * 8bit per LED, each bit costs 1.270us time
#define WS2812_TIM_PERIOD_TIME      1270                        // 1270 nsec
#define WS2812_TIM_PRESCALER        0                           // no prescaler
#define WS2812_T0H_TIME             470                         // 470ns
#define WS2812_T1H_TIME             800                         // 800ns
#define WS2812_T0L_TIME             800                         // 800ns
#define WS2812_T1L_TIME             470                         // 470ns
// #define WS2812_PAUSE_TIME        50000                       // pause, should be longer than 50us
#define WS2812_PAUSE_TIME           300000                      // WS2812S (special chinese version) need a pause longer than 280us

#define WS2812_TIM_PERIOD_FLOAT     (((WS2812_TIM_CLK / (1 + WS2812_TIM_PRESCALER)) * WS2812_TIM_PERIOD_TIME) / 1000.0 - 1.0)   // 106,68 @84MHz
#define WS2812_TIM_PERIOD           (uint16_t) (WS2812_TIM_PERIOD_FLOAT + 0.5)                                                  // 107
#define WS2812_T0H                  (uint16_t) ((WS2812_TIM_PERIOD_FLOAT * WS2812_T0H_TIME) / WS2812_TIM_PERIOD_TIME + 0.5)     //  39
#define WS2812_T1H                  (uint16_t) ((WS2812_TIM_PERIOD_FLOAT * WS2812_T1H_TIME) / WS2812_TIM_PERIOD_TIME + 0.5)     //  67
#define WS2812_T0L                  (uint16_t) ((WS2812_TIM_PERIOD_FLOAT * WS2812_T0L_TIME) / WS2812_TIM_PERIOD_TIME + 0.5)     //  67
#define WS2812_T1L                  (uint16_t) ((WS2812_TIM_PERIOD_FLOAT * WS2812_T1L_TIME) / WS2812_TIM_PERIOD_TIME + 0.5)     //  39
#define WS2812_PAUSE_LEN            (uint16_t) (WS2812_PAUSE_TIME / WS2812_TIM_PERIOD_TIME + 1)                                 //  40

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * DMA buffer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast16_t                ws2812_max_leds;

static volatile uint_fast16_t       current_dma_buf_pos;
static volatile uint_fast16_t       current_led_offset;
static volatile uint_fast16_t       current_data_pause_len;
static volatile uint_fast16_t       current_leds;

#define DATA_LEN(n)                 ((n) * WS2812_BIT_PER_LED)                                  // number of total bytes to transfer data
#define PAUSE_LEN                   (WS2812_PAUSE_LEN)                                          // number of total bytes to transfer pause
#define DMA_BUF_LEN                 (2 * WS2812_BIT_PER_LED)                                    // DMA buffer length: 2 LEDs

static volatile uint32_t            ws2812_dma_status;                                          // DMA status
static volatile WS2812_RGB *        rgb_buf[2];                                                 // RGB values
static volatile uint_fast8_t        current_rgb_buf_idx;                                        // current rgb buffer index
static uint_fast8_t                 next_rgb_buf_idx;                                           // next rgb buffer index
typedef uint16_t                    DMA_BUFFER_TYPE;                                            // 16bit DMA buffer, must be aligned to 16 bit
static volatile DMA_BUFFER_TYPE     dma_buf[DMA_BUF_LEN];                                       // DMA buffer

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * Timer for data: TIM3 for STM32F4xx
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
// Timer:
#define WS2812_TIM_CLOCK_CMD          RCC_APB1PeriphClockCmd
#define WS2812_TIM_CLOCK              RCC_APB1Periph_TIM3
#define WS2812_TIM                    TIM3
#define WS2812_TIM_AF                 GPIO_AF_TIM3
#define WS2812_TIM_CCR_REG1           TIM3->CCR1
#define WS2812_TIM_DMA_TRG1           TIM_DMA_CC1
// GPIO:
#define WS2812_GPIO_CLOCK_CMD         RCC_AHB1PeriphClockCmd
#define WS2812_GPIO_CLOCK             RCC_AHB1Periph_GPIOC
#define WS2812_GPIO_PORT              GPIOC
#define WS2812_GPIO_PIN               GPIO_Pin_6
#define WS2812_GPIO_SOURCE            GPIO_PinSource6
// DMA TIM3 - DMA1, Channel5, Stream4
#define WS2812_DMA_CLOCK_CMD          RCC_AHB1PeriphClockCmd
#define WS2812_DMA_CLOCK              RCC_AHB1Periph_DMA1
#define WS2812_DMA_STREAM             DMA1_Stream4
#define WS2812_DMA_CHANNEL            DMA_Channel_5
// transfer complete interrupt - DMA1, Stream4
#define WS2812_DMA_CHANNEL_IRQn       DMA1_Stream4_IRQn
#define WS2812_DMA_CHANNEL_ISR        DMA1_Stream4_IRQHandler
#define WS2812_DMA_CHANNEL_IRQ_TC     DMA_IT_TCIF4                    // transfer complete interrupt
#define WS2812_DMA_CHANNEL_IRQ_HT     DMA_IT_HTIF4                    // half-transfer interrupt

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: initialize DMA
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
ws2812_dma_init (void)
{
    DMA_InitTypeDef         dma;
    DMA_StructInit (&dma);

    DMA_Cmd(WS2812_DMA_STREAM, DISABLE);
    DMA_DeInit(WS2812_DMA_STREAM);

    dma.DMA_Mode                = DMA_Mode_Circular;
    dma.DMA_PeripheralBaseAddr  = (uint32_t) &WS2812_TIM_CCR_REG1;
    dma.DMA_PeripheralDataSize  = DMA_PeripheralDataSize_HalfWord;          // 16bit
    dma.DMA_MemoryDataSize      = DMA_MemoryDataSize_HalfWord;              // 16bit
    dma.DMA_BufferSize          = DMA_BUF_LEN;
    dma.DMA_PeripheralInc       = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc           = DMA_MemoryInc_Enable;
    dma.DMA_Priority            = DMA_Priority_VeryHigh;                    // DMA_Priority_High;

    dma.DMA_DIR                 = DMA_DIR_MemoryToPeripheral;
    dma.DMA_Channel             = WS2812_DMA_CHANNEL;
    dma.DMA_Memory0BaseAddr     = (uint32_t)dma_buf;
    dma.DMA_FIFOMode            = DMA_FIFOMode_Disable;
    dma.DMA_FIFOThreshold       = DMA_FIFOThreshold_HalfFull;
    dma.DMA_MemoryBurst         = DMA_MemoryBurst_Single;
    dma.DMA_PeripheralBurst     = DMA_PeripheralBurst_Single;

    DMA_Init(WS2812_DMA_STREAM, &dma);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * start DMA & timer (stopped when Transfer-Complete-Interrupt arrives)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
ws2812_dma_start (void)
{
    ws2812_dma_status = 1;                                                          // set status to "busy"

    TIM_Cmd (WS2812_TIM, DISABLE);                                                  // disable timer
    DMA_Cmd (WS2812_DMA_STREAM, DISABLE);                                           // disable DMA
    DMA_SetCurrDataCounter(WS2812_DMA_STREAM, DMA_BUF_LEN);                   // set counter to data len
    DMA_ITConfig(WS2812_DMA_STREAM, DMA_IT_TC | DMA_IT_HT, ENABLE);                 // enable transfer complete and half transfer interrupt
    DMA_Cmd (WS2812_DMA_STREAM, ENABLE);                                            // enable DMA
    TIM_Cmd(WS2812_TIM, ENABLE);                                                    // Timer enable
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * clear all LEDs
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
ws2812_clear_all (uint_fast16_t n_leds)
{
    WS2812_RGB  rgb = { 0, 0, 0 };

    while (ws2812_dma_status != 0)
    {
        ;
    }

    ws2812_set_all (&rgb, n_leds, 1);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * setup timer buffer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
ws2812_setup_dma_buf (uint_fast8_t at_half_pos)
{
    uint_fast8_t                i;
    uint_fast16_t               dma_buf_pos;
    volatile WS2812_RGB *       led;
    volatile DMA_BUFFER_TYPE *  timer_p;

    dma_buf_pos = current_dma_buf_pos;

    if (at_half_pos)
    {
        timer_p = dma_buf + DMA_BUF_LEN / 2;
    }
    else
    {
        timer_p = dma_buf;
    }

    if (current_led_offset < current_leds)
    {
        led = rgb_buf[current_rgb_buf_idx] + current_led_offset;

#if DSP_USE_WS2812_GRB == 1                                     // order G R B
        for (i = 0x80; i != 0; i >>= 1)                         // color green
        {
            *timer_p++ = (led->green & i) ? WS2812_T1H : WS2812_T0H;
        }

        for (i = 0x80; i != 0; i >>= 1)                         // color red
        {
            *timer_p++ = (led->red & i) ? WS2812_T1H : WS2812_T0H;
        }
#else // DSP_USE_WS2812_RGB == 1                                // order R G B
        for (i = 0x80; i != 0; i >>= 1)                         // color red
        {
            *timer_p++ = (led->red & i) ? WS2812_T1H : WS2812_T0H;
        }

        for (i = 0x80; i != 0; i >>= 1)                         // color green
        {
            *timer_p++ = (led->green & i) ? WS2812_T1H : WS2812_T0H;
        }
#endif

        for (i = 0x80; i != 0; i >>= 1)                         // color blue
        {
            *timer_p++ = (led->blue & i) ? WS2812_T1H : WS2812_T0H;
        }

        dma_buf_pos += WS2812_BIT_PER_LED;
        current_led_offset++;
    }
    else
    {
        uint_fast16_t bytes_to_write = WS2812_BIT_PER_LED;

        while (bytes_to_write > 0 && dma_buf_pos < current_data_pause_len)                  // pause (min. 50us)
        {
            *timer_p++ = 0;
            dma_buf_pos++;
            bytes_to_write--;
        }

        while (bytes_to_write > 0)                                                          // fill rest of buffer with 0
        {
            *timer_p++ = 0;
            bytes_to_write--;
        }
    }

    current_dma_buf_pos = dma_buf_pos;
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ISR DMA (will be called, when all data has been transferred)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void WS2812_DMA_CHANNEL_ISR (void);
void
WS2812_DMA_CHANNEL_ISR (void)
{
    if (DMA_GetITStatus(WS2812_DMA_STREAM, WS2812_DMA_CHANNEL_IRQ_HT))              // check half-transfer interrupt flag
    {
        DMA_ClearITPendingBit (WS2812_DMA_STREAM, WS2812_DMA_CHANNEL_IRQ_HT);       // reset flag
        ws2812_setup_dma_buf (0);
    }

    if (DMA_GetITStatus(WS2812_DMA_STREAM, WS2812_DMA_CHANNEL_IRQ_TC))              // check transfer complete interrupt flag
    {
        DMA_ClearITPendingBit (WS2812_DMA_STREAM, WS2812_DMA_CHANNEL_IRQ_TC);       // reset flag

        if (current_dma_buf_pos < current_data_pause_len)
        {
            ws2812_setup_dma_buf (1);
        }
        else
        {
            DMA_Cmd (WS2812_DMA_STREAM, DISABLE);                                   // disable DMA
            ws2812_dma_status = 0;                                                  // set status to ready
        }
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * refresh buffer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
ws2812_refresh (uint_fast16_t n_leds)
{
    uint_fast16_t   i;

    if (n_leds > ws2812_max_leds)
    {
        n_leds = ws2812_max_leds;
    }

    while (ws2812_dma_status != 0)
    {
        ;                                                        // wait until DMA transfer is ready
    }

    current_rgb_buf_idx     = next_rgb_buf_idx;
    next_rgb_buf_idx        = next_rgb_buf_idx ? 0 : 1;

    current_dma_buf_pos     = 0;
    current_led_offset      = 0;
    current_data_pause_len  = DATA_LEN(n_leds) + PAUSE_LEN;
    current_leds            = n_leds;

    ws2812_setup_dma_buf (0);
    ws2812_setup_dma_buf (1);
    ws2812_dma_start();

    for (i = 0; i < ws2812_max_leds; i++)                       // copy current rgb buffer during DMA transfer
    {
        rgb_buf[next_rgb_buf_idx][i] = rgb_buf[current_rgb_buf_idx][i];
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * set one RGB value
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
ws2812_set_led (uint_fast16_t n, WS2812_RGB * rgb)
{
    if (n < ws2812_max_leds)
    {
        rgb_buf[next_rgb_buf_idx][n].red      = rgb->red;
        rgb_buf[next_rgb_buf_idx][n].green    = rgb->green;
        rgb_buf[next_rgb_buf_idx][n].blue     = rgb->blue;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * set all LEDs to RGB value
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
ws2812_set_all (WS2812_RGB * rgb, uint_fast16_t n_leds, uint_fast8_t refresh)
{
    uint_fast16_t n;

    if (n_leds > ws2812_max_leds)
    {
        n_leds = ws2812_max_leds;
    }

    for (n = 0; n < n_leds; n++)
    {
        rgb_buf[next_rgb_buf_idx][n].red      = rgb->red;
        rgb_buf[next_rgb_buf_idx][n].green    = rgb->green;
        rgb_buf[next_rgb_buf_idx][n].blue     = rgb->blue;
    }

    if (refresh)
    {
        ws2812_refresh (n_leds);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * initialize WS2812
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
ws2812_init (uint_fast16_t n_leds)
{
    GPIO_InitTypeDef        gpio;
    TIM_TimeBaseInitTypeDef tb;
    TIM_OCInitTypeDef       toc;
    NVIC_InitTypeDef        nvic;

    ws2812_max_leds = n_leds;
    rgb_buf[0] = calloc (n_leds, sizeof (WS2812_RGB));
    rgb_buf[1] = calloc (n_leds, sizeof (WS2812_RGB));

    ws2812_dma_status = 0;

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize gpio
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    GPIO_StructInit (&gpio);
    WS2812_GPIO_CLOCK_CMD (WS2812_GPIO_CLOCK, ENABLE);                          // clock enable

    gpio.GPIO_Pin     = WS2812_GPIO_PIN;

    // 1st path: set data pin to input with pulldown, then check if external pullup connected:
    gpio.GPIO_Mode    = GPIO_Mode_IN;                                           // set as input
    gpio.GPIO_PuPd    = GPIO_PuPd_DOWN;                                         // with internal pulldown
    gpio.GPIO_Speed   = GPIO_Speed_100MHz;
    GPIO_Init(WS2812_GPIO_PORT, &gpio);
    delay_msec (1);                                                             // wait a moment

    // 2nd path: if external pullup detected, use open-drain, else use push-pull
    if (GPIO_ReadInputDataBit(WS2812_GPIO_PORT, WS2812_GPIO_PIN) == Bit_SET)    // external 4k7 pullup connected?
    {
        gpio.GPIO_OType   = GPIO_OType_OD;                                      // yes, set output type to open-drain
    }
    else
    {
        gpio.GPIO_OType   = GPIO_OType_PP;                                      // no, set output type to push-pull
    }

    gpio.GPIO_Mode    = GPIO_Mode_AF;                                           // set as alternate output
    gpio.GPIO_PuPd    = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed   = GPIO_Speed_100MHz;
    GPIO_Init(WS2812_GPIO_PORT, &gpio);
    WS2812_GPIO_PORT->BSRRH = WS2812_GPIO_PIN;                                  // set pin to Low
    GPIO_PinAFConfig(WS2812_GPIO_PORT, WS2812_GPIO_SOURCE, WS2812_TIM_AF);

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize TIMER
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    TIM_TimeBaseStructInit (&tb);
    TIM_OCStructInit (&toc);

    WS2812_TIM_CLOCK_CMD (WS2812_TIM_CLOCK, ENABLE);            // clock enable (TIM)
    WS2812_DMA_CLOCK_CMD (WS2812_DMA_CLOCK, ENABLE);            // clock Enable (DMA)

    tb.TIM_Period           = WS2812_TIM_PERIOD;
    tb.TIM_Prescaler        = WS2812_TIM_PRESCALER;
    tb.TIM_ClockDivision    = TIM_CKD_DIV1;
    tb.TIM_CounterMode      = TIM_CounterMode_Up;
    TIM_TimeBaseInit (WS2812_TIM, &tb);

    toc.TIM_OCMode          = TIM_OCMode_PWM1;
    toc.TIM_OutputState     = TIM_OutputState_Enable;
    toc.TIM_Pulse           = 0;
    toc.TIM_OCPolarity      = TIM_OCPolarity_High;

    TIM_OC1Init(WS2812_TIM, &toc);
    TIM_OC1PreloadConfig (WS2812_TIM, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig (WS2812_TIM, ENABLE);
    TIM_CtrlPWMOutputs(WS2812_TIM, ENABLE);
    TIM_DMACmd (WS2812_TIM, WS2812_TIM_DMA_TRG1, ENABLE);

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize NVIC
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    nvic.NVIC_IRQChannel                    = WS2812_DMA_CHANNEL_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init(&nvic);

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize DMA
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    ws2812_dma_init ();
    ws2812_clear_all (ws2812_max_leds);
}
