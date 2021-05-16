/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * mcurses.c - mcurses lib
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2011-2021 Frank Meyer - frank(at)fli4l.de
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

#ifdef unix
#include <termio.h>
#include <fcntl.h>
#define PROGMEM
#define PSTR(x)                                 (x)
#define pgm_read_byte(s)                        (*s)
#elif (defined __SDCC_z80)
#define PROGMEM
#define PSTR(x)                                 (x)
#define pgm_read_byte(s)                        (*s)
#elif (defined STM32F4XX)
#ifndef HSE_VALUE
#define HSE_VALUE                               ((uint32_t)8000000)         /* STM32F4 discovery uses a 8Mhz external crystal */
#endif
#include "stm32f4xx.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_usart.h"
#include "stm32f4xx_rcc.h"
#include "misc.h"
#include "delay.h"
#define PROGMEM
#define PSTR(x)                                 (x)
#define pgm_read_byte(s)                        (*s)
#else // AVR
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#endif

#include "mcurses.h"

#define SEQ_CSI                                 PSTR("\033[")                   // code introducer
#define SEQ_CLEAR                               PSTR("\033[2J")                 // clear screen
#define SEQ_CLRTOBOT                            PSTR("\033[J")                  // clear to bottom
#define SEQ_CLRTOEOL                            PSTR("\033[K")                  // clear to end of line
#define SEQ_DELCH                               PSTR("\033[P")                  // delete character
#define SEQ_NEXTLINE                            PSTR("\033E")                   // goto next line (scroll up at end of scrolling region)
#define SEQ_INSERTLINE                          PSTR("\033[L")                  // insert line
#define SEQ_DELETELINE                          PSTR("\033[M")                  // delete line
#define SEQ_ATTRSET                             PSTR("\033[0")                  // set attributes, e.g. "\033[0;7;1m"
#define SEQ_ATTRSET_REVERSE                     PSTR(";7")                      // reverse
#define SEQ_ATTRSET_UNDERLINE                   PSTR(";4")                      // underline
#define SEQ_ATTRSET_BLINK                       PSTR(";5")                      // blink
#define SEQ_ATTRSET_BOLD                        PSTR(";1")                      // bold
#define SEQ_ATTRSET_DIM                         PSTR(";2")                      // dim
#define SEQ_ATTRSET_FCOLOR                      PSTR(";3")                      // forground color
#define SEQ_ATTRSET_BCOLOR                      PSTR(";4")                      // background color
#define SEQ_INSERT_MODE                         PSTR("\033[4h")                 // set insert mode
#define SEQ_REPLACE_MODE                        PSTR("\033[4l")                 // set replace mode
#define SEQ_ATTRIBUTES_OFF                      PSTR("\033[0m")                 // reset attributes
#define SEQ_RESET_SCRREG                        PSTR("\033[r")                  // reset scrolling region
#define SEQ_LOAD_G1                             PSTR("\033)0")                  // load G1 character set
#define SEQ_CURSOR_VIS                          PSTR("\033[?25")                // set cursor visible/not visible

static uint_fast8_t                             mcurses_scrl_start = 0;         // start of scrolling region, default is 0
static uint_fast8_t                             mcurses_scrl_end;               // end of scrolling region, default is last line
static uint_fast8_t                             mcurses_nodelay;                // nodelay flag
static uint_fast8_t                             mcurses_halfdelay;              // halfdelay value, in tenths of a second

uint_fast8_t                                    mcurses_is_up = 0;              // flag: mcurses is up
uint_fast8_t                                    mcurses_cury = 0xff;            // current y position of cursor, public (getyx())
uint_fast8_t                                    mcurses_curx = 0xff;            // current x position of cursor, public (getyx())

static void                                     mcurses_puts_P (const char *);

uint_fast8_t                                    LINES = MCURSES_LINES;
uint_fast8_t                                    COLS  = MCURSES_COLS;

