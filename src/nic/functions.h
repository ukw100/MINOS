/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * functions.h - declarations for nic interpreter runtime system
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

#define MAX_BUTTONS                 8
#define MAX_BUTTON_CNT              5                                   // if 5 times same state, button state is valid

typedef struct
{
    int port;
    int pin;
    int active_low;
    int pressed_cnt;
    int released_cnt;
    int pressed;
} BUTTON;

extern volatile                 BUTTON buttons[MAX_BUTTONS];
extern volatile int             buttons_used;

extern int      alarm_slots_used;
extern void     nici_alarm_reset_all ();
extern void     update_alarm_timers (void);
extern void     nici_file_close_all_open_files (void);
extern void     tft_reset_font (void);

extern int      (*nici_functions[])(FIP_RUN *);
