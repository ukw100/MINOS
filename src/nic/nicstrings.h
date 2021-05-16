/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nicstrings.h - declarations for string handling routines
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

#define STRING_FLAG_NONE            0x00
#define STRING_FLAG_TEMP_ACTIVE     0x01

typedef struct
{
    int             siz;            // allocated size
    int             len;            // len
    unsigned char * str;            // C string
    int             flags;          // flags
} STRING;

extern STRING **                stringslots;
extern STRING **                tmp_stringslots;

extern int                      new_stringslot (unsigned char *);
void                            del_stringslots (int);
extern int                      new_tmp_stringslot (unsigned char *);
extern void                     deactivate_tmp_strings (void);
extern int                      length_of_string (STRING *);

extern const unsigned char *    str_of_string (STRING *);
extern STRING *                 copy_string2string (STRING *, STRING *);
extern STRING *                 copy_str2string (STRING *, unsigned char *);
extern STRING *                 concat_string2string (STRING *, STRING *);
extern STRING *                 concat_str2string (STRING *, unsigned char *);
extern void                     deallocate_strings (void);
extern void                     string_statistics (void);
