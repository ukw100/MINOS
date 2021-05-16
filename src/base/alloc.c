/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * alloc.c - allocation handling routines for nic interpreter
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
#include <stdio.h>
#include <stdlib.h>

#include "alloc.h"

#if ALLOC_ENABLED == 1

#define MAX_SLOTS   1024

static int              max_slots_used  = 0;
static unsigned long    lo_mem_addr     = 0xFFFFFFFF;
static unsigned long    hi_mem_addr     = 0x00000000;

typedef struct
{
    char *          fname;
    int             line;
    unsigned long   addr;
    size_t          size;
} SLOT;

static SLOT slots[MAX_SLOTS];

static void
zero (char * str, char * fname, int line, void * addr, size_t size)
{
    fprintf (stderr, "%s line %d: zero %s addr: 0x%08lx size: %d\n", fname, line, str, (unsigned long) addr, size);
}

static void
malloc_slot (char * fname, int line, void * addr, size_t size)
{
    int     i;

    if (size == 0)
    {
        zero ("malloc", fname, line, addr, size);
    }

    for (i = 0; i < MAX_SLOTS; i++)
    {
        if (! slots[i].addr)
        {
            if (max_slots_used < i)
            {
                max_slots_used = i + 1;
            }

            if (lo_mem_addr > (unsigned long) addr)
            {
                lo_mem_addr = (unsigned long) addr;
            }

            if (hi_mem_addr < (unsigned long) addr + size)
            {
                hi_mem_addr = (unsigned long) addr + size;
            }

            slots[i].fname  = fname;
            slots[i].line   = line;
            slots[i].addr   = (unsigned long) addr;
            slots[i].size   = size;
            break;
        }
    }
}

static void
realloc_slot (char * fname, int line, void * old_addr, void * new_addr, size_t size)
{
    int     i;

    if (size == 0)
    {
        zero ("realloc", fname, line, new_addr, size);
    }

    for (i = 0; i < MAX_SLOTS; i++)
    {
        if (slots[i].addr == (unsigned long) old_addr)
        {
            slots[i].fname  = fname;
            slots[i].line   = line;
            slots[i].addr   = (unsigned long) new_addr;
            slots[i].size   = size;
            break;
        }
    }
}

static int
free_slot (char * fname, int line, void * addr)
{
    int     i;
    int     rtc = -1;

    for (i = 0; i < MAX_SLOTS; i++)
    {
        if (slots[i].addr == (unsigned long) addr)
        {
            // fprintf (stderr, "free_slot found: file: %s line: %d address 0x%08lx allocated: file: %s line: %d\n", fname, line, (unsigned long) addr, slots[i].fname, slots[i].line);
            slots[i].fname  = (char *) NULL;
            slots[i].line   = 0;
            slots[i].addr   = 0;
            slots[i].size   = 0;
            rtc = i;
            break;
        }
    }

    if (i == MAX_SLOTS)
    {
        fprintf (stderr, "free_slot: file: %s line: %d address 0x%08lx not allocated\n", fname, line, (unsigned long) addr);
    }

    return rtc;
}

void *
alloc_malloc (char * fname, int line, size_t size)
{
    void *  rtc;

    rtc = malloc (size);
    malloc_slot (fname, line, rtc, size);
    return rtc;
}

void *
alloc_realloc (char * fname, int line, void * ptr, size_t size)
{
    void *  rtc;

    rtc = realloc (ptr, size);
    realloc_slot (fname, line, ptr, rtc, size);
    return rtc;
}

void *
alloc_calloc (char * fname, int line, size_t nmemb, size_t size)
{
    void *  rtc;

    rtc = calloc (nmemb, size);
    malloc_slot (fname, line, rtc, nmemb * size);
    return rtc;
}

void
alloc_free (char * fname, int line, void * ptr)
{
    if (free_slot (fname, line, ptr) >= 0)
    {
        free (ptr);
    }
}

int
alloc_max_slots_used (void)
{
    return max_slots_used;
}

unsigned long
alloc_max_memory_used (void)
{
    if (hi_mem_addr > lo_mem_addr)
    {
        return hi_mem_addr - lo_mem_addr;
    }
    return 0;
}

void
alloc_list (void)
{
    int     header_printed = 0;
    int     sum = 0;
    int     i;

    for (i = 0; i < MAX_SLOTS; i++)
    {
        if (slots[i].addr)
        {
            if (! header_printed)
            {
                fprintf (stderr, "alloc list:\n");
                header_printed = 1;
            }

            fprintf (stderr, "%3d: file: %10s line: %5d addr: 0x%08lx size: %5d\n", i, slots[i].fname, slots[i].line, slots[i].addr, slots[i].size);
            sum += slots[i].size;
        }
    }
    if (sum > 0)
    {
        fprintf (stderr, "alloc sum = %5d\n", sum);
    }
}

void
alloc_free_holes (void)
{
    int     i;

    for (i = 0; i < MAX_SLOTS; i++)
    {
        if (slots[i].addr)
        {
            free ((void *) slots[i].addr);

            slots[i].fname  = (char *) NULL;
            slots[i].line   = 0;
            slots[i].addr   = 0;
            slots[i].size   = 0;
        }
    }
}

#endif
