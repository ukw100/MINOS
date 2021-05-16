/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart-driver.h - UART driver routines for STM32F4XX
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 *
 * Possible UARTs of STM32F407:
 *           ALTERNATE=0    ALTERNATE=1    ALTERNATE=2
 *  +--------------------------------------------------+
 *  | UART | TX   | RX   || TX   | RX   || TX   | RX   |
 *  |======|======|======||======|======||======|======|
 *  | 1    | PA9  | PA10 || PB6  | PB7  ||      |      |
 *  | 2    | PA2  | PA3  || PD5  | PD6  ||      |      |
 *  | 3    | PB10 | PB11 || PC10 | PC11 || PD8  | PD9  |
 *  | 4    | PA0  | PA1  || PC10 | PC11 ||      |      |
 *  | 5    | PC12 | PD2  ||      |      ||      |      |
 *  | 6    | PC6  | PC7  || PG14 | PG9  ||      |      |
 *  +--------------------------------------------------+
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_rcc.h"
#include "misc.h"

#include "uart.h"

#define STRBUF_SIZE                 256                                         // (v)printf buffer size
#define UART_TXBUFLEN               64
#define UART_RXBUFLEN               64

#define INTERRUPT_CHAR              0x03                                        // CTRL-C

static volatile uint8_t             uart_txbuf[N_UARTS][UART_TXBUFLEN];         // tx ringbuffer
static volatile uint_fast16_t       uart_txsize[N_UARTS];                       // tx size
static volatile uint8_t             uart_rxbuf[N_UARTS][UART_RXBUFLEN];         // rx ringbuffer
static volatile uint_fast16_t       uart_rxsize[N_UARTS];                       // rx size

static uint_fast8_t                 uart_rxstart[N_UARTS];                      // head, not volatile

