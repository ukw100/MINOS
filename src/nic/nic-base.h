/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * nic-base.h - some basic declarations
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

#ifndef TRUE
#define TRUE                                1
#endif

#ifndef FALSE
#define FALSE                               0
#endif

#ifndef OK
#define OK                                  0
#endif

#ifndef ERR
#define ERR                                 (-1)
#endif

#define ustrcpy(t,s)                        (void) strcpy  ((char *) (t), (char *) (s))
#define ustrncpy(t,s,n)                     (void) strncpy ((char *) (t), (char *) (s), (size_t) (n))
#define ustrcat(t,s)                        (void) strcat  ((char *) (t), (char *) (s))
#define ustrncat(t,s,n)                     (void) strncat ((char *) (t), (char *) (s), (size_t) (n))
#define ustrlen(s)                          (int) strlen  ((char *) (s))
#define ustrcmp(t,s)                        strcmp  ((char *) (t), (char *) (s))
#define ustrncmp(t,s,n)                     strncmp ((char *) (t), (char *) (s), (size_t) (n))
#define ustricmp(t,s)                       ustricmp_func ((unsigned char *) (t), (unsigned char *) (s))
#define ustrchr(s,c)                        (unsigned char *) strchr  ((char *) s, (int) c)
#define ustrrchr(s,c)                       (unsigned char *) strrchr ((char *) s, (int) c)
#define ustrpbrk(t,s)                       (unsigned char *) strpbrk ((char *) t, (char *) s)
#define ustrspn(t,s)                        (unsigned char *) strspn  ((char *) t, (char *) s)
#define ustrcspn(t,s)                       (unsigned char *) strcspn ((char *) t, (char *) s)
#define ustrtok(t,s)                        (unsigned char *) strtok  ((char *) t, (char *) s)
#if defined (WIN32)
#  define ustrdup(s)                        (unsigned char *) _strdup ((char *) (s))
#else
#  define ustrdup(s)                        (unsigned char *) strdup ((char *) (s))
#endif

#define uatoi(s)                            atoi ((char *) (s))

#if defined (unix) || defined (WIN32)
#  define console_printf(fmt, ...)          fprintf(stdout, fmt, ##__VA_ARGS__)
#  define console_puts(s)                   fputs (s, stdout)
#  define console_putc(ch)                  fputc (ch, stdout)
#elif defined(STM32F4XX)
#  include <stm32f4xx.h>
#  include "console.h"
#else
#  error unknown system platform
#endif
