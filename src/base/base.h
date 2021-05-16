/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * base.h - declarations of base routines
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2016-2021 Frank Meyer - frank(at)fli4l.de
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
#ifndef BASE_H
#define BASE_H

#include <string.h>
#include <stdint.h>
#include <time.h>

#define u_atoi(s)   atoi ((char *) s)

enum
{
    DATE_CODE_NONE,                                                                         // no date code
    DATE_CODE_NEW_YEAR,                                                                     // Neujahr
    DATE_CODE_THREE_MAGI,                                                                   // Heilige drei K▒nige
    DATE_CODE_FIRST_MAY,                                                                    // Maifeiertag
    DATE_CODE_GERMANY_UNITY_DAY,                                                            // Tag der deutschen Einheit
    DATE_CODE_CHRISTMAS_DAY1,                                                               // 1. Weihnachtsfeiertag
    DATE_CODE_CHRISTMAS_DAY2,                                                               // 2. Weihnachtsfeiertag

    DATE_CODE_CARNIVAL_MONDAY,                                                              // Rosenmontag
    DATE_CODE_GOOD_FRIDAY,                                                                  // Karfreitag
    DATE_CODE_EASTER_SUNDAY,                                                                // Ostersonntag
    DATE_CODE_EASTER_MONDAY,                                                                // Ostermontag
    DATE_CODE_ASCENSION_DAY,                                                                // Christi Himmelfahrt
    DATE_CODE_PENTECOST_SUNDAY,                                                             // Pfingstsonntag
    DATE_CODE_PENTECOST_MONDAY,                                                             // Pfingstmontag
    DATE_CODE_CORPUS_CHRISTI,                                                               // Fronleichnam
    DATE_CODE_ADVENT1,                                                                      // 1st Advent
    DATE_CODE_ADVENT2,                                                                      // 2nd Advent
    DATE_CODE_ADVENT3,                                                                      // 3rd Advent
    DATE_CODE_ADVENT4,                                                                      // 4th Advent

    N_DATE_CODES
};

extern const char * wdays_en[7];
extern const char * wdays_de[7];

extern void             seconds_to_tm (struct tm *, uint32_t);
extern uint_fast16_t    add_days (uint_fast16_t, int, int);
extern int              dayofweek (int, int, int);
extern uint_fast8_t     days_of_month (uint_fast8_t, uint_fast16_t);
extern void             init_date_codes (int);
extern uint_fast8_t     get_date_code (uint_fast16_t, int);
uint_fast16_t           get_date_by_date_code (uint_fast8_t , int);
extern uint16_t         htoi (char *, uint8_t);
extern void             strsubst (char *, int, int);
#endif
