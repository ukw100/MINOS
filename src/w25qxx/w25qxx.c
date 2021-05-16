/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * wq25xx.c - W25Qxx driver
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * STM32F407VET6 Black Board:
 *
 * F_CS         PB0
 * SPI1 CLK     PB3
 * SPI1 MISO    PB4
 * SPI1 MOSI    PB5
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2018 - 2021 Frank Meyer - frank(at)fli4l.de
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
#include "w25qxx.h"
#include "io.h"

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * SPI for data: DMA1, channel 3, SPI1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
// SPI device
#define W25QXX_SPI_DEVICE             SPI1                                              // SPI device
#define W25QXX_SPI_CLOCK_CMD          RCC_APB2PeriphClockCmd                            // APB1
#define W25QXX_SPI_CLOCK              RCC_APB2Periph_SPI1                               // APB1 SPI1

// GPIO SPI:
#define W25QXX_GPIO_CLOCK_CMD         RCC_AHB1PeriphClockCmd                            // AHB1
#define W25QXX_GPIO_CLOCK             RCC_AHB1Periph_GPIOB                              // AHB1 GPIOB
#define W25QXX_GPIO_PORT              GPIOB                                             // GPIOB
#define W25QXX_GPIO_CS_PIN            GPIO_Pin_0                                        // /CS = PB0
#define W25QXX_GPIO_CS_SOURCE         GPIO_PinSource0
#define W25QXX_GPIO_SCK_PIN           GPIO_Pin_3                                        // SCK = PB3
#define W25QXX_GPIO_SCK_SOURCE        GPIO_PinSource3
#define W25QXX_GPIO_MISO_PIN          GPIO_Pin_4                                        // MOSI = PB4
#define W25QXX_GPIO_MISO_SOURCE       GPIO_PinSource4
#define W25QXX_GPIO_MOSI_PIN          GPIO_Pin_5                                        // MOSI = PB5
#define W25QXX_GPIO_MOSI_SOURCE       GPIO_PinSource5
#define W25QXX_SPI_AF                 GPIO_AF_SPI1                                      // GPIO SPI1 alternate function

// DMA
#define W25QXX_DMA_CLOCK_CMD          RCC_AHB1PeriphClockCmd                            // AHB1
#define W25QXX_DMA_CLOCK              RCC_AHB1Periph_DMA2                               // AHB1 DMA2
#define W25QXX_DMA_CHANNEL            DMA_Channel_3                                     // DMA  Channel3

#define W25QXX_DMA_TX_STREAM          DMA2_Stream3                                      // DMA2 Stream3 for TX
#define W25QXX_DMA_RX_STREAM          DMA2_Stream0                                      // DMA2 Stream0 for RX

// transfer complete interrupt - DMA1, Stream0 (RX channel)
#define W25QXX_DMA_RX_IRQn            DMA2_Stream0_IRQn                                 // DMA2 Stream0 IRQn
#define W25QXX_DMA_RX_ISR             DMA2_Stream0_IRQHandler                           // DMA2 Stream0 IRQ Handler
#define W25QXX_DMA_RX_IRQ_FLAG        DMA_IT_TCIF0                                      // transfer complete on Stream0

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * DMA buffer
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */

#define W25QXX_BUFLEN                  (256 + 256)

static volatile uint32_t                w25qxx_dma_status;                              // DMA status
static volatile uint8_t                 w25qxx_in[W25QXX_BUFLEN];                       // 16bit DMA buffer, must be aligned to 16 bit
static volatile uint8_t                 w25qxx_out[W25QXX_BUFLEN];                      // 16bit DMA buffer, must be aligned to 16 bit


