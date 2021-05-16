/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * funclist.h - declarations of all intern functions of nic interpreter
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

#if DEFINE_FUNCTIONS == 0 && DEFINE_CFUNCTIONS == 0
typedef struct
{
    const char *            name;
    const int               min_args;
    const int               max_args;
    const int               return_type;
} FUNCTION_LIST;

#define ITEM(function,name,min,max,type)    { name,min,max,type }
FUNCTION_LIST function_list[] =

#elif DEFINE_FUNCTIONS == 1

#define ITEM(function,name,min,max,type)    function
int (*nici_functions[])(FIP_RUN *) =

#elif DEFINE_CFUNCTIONS == 1

#define ITEM(function,name,min,max,type)    name
char * nici_functions[] =

#endif

{
    // function                         name                        min     max     return
    ITEM(nici_console_putc,             "console.putc",             1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_console_print,            "console.print",            1,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_console_println,          "console.println",          1,      3,      FUNCTION_TYPE_INT),

    ITEM(nici_string_length,            "string.length",            1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_string_substring,         "string.substring",         2,      3,      FUNCTION_TYPE_STRING),
    ITEM(nici_string_tokens,            "string.tokens",            2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_string_get_token,         "string.get_token",         3,      3,      FUNCTION_TYPE_STRING),

    ITEM(nici_int_tochar,               "int.tochar",               1,      1,      FUNCTION_TYPE_STRING),

    ITEM(nici_polar_to_x,               "polar.to_x",               2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_polar_to_y,               "polar.to_y",               2,      2,      FUNCTION_TYPE_INT),

    ITEM(nici_time_start,               "time.start",               0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_time_stop,                "time.stop",                0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_time_delay,               "time.delay",               1,      1,      FUNCTION_TYPE_VOID),

    ITEM(nici_alarm_set,                "alarm.set",                1,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_alarm_check,              "alarm.check",              1,      1,      FUNCTION_TYPE_INT),

    ITEM(nici_date_datetime,            "date.datetime",            0,      0,      FUNCTION_TYPE_STRING),

    ITEM(nici_rtc_calibrate,            "rtc.calibrate",            2,      2,      FUNCTION_TYPE_INT),

    ITEM(nici_bit_set,                  "bit.set",                  2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bit_reset,                "bit.reset",                2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bit_toggle,               "bit.toggle",               2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bit_isset,                "bit.isset",                2,      2,      FUNCTION_TYPE_INT),

    ITEM(nici_bitmask_and,              "bitmask.and",              2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bitmask_nand,             "bitmask.nand",             2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bitmask_or,               "bitmask.or",               2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bitmask_nor,              "bitmask.nor",              2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bitmask_xor,              "bitmask.xor",              2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_bitmask_xnor,             "bitmask.xnor",             2,      2,      FUNCTION_TYPE_INT),

    ITEM(nici_mcurses_initscr,          "mcurses.initscr",          0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_move,             "mcurses.move",             2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_attrset,          "mcurses.attrset",          1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_addch,            "mcurses.addch",            1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_mvaddch,          "mcurses.mvaddch",          3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_addstr,           "mcurses.addstr",           1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_mvaddstr,         "mcurses.mvaddstr",         3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_printw,           "mcurses.printw",           1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_mvprintw,         "mcurses.mvprintw",         3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_getnstr,          "mcurses.getnstr",          2,      2,      FUNCTION_TYPE_STRING),
    ITEM(nici_mcurses_mvgetnstr,        "mcurses.mvgetnstr",        4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_setscrreg,        "mcurses.setscrreg",        2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_deleteln,         "mcurses.deleteln",         0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_insertln,         "mcurses.insertln",         0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_scroll,           "mcurses.scroll",           0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_clear,            "mcurses.clear",            0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_erase,            "mcurses.erase",            0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_clrtobot,         "mcurses.clrtobot",         0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_clrtoeol,         "mcurses.clrtoeol",         0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_delch,            "mcurses.delch",            0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_mvdelch,          "mcurses.mvdelch",          2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_insch,            "mcurses.insch",            1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_mvinsch,          "mcurses.mvinsch",          3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_nodelay,          "mcurses.nodelay",          1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_halfdelay,        "mcurses.halfdelay",        1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_getch,            "mcurses.getch",            0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_curs_set,         "mcurses.curs_set",         1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_refresh,          "mcurses.refresh",          0,      0,      FUNCTION_TYPE_VOID),
    ITEM(nici_mcurses_gety,             "mcurses.gety",             0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_mcurses_getx,             "mcurses.getx",             0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_mcurses_endwin,           "mcurses.endwin",           0,      0,      FUNCTION_TYPE_VOID),

    ITEM(nici_gpio_init,                "gpio.init",                3,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_gpio_set,                 "gpio.set",                 2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_gpio_reset,               "gpio.reset",               2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_gpio_toggle,              "gpio.toggle",              2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_gpio_get,                 "gpio.get",                 2,      2,      FUNCTION_TYPE_INT),

    ITEM(nici_uart_init,                "uart.init",                3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_uart_rxchars,             "uart.rxchars",             1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_uart_getc,                "uart.getc",                1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_uart_putc,                "uart.putc",                2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_uart_print,               "uart.print",               2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_uart_println,             "uart.println",             2,      2,      FUNCTION_TYPE_VOID),

    ITEM(nici_ws2812_init,              "ws2812.init",              1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_ws2812_set,               "ws2812.set",               4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_ws2812_clear,             "ws2812.clear",             1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_ws2812_refresh,           "ws2812.refresh",           1,      1,      FUNCTION_TYPE_VOID),

    ITEM(nici_button_init,              "button.init",              3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_button_pressed,           "button.pressed",           1,      1,      FUNCTION_TYPE_INT),

    ITEM(nici_i2c_init,                 "i2c.init",                 3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_read,                 "i2c.read",                 4,      4,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_write,                "i2c.write",                4,      4,      FUNCTION_TYPE_INT),

    ITEM(nici_i2c_lcd_init,             "i2c.lcd.init",             5,      5,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_clear,            "i2c.lcd.clear",            0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_home,             "i2c.lcd.home",             0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_move,             "i2c.lcd.move",             2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_backlight,        "i2c.lcd.backlight",        1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_define_char,      "i2c.lcd.define",           2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_print,            "i2c.lcd.print",            1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_mvprint,          "i2c.lcd.mvprint",          3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_lcd_clrtoeol,         "i2c.lcd.clrtoeol",         0,      0,      FUNCTION_TYPE_INT),

    ITEM(nici_i2c_ds3231_init,          "i2c.ds3231.init",          3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_ds3231_set_date_time, "i2c.ds3231.set",           1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_ds3231_get_date_time, "i2c.ds3231.get",           0,      0,      FUNCTION_TYPE_STRING),

    ITEM(nici_i2c_at24c32_init,         "i2c.at24c32.init",         3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_at24c32_write,        "i2c.at24c32.write",        3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_i2c_at24c32_read,         "i2c.at24c32.read",         3,      3,      FUNCTION_TYPE_INT),

    ITEM(nici_file_open,                "file.open",                2,      2,      FUNCTION_TYPE_INT),
    ITEM(nici_file_getc,                "file.getc",                1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_file_putc,                "file.putc",                2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_file_readln,              "file.readln",              1,      1,      FUNCTION_TYPE_STRING),
    ITEM(nici_file_writeln,             "file.writeln",             2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_file_write,               "file.write",               2,      2,      FUNCTION_TYPE_VOID),
    ITEM(nici_file_tell,                "file.tell",                1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_file_seek,                "file.seek",                3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_file_eof,                 "file.eof",                 1,      1,      FUNCTION_TYPE_INT),
    ITEM(nici_file_close,               "file.close",               1,      1,      FUNCTION_TYPE_VOID),

    ITEM(nici_tft_init,                 "tft.init",                 1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_rgb64_to_color565,    "tft.rgb64_to_color565",    3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_tft_rgb256_to_color565,   "tft.rgb256_to_color565",   3,      3,      FUNCTION_TYPE_INT),
    ITEM(nici_tft_fadein_backlight,     "tft.fadein_backlight",     1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_fadeout_backlight,    "tft.fadeout_backlight",    1,      1,      FUNCTION_TYPE_VOID),

    ITEM(nici_tft_draw_pixel,           "tft.draw_pixel",           3,      3,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_horizontal_line, "tft.draw_horizontal_line", 4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_vertical_line,   "tft.draw_vertical_line",   4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_rectangle,       "tft.draw_rectangle",       5,      5,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_fill_rectangle,       "tft.fill_rectangle",       5,      5,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_fill_screen,          "tft.fill_screen",          1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_line,            "tft.draw_line",            5,      5,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_thick_line,      "tft.draw_thick_line",      5,      5,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_circle,          "tft.draw_circle",          4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_thick_circle,    "tft.draw_thick_circle",    4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_draw_image,           "tft.draw_image",           4,      4,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_fonts,                "tft.fonts",                0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_tft_set_font,             "tft.set_font",             1,      1,      FUNCTION_TYPE_VOID),
    ITEM(nici_tft_font_height,          "tft.font_height",          0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_tft_font_width,           "tft.font_width",           0,      0,      FUNCTION_TYPE_INT),
    ITEM(nici_tft_draw_string,          "tft.draw_string",          5,      5,      FUNCTION_TYPE_VOID),

    ITEM(flash_device_id,               "flash.device_id",          0,      0,      FUNCTION_TYPE_INT),
    ITEM(flash_statusreg1,              "flash.statusreg1",         0,      0,      FUNCTION_TYPE_INT),
    ITEM(flash_statusreg2,              "flash.statusreg2",         0,      0,      FUNCTION_TYPE_INT),
    ITEM(flash_unique_id,               "flash.unique_id",          0,      0,      FUNCTION_TYPE_STRING),
};
