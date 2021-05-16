/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nic.h - declarations for nic interpreter
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
#include "nic-common.h"

#define                 RESULT_UNKNOWN          0x00
#define                 RESULT_INT              0x01
#define                 RESULT_CSTRING          0x02
#define                 RESULT_INT_ARRAY        0x04
#define                 RESULT_CSTRING_ARRAY    0x08
#define                 RESULT_BYTE_ARRAY       0x10

extern int              get_argument (FIP_RUN *, int argi, unsigned char **, int *);
extern int              get_argument_int (FIP_RUN *, int);
extern int              get_argument_byte (FIP_RUN *, int);
extern uint8_t *        get_argument_byte_ptr (FIP_RUN *, int);
extern unsigned char *  get_argument_string (FIP_RUN *, int);
extern int              nici (int, FIP_RUN *);
extern int              cmd_nic (int argc, const char **);