/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * INTERN: initialize DMA
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
w25qxx_dma_init (void)
{
    DMA_InitTypeDef dma;

    DMA_StructInit (&dma);

    DMA_Cmd(W25QXX_DMA_TX_STREAM, DISABLE);
    DMA_Cmd(W25QXX_DMA_RX_STREAM, DISABLE);
    DMA_DeInit(W25QXX_DMA_TX_STREAM);
    DMA_DeInit(W25QXX_DMA_RX_STREAM);

    dma.DMA_Mode                = DMA_Mode_Normal;                          // normal dma mode
    dma.DMA_PeripheralBaseAddr  = (uint32_t)&W25QXX_SPI_DEVICE->DR;         // base addr of periph
    dma.DMA_PeripheralDataSize  = DMA_PeripheralDataSize_Byte;              // use 8 bit, 16 bit: DMA_PeripheralDataSize_HalfWord
    dma.DMA_MemoryDataSize      = DMA_MemoryDataSize_Byte;                  // use 8 bit, 16 bit: DMA_MemoryDataSize_HalfWord
    dma.DMA_BufferSize          = W25QXX_BUFLEN;                            // buffer size
    dma.DMA_PeripheralInc       = DMA_PeripheralInc_Disable;                // disable periph inc
    dma.DMA_MemoryInc           = DMA_MemoryInc_Enable;                     // enable memory inc
    dma.DMA_Priority            = DMA_Priority_High;                        // or DMA_Priority_VeryHigh;
    dma.DMA_FIFOMode            = DMA_FIFOMode_Disable;
    dma.DMA_FIFOThreshold       = DMA_FIFOThreshold_1QuarterFull;           // or DMA_FIFOThreshold_HalfFull
    dma.DMA_MemoryBurst         = DMA_MemoryBurst_Single;
    dma.DMA_PeripheralBurst     = DMA_PeripheralBurst_Single;

    // DMA TX
    dma.DMA_DIR                 = DMA_DIR_MemoryToPeripheral;
    dma.DMA_Channel             = W25QXX_DMA_CHANNEL;
    dma.DMA_Memory0BaseAddr     = (uint32_t) w25qxx_out;
    DMA_Init(W25QXX_DMA_TX_STREAM, &dma);

    // DMA RX
    dma.DMA_DIR                 = DMA_DIR_PeripheralToMemory;
    dma.DMA_Channel             = W25QXX_DMA_CHANNEL;
    dma.DMA_Memory0BaseAddr     = (uint32_t) w25qxx_in;
    DMA_Init(W25QXX_DMA_RX_STREAM, &dma);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * start DMA (stopped when Transfer-Complete-Interrupt arrives)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
w25qxx_dma_start (int buffersize)
{
    while (w25qxx_dma_status != 0)
    {
        ;                                                                           // wait until DMA transfer is ready
    }

    w25qxx_dma_status = 1;                                                          // set status to "busy"

#if 1
    w25qxx_dma_init ();                                                             // TODO!
#else
    DMA_Cmd (W25QXX_DMA_TX_STREAM, DISABLE);                                        // disable DMA TX
    DMA_Cmd (W25QXX_DMA_RX_STREAM, DISABLE);                                        // disable DMA RX
    DMA_DeInit(W25QXX_DMA_TX_STREAM);
    DMA_DeInit(W25QXX_DMA_RX_STREAM);
#endif

    DMA_SetCurrDataCounter(W25QXX_DMA_TX_STREAM, buffersize);                       // set new buffer size for TX
    DMA_SetCurrDataCounter(W25QXX_DMA_RX_STREAM, buffersize);                       // set new buffer size for RX
    DMA_ITConfig(W25QXX_DMA_RX_STREAM, DMA_IT_TC, ENABLE);                          // enable transfer complete interrupt (only for RX)
    GPIO_RESET_BIT(W25QXX_GPIO_PORT, W25QXX_GPIO_CS_PIN);                           // set /CS to Low

    DMA_Cmd(W25QXX_DMA_TX_STREAM, ENABLE);                                          // DMA enable TX
    DMA_Cmd(W25QXX_DMA_RX_STREAM, ENABLE);                                          // DMA enable RX
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * ISR DMA (will be called, when all data has been transferred)
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void W25QXX_DMA_RX_ISR (void);
void
W25QXX_DMA_RX_ISR (void)
{
    if (DMA_GetITStatus(W25QXX_DMA_RX_STREAM, W25QXX_DMA_RX_IRQ_FLAG))              // check transfer complete interrupt flag
    {
        DMA_ClearITPendingBit (W25QXX_DMA_RX_STREAM, W25QXX_DMA_RX_IRQ_FLAG);       // reset flag
        w25qxx_dma_status = 0;                                                      // set status to ready
        GPIO_SET_BIT(W25QXX_GPIO_PORT, W25QXX_GPIO_CS_PIN);                         // set /CS to High
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * read device id
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
w25qxx_device_id (void)
{
    uint_fast8_t    device_id;
    int             buffersize;

    buffersize = 5;
    w25qxx_out[0] = 0xAB;
    w25qxx_out[1] = 0x00;
    w25qxx_out[2] = 0x00;
    w25qxx_out[3] = 0x00;
    w25qxx_out[4] = 0x00;

    w25qxx_dma_start(buffersize);

    while (w25qxx_dma_status != 0)
    {
        ;
    }

    device_id = w25qxx_in[4];
    return device_id;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * read status register 1
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
w25qxx_statusreg1 (void)
{
    int             buffersize;
    uint_fast8_t    rtc;

    buffersize = 2;
    w25qxx_out[0] = 0x05;
    w25qxx_out[1] = 0x00;

    w25qxx_dma_start(buffersize);

    while (w25qxx_dma_status != 0)
    {
        ;
    }

    rtc = w25qxx_in[1];
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * read status register 2
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
uint_fast8_t
w25qxx_statusreg2 (void)
{
    int             buffersize;
    uint_fast8_t    rtc;

    buffersize = 2;
    w25qxx_out[0] = 0x35;
    w25qxx_out[1] = 0x00;

    w25qxx_dma_start(buffersize);

    while (w25qxx_dma_status != 0)
    {
        ;
    }

    rtc = w25qxx_in[1];
    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * read unique id
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
char *
w25qxx_unique_id (void)
{
    static char     unique_id[32];
    char *          p;
    int             buffersize;
    uint_fast8_t    idx;

    buffersize = 5 + 8;
    w25qxx_out[0] = 0x4B;
    w25qxx_out[1] = 0x00;
    w25qxx_out[2] = 0x00;
    w25qxx_out[3] = 0x00;
    w25qxx_out[4] = 0x00;

    w25qxx_dma_start(buffersize);

    while (w25qxx_dma_status != 0)
    {
        ;
    }

    p = unique_id;

    for (idx = 0; idx < 8; idx++)
    {
        sprintf (p, "%02x", w25qxx_in[5 + idx]);
        p += 2;
    }
    return unique_id;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * initialize W25QXX
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
w25qxx_init (void)
{
    GPIO_InitTypeDef        gpio;
    NVIC_InitTypeDef        nvic;

    w25qxx_dma_status = 0;

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize gpio
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    GPIO_StructInit (&gpio);
    W25QXX_GPIO_CLOCK_CMD (W25QXX_GPIO_CLOCK, ENABLE);              // clock enable

    gpio.GPIO_Pin     = W25QXX_GPIO_CS_PIN;

    gpio.GPIO_Mode    = GPIO_Mode_OUT;
    gpio.GPIO_OType   = GPIO_OType_PP;
    gpio.GPIO_PuPd    = GPIO_PuPd_NOPULL;
    gpio.GPIO_Speed   = GPIO_Speed_100MHz;
    GPIO_Init(W25QXX_GPIO_PORT, &gpio);
    GPIO_SET_BIT(W25QXX_GPIO_PORT, W25QXX_GPIO_CS_PIN);             // set /CS to High

    gpio.GPIO_Pin     = W25QXX_GPIO_SCK_PIN | W25QXX_GPIO_MISO_PIN | W25QXX_GPIO_MOSI_PIN;

    gpio.GPIO_Mode    = GPIO_Mode_AF;
    gpio.GPIO_OType   = GPIO_OType_PP;                              // fm: Ist das richtig für Eingang MISO?
    gpio.GPIO_PuPd    = GPIO_PuPd_DOWN;
    gpio.GPIO_Speed   = GPIO_Speed_100MHz;
    GPIO_Init(W25QXX_GPIO_PORT, &gpio);

    GPIO_RESET_BIT(W25QXX_GPIO_PORT, W25QXX_GPIO_SCK_PIN);          // set SCK to Low
    GPIO_PinAFConfig(W25QXX_GPIO_PORT, W25QXX_GPIO_SCK_SOURCE,  W25QXX_SPI_AF);
    GPIO_PinAFConfig(W25QXX_GPIO_PORT, W25QXX_GPIO_MISO_SOURCE, W25QXX_SPI_AF);
    GPIO_PinAFConfig(W25QXX_GPIO_PORT, W25QXX_GPIO_MOSI_SOURCE, W25QXX_SPI_AF);

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize SPI
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    SPI_InitTypeDef spi;

    W25QXX_SPI_CLOCK_CMD (W25QXX_SPI_CLOCK, ENABLE);                // clock enable (TIM)
    W25QXX_DMA_CLOCK_CMD (W25QXX_DMA_CLOCK, ENABLE);                // clock Enable (DMA)

    spi.SPI_BaudRatePrescaler   = SPI_BaudRatePrescaler_2;          // F4xx: APB2CLK/2 = 84/2 = 42 MHz
    spi.SPI_CPOL                = SPI_CPOL_Low;                     // passive state of SCK is low
    spi.SPI_CPHA                = SPI_CPHA_1Edge;                   // read data on rising edge, write data on falling edge (depends from CPOL!)
    spi.SPI_CRCPolynomial       = 0;                                // no CRC
    spi.SPI_DataSize            = SPI_DataSize_8b;                  // size of data word is 8 bit
    spi.SPI_Direction           = SPI_Direction_2Lines_FullDuplex;  // full duplex
    spi.SPI_FirstBit            = SPI_FirstBit_MSB;                 // MSB first
    spi.SPI_Mode                = SPI_Mode_Master;                  // Master
    spi.SPI_NSS                 = SPI_NSS_Soft;                     // NSS controlled by software, here not needed
    SPI_Init(W25QXX_SPI_DEVICE, &spi);

    SPI_I2S_DMACmd(W25QXX_SPI_DEVICE, SPI_I2S_DMAReq_Tx, ENABLE);   // enable DMA TX on SPI
    SPI_I2S_DMACmd(W25QXX_SPI_DEVICE, SPI_I2S_DMAReq_Rx, ENABLE);   // enable DMA RX on SPI

    SPI_Cmd(W25QXX_SPI_DEVICE, ENABLE);                             // SPI enable

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize NVIC
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    nvic.NVIC_IRQChannel                    = W25QXX_DMA_RX_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority  = 0;
    nvic.NVIC_IRQChannelSubPriority         = 0;
    nvic.NVIC_IRQChannelCmd                 = ENABLE;
    NVIC_Init(&nvic);

    /*---------------------------------------------------------------------------------------------------------------------------------------------------
     * initialize DMA
     *---------------------------------------------------------------------------------------------------------------------------------------------------
     */
    w25qxx_dma_init ();
}
