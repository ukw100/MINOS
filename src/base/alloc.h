/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * alloc.h - nic interpreter allocation routines
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

#if defined(unix)
#define ALLOC_ENABLED           1

#elif defined(WIN32)
#define ALLOC_ENABLED           1

#elif defined(STM32F407VE)
#define ALLOC_ENABLED           0

#else
#define ALLOC_ENABLED           0
#endif

#if ALLOC_ENABLED == 1

extern void *                   alloc_malloc (char *, int, size_t);
extern void *                   alloc_realloc (char *, int, void *, size_t);
extern void *                   alloc_calloc (char *, int, size_t, size_t);
extern void                     alloc_free (char *, int, void *);
extern int                      alloc_max_slots_used (void);
extern unsigned long            alloc_max_memory_used (void);
extern void                     alloc_list (void);
extern void                     alloc_free_holes (void);

#else

#define alloc_malloc(f,l,s)     malloc(s)
#define alloc_realloc(f,l,p,s)  realloc(p,s)
#define alloc_calloc(f,l,m,s)   calloc(m,s)
#define alloc_free(f,l,p)       free(p)
#define alloc_max_slots_used()  (-1)
#define alloc_max_memory_used() (0L)
#define alloc_list()
#define alloc_free_holes()

#endif