static volatile uint_fast8_t        uart_raw[N_UARTS];                          // raw mode: no interrupts
static volatile uint_fast8_t        uart_int[N_UARTS];                          // flag: user pressed CTRL-C

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * Possible UARTs of STM32F407:
 *
 *           ALTERNATE=0    ALTERNATE=1    ALTERNATE=2
 *  +--------------------------------------------------+
 *  | UART | TX   | RX   || TX   | RX   || TX   | RX   |
 *  |======|======|======||======|======||======|======|
 *  | 1    | PA9  | PA10 || PB6  | PB7  ||      |      |
 *  | 2    | PA2  | PA3  || PD5  | PD6  ||      |      |
 *  | 3    | PB10 | PB11 || PC10 | PC11 || PD8  | PD9  |
 *  | 4    | PA0  | PA1  || PC10 | PC11 ||      |      |
 *  | 5    | PC12 | PD2  ||      |      ||      |      |
 *  | 6    | PC6  | PC7  || PG14 | PG9  ||      |      |
 *  +--------------------------------------------------+
 *
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart1_init - initialize USART1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart1_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // A9/A10
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART1, ENABLE);
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource9,  GPIO_AF_USART1);                 // TX A9
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource10, GPIO_AF_USART1);                 // RX A10

        gpio.GPIO_Pin = GPIO_Pin_9;
        GPIO_Init(GPIOA, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_10;
        GPIO_Init(GPIOA, &gpio);
    }
    else if (alternate == 1)                                                        // B6/B7
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOB, ENABLE);
        RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART1, ENABLE);
        GPIO_PinAFConfig (GPIOB, GPIO_PinSource6, GPIO_AF_USART1);                  // TX B6
        GPIO_PinAFConfig (GPIOB, GPIO_PinSource7, GPIO_AF_USART1);                  // RX B7

        gpio.GPIO_Pin = GPIO_Pin_6;
        GPIO_Init(GPIOB, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_7;
        GPIO_Init(GPIOB, &gpio);
    }

    USART_OverSampling8Cmd(USART1, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART1, &uart);
    USART_Cmd(USART1, ENABLE);                                          // UART enable
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);                      // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = USART1_IRQn;              // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart2_init - initialize USART2
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart2_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // A2/A3
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART2, ENABLE);
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource2, GPIO_AF_USART2);                  // TX A2
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource3, GPIO_AF_USART2);                  // RX A3

        gpio.GPIO_Pin = GPIO_Pin_2;
        GPIO_Init(GPIOA, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_3;
        GPIO_Init(GPIOA, &gpio);
    }
    else if (alternate == 1)                                                        // D5/D6
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOD, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART2, ENABLE);
        GPIO_PinAFConfig (GPIOD, GPIO_PinSource5, GPIO_AF_USART2);                  // TX D5
        GPIO_PinAFConfig (GPIOD, GPIO_PinSource6, GPIO_AF_USART2);                  // RX D6

        gpio.GPIO_Pin = GPIO_Pin_5;
        GPIO_Init(GPIOD, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_6;
        GPIO_Init(GPIOD, &gpio);
    }

    USART_OverSampling8Cmd(USART2, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART2, &uart);
    USART_Cmd(USART2, ENABLE);                                          // UART enable
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);                      // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = USART2_IRQn;              // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart3_init - initialize USART3
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart3_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // B10/B11
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOB, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART3, ENABLE);
        GPIO_PinAFConfig (GPIOB, GPIO_PinSource10, GPIO_AF_USART3);                 // TX B10
        GPIO_PinAFConfig (GPIOB, GPIO_PinSource11, GPIO_AF_USART3);                 // RX B11

        gpio.GPIO_Pin = GPIO_Pin_10;
        GPIO_Init(GPIOB, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_11;
        GPIO_Init(GPIOB, &gpio);
    }
    else if (alternate == 1)                                                        // C10/C11
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOC, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART3, ENABLE);
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource10, GPIO_AF_USART3);                 // TX C10
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource11, GPIO_AF_USART3);                 // RX C11

        gpio.GPIO_Pin = GPIO_Pin_10;
        GPIO_Init(GPIOC, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_11;
        GPIO_Init(GPIOC, &gpio);
    }
    else if (alternate == 2)                                                        // D8/D9
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOD, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_USART3, ENABLE);
        GPIO_PinAFConfig (GPIOD, GPIO_PinSource8, GPIO_AF_USART3);                  // TX D8
        GPIO_PinAFConfig (GPIOD, GPIO_PinSource9, GPIO_AF_USART3);                  // RX D9

        gpio.GPIO_Pin = GPIO_Pin_8;
        GPIO_Init(GPIOD, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_9;
        GPIO_Init(GPIOD, &gpio);
    }

    USART_OverSampling8Cmd(USART3, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART3, &uart);
    USART_Cmd(USART3, ENABLE);                                          // UART enable
    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);                      // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = USART3_IRQn;              // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart4_init - initialize UART4
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart4_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // A0/A1
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_UART4, ENABLE);
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource0, GPIO_AF_UART4);                   // TX A0
        GPIO_PinAFConfig (GPIOA, GPIO_PinSource1, GPIO_AF_UART4);                   // RX A1

        gpio.GPIO_Pin = GPIO_Pin_0;
        GPIO_Init(GPIOA, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_1;
        GPIO_Init(GPIOA, &gpio);
    }
    else if (alternate == 1)                                                        // C10/C11
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOC, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_UART4, ENABLE);
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource10, GPIO_AF_UART4);                  // TX C10
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource11, GPIO_AF_UART4);                  // RX C11

        gpio.GPIO_Pin = GPIO_Pin_10;
        GPIO_Init(GPIOC, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_11;
        GPIO_Init(GPIOC, &gpio);
    }

    USART_OverSampling8Cmd(UART4, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(UART4, &uart);
    USART_Cmd(UART4, ENABLE);                                           // UART enable
    USART_ITConfig(UART4, USART_IT_RXNE, ENABLE);                       // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = UART4_IRQn;               // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart5_init - initialize UART5
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart5_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // C12/D2
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOC, ENABLE);
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOD, ENABLE);
        RCC_APB1PeriphClockCmd (RCC_APB1Periph_UART5, ENABLE);
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource12, GPIO_AF_UART5);                  // TX C12
        GPIO_PinAFConfig (GPIOD, GPIO_PinSource2,  GPIO_AF_UART5);                  // RX D2

        gpio.GPIO_Pin = GPIO_Pin_12;
        GPIO_Init(GPIOC, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_2;
        GPIO_Init(GPIOD, &gpio);
    }

    USART_OverSampling8Cmd(UART5, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(UART5, &uart);
    USART_Cmd(UART5, ENABLE);                                           // UART enable
    USART_ITConfig(UART5, USART_IT_RXNE, ENABLE);                       // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = UART5_IRQn;               // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart6_init - initialize USART6
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
uart6_init (uint_fast8_t alternate, uint32_t baudrate)
{
    GPIO_InitTypeDef    gpio;
    USART_InitTypeDef   uart;
    NVIC_InitTypeDef    nvic;

    GPIO_StructInit (&gpio);
    USART_StructInit (&uart);

    // UART as alternate function with PushPull
    gpio.GPIO_Mode  = GPIO_Mode_AF;
    gpio.GPIO_Speed = GPIO_Speed_100MHz;
    gpio.GPIO_OType = GPIO_OType_PP;
    gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                                 // or GPIO_PuPd_NOPULL

    if (alternate == 0)                                                             // C6/C7
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOA, ENABLE);
        RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART6, ENABLE);
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource6,  GPIO_AF_USART6);                 // TX C6
        GPIO_PinAFConfig (GPIOC, GPIO_PinSource7, GPIO_AF_USART6);                  // RX C7

        gpio.GPIO_Pin = GPIO_Pin_6;
        GPIO_Init(GPIOC, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_7;
        GPIO_Init(GPIOC, &gpio);
    }
    else if (alternate == 1)                                                        // G14/G9
    {
        RCC_AHB1PeriphClockCmd (RCC_AHB1Periph_GPIOG, ENABLE);
        RCC_APB2PeriphClockCmd (RCC_APB2Periph_USART6, ENABLE);
        GPIO_PinAFConfig (GPIOG, GPIO_PinSource14, GPIO_AF_USART6);                 // TX G14
        GPIO_PinAFConfig (GPIOG, GPIO_PinSource9,  GPIO_AF_USART6);                 // RX G9

        gpio.GPIO_Pin = GPIO_Pin_14;
        GPIO_Init(GPIOG, &gpio);

        gpio.GPIO_Pin = GPIO_Pin_9;
        GPIO_Init(GPIOG, &gpio);
    }

    USART_OverSampling8Cmd(USART6, ENABLE);

    // 8 bits, 1 stop bit, no parity, no RTS+CTS
    uart.USART_BaudRate             = baudrate;
    uart.USART_WordLength           = USART_WordLength_8b;
    uart.USART_StopBits             = USART_StopBits_1;
    uart.USART_Parity               = USART_Parity_No;
    uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

    USART_Init(USART6, &uart);
    USART_Cmd(USART6, ENABLE);                                          // UART enable
    USART_ITConfig(USART6, USART_IT_RXNE, ENABLE);                      // RX-Interrupt enable

    nvic.NVIC_IRQChannel                    = USART6_IRQn;              // enable UART Interrupt-Vector
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init (&nvic);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_init () - initialize UART
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
uart_init (uint_fast8_t uart_number, uint_fast8_t alternate, uint32_t baudrate)
{
    uart_raw[uart_number] = 1;
    uart_int[uart_number] = 0;

    switch (uart_number)
    {
        case UART_NUMBER_1:     uart1_init (alternate, baudrate);   break;
        case UART_NUMBER_2:     uart2_init (alternate, baudrate);   break;
        case UART_NUMBER_3:     uart3_init (alternate, baudrate);   break;
        case UART_NUMBER_4:     uart4_init (alternate, baudrate);   break;
        case UART_NUMBER_5:     uart5_init (alternate, baudrate);   break;
        case UART_NUMBER_6:     uart6_init (alternate, baudrate);   break;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_putc () - put a character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
uart_putc (uint_fast8_t uart_number, uint_fast8_t ch)
{
    static uint_fast8_t uart_txstop[N_UARTS];                                   // tail

    while (uart_txsize[uart_number] >= UART_TXBUFLEN)                                        // buffer full?
    {                                                                           // yes
        ;                                                                       // wait
    }

    uart_txbuf[uart_number][uart_txstop[uart_number]++] = ch;                   // store character

    if (uart_txstop[uart_number] >= UART_TXBUFLEN)                              // at end of ringbuffer?
    {                                                                           // yes
        uart_txstop[uart_number] = 0;                                           // reset to beginning
    }

    __disable_irq();
    uart_txsize[uart_number]++;                                                 // increment used size
    __enable_irq();

    switch (uart_number)
    {
        case UART_NUMBER_1: USART_ITConfig(USART1, USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
        case UART_NUMBER_2: USART_ITConfig(USART2, USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
        case UART_NUMBER_3: USART_ITConfig(USART3, USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
        case UART_NUMBER_4: USART_ITConfig(UART4,  USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
        case UART_NUMBER_5: USART_ITConfig(UART5,  USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
        case UART_NUMBER_6: USART_ITConfig(USART6, USART_IT_TXE, ENABLE); break;    // enable TXE interrupt
    }

}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_puts () - put a string
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
uart_puts (uint_fast8_t uart_number, const char * s)
{
    uint_fast8_t ch;

    while ((ch = (uint_fast8_t) *s) != '\0')
    {
        uart_putc (uart_number, ch);
        s++;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_vprintf () - print a formatted message (by va_list)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
uart_vprintf (uint_fast8_t uart_number, const char * fmt, va_list ap)
{
    static char str_buf[STRBUF_SIZE];
    int         len;

    (void) vsnprintf ((char *) str_buf, STRBUF_SIZE, fmt, ap);
    len = strlen (str_buf);
    uart_puts (uart_number, str_buf);
    return len;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_printf () - print a formatted message (varargs)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
uart_printf (uint_fast8_t uart_number, const char * fmt, ...)
{
    int     len;
    va_list ap;

    va_start (ap, fmt);
    len = uart_vprintf (uart_number, fmt, ap);
    va_end (ap);
    return len;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_getc () - get a character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
uart_getc (uint_fast8_t uart_number)
{
    uint_fast8_t         ch;

    while (uart_rxsize[uart_number] == 0)                                       // rx buffer empty?
    {                                                                           // yes, wait
        ;
    }

    ch = uart_rxbuf[uart_number][uart_rxstart[uart_number]++];                  // get character from ringbuffer

    if (uart_rxstart[uart_number] == UART_RXBUFLEN)                             // at end of rx buffer?
    {                                                                           // yes
        uart_rxstart[uart_number] = 0;                                          // reset to beginning
    }

    __disable_irq();
    uart_rxsize[uart_number]--;                                                 // decrement size
    __enable_irq();

    return (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_set_rawmode () - set/unset raw mode
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
uart_set_rawmode (uint_fast8_t uart_number, uint_fast8_t rawmode)
{
    uart_raw[uart_number] = rawmode;

    if (rawmode)
    {
        uart_int[uart_number] = 0;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_interrupted () - check if interrupted by CTRL-C
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
uart_interrupted (uint_fast8_t uart_number)
{
    uint_fast8_t rtc;

    if (uart_int[uart_number])
    {
        rtc = 1;
        uart_int[uart_number] = 0;
    }
    else
    {
        rtc = 0;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_poll() - poll for a character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
uart_poll (uint_fast8_t uart_number, uint_fast8_t * chp)
{
    uint_fast8_t        ch;

    if (uart_rxsize[uart_number] == 0)                                          // rx buffer empty?
    {                                                                           // yes, return 0
        return 0;
    }

    ch = uart_rxbuf[uart_number][uart_rxstart[uart_number]++];                  // get character from ringbuffer

    if (uart_rxstart[uart_number] == UART_RXBUFLEN)                             // at end of rx buffer?
    {                                                                           // yes
        uart_rxstart[uart_number] = 0;                                          // reset to beginning
    }

    __disable_irq();
    uart_rxsize[uart_number]--;                                                 // decrement size
    __enable_irq();

    *chp = ch;
    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_get_rxsize() - get size of input buffer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast16_t
uart_get_rxsize (uint_fast8_t uart_number)
{
    return uart_rxsize[uart_number];
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * uart_flush () - flush output
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
uart_flush (uint_fast8_t uart_number)
{
    while (uart_txsize[uart_number] > 0)                                        // tx buffer empty?
    {
        ;                                                                       // no, wait
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * USART1_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void USART1_IRQHandler (void);

void
USART1_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (USART1, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (USART1, USART_IT_RXNE);
        value = USART_ReceiveData (USART1);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_1] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_1] = 1;
        }

        if (uart_rxsize[UART_NUMBER_1] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_1][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_1]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (USART1, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (USART1, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_1] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_1][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_1]--;                                       // decrement size

            USART_SendData(USART1, ch);
        }
        else
        {
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);                      // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * USART2_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void USART2_IRQHandler (void);

void
USART2_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (USART2, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (USART2, USART_IT_RXNE);
        value = USART_ReceiveData (USART2);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_2] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_2] = 1;
        }

        if (uart_rxsize[UART_NUMBER_2] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_2][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_2]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (USART2, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (USART2, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_2] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_2][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_2]--;                                       // decrement size

            USART_SendData(USART2, ch);
        }
        else
        {
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);                      // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * USART3_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void USART3_IRQHandler (void);

void
USART3_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (USART3, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (USART3, USART_IT_RXNE);
        value = USART_ReceiveData (USART3);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_3] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_3] = 1;
        }

        if (uart_rxsize[UART_NUMBER_3] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_3][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_3]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (USART3, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (USART3, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_3] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_3][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_3]--;                                       // decrement size

            USART_SendData(USART3, ch);
        }
        else
        {
            USART_ITConfig(USART3, USART_IT_TXE, DISABLE);                      // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * UART4_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void UART4_IRQHandler (void);

void
UART4_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (UART4, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (UART4, USART_IT_RXNE);
        value = USART_ReceiveData (UART4);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_4] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_4] = 1;
        }

        if (uart_rxsize[UART_NUMBER_4] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_4][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_4]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (UART4, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (UART4, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_4] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_4][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_4]--;                                       // decrement size

            USART_SendData(UART4, ch);
        }
        else
        {
            USART_ITConfig(UART4, USART_IT_TXE, DISABLE);                       // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * UART5_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void UART5_IRQHandler (void);

void
UART5_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (UART5, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (UART5, USART_IT_RXNE);
        value = USART_ReceiveData (UART5);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_5] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_5] = 1;
        }

        if (uart_rxsize[UART_NUMBER_5] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_5][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_5]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (UART5, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (UART5, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_5] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_5][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_5]--;                                       // decrement size

            USART_SendData(UART5, ch);
        }
        else
        {
            USART_ITConfig(UART5, USART_IT_TXE, DISABLE);                       // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * USART6_IRQHandler ()
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void USART6_IRQHandler (void);

void
USART6_IRQHandler (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (USART6, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (USART6, USART_IT_RXNE);
        value = USART_ReceiveData (USART6);

        ch = value & 0xFF;

        if (! uart_raw[UART_NUMBER_6] && ch == INTERRUPT_CHAR)                  // no raw mode & user pressed CTRL-C
        {
            uart_int[UART_NUMBER_6] = 1;
        }

        if (uart_rxsize[UART_NUMBER_6] < UART_RXBUFLEN)                         // buffer full?
        {                                                                       // no
            uart_rxbuf[UART_NUMBER_6][uart_rxstop++] = ch;                      // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize[UART_NUMBER_6]++;                                       // increment used size
        }
    }

    if (USART_GetITStatus (USART6, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head

        USART_ClearITPendingBit (USART6, USART_IT_TXE);

        if (uart_txsize[UART_NUMBER_6] > 0)                                     // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[UART_NUMBER_6][uart_txstart++];                     // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize[UART_NUMBER_6]--;                                       // decrement size

            USART_SendData(USART6, ch);
        }
        else
        {
            USART_ITConfig(USART6, USART_IT_TXE, DISABLE);                      // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
}