#if defined(unix)

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init, done, putc, getc, nodelay, halfdelay, flush for UNIX or LINUX
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static struct termio                            mcurses_oldmode;
static struct termio                            mcurses_newmode;

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_init (void)
{
    uint_fast8_t    rtc = 0;
    int             fd;

    fd = fileno (stdin);

    if (ioctl (fd, TCGETA, &mcurses_oldmode) >= 0 || ioctl (fd, TCGETA, &mcurses_newmode) >= 0)
    {
        mcurses_newmode.c_lflag &= ~ICANON;                                         // switch off canonical input
        mcurses_newmode.c_lflag &= ~ECHO;                                           // switch off echo
        mcurses_newmode.c_iflag &= ~ICRNL;                                          // switch off CR->NL mapping
        mcurses_newmode.c_oflag &= ~TAB3;                                           // switch off TAB conversion
        mcurses_newmode.c_cc[VINTR] = '\377';                                       // disable VINTR VQUIT
        mcurses_newmode.c_cc[VQUIT] = '\377';                                       // but don't touch VSWTCH
        mcurses_newmode.c_cc[VMIN] = 1;                                             // block input:
        mcurses_newmode.c_cc[VTIME] = 0;                                            // one character

        if (ioctl (fd, TCSETAW, &mcurses_newmode) >= 0)
        {
            rtc = 1;
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
    int     fd;

    fd = fileno (stdin);

    (void) ioctl (fd, TCSETAW, &mcurses_oldmode);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    putchar (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    uint_fast8_t ch;

    ch = getchar ();

    return (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    int     fd;
    int     fl;

    fd = fileno (stdin);

    if ((fl = fcntl (fd, F_GETFL, 0)) >= 0)
    {
        if (flag)
        {
            fl |= O_NDELAY;
        }
        else
        {
            fl &= ~O_NDELAY;
        }
        (void) fcntl (fd, F_SETFL, fl);
        mcurses_nodelay = flag;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    if (tenths == 0)
    {
        mcurses_newmode.c_cc[VMIN] = 1;                        /* block input:     */
        mcurses_newmode.c_cc[VTIME] = 0;                       /* one character    */
    }
    else
    {
        mcurses_newmode.c_cc[VMIN] = 0;                        /* set timeout      */
        mcurses_newmode.c_cc[VTIME] = tenths;                  /* in tenths of sec */
    }

    mcurses_halfdelay = tenths;

    (void) ioctl (0, TCSETAW, &mcurses_newmode);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (unix/linux)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
    fflush (stdout);
}

#elif defined (__SDCC_z80)
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_init (void)
{
    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    putchar (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    uint_fast8_t    ch;

    ch = getchar ();
    return (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    mcurses_nodelay = flag;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    mcurses_halfdelay = tenths;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (SDCC Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
}

#elif defined(STM32F4XX)

#if MCURSES_UART_NUMBER == 0        // USB

#include "stm32f4xx_exti.h"
#include "usbd_cdc_core.h"
#include "usbd_usr.h"
#include "usbd_desc.h"
#include "usbd_cdc_vcp.h"
#include "usb_dcd_int.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init, done, putc, getc, nodelay, halfdelay, flush for STM32F4XX USB
 *
 * The USB data must be 4 byte aligned if DMA is enabled. This macro handles
 * the alignment, if necessary (it's actually magic, but don't tell anyone).
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
__ALIGN_BEGIN USB_OTG_CORE_HANDLE  USB_OTG_dev __ALIGN_END;

void SysTick_Handler(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void OTG_FS_IRQHandler(void);
void OTG_FS_WKUP_IRQHandler(void);

static volatile uint32_t halfdelay_counter;

void SysTick_Handler(void)
{
    if (halfdelay_counter > 0)
    {
        halfdelay_counter--;
    }
}

static void
die(void)
{
    while (1)
    {
        ;
    }
}

void NMI_Handler(void)       {}
void HardFault_Handler(void) { die(); }
void MemManage_Handler(void) { die(); }
void BusFault_Handler(void)  { die(); }
void UsageFault_Handler(void){ die(); }
void SVC_Handler(void)       {}
void DebugMon_Handler(void)  {}
void PendSV_Handler(void)    {}

void OTG_FS_IRQHandler(void)
{
    USBD_OTG_ISR_Handler (&USB_OTG_dev);
}

void OTG_FS_WKUP_IRQHandler(void)
{
    if(USB_OTG_dev.cfg.low_power)
    {
        *(uint32_t *)(0xE000ED10) &= 0xFFFFFFF9 ;
        SystemInit();
        USB_OTG_UngateClock(&USB_OTG_dev);
    }
    EXTI_ClearITPendingBit(EXTI_Line18);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_init (void)
{
    static uint_fast8_t already_called = 0;
    uint_fast8_t        rtc = 1;
    uint_fast8_t        cnt = 0;
    uint8_t             ch;

    if (! already_called)
    {
        SysTick_Config(SystemCoreClock / 1000);
        USBD_Init(&USB_OTG_dev, USB_OTG_FS_CORE_ID, &USR_desc, &USBD_CDC_cb, &USR_cb);
        already_called = 1;
    }

    halfdelay_counter = 1000;

    while (! VCP_get_char (&ch))                                // wait until character available
    {
        if (halfdelay_counter == 0)
        {
            mcurses_puts_P ("Press any key to start...\r\n");

            cnt++;

            if (cnt > 10)                                           // after 10 seconds timeout
            {
                rtc = 0;
                break;
            }

            halfdelay_counter = 1000;
        }
    }

    halfdelay_counter = 0;
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    VCP_put_char(ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    uint8_t ch;

    if (mcurses_halfdelay)
    {
        halfdelay_counter = 100 * mcurses_halfdelay;
    }

    while (! VCP_get_char (&ch))                                // wait until character available
    {
        if (mcurses_nodelay)
        {                                                       // if nodelay set, return ERR
            return (ERR);
        }

        if (mcurses_halfdelay && halfdelay_counter == 0)
        {
            return (ERR);
        }
    }

    return ch;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    mcurses_nodelay = flag;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (STM32FXX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    mcurses_halfdelay = tenths;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (STM32F4XX USB)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
}

#elif MCURSES_UART_NUMBER > 0

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init, done, putc, getc, nodelay, halfdelay, flush for STM32F4XX UART
 *
 *            UART1 : TX:[PA9,PB6] RX:[PA10,PB7]
 *            UART2 : TX:[PA2,PD5] RX:[PA3,PD6]
 *            UART3 : TX:[PB10,PC10,PD8] RX:[PB11,PC11,PD9]
 *            UART4 : TX:[PA0,PC10] RX:[PA1,PC11]
 *            UART5 : TX:[PC12] RX:[PD2]
 *            UART6 : TX:[PC6,PG14] RX:[PC7,PG9]
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */


#define UART_TXBUFLEN                          128                              // 128 Bytes ringbuffer for UART
#define UART_RXBUFLEN                          128                              // 128 Bytes ringbuffer for UART

static volatile uint_fast8_t    uart_txbuf[UART_TXBUFLEN];                      // tx ringbuffer
static volatile uint_fast8_t    uart_txsize = 0;                                // tx size
static volatile uint_fast8_t    uart_rxbuf[UART_RXBUFLEN];                      // rx ringbuffer
static volatile uint_fast8_t    uart_rxsize = 0;                                // rx size

#define UART_NUMBER             MCURSES_UART_NUMBER
#define BAUD                    MCURSES_BAUD

#define _CONCAT(a,b)            a##b
#define CONCAT(a,b)             _CONCAT(a,b)

#if UART_NUMBER == 1

#define UART_TX_PORT_LETTER         A       // A9/A10 or B6/B7
#define UART_TX_PIN_NUMBER          9
#define UART_RX_PORT_LETTER         A
#define UART_RX_PIN_NUMBER          10

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB2PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB2PeriphClockCmd(c,e)
#define UART_NAME                   USART1
#define UART_APB_CLOCK_USART        RCC_APB2Periph_USART1
#define UART_GPIO_AF_UART           GPIO_AF_USART1
#define UART_IRQ_HANDLER            USART1_IRQHandler
#define UART_IRQ_CHANNEL            USART1_IRQn

#elif UART_NUMBER == 2

#define UART_TX_PORT_LETTER         A       // A2/A3 or D5/D6
#define UART_TX_PIN_NUMBER          2
#define UART_RX_PORT_LETTER         A
#define UART_RX_PIN_NUMBER          3

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB1PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB1PeriphClockCmd(c,e)
#define UART_NAME                   USART2
#define UART_APB_CLOCK_USART        RCC_APB1Periph_USART2
#define UART_GPIO_AF_UART           GPIO_AF_USART2
#define UART_IRQ_HANDLER            USART2_IRQHandler
#define UART_IRQ_CHANNEL            USART2_IRQn

#elif UART_NUMBER == 3

#define UART_TX_PORT_LETTER         D       // D8/D9 or B10/B11 or C10/C11
#define UART_TX_PIN_NUMBER          8
#define UART_RX_PORT_LETTER         D
#define UART_RX_PIN_NUMBER          9

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB1PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB1PeriphClockCmd(c,e)
#define UART_NAME                   USART3
#define UART_APB_CLOCK_USART        RCC_APB1Periph_USART3
#define UART_GPIO_AF_UART           GPIO_AF_USART3
#define UART_IRQ_HANDLER            USART3_IRQHandler
#define UART_IRQ_CHANNEL            USART3_IRQn

#elif UART_NUMBER == 4

#define UART_TX_PORT_LETTER         A       // A0/A1 or C10/C11
#define UART_TX_PIN_NUMBER          0
#define UART_RX_PORT_LETTER         A
#define UART_RX_PIN_NUMBER          1

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB1PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB1PeriphClockCmd(c,e)
#define UART_NAME                   UART4
#define UART_APB_CLOCK_USART        RCC_APB1Periph_UART4
#define UART_GPIO_AF_UART           GPIO_AF_UART4
#define UART_IRQ_HANDLER            UART4_IRQHandler
#define UART_IRQ_CHANNEL            UART4_IRQn

#elif UART_NUMBER == 5

#define UART_TX_PORT_LETTER         C       // C12/D2
#define UART_TX_PIN_NUMBER          12
#define UART_RX_PORT_LETTER         D
#define UART_RX_PIN_NUMBER          2

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB1PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB1PeriphClockCmd(c,e)
#define UART_NAME                   UART5
#define UART_APB_CLOCK_USART        RCC_APB1Periph_UART5
#define UART_GPIO_AF_UART           GPIO_AF_UART5
#define UART_IRQ_HANDLER            UART5_IRQHandler
#define UART_IRQ_CHANNEL            UART5_IRQn

#elif UART_NUMBER == 6

#define UART_TX_PORT_LETTER         C       // C6/C7 or G14/G9
#define UART_TX_PIN_NUMBER          6
#define UART_RX_PORT_LETTER         C
#define UART_RX_PIN_NUMBER          7

#define UART_AHB_CLOCK_CMD(c,e)     RCC_AHB2PeriphClockCmd(c,e)
#define UART_APB_CLOCK_CMD(c,e)     RCC_APB2PeriphClockCmd(c,e)
#define UART_NAME                   USART6
#define UART_APB_CLOCK_USART        RCC_APB2Periph_USART6
#define UART_GPIO_AF_UART           GPIO_AF_USART6
#define UART_IRQ_HANDLER            USART6_IRQHandler
#define UART_IRQ_CHANNEL            USART6_IRQn

#else
#error wrong number for UART_NUMBER, choose 1-6
#endif

#define UART_TX_PORT                CONCAT(GPIO, UART_TX_PORT_LETTER)
#define UART_TX_AHB_CLOCK_PORT      CONCAT(RCC_AHB1Periph_GPIO, UART_TX_PORT_LETTER)
#define UART_TX_PIN                 CONCAT(GPIO_Pin_, UART_TX_PIN_NUMBER)
#define UART_TX_PINSOURCE           CONCAT(GPIO_PinSource,  UART_TX_PIN_NUMBER)

#define UART_RX_PORT                CONCAT(GPIO, UART_RX_PORT_LETTER)
#define UART_RX_AHB_CLOCK_PORT      CONCAT(RCC_AHB1Periph_GPIO, UART_RX_PORT_LETTER)
#define UART_RX_PIN                 CONCAT(GPIO_Pin_, UART_RX_PIN_NUMBER)
#define UART_RX_PINSOURCE           CONCAT(GPIO_PinSource, UART_RX_PIN_NUMBER)


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_init (void)
{
    static uint_fast8_t already_called = 0;

    if (! already_called)
    {
        already_called = 1;

        GPIO_InitTypeDef    gpio;
        USART_InitTypeDef   uart;
        NVIC_InitTypeDef    nvic;

        UART_AHB_CLOCK_CMD (UART_TX_AHB_CLOCK_PORT, ENABLE);
        UART_AHB_CLOCK_CMD (UART_RX_AHB_CLOCK_PORT, ENABLE);

        UART_APB_CLOCK_CMD (UART_APB_CLOCK_USART, ENABLE);

        GPIO_PinAFConfig (UART_TX_PORT, UART_TX_PINSOURCE, UART_GPIO_AF_UART);  // TX
        GPIO_PinAFConfig (UART_RX_PORT, UART_RX_PINSOURCE, UART_GPIO_AF_UART);  // RX

        gpio.GPIO_Mode  = GPIO_Mode_AF;
        gpio.GPIO_Speed = GPIO_Speed_100MHz;
        gpio.GPIO_OType = GPIO_OType_PP;
        gpio.GPIO_PuPd  = GPIO_PuPd_UP;                                         // fm: perhaps better: GPIO_PuPd_NOPULL

        gpio.GPIO_Pin = UART_TX_PIN;
        GPIO_Init(UART_TX_PORT, &gpio);

        gpio.GPIO_Pin = UART_RX_PIN;
        GPIO_Init(UART_RX_PORT, &gpio);

        USART_OverSampling8Cmd(UART_NAME, ENABLE);

        // 8 bits, 1 stop bit, no parity, no RTS+CTS
        uart.USART_BaudRate             = MCURSES_BAUD;
        uart.USART_WordLength           = USART_WordLength_8b;
        uart.USART_StopBits             = USART_StopBits_1;
        uart.USART_Parity               = USART_Parity_No;
        uart.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
        uart.USART_Mode                 = USART_Mode_Rx | USART_Mode_Tx;

        USART_Init(UART_NAME, &uart);

        // UART enable
        USART_Cmd(UART_NAME, ENABLE);

        // RX-Interrupt enable
        USART_ITConfig(UART_NAME, USART_IT_RXNE, ENABLE);

        // enable UART Interrupt-Vector
        nvic.NVIC_IRQChannel                    = UART_IRQ_CHANNEL;
        nvic.NVIC_IRQChannelPreemptionPriority  = 0;
        nvic.NVIC_IRQChannelSubPriority         = 0;
        nvic.NVIC_IRQChannelCmd                 = ENABLE;
        NVIC_Init (&nvic);
    }
    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    static uint_fast8_t uart_txstop  = 0;                                       // tail

    while (uart_txsize >= UART_TXBUFLEN)                                        // buffer full?
    {                                                                           // yes
        ;                                                                       // wait
    }

    uart_txbuf[uart_txstop++] = ch;                                             // store character

    if (uart_txstop >= UART_TXBUFLEN)                                           // at end of ringbuffer?
    {                                                                           // yes
        uart_txstop = 0;                                                        // reset to beginning
    }

    __disable_irq();
    uart_txsize++;                                                              // increment used size
    __enable_irq();

    USART_ITConfig(UART_NAME, USART_IT_TXE, ENABLE);                           // enable TXE interrupt
}

#if 0
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc - non-interrupt version (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    while (USART_GetFlagStatus(UART_NAME, USART_FLAG_TXE) == RESET)
    {
         ;
    }
    USART_SendData(UART_NAME, ch);
}
#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    static uint_fast8_t  uart_rxstart = 0;                                      // head
    uint_fast8_t         ch;

    while (uart_rxsize == 0)                                                    // rx buffer empty?
    {                                                                           // yes, wait
        if (mcurses_nodelay)
        {                                                                       // or if nodelay set, return ERR
            return (ERR);
        }
    }

    ch = uart_rxbuf[uart_rxstart++];                                            // get character from ringbuffer

    if (uart_rxstart == UART_RXBUFLEN)                                          // at end of rx buffer?
    {                                                                           // yes
        uart_rxstart = 0;                                                       // reset to beginning
    }

    __disable_irq();
    uart_rxsize--;                                                              // decrement size
    __enable_irq();

    return (ch);
}

#if 0
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc - non-interrupt version (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    uint_fast8_t ch;

    while (USART_GetFlagStatus(UART_NAME, USART_FLAG_RXNE) == RESET)            // wait until character available
    {
        if (mcurses_nodelay)
        {                                                                       // if nodelay set, return ERR
            return (ERR);
        }
    }
    ch = USART_ReceiveData(USART1);
    return ch;
}
#endif


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    mcurses_nodelay = flag;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (STM32FXX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    mcurses_halfdelay = tenths;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
    while (uart_txsize > 0)                                                     // tx buffer empty?
    {
        ;                                                                       // no, wait
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: UART RX/TX interrupt (STM32F4XX UART)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void UART_IRQ_HANDLER (void)
{
    static uint_fast8_t     uart_rxstop  = 0;                                   // tail
    uint16_t                value;
    uint_fast8_t            ch;

    if (USART_GetITStatus (UART_NAME, USART_IT_RXNE) != RESET)
    {
        USART_ClearITPendingBit (UART_NAME, USART_IT_RXNE);
        value = USART_ReceiveData (UART_NAME);

        ch = value & 0xFF;

        if (uart_rxsize < UART_RXBUFLEN)                                        // buffer full?
        {                                                                       // no
            uart_rxbuf[uart_rxstop++] = ch;                                     // store character

            if (uart_rxstop >= UART_RXBUFLEN)                                   // at end of ringbuffer?
            {                                                                   // yes
                uart_rxstop = 0;                                                // reset to beginning
            }

            uart_rxsize++;                                                      // increment used size
        }
    }

    if (USART_GetITStatus (UART_NAME, USART_IT_TXE) != RESET)
    {
        static uint_fast8_t  uart_txstart = 0;                                  // head
        uint_fast8_t         ch;

        USART_ClearITPendingBit (UART_NAME, USART_IT_TXE);

        if (uart_txsize > 0)                                                    // tx buffer empty?
        {                                                                       // no
            ch = uart_txbuf[uart_txstart++];                                    // get character to send, increment offset

            if (uart_txstart == UART_TXBUFLEN)                                  // at end of tx buffer?
            {                                                                   // yes
                uart_txstart = 0;                                               // reset to beginning
            }

            uart_txsize--;                                                      // decrement size

            USART_SendData(UART_NAME, ch);
        }
        else
        {
            USART_ITConfig(UART_NAME, USART_IT_TXE, DISABLE);                   // disable TXE interrupt
        }
    }
    else
    {
        ;
    }
    USART_ITConfig(UART_NAME, USART_IT_RXNE, ENABLE);
}

#else // MCURSES_UART_NUMBER < 0, use external console driver

#include "console.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init, done, putc, getc, nodelay, halfdelay, flush for STM32F4XX console
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (STM32F4XX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_init (void)
{
    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (STM32F4XX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (STM32F4XX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    console_putc (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (STM32F4XX  console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    uint_fast8_t    ch;

    ch = console_getc ();
    return (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (STM32F4XX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    mcurses_nodelay = flag;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (STM32FXX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    mcurses_halfdelay = tenths;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (STM32F4XX console)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
    console_flush ();
}

#endif // MCURSES_UART_NUMBER == 0

#else // AVR

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init, done, putc, getc for AVR
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#define BAUD                                    MCURSES_BAUD
#include <util/setbaud.h>

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * Newer ATmegas, e.g. ATmega88, ATmega168
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifdef UBRR0H

#define UART0_UBRRH                             UBRR0H
#define UART0_UBRRL                             UBRR0L
#define UART0_UCSRA                             UCSR0A
#define UART0_UCSRB                             UCSR0B
#define UART0_UCSRC                             UCSR0C
#define UART0_UDRE_BIT_VALUE                    (1<<UDRE0)
#define UART0_UCSZ1_BIT_VALUE                   (1<<UCSZ01)
#define UART0_UCSZ0_BIT_VALUE                   (1<<UCSZ00)
#ifdef URSEL0
#define UART0_URSEL_BIT_VALUE                   (1<<URSEL0)
#else
#define UART0_URSEL_BIT_VALUE                   (0)
#endif
#define UART0_TXEN_BIT_VALUE                    (1<<TXEN0)
#define UART0_RXEN_BIT_VALUE                    (1<<RXEN0)
#define UART0_RXCIE_BIT_VALUE                   (1<<RXCIE0)
#define UART0_UDR                               UDR0
#define UART0_U2X                               U2X0
#define UART0_RXC                               RXC0

#ifdef USART0_TXC_vect                                                  // e.g. ATmega162 with 2 UARTs
#define UART0_TXC_vect                          USART0_TXC_vect
#define UART0_RXC_vect                          USART0_RXC_vect
#define UART0_UDRE_vect                         USART0_UDRE_vect
#elif defined(USART0_TX_vect)                                           // e.g. ATmega644 with 2 UARTs
#define UART0_TXC_vect                          USART0_TX_vect
#define UART0_RXC_vect                          USART0_RX_vect
#define UART0_UDRE_vect                         USART0_UDRE_vect
#else                                                                   // e.g. ATmega168 with 1 UART
#define UART0_TXC_vect                          USART_TX_vect
#define UART0_RXC_vect                          USART_RX_vect
#define UART0_UDRE_vect                         USART_UDRE_vect
#endif

#define UART0_UDRIE                             UDRIE0

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ATmegas with 2nd UART, e.g. ATmega162
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#ifdef UBRR1H
#define UART1_UBRRH                             UBRR1H
#define UART1_UBRRL                             UBRR1L
#define UART1_UCSRA                             UCSR1A
#define UART1_UCSRB                             UCSR1B
#define UART1_UCSRC                             UCSR1C
#define UART1_UDRE_BIT_VALUE                    (1<<UDRE1)
#define UART1_UCSZ1_BIT_VALUE                   (1<<UCSZ11)
#define UART1_UCSZ0_BIT_VALUE                   (1<<UCSZ10)
#ifdef URSEL1
#define UART1_URSEL_BIT_VALUE                   (1<<URSEL1)
#else
#define UART1_URSEL_BIT_VALUE                   (0)
#endif
#define UART1_TXEN_BIT_VALUE                    (1<<TXEN1)
#define UART1_RXEN_BIT_VALUE                    (1<<RXEN1)
#define UART1_RXCIE_BIT_VALUE                   (1<<RXCIE1)
#define UART1_UDR                               UDR1
#define UART1_U2X                               U2X1
#define UART1_RXC                               RXC1

#ifdef USART1_TXC_vect                                                  // e.g. ATmega162 with 2 UARTs
#define UART1_TXC_vect                          USART1_TXC_vect
#define UART1_RXC_vect                          USART1_RXC_vect
#define UART1_UDRE_vect                         USART1_UDRE_vect
#else                                                                   // e.g. ATmega644 with 2 UARTs
#define UART1_TXC_vect                          USART1_TX_vect
#define UART1_RXC_vect                          USART1_RX_vect
#define UART1_UDRE_vect                         USART1_UDRE_vect
#endif

#define UART1_UDRIE                             UDRIE1
#endif // UBRR1H

#else

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * older ATmegas, e.g. ATmega8, ATmega16
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define UART0_UBRRH                             UBRRH
#define UART0_UBRRL                             UBRRL
#define UART0_UCSRA                             UCSRA
#define UART0_UCSRB                             UCSRB
#define UART0_UCSRC                             UCSRC
#define UART0_UDRE_BIT_VALUE                    (1<<UDRE)
#define UART0_UCSZ1_BIT_VALUE                   (1<<UCSZ1)
#define UART0_UCSZ0_BIT_VALUE                   (1<<UCSZ0)
#ifdef URSEL
#define UART0_URSEL_BIT_VALUE                   (1<<URSEL)
#else
#define UART0_URSEL_BIT_VALUE                   (0)
#endif
#define UART0_TXEN_BIT_VALUE                    (1<<TXEN)
#define UART0_RXEN_BIT_VALUE                    (1<<RXEN)
#define UART0_RXCIE_BIT_VALUE                   (1<<RXCIE)
#define UART0_UDR                               UDR
#define UART0_U2X                               U2X
#define UART0_RXC                               RXC
#define UART0_UDRE_vect                         USART_UDRE_vect
#define UART0_TXC_vect                          USART_TXC_vect
#define UART0_RXC_vect                          USART_RXC_vect
#define UART0_UDRIE                             UDRIE

#endif

#define UART_TXBUFLEN                           64                              // 64 Bytes ringbuffer for UART
#define UART_RXBUFLEN                           16                              // 16 Bytes ringbuffer for UART

static volatile uint_fast8_t uart_txbuf[UART_TXBUFLEN];                         // tx ringbuffer
static volatile uint_fast8_t uart_txsize = 0;                                   // tx size
static volatile uint_fast8_t uart_rxbuf[UART_RXBUFLEN];                         // rx ringbuffer
static volatile uint_fast8_t uart_rxsize = 0;                                   // rx size

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: init (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void uint_fast8_t
mcurses_phyio_init (void)
{
    UART0_UBRRH = UBRRH_VALUE;                                                  // set baud rate
    UART0_UBRRL = UBRRL_VALUE;

#if USE_2X
    UART0_UCSRA |= (1<<UART0_U2X);
#else
    UART0_UCSRA &= ~(1<<UART0_U2X);
#endif

    UART0_UCSRC = UART0_UCSZ1_BIT_VALUE | UART0_UCSZ0_BIT_VALUE | UART0_URSEL_BIT_VALUE;    // 8 bit, no parity
    UART0_UCSRB |= UART0_TXEN_BIT_VALUE | UART0_RXEN_BIT_VALUE | UART0_RXCIE_BIT_VALUE;     // enable UART TX, RX, and RX interrupt

    return 1;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: done (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_done (void)
{
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: putc (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_putc (uint_fast8_t ch)
{
    static uint_fast8_t uart_txstop  = 0;                                       // tail

    while (uart_txsize >= UART_TXBUFLEN)                                        // buffer full?
    {                                                                           // yes
        ;                                                                       // wait
    }

    uart_txbuf[uart_txstop++] = ch;                                             // store character

    if (uart_txstop >= UART_TXBUFLEN)                                           // at end of ringbuffer?
    {                                                                           // yes
        uart_txstop = 0;                                                        // reset to beginning
    }

    cli();
    uart_txsize++;                                                              // increment used size
    sei();

    UART0_UCSRB |= (1 << UART0_UDRIE);                                          // enable UDRE interrupt

#if 0
    while (!(UART0_UCSRA & UART0_UDRE_BIT_VALUE))
    {
        ;
    }

    UART0_UDR = ch;
#endif
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: getc (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static uint_fast8_t
mcurses_phyio_getc (void)
{
    static uint_fast8_t  uart_rxstart = 0;                                      // head
    uint_fast8_t         ch;

    while (uart_rxsize == 0)                                                    // rx buffer empty?
    {                                                                           // yes, wait
        if (mcurses_nodelay)
        {                                                                       // or if nodelay set, return ERR
            return (ERR);
        }
    }

    ch = uart_rxbuf[uart_rxstart++];                                            // get character from ringbuffer

    if (uart_rxstart == UART_RXBUFLEN)                                          // at end of rx buffer?
    {                                                                           // yes
        uart_rxstart = 0;                                                       // reset to beginning
    }

    cli();
    uart_rxsize--;                                                              // decrement size
    sei();

#if 0
    while (!(UART0_UCSRA & (1<<UART0_RXC)))                                     // character available?
    {                                                                           // no
        if (mcurses_nodelay)
        {                                                                       // if nodelay set, return ERR
            return (ERR);
        }
    }

    ch = UART0_UDR;                                                             // read character from UDRx
#endif
    return (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset nodelay (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_nodelay (uint_fast8_t flag)
{
    mcurses_nodelay = flag;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: set/reset halfdelay (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_halfdelay (uint_fast8_t tenths)
{
    mcurses_halfdelay = tenths;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: flush output (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_phyio_flush_output ()
{
    while (uart_txsize > 0)                                                     // tx buffer empty?
    {
        ;                                                                       // no, wait
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: UART interrupt handler, called if UART has received a character (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ISR(UART0_RXC_vect)
{
    static uint_fast8_t  uart_rxstop  = 0;                                      // tail
    uint_fast8_t         ch;

    ch = UART0_UDR;

    if (uart_rxsize < UART_RXBUFLEN)                                            // buffer full?
    {                                                                           // no
        uart_rxbuf[uart_rxstop++] = ch;                                         // store character

        if (uart_rxstop >= UART_RXBUFLEN)                                       // at end of ringbuffer?
        {                                                                       // yes
            uart_rxstop = 0;                                                    // reset to beginning
        }

        uart_rxsize++;                                                          // increment used size
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * PHYIO: UART interrupt handler, called if UART is ready to send a character (AVR)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
ISR(UART0_UDRE_vect)
{
    static uint_fast8_t  uart_txstart = 0;                                      // head
    uint_fast8_t         ch;

    if (uart_txsize > 0)                                                        // tx buffer empty?
    {                                                                           // no
        ch = uart_txbuf[uart_txstart++];                                        // get character to send, increment offset

        if (uart_txstart == UART_TXBUFLEN)                                      // at end of tx buffer?
        {                                                                       // yes
            uart_txstart = 0;                                                   // reset to beginning
        }

        uart_txsize--;                                                          // decrement size

        UART0_UDR = ch;                                                         // write character, don't wait
    }
    else
    {
        UART0_UCSRB &= ~(1 << UART0_UDRIE);                                     // tx buffer empty, disable UDRE interrupt
    }
}

#endif // !unix

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: put a character (raw)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_putc (uint_fast8_t ch)
{
    mcurses_phyio_putc (ch);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: put a string from flash (raw)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_puts_P (const char * str)
{
    uint_fast8_t ch;

    while ((ch = pgm_read_byte(str)) != '\0')
    {
        mcurses_putc (ch);
        str++;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: put a 3/2/1 digit integer number (raw)
 *
 * Here we don't want to use sprintf (too big on AVR/Z80) or itoa (not available on Z80)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mcurses_puti (uint_fast8_t i)
{
    uint_fast8_t ii;

    if (i >= 10)
    {
        if (i >= 100)
        {
            ii = i / 100;
            mcurses_putc (ii + '0');
            i -= 100 * ii;
        }

        ii = i / 10;
        mcurses_putc (ii + '0');
        i -= 10 * ii;
    }

    mcurses_putc (i + '0');
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: addch or insch a character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define CHARSET_G0      0
#define CHARSET_G1      1

static void
mcurses_addch_or_insch (uint_fast8_t ch, uint_fast8_t insert)
{
    static uint_fast8_t  charset = 0xff;
    static uint_fast8_t  insert_mode = FALSE;

    if (ch >= 0x80 && ch <= 0x9F)
    {
        if (charset != CHARSET_G1)
        {
            mcurses_putc ('\016');                                              // switch to G1 set
            charset = CHARSET_G1;
        }
        ch -= 0x20;                                                             // subtract offset to G1 characters
    }
    else
    {
        if (charset != CHARSET_G0)
        {
            mcurses_putc ('\017');                                              // switch to G0 set
            charset = CHARSET_G0;
        }
    }

    if (insert)
    {
        if (! insert_mode)
        {
            mcurses_puts_P (SEQ_INSERT_MODE);
            insert_mode = TRUE;
        }
    }
    else
    {
        if (insert_mode)
        {
            mcurses_puts_P (SEQ_REPLACE_MODE);
            insert_mode = FALSE;
        }
    }

    if (ch == '\r')
    {
        mcurses_putc (ch);
        mcurses_curx = 0;
    }
    else if (ch == '\n')
    {
        mcurses_putc (ch);

        if (mcurses_cury < LINES - 1)
        {
            mcurses_cury++;
        }
    }
    else if (mcurses_curx < COLS)
    {
        mcurses_putc (ch);
        mcurses_curx++;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: set scrolling region (raw)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mysetscrreg (uint_fast8_t top, uint_fast8_t bottom)
{
    if (top == bottom)
    {
        mcurses_puts_P (SEQ_RESET_SCRREG);                                      // reset scrolling region
    }
    else
    {
        mcurses_puts_P (SEQ_CSI);
        mcurses_puti (top + 1);
        mcurses_putc (';');
        mcurses_puti (bottom + 1);
        mcurses_putc ('r');
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * move cursor (raw)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
mymove (uint_fast8_t y, uint_fast8_t x)
{
    mcurses_puts_P (SEQ_CSI);
    mcurses_puti (y + 1);
    mcurses_putc (';');
    mcurses_puti (x + 1);
    mcurses_putc ('H');
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: initialize
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
initscr (void)
{
    uint_fast8_t y;
    uint_fast8_t x;
    uint_fast8_t rtc;

    if (mcurses_phyio_init ())
    {
        mymove (254, 254);                                                          // 255 + 1 = 256 is too much for PuTTY

        if (queryyx (&y, &x) == OK)
        {
            LINES = y + 1;
            COLS  = x + 1;
        }
        else
        {
            LINES = MCURSES_LINES;
            COLS  = MCURSES_COLS;
        }

        mcurses_scrl_end = LINES - 1;

        mcurses_puts_P (SEQ_LOAD_G1);                                               // load graphic charset into G1
        attrset (A_NORMAL);
        clear ();
        move (0, 0);
        mcurses_is_up = 1;
        rtc = OK;
    }
    else
    {
        rtc = ERR;
    }
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: add character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
addch (uint_fast8_t ch)
{
    mcurses_addch_or_insch (ch, FALSE);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: add string
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
addstr (const char * str)
{
    while (*str)
    {
        mcurses_addch_or_insch (*str++, FALSE);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: add string
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
addstr_P (const char * str)
{
    uint_fast8_t ch;

    while ((ch = pgm_read_byte(str)) != '\0')
    {
        mcurses_addch_or_insch (ch, FALSE);
        str++;
    }
}

#if defined(unix) || defined(STM32F4XX)
/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  MCURSES: add formatted string (va_list)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
vprintw (const char * fmt, va_list ap)
{
    static char str_buf[256];

    (void) vsprintf ((char *) str_buf, fmt, ap);
    (void) addstr (str_buf);
} /* vprintw (fmt, ap) */


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 *  MCURSES: add formatted string (variable numer of arguments)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
printw (const char * fmt, ...)
{
    va_list ap;

    va_start (ap, fmt);
    vprintw (fmt, ap);
    va_end (ap);
} /* printw (fmt, ...) */

#if defined(STM32F4XX)
// (v)sprintf needs it
caddr_t _sbrk(int increment)
{
    extern char end asm("end");
    register char * pStack asm("sp");
    static char *   s_pHeapEnd;

    if (!s_pHeapEnd)
    {
        s_pHeapEnd = &end;
    }

    if (s_pHeapEnd + increment > pStack)
    {
        return (caddr_t) -1;
    }

    char * pOldHeapEnd = s_pHeapEnd;
    s_pHeapEnd += increment;
    return (caddr_t) pOldHeapEnd;
}
#endif
#endif

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: set attribute(s)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
attrset (uint_fast16_t attr)
{
    static uint_fast16_t    mcurses_attr = 0xffff;                    // current attributes
    uint_fast8_t            idx;

    if (attr != mcurses_attr)
    {
        mcurses_puts_P (SEQ_ATTRSET);

        idx = (attr & F_COLOR) >> 8;

        if (idx >= 1 && idx <= 8)
        {
            mcurses_puts_P (SEQ_ATTRSET_FCOLOR);
            mcurses_putc (idx - 1 + '0');
        }

        idx = (attr & B_COLOR) >> 12;

        if (idx >= 1 && idx <= 8)
        {
            mcurses_puts_P (SEQ_ATTRSET_BCOLOR);
            mcurses_putc (idx - 1 + '0');
        }

        if (attr & A_REVERSE)
        {
            mcurses_puts_P (SEQ_ATTRSET_REVERSE);
        }
        if (attr & A_UNDERLINE)
        {
            mcurses_puts_P (SEQ_ATTRSET_UNDERLINE);
        }
        if (attr & A_BLINK)
        {
            mcurses_puts_P (SEQ_ATTRSET_BLINK);
        }
        if (attr & A_BOLD)
        {
            mcurses_puts_P (SEQ_ATTRSET_BOLD);
        }
        if (attr & A_DIM)
        {
            mcurses_puts_P (SEQ_ATTRSET_DIM);
        }
        mcurses_putc ('m');
        mcurses_attr = attr;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: move cursor
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
move (uint_fast8_t y, uint_fast8_t x)
{
    if (mcurses_cury != y || mcurses_curx != x)
    {
        mcurses_cury = y;
        mcurses_curx = x;
        mymove (y, x);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: delete line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
deleteln (void)
{
    mysetscrreg (mcurses_scrl_start, mcurses_scrl_end);                         // set scrolling region
    mymove (mcurses_cury, 0);                                                   // goto to current line
    mcurses_puts_P (SEQ_DELETELINE);                                            // delete line
    mysetscrreg (0, 0);                                                         // reset scrolling region
    mymove (mcurses_cury, mcurses_curx);                                        // restore position
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: insert line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
insertln (void)
{
    mysetscrreg (mcurses_cury, mcurses_scrl_end);                               // set scrolling region
    mymove (mcurses_cury, 0);                                                   // goto to current line
    mcurses_puts_P (SEQ_INSERTLINE);                                            // insert line
    mysetscrreg (0, 0);                                                         // reset scrolling region
    mymove (mcurses_cury, mcurses_curx);                                        // restore position
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: scroll
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
scroll (void)
{
    mysetscrreg (mcurses_scrl_start, mcurses_scrl_end);                         // set scrolling region
    mymove (mcurses_scrl_end, 0);                                               // goto to last line of scrolling region
    mcurses_puts_P (SEQ_NEXTLINE);                                              // next line
    mysetscrreg (0, 0);                                                         // reset scrolling region
    mymove (mcurses_cury, mcurses_curx);                                        // restore position
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: clear
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
clear (void)
{
    mcurses_puts_P (SEQ_CLEAR);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: clear to bottom of screen
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
clrtobot (void)
{
    mcurses_puts_P (SEQ_CLRTOBOT);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: clear to end of line
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
clrtoeol (void)
{
    if (mcurses_curx < COLS)
    {
        mcurses_puts_P (SEQ_CLRTOEOL);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: delete character at cursor position
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
delch (void)
{
    mcurses_puts_P (SEQ_DELCH);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: insert character
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
insch (uint_fast8_t ch)
{
    mcurses_addch_or_insch (ch, TRUE);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: set scrolling region
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
setscrreg (uint_fast8_t t, uint_fast8_t b)
{
    mcurses_scrl_start = t;
    mcurses_scrl_end = b;
}

void
curs_set (uint_fast8_t visibility)
{
    mcurses_puts_P (SEQ_CURSOR_VIS);

    if (visibility == 0)
    {
        mcurses_putc ('l');
    }
    else
    {
        mcurses_putc ('h');
    }
}


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: refresh: flush output
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
refresh (void)
{
    mcurses_phyio_flush_output ();
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: set/reset nodelay
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
nodelay (uint_fast8_t flag)
{
    if (mcurses_nodelay != flag)
    {
        mcurses_phyio_nodelay (flag);
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: set/reset halfdelay
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
halfdelay (uint_fast8_t tenths)
{
    mcurses_phyio_halfdelay (tenths);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: read key
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define MAX_KEYS                ((KEY_F1 + 12) - 0x80)

static const char * function_keys[MAX_KEYS] =
{
    "B",                        // KEY_DOWN                 0x80                // Down arrow key
    "A",                        // KEY_UP                   0x81                // Up arrow key
    "D",                        // KEY_LEFT                 0x82                // Left arrow key
    "C",                        // KEY_RIGHT                0x83                // Right arrow key
    "G",                        // KEY_CENTER               0x84                // 5 on Keypad when keypad-mode is off
    "1~",                       // KEY_HOME                 0x85                // Home key
    "3~",                       // KEY_DC                   0x86                // Delete character key
    "2~",                       // KEY_IC                   0x87                // Ins char/toggle ins mode key
    "6~",                       // KEY_NPAGE                0x88                // Next-page key
    "5~",                       // KEY_PPAGE                0x89                // Previous-page key
    "4~",                       // KEY_END                  0x8A                // End key
    "Z",                        // KEY_BTAB                 0x8B                // Back tab key
    "OB",                       // KEY_SDOWN                0x8C                // Down arrow key
    "OA",                       // KEY_SUP                  0x8D                // Up arrow key
    "OD",                       // KEY_SLEFT                0x8E                // Left arrow key
    "OC",                       // KEY_SRIGHT               0x8F                // Right arrow key
    "OG",                       // KEY_SCENTER              0x90                // SHIFT 5 on Keypad when keypad-mode is off

#if 0 // VT400:
    "11~",                      // KEY_F(1)                 0x91                // Function key F1
    "12~",                      // KEY_F(2)                 0x92                // Function key F2
    "13~",                      // KEY_F(3)                 0x93                // Function key F3
    "14~",                      // KEY_F(4)                 0x94                // Function key F4
    "15~",                      // KEY_F(5)                 0x95                // Function key F5
#else // Linux console or PuTTY
    "[A",                       // KEY_F(1)                 0x91                // Function key F1
    "[B",                       // KEY_F(2)                 0x92                // Function key F2
    "[C",                       // KEY_F(3)                 0x93                // Function key F3
    "[D",                       // KEY_F(4)                 0x94                // Function key F4
    "[E",                       // KEY_F(5)                 0x95                // Function key F5
#endif

    "17~",                      // KEY_F(6)                 0x96                // Function key F6
    "18~",                      // KEY_F(7)                 0x97                // Function key F7
    "19~",                      // KEY_F(8)                 0x98                // Function key F8
    "20~",                      // KEY_F(9)                 0x99                // Function key F9
    "21~",                      // KEY_F(10)                0x9A                // Function key F10
    "23~",                      // KEY_F(11)                0x9B                // Function key F11
    "24~"                       // KEY_F(12)                0x9C                // Function key F12
};

uint_fast8_t
getch (void)
{
    char    buf[4];
    uint_fast8_t ch;
    uint_fast8_t idx;

    refresh ();
    ch = mcurses_phyio_getc ();

    if (ch == 0x7F)                                                             // BACKSPACE on VT200 sends DEL char
    {
        ch = KEY_BACKSPACE;                                                     // map it to '\b'
    }
    else if (ch == '\033')                                                      // ESCAPE
    {
        while ((ch = mcurses_phyio_getc ()) == ERR)
        {
            ;
        }

        if (ch == '\033')                                                       // 2 x ESCAPE
        {
            return KEY_ESCAPE;
        }
        else if (ch == '[')
        {
            for (idx = 0; idx < 3; idx++)
            {
                while ((ch = mcurses_phyio_getc ()) == ERR)
                {
                    ;
                }

                buf[idx] = ch;

                if ((ch >= 'A' && ch <= 'Z') || ch == '~')
                {
                    idx++;
                    break;
                }
            }

            buf[idx] = '\0';

            for (idx = 0; idx < MAX_KEYS && function_keys[idx]; idx++)
            {
                if (! strcmp (buf, function_keys[idx]))
                {
                    ch = idx + 0x80;
                    break;
                }
            }

            if (idx == MAX_KEYS || ! function_keys[idx])
            {
                ch = ERR;
            }
        }
        else if (ch == 'O')
        {
            while ((ch = mcurses_phyio_getc ()) == ERR)
            {
                ;
            }

            if (ch == 'A')
            {
                ch = KEY_SUP;
            }
            else if (ch == 'B')
            {
                ch = KEY_SDOWN;
            }
            else if (ch == 'C')
            {
                ch = KEY_SRIGHT;
            }
            else if (ch == 'D')
            {
                ch = KEY_SLEFT;
            }
            else
            {
                ch = ERR;
            }
        }
        else
        {
            ch = ERR;
        }
    }

    return ch;
}

int
queryyx (uint_fast8_t * yp, uint_fast8_t * xp)
{
    char            ybuf[8];
    char            xbuf[8];
    int             yidx = 0;
    int             xidx = 0;
    int             rtc = ERR;
    uint_fast8_t    got_answer = FALSE;
    uint_fast8_t    ch;

#if 0
    while (uart_rxsize > 0)                                                         // flush input (dirty hack, works only with UART)
    {
        (void) mcurses_phyio_getc ();
    }
#endif

    mcurses_puts_P ("\033[6n");                                                     // get cursor position

#if (defined STM32F4XX)
    delay_set_timout (100);                                                         // set timeout: 1/10 sec

    while (! delay_got_timeout ())
    {
        if (console_get_rxsize() > 0)                                               // check for answer (dirty hack, works only with UART)
        {
            got_answer = TRUE;
            break;
        }
    }
#else
    got_answer = TRUE;                                                              // that's a lie, but works
#endif

    if (got_answer)
    {
        ch = mcurses_phyio_getc ();

        if (ch == '\033')
        {
            ch = mcurses_phyio_getc ();

            if (ch == '[')
            {
                while ((ch = mcurses_phyio_getc ()) != ';')
                {
                    if (yidx < 8 - 1)
                    {
                        ybuf[yidx++] = ch;
                    }
                }

                ybuf[yidx] = '\0';

                while ((ch = mcurses_phyio_getc ()) != 'R')
                {
                    if (xidx < 8 - 1)
                    {
                        xbuf[xidx++] = ch;
                    }
                }

                xbuf[xidx] = '\0';
                mcurses_cury = atoi (ybuf) - 1;
                *yp = mcurses_cury;
                mcurses_curx = atoi (xbuf) - 1;
                *xp = mcurses_curx;
                rtc = OK;
            }
        }
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: read string (with mini editor built-in)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
getnstr (char * str, uint_fast8_t maxlen)
{
    uint_fast8_t    ch;
    uint_fast8_t    curlen = 0;
    uint_fast8_t    curpos = 0;
    uint_fast8_t    starty;
    uint_fast8_t    startx;
    uint_fast8_t    i;

    maxlen--;                               // reserve one byte in order to store '\0' in last position

    getyx (starty, startx);                 // get current cursor position

    if (*str)
    {
        addstr (str);
        curpos = strlen (str);
        curlen = curpos;
    }

    while ((ch = getch ()) != KEY_CR && ch != KEY_ESCAPE)
    {
        switch (ch)
        {
            case KEY_LEFT:
                if (curpos > 0)
                {
                    curpos--;
                }
                break;
            case KEY_RIGHT:
                if (curpos < curlen)
                {
                    curpos++;
                }
                break;
            case KEY_HOME:
                curpos = 0;
                break;
            case KEY_END:
                curpos = curlen;
                break;
            case KEY_BACKSPACE:
                if (curpos > 0)
                {
                    curpos--;
                    curlen--;
                    move (starty, startx + curpos);

                    for (i = curpos; i < curlen; i++)
                    {
                        str[i] = str[i + 1];
                    }
                    str[i] = '\0';
                    delch();
                }
                break;

            case KEY_DC:
                if (curlen > 0)
                {
                    curlen--;
                    for (i = curpos; i < curlen; i++)
                    {
                        str[i] = str[i + 1];
                    }
                    str[i] = '\0';
                    delch();
                }
                break;

            default:
                if (curlen < maxlen && (ch & 0x7F) >= 32 && (ch & 0x7F) < 127)      // printable ascii 7bit or printable 8bit ISO8859
                {
                    for (i = curlen; i > curpos; i--)
                    {
                        str[i] = str[i - 1];
                    }
                    insch (ch);
                    str[curpos] = ch;
                    curpos++;
                    curlen++;
                }
        }
        move (starty, startx + curpos);
    }

    if (ch == KEY_ESCAPE)
    {
        curlen = 0;
    }

    str[curlen] = '\0';
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * MCURSES: endwin
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
endwin (void)
{
    mcurses_puts_P(SEQ_ATTRIBUTES_OFF);                                        // reset attributes
    move (LINES - 1, 0);                                                        // move cursor to last line
    clrtoeol ();                                                                // clear this line
    mcurses_putc ('\017');                                                      // switch to G0 set
    curs_set (TRUE);                                                            // show cursor
    mcurses_puts_P(SEQ_REPLACE_MODE);                                           // reset insert mode
    refresh ();                                                                 // flush output
    mcurses_phyio_done ();                                                      // end of physical I/O
    mcurses_is_up = 0;
}
