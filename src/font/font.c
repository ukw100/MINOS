/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * font.c - TFT fonts
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
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#ifdef unix
#define TFT_HEIGHT  150
#define TFT_WIDTH   150
#else
#include "tft.h"
#endif

#if defined ILI9341 || defined SSD1963

#include "font.h"

#define FONT_05x08           0
#define FONT_05x12           1
#define FONT_06x08           2
#define FONT_06x10           3
#define FONT_08x08           4
#define FONT_08x12           5
#define FONT_08x14           6
#define FONT_10x16           7
#define FONT_12x16           8
#define FONT_12x20           9
#define FONT_16x26          10
#define FONT_22x36          11
#define FONT_24x40          12
#define FONT_32x53          13
#define N_FONTS             14

#include "font-iso88591-05x08.h"
#include "font-iso88591-05x12.h"
#include "font-iso88591-06x08.h"
#include "font-iso88591-06x10.h"
#include "font-iso88591-08x08.h"
#include "font-iso88591-08x12.h"
#include "font-iso88591-08x14.h"
#include "font-iso88591-10x16.h"
#include "font-iso88591-12x16.h"
#include "font-iso88591-12x20.h"
#include "font-iso88591-16x26.h"
#include "font-iso88591-22x36.h"
#include "font-iso88591-24x40.h"
#include "font-iso88591-32x53.h"

static const unsigned char * fonts[N_FONTS] =
{
    &font_05x08[0][0],
    &font_05x12[0][0],
    &font_06x08[0][0],
    &font_06x10[0][0],
    &font_08x08[0][0],
    &font_08x12[0][0],
    &font_08x14[0][0],
    &font_10x16[0][0],
    &font_12x16[0][0],
    &font_12x20[0][0],
    &font_16x26[0][0],
    &font_22x36[0][0],
    &font_24x40[0][0],
    &font_32x53[0][0],
};

static const uint_fast16_t font_widths[N_FONTS] =
{
    FONT_WIDTH_05x08,
    FONT_WIDTH_05x12,
    FONT_WIDTH_06x08,
    FONT_WIDTH_06x10,
    FONT_WIDTH_08x08,
    FONT_WIDTH_08x12,
    FONT_WIDTH_08x14,
    FONT_WIDTH_10x16,
    FONT_WIDTH_12x16,
    FONT_WIDTH_12x20,
    FONT_WIDTH_16x26,
    FONT_WIDTH_22x36,
    FONT_WIDTH_24x40,
    FONT_WIDTH_32x53,
};

static const uint_fast16_t font_heights[N_FONTS] =
{
    FONT_HEIGHT_05x08,
    FONT_HEIGHT_05x12,
    FONT_HEIGHT_06x08,
    FONT_HEIGHT_06x10,
    FONT_HEIGHT_08x08,
    FONT_HEIGHT_08x12,
    FONT_HEIGHT_08x14,
    FONT_HEIGHT_10x16,
    FONT_HEIGHT_12x16,
    FONT_HEIGHT_12x20,
    FONT_HEIGHT_16x26,
    FONT_HEIGHT_22x36,
    FONT_HEIGHT_24x40,
    FONT_HEIGHT_32x53,
};

#define BYTES_PER_ROW   ((font_widths[current_font] - 1) / 8 + 1)
#define BITS_PER_ROW    (8 * BYTES_PER_ROW)

static int      current_font = 0;

#ifdef unix // some test functions

static char screen[TFT_HEIGHT][TFT_WIDTH];

static int  last_row_used = 0;

void
tft_show_screen (void)
{
    int y;
    int x;

    for (y = 0; y < last_row_used; y++)
    {
        for (x = 0; x < TFT_WIDTH; x++)
        {
            if (screen[y][x])
            {
                putchar (screen[y][x]);
            }
            else
            {
                putchar (' ');
            }
        }
        putchar ('\n');
    }
}

void
tft_draw_pixel (int x, int y, int color565)
{
    if (x >= 0 && y >= 0 && x < TFT_WIDTH && y < TFT_HEIGHT)
    {
        if (color565)
        {
            screen[y][x] = 'X';
        }
        else
        {
            screen[y][x] = ' ';
        }
    }

    if (last_row_used < y)
    {
        last_row_used = y;
    }
}
#endif // unix

void
set_font (int font)
{
    if (font >= 0 && font < N_FONTS)
    {
        current_font = font;
    }
}

int
font_width (void)
{
    return font_widths[current_font];
}

int
number_of_fonts (void)
{
    return N_FONTS;
}

int
font_height (void)
{
    return font_heights[current_font];
}

void
draw_letter (unsigned char ch, uint_fast16_t y, uint_fast16_t x, uint_fast16_t fcolor565, uint_fast16_t bcolor565)
{
    uint_fast16_t   yy;
    uint_fast16_t   xx;
    uint_fast16_t   idx;
    uint32_t        font_line;
    uint_fast8_t    ii;
    uint_fast8_t    bytes_per_row;
    uint_fast8_t    bits_per_row;
    uint_fast16_t   current_row;
    uint_fast16_t   py;

    bytes_per_row   = BYTES_PER_ROW;
    bits_per_row    = BITS_PER_ROW;
    current_row     = bytes_per_row * font_heights[current_font] * ch;
    idx             = 0;

    if (y + font_heights[current_font] < TFT_HEIGHT && x + font_widths[current_font] < TFT_WIDTH)
    {
        for (yy = 0; yy < font_heights[current_font]; yy++)
        {
            font_line = 0;

            for (ii = 0; ii < bytes_per_row; ii++)
            {
                font_line |= fonts[current_font][current_row + idx] << (ii * 8);
                idx++;
            }

            py = y + yy;

#ifdef unix
            for (xx = 0, ii = bits_per_row - 1; xx < font_widths[current_font]; xx++, ii--)
            {
                if (font_line & (1 << ii))
                {
                    tft_draw_pixel (x + xx, py, fcolor565);
                }
                else
                {
                    tft_draw_pixel (x + xx, py, bcolor565);
                }
            }
#else // faster
            tft_set_area (x, x + font_widths[current_font] - 1, py, py);

            for (xx = 0, ii = bits_per_row - 1; xx < font_widths[current_font]; xx++, ii--)
            {
                if (font_line & (1 << ii))
                {
                    tft_write_data (fcolor565);
                }
                else
                {
                    tft_write_data (bcolor565);
                }
            }
#endif
        }
    }
}

void
draw_string (unsigned char * s, uint_fast16_t y, uint_fast16_t x, uint_fast16_t fcolor565, uint_fast16_t bcolor565)
{
    unsigned char * p;

    for (p = s; *p; p++)
    {
        draw_letter (*p, y, x, fcolor565, bcolor565);
        x += font_widths[current_font];
    }
}

#else // no TFT, define stubs

void
set_font (int font)
{
    (void) font;
}

int
font_width (void)
{
    return 0;
}

int
number_of_fonts (void)
{
    return 0;
}

int
font_height (void)
{
    return 0;
}

void
draw_string (unsigned char * s, uint_fast16_t y, uint_fast16_t x, uint_fast16_t fcolor565, uint_fast16_t bcolor565)
{
    (void) s, (void) y, (void) x, (void) fcolor565, (void) bcolor565;
}

#endif
