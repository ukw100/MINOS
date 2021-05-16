/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fe.c - mini editor
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2018-2021 Frank Meyer - frank(at)fli4l.de
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
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "mcurses.h"

#ifdef unix
#include <sys/stat.h>
#else
#include "fs.h"
#endif

#define BUFFER_CHUNK_SIZE   1024

#define EOB_STRING          "*EOB*"
#define WINDOW_LINES        (LINES - 2)

#define TOP_LINE            0
#define BOTTOM_LINE         (LINES - 3)

#define TOP_EDIT_LINE       (TOP_LINE + 4)
#define BOTTOM_EDIT_LINE    (BOTTOM_LINE - 4)

#define STATUS_LINE         (LINES - 2)
#define PROMPT_LINE         (LINES - 1)

typedef struct
{
    const char *    fname;
    char *          buf;
    int             pos;
    int             size;
    int             gap_pos;
    int             gap_size;
    int             window_start;
    int             window_end;
    int             modified;
    int             select_pos;
    int             line;                                                       // line number
} BUFFER;

static int          wish_x = -1;

#define PASTE_BUFFER_ALLOC_GRANULARITY      256
static int          allocated_paste_buffer_len;
static int          used_paste_buffer_len;
static char *       paste_buffer;

static int
move_gap (BUFFER * bp, int pos)
{
    if (bp->gap_pos < pos)
    {
        int n = pos - bp->gap_pos;

        while (n--)
        {
            bp->buf[bp->gap_pos] = bp->buf[bp->gap_pos + bp->gap_size];
            bp->gap_pos++;
        }
    }
    else if (bp->gap_pos > pos)
    {
        int n = bp->gap_pos - pos;

        while (n--)
        {
            bp->buf[bp->gap_pos + bp->gap_size - 1] = bp->buf[bp->gap_pos - 1];
            bp->gap_pos--;
        }
    }
    return 1;
}

static int
char_at (BUFFER * bp, int pos)
{
    int ch;

    if (pos < bp->gap_pos)
    {
        ch = bp->buf[pos];
    }
    else
    {
        ch = bp->buf[pos + bp->gap_size];
    }

    return ch;
}

static int
search_backward (BUFFER * bp, int pos, int ch)
{
    while (pos >= 0 && char_at (bp, pos) != ch)
    {
        pos--;
    }

    return pos; // negative, if not found
}

static int
search_forward (BUFFER * bp, int pos, int ch)
{
    while (pos < bp->size && char_at (bp, pos) != ch)
    {
        pos++;
    }

    return pos; // == size, if not found
}

static int
display_buffer (BUFFER * bp, int pos)
{
    uint_fast8_t    lines       = 0;
    int             ch;
    int             y           = 0;
    int             x           = 0;

    move (TOP_LINE, 0);

    for (pos = 0; pos < bp->size; pos++)
    {
        if (pos == bp->pos)
        {
            bp->line = lines;                       // sets current line!
            getyx (y, x);                           // saves cursor position
        }

        ch = char_at (bp, pos);

        if (ch == '\n')
        {
            clrtoeol ();
            lines++;

            if (lines > BOTTOM_LINE)
            {
                break;
            }

            addch ('\r');
        }

        addch (ch);
    }

    move (y, x);

    return pos;
}

static void
show_buffer_status_line (BUFFER * bp, int total_update)
{
    int             savey;
    int             savex;
    uint_fast8_t    x;

    getyx (savey, savex);

    move (STATUS_LINE, 0);
    attrset (A_REVERSE);

    addch (' ');

    if (bp->modified)
    {
        addch ('*');
    }
    else
    {
        addch (' ');
    }

    addch (' ');

    if (bp->fname)
    {
        addstr (bp->fname);
    }

    // printw (" %5d", used_paste_buffer_len);                      // only for debugging

    if (total_update)
    {
        x = getx ();

        while (x < COLS - 10)
        {
            addch (' ');
            x++;
        }
    }
    else
    {
        move (STATUS_LINE, COLS - 10);
    }

    if (bp->select_pos >= 0)
    {
        addch ('M');
    }
    else
    {
        addch (' ');
    }

    addch (' ');
    printw ("%5d", bp->line + 1);

    x = getx ();

    if (total_update)
    {
        while (x < COLS)
        {
            addch (' ');
            x++;
        }
    }

    attrset (A_NORMAL);

    move (savey, savex);
}


static int
calculate_window_end (BUFFER * bp, int lines)
{
    int idx;
    int window_end = bp->window_start + 1;

    for (idx = 0; idx < lines && window_end < bp->size; idx++)
    {
        window_end = search_forward (bp, window_end, '\n');

        if (window_end < bp->size)
        {
            window_end++;
        }
    }

    return (window_end);
}

#if 000

static int  reverse = 0;

static void
bp_addch_or_insch (BUFFER * bp, int pos)
{
    if (bp->select_pos >= 0 &&
        ((bp->pos > bp->select_pos && pos >= bp->select_pos && pos < bp->pos) ||
        (bp->select_pos > bp->pos && pos >= bp->pos && pos < bp->select_pos)))
    {
        if (! reverse)
        {
            attrset (A_REVERSE);
            reverse = 1;
        }
    }
    else
    {
        if (reverse)
        {
            attrset (A_NORMAL);
            reverse = 0;
        }
    }
}

static void
bp_addch (BUFFER * bp, int pos)
{
    int ch;

    bp_addch_or_insch (bp, pos);
    ch = char_at (bp, pos);
    addch (ch);
}

static void
bp_insch (BUFFER * bp, int pos)
{
    int ch;

    bp_addch_or_insch (bp, pos);
    ch = char_at (bp, pos);
    insch (ch);
}

#endif // 000

static int
realloc_buffer (BUFFER * bp)
{
    move_gap (bp, bp->size);
    bp->buf = realloc (bp->buf, bp->size + bp->gap_size + BUFFER_CHUNK_SIZE);
    bp->gap_size += BUFFER_CHUNK_SIZE;
    return 1;
}

static void
bp_insert_ch (BUFFER * bp, int pos, int ch)
{
    if (bp->gap_size == 0)
    {
        realloc_buffer (bp);
    }

    move_gap (bp, pos);
    bp->buf[pos] = ch;
    bp->size++;
    bp->gap_size--;
    bp->gap_pos++;
}

static void
bp_del_ch (BUFFER * bp, int pos, int n)
{
    move_gap (bp, pos);
    bp->gap_size += n;
    bp->size -= n;
}

static void
bp_scroll_up (BUFFER * bp)
{
    int y;
    int x;
    int ch;
    int pos;
    int window_end  = bp->window_end;
    int size        = bp->size;

    getyx (y, x);

    scroll ();

    move (BOTTOM_LINE, 0);

    while (window_end < size && (ch = char_at (bp, window_end)) != '\n')
    {
#if 000
        bp_addch (bp, window_end);
#else
        addch (ch);
#endif
        window_end++;
    }

    if (window_end < bp->size)
    {
        bp->window_end = window_end + 1;
    }
    else
    {
        bp->window_end = bp->size;
    }

    pos = search_forward (bp, bp->window_start, '\n');

    if (pos < bp->size)
    {
        bp->window_start = pos + 1;
    }
    else
    {
        bp->window_start = bp->size;
    }

    move (y, x);
}

static void
bp_scroll_down (BUFFER * bp)
{
    int y;
    int x;
    int ch;

    if (bp->window_start > 0)
    {
        int window_start    = bp->window_start;
        int size            = bp->size;

        window_start = search_backward (bp, window_start - 2, '\n');

        if (window_start < 0)
        {
            window_start = -1;
        }

        window_start++;

        bp->window_start = window_start;

        bp->window_end = calculate_window_end (bp, WINDOW_LINES);

        getyx (y, x);
        move (0, 0);
        insertln ();

        while (window_start < size && (ch = char_at (bp, window_start)) != '\n')
        {
#if 000
            bp_addch (bp, window_start);
#else
            addch (ch);
#endif
            window_start++;
        }

        move (y, x);
    }
}


static void
bp_insertln (BUFFER * bp)
{
    insertln ();

    bp->window_end = calculate_window_end (bp, WINDOW_LINES);
}

static void
bp_deleteln (BUFFER * bp)
{
    int y;
    int x;
    int ch;
    int window_end;
    int size        = bp->size;

    getyx (y, x);
    deleteln ();

    window_end = calculate_window_end (bp, WINDOW_LINES - 1);                                   // one line less, because we have to print it

    move (BOTTOM_LINE, 0);

    while (window_end < size && (ch = char_at (bp, window_end)) != '\n')
    {
#if 000
        bp_addch (bp, window_end);
#else
        addch (ch);
#endif
        window_end++;
    }

    if (window_end < bp->size)
    {
        bp->window_end = window_end + 1;
    }
    else
    {
        bp->window_end = size;
    }

    move (y, x);
}

static int
cmd_move_left (BUFFER * bp)
{
    if (bp->pos > 0)
    {
        int y;
        int x;

        bp->pos--;
        getyx (y, x);

        if (char_at (bp, bp->pos) == '\n')
        {
            int new_pos = search_backward (bp, bp->pos - 1, '\n') + 1;
            int new_x = 0;

            y--;

            while (new_pos < bp->size && char_at (bp, new_pos) != '\n')
            {
                new_pos++;
                new_x++;
            }

            move (y, new_x);
            bp->line--;
        }
        else
        {
            move (y, x - 1);
        }
    }
    return 1;
}

static int
cmd_move_right (BUFFER * bp)
{
    if (bp->pos < bp->size)
    {
        int y;
        int x;
        int ch;

        getyx (y, x);
        ch = char_at (bp, bp->pos);

        if (ch == '\n')
        {
            move (y + 1, 0);
            bp->line++;
        }
        else
        {
            move (y, x + 1);
        }

        bp->pos++;
    }
    return 1;
}

static int
cmd_move_up (BUFFER * bp)
{
    if (bp->pos > 0)
    {
        int new_pos = search_backward (bp, bp->pos - 1, '\n');

        if (new_pos >= 0)
        {
            int y;
            int x;
            int new_x;

            new_pos = search_backward (bp, new_pos - 1, '\n') + 1;
            getyx (y, x);

            if (y > TOP_EDIT_LINE || bp->window_start == 0)
            {
                y--;
            }
            else
            {
                bp_scroll_down (bp);
            }

            new_x = 0;

            if (wish_x >= 0)
            {
                x = wish_x;
            }
            else
            {
                wish_x = x;
            }

            while (new_pos < bp->size && char_at (bp, new_pos) != '\n' && new_x < x)
            {
                new_pos++;
                new_x++;
            }

            move (y, new_x);
            bp->pos = new_pos;
            bp->line--;
        }
        return 1;
    }
    return 0;
}

static int
cmd_move_down (BUFFER * bp)
{
    if (bp->pos < bp->size)
    {
        int new_pos = search_forward (bp, bp->pos, '\n');

        if (new_pos < bp->size)
        {
            uint_fast8_t y;
            uint_fast8_t x;
            uint_fast8_t new_x = 0;

            new_pos++;
            getyx (y, x);

            if (y < BOTTOM_EDIT_LINE)
            {
                y++;
            }
            else
            {
                bp_scroll_up (bp);
            }

            if (wish_x >= 0)
            {
                x = wish_x;
            }
            else
            {
                wish_x = x;
            }

            while (new_pos < bp->size && char_at (bp, new_pos) != '\n' && new_x < x)
            {
                new_pos++;
                new_x++;
            }

            move (y, new_x);
            bp->pos = new_pos;
            bp->line++;
        }
        return 1;
    }
    return 0;
}

static int
cmd_move_bol (BUFFER * bp)
{
    if (bp->pos > 0)
    {
        int y;
        int x;
        int new_pos = search_backward (bp, bp->pos - 1, '\n') + 1;

        getyx (y, x);

        if (x > 0)
        {
            move (y, 0);
        }

        bp->pos = new_pos;
    }
    return 1;
}

static int
cmd_move_eol (BUFFER * bp)
{
    int y;
    int x;
    int new_pos = search_forward (bp, bp->pos, '\n');

    getyx (y, x);
    x += new_pos - bp->pos;
    bp->pos = new_pos;
    move (y, x);

    return 1;
}

static int
cmd_delete_ch (BUFFER * bp)
{
    if (bp->pos < bp->size)
    {
        int ch;
        int y;
        int x;
        int new_pos;

        move_gap (bp, bp->pos);
        ch = char_at (bp, bp->pos);

        bp->gap_size++;
        bp->size--;
        bp->window_end--;
        bp->modified = TRUE;

        if (ch == '\n')
        {
            new_pos = bp->pos;

            getyx (y, x);

            while (new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n')
            {
#if 000
                bp_addch (bp, new_pos);
#else
                addch (ch);
#endif
                new_pos++;
            }

            move (y + 1, 0);
            bp_deleteln (bp);
            move (y, x);
        }
        else
        {
            uint_fast8_t xx;

            getyx (y, x);
            delch ();

            for (xx = x, new_pos = bp->pos; new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n'; xx++, new_pos++)
            {
                if (xx == COLS - 1)
                {
                    move (y, COLS - 1);
                    addch (ch);
                    move (y, x);
                    break;
                }
            }
        }
    }
    return 1;
}

static int
cmd_delete_to_eol (BUFFER * bp)
{
    int new_pos;
    int n;

    new_pos = search_forward (bp, bp->pos, '\n');

    n = new_pos - bp->pos;

    bp_del_ch (bp, bp->pos, n);

    bp->modified = TRUE;
    clrtoeol ();

    return 1;
}

static int
cmd_delete_to_bol (BUFFER * bp)
{
    int new_pos;
    int n;

    if (bp->pos > 0)
    {
        int y;
        int x;
        int ch;

        getyx (y, x);

        new_pos = search_backward (bp, bp->pos - 1, '\n') + 1;

        n = bp->pos - new_pos;
        bp->pos = new_pos;

        bp_del_ch (bp, new_pos, n);
        bp->modified = TRUE;

        x = 0;
        move (y, x);

        while (new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n')
        {
#if 000
            bp_addch (bp, new_pos);
#else
            addch (ch);
#endif
            new_pos++;
        }

        clrtoeol ();
        move (y, x);
    }
    return 1;
}

static int
cmd_delete_back (BUFFER * bp)
{
    if (bp->pos > 0)
    {
        int y;
        int x;

        bp->pos--;
        move_gap (bp, bp->pos);
        bp->gap_size++;
        bp->size--;
        bp->window_end--;
        bp->modified = TRUE;

        getyx (y, x);

        if (x > 0)
        {
            int             ch;
            int             new_pos;
            uint_fast8_t    xx;

            x--;

            move (y, x);
            delch ();

            for (xx = x, new_pos = bp->pos; new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n'; xx++, new_pos++)
            {
                if (xx == COLS - 1)
                {
                    move (y, COLS - 1);
                    addch (ch);
                    move (y, x);
                    break;
                }
            }
        }
        else
        {
            int ch;
            int len = 0;
            int pos = bp->pos;
            int new_pos;

            bp_deleteln (bp);

            if (y > TOP_EDIT_LINE)
            {
                move (y - 1, 0);
            }
            else
            {
                if (bp->window_start > 0)
                {
                    bp_scroll_down (bp);
                    y++;
                }
                else
                {
                    move (y - 1, 0);
                }
            }

            new_pos = search_backward (bp, pos - 1, '\n') + 1;

            if (new_pos >= 0)
            {
                len = pos - new_pos;

                while (new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n')
                {
#if 000
                    bp_addch (bp, new_pos);
#else
                    addch (ch);
#endif
                    new_pos++;
                }
            }

            move (y - 1, len);
            bp->line--;
        }
    }
    return 1;
}

static int
insert_ch (BUFFER * bp, int ch)
{
    if (ch == KEY_CR)
    {
        ch = '\n';
    }

    bp_insert_ch (bp, bp->pos, ch);

    bp->window_end++;
    bp->modified = TRUE;
    bp->pos++;

    if (ch == '\n')
    {
        uint_fast8_t    y;
        uint_fast8_t    x;
        int             new_pos;

        getyx (y, x);

        if (y < BOTTOM_EDIT_LINE)
        {
            y++;
        }
        else
        {
            bp_scroll_up (bp);
            move (y - 1, x);
        }

        clrtoeol ();
        x = 0;
        move (y, x);
        bp_insertln (bp);

        new_pos = bp->pos;

        while (new_pos < bp->size && (ch = char_at (bp, new_pos)) != '\n')
        {
#if 000
            bp_addch (bp, new_pos);
#else
            addch (ch);
#endif
            new_pos++;
        }

        move (y, x);
        bp->line++;
    }
    else
    {
#if 000
        bp_insch (bp, bp->pos - 1);
#else
        insch (ch);
#endif
    }

    return 1;
}

static int
cmd_next_page (BUFFER * bp)
{
    if (bp)
    {
        int l = LINES * 3 / 4;

        while (l > 0)
        {
            cmd_move_down (bp);
            l--;
        }

        return 1;
    }
    return 0;
}

static int
cmd_prev_page (BUFFER * bp)
{
    if (bp)
    {
        int l = LINES * 3 / 4;

        while (l > 0)
        {
            cmd_move_up (bp);
            l--;
        }

        return 1;
    }
    return 0;
}

static int
cmd_tabulate (BUFFER * bp)
{
    if (bp)
    {
        int     x;
        int     new_tab_pos;

        x = getx ();
        new_tab_pos = ((x + 4) / 4) * 4;

        while (x < new_tab_pos)
        {
            insert_ch (bp, ' ');
            x++;
        }

        return 1;
    }
    return 0;
}

static void
cmd_start_region (BUFFER * bp)
{
    if (bp->select_pos < 0)
    {
        bp->select_pos = bp->pos;
    }
    else
    {
        bp->select_pos = -1;
    }
}

static void
fill_paste_buffer (BUFFER * bp, int start_pos, int end_pos)
{
    int     len = end_pos - start_pos;
    int     idx = 0;
    int     pos;

    if (len > allocated_paste_buffer_len)
    {
        allocated_paste_buffer_len = len + PASTE_BUFFER_ALLOC_GRANULARITY;

        if (! paste_buffer)
        {
            paste_buffer = malloc (allocated_paste_buffer_len);
        }
        else
        {
            paste_buffer = realloc (paste_buffer, allocated_paste_buffer_len);
        }
    }

    for (pos = start_pos; pos < end_pos; pos++)
    {
        paste_buffer[idx++] = char_at (bp, pos);
    }

    used_paste_buffer_len = idx;
}

static void
cmd_copy_region (BUFFER * bp)
{
    if (bp->select_pos >= 0)
    {
        if (bp->pos > bp->select_pos)
        {
            fill_paste_buffer (bp, bp->select_pos, bp->pos);
        }
        else if (bp->pos < bp->select_pos)
        {
            fill_paste_buffer (bp, bp->pos, bp->select_pos);
        }
        else
        {
            used_paste_buffer_len = 0;
        }

        bp->select_pos = -1;
    }
}

static void
cmd_cut_region (BUFFER * bp)
{
    if (bp->select_pos >= 0)
    {
        int     len;

        if (bp->pos > bp->select_pos)
        {
            fill_paste_buffer (bp, bp->select_pos, bp->pos);

            len = bp->pos - bp->select_pos;

            while (len--)
            {
                cmd_delete_back (bp);
            }
        }
        else if (bp->pos < bp->select_pos)
        {
            fill_paste_buffer (bp, bp->pos, bp->select_pos);

            len = bp->select_pos - bp->pos;

            while (len--)
            {
                cmd_delete_ch (bp);
            }
        }
        else
        {
            used_paste_buffer_len = 0;
        }

        bp->select_pos = -1;
    }
}

static void
cmd_paste_region (BUFFER * bp)
{
    int idx;

    for (idx = 0; idx < used_paste_buffer_len; idx++)
    {
        insert_ch (bp, paste_buffer[idx]);
    }
}

static void
cmd_goto_line (BUFFER * bp)
{
    char    buf[10];
    int     y;
    int     x;

    getyx (y, x);

    move (PROMPT_LINE, 0);
    clrtoeol ();
    addstr ("Goto line: ");
    buf[0] = '\0';
    getnstr (buf, 8);

    move (y, x);

    if (*buf)
    {
        int line = atoi (buf);

        if (line > 0)
        {
            int     curline;

            line--;

            if (line < bp->line)
            {
                while (line < bp->line)
                {
                    curline = bp->line;
                    cmd_move_up (bp);

                    if (bp->line == curline)
                    {
                        break;      // cmd_move_up() had no effect
                    }
                }
            }
            else if (line > bp->line)
            {
                while (line > bp->line)
                {
                    curline = bp->line;
                    cmd_move_down (bp);

                    if (bp->line == curline)
                    {
                        break;      // cmd_move_up() had no effect
                    }
                }
            }

        }
    }
}

static void
free_buffer (BUFFER * bp)
{
    if (bp)
    {
        if (bp->buf)
        {
            free (bp->buf);
            bp->buf = 0;
        }
        free (bp);
    }
}

static BUFFER *
new_buffer (const char * fname)
{
    BUFFER *    bp = (BUFFER *) NULL;
    int         buffersize = 0;

    if (fname)
    {
#ifdef unix
        struct stat st;

        if (stat (fname, &st) == 0)
        {
            buffersize = st.st_size;
        }
#else
        FILINFO     fno;
        FRESULT     res = f_stat (fname, &fno);

        if (res == FR_OK)
        {
            if (fno.fattrib & AM_DIR)
            {
                fprintf (stderr, "%s: is a directory", fname);
                return bp;
            }
            buffersize = fno.fsize;
        }
#endif
    }

    bp = malloc (sizeof (BUFFER));

    if (bp)
    {
        bp->fname = fname;

        bp->buf = malloc (buffersize + BUFFER_CHUNK_SIZE);

        if (bp->buf)
        {
            bp->size        = buffersize;
            bp->gap_pos     = 0;
            bp->gap_size    = BUFFER_CHUNK_SIZE;
            bp->pos         = 0;
            bp->modified    = 0;
            bp->select_pos  = -1;
            bp->line        = 0;

            if (buffersize)
            {
                char *  p   = bp->buf;
                int     pos = 0;
                int     col;
                int     ch;
                int     last_ch = '\n';
                FILE *  fp  = fopen (fname, "r");

                if (fp)
                {
                    while ((ch = getc (fp)) != EOF)
                    {
                        if (ch != '\r')
                        {
                            *p++ = ch;
                            pos++;
                        }
                        last_ch = ch;
                    }

                    if (pos > 0 && last_ch != '\n')
                    {
                        *p++ = '\n';
                        pos++;
                    }

                    fclose (fp);
                }
                else
                {
                    fprintf (stderr, "%s: cannot open", fname);
                    free_buffer (bp);
                    bp = 0;
                    return bp;
                }

                /* correct positions and sizes */
                bp->size        = pos;
                bp->gap_pos     = pos;
                bp->gap_size    = BUFFER_CHUNK_SIZE + (buffersize - pos);           // correct gap size, because CR ignored above
                bp->pos         = 0;

                pos = 0;
                col = 0;

                while (pos < bp->size)
                {
                    ch = char_at (bp, pos);

                    if (ch == '\n')
                    {
                        col = 0;
                        pos++;
                    }
                    else if (ch == '\t')
                    {
                        int new_tab_pos = ((col + 4) / 4) * 4;

                        bp_del_ch (bp, pos, 1);

                        while (col < new_tab_pos)
                        {
                            bp_insert_ch (bp, pos, ' ');
                            col++;
                            pos++;
                        }
                    }
                    else
                    {
                        pos++;
                    }
                }
            }
        }
        else
        {
            free (bp);
            bp = 0;
        }
    }

    return bp;
}

static int
edit (BUFFER * bp)
{
    int             ch;
    int             pos;
    int             line            = -1;
    int             modified        = -1;
    int             selecting       = -1;
    int             total_update    = 1;
    int             do_exit         = FALSE;

    pos = display_buffer (bp, 0);

#if 0
    if (lines < BOTTOM_LINE)
    {
        addstr (EOB_STRING);
    }
#endif

    bp->window_start    = 0;

    if (pos < bp->size)
    {
        bp->window_end = pos + 1;
    }
    else
    {
        bp->window_end = pos;
    }

    move (TOP_LINE, 0);

    while (! do_exit)
    {
        if (line != bp->line || modified != bp->modified || selecting != (bp->select_pos >= 0))
        {
            show_buffer_status_line (bp, total_update);

            line            = bp->line;
            modified        = bp->modified;
            selecting       = (bp->select_pos >= 0);
            total_update    = 0;
        }

        ch = getch ();

        switch (ch)
        {
            case KEY_CTRL('@'):                         cmd_start_region (bp);  break;
            case KEY_CTRL('C'):                         cmd_copy_region (bp);   break;
            case KEY_CTRL('X'):                         cmd_cut_region (bp);    break;
            case KEY_CTRL('V'):                         cmd_paste_region (bp);  break;
            case KEY_CTRL('G'):                         cmd_goto_line (bp);     break;
            case KEY_LEFT:                              cmd_move_left (bp);     break;
            case KEY_RIGHT:                             cmd_move_right (bp);    break;
            case KEY_UP:                                cmd_move_up (bp);       break;
            case KEY_DOWN:                              cmd_move_down (bp);     break;
            case KEY_HOME:      case KEY_CTRL('A'):     cmd_move_bol (bp);      break;
            case KEY_END:       case KEY_CTRL('E'):     cmd_move_eol (bp);      break;
            case KEY_DC:        case KEY_CTRL('D'):     cmd_delete_ch (bp);     break;
            case KEY_IC:                                                        break;
            case KEY_CTRL('K'):                         cmd_delete_to_eol (bp); break;
            case KEY_CTRL('U'):                         cmd_delete_to_bol (bp); break;
            case KEY_NPAGE:                             cmd_next_page (bp);     break;
            case KEY_PPAGE:                             cmd_prev_page (bp);     break;
            case KEY_ESCAPE:                            do_exit = 1;            break;
            case KEY_BACKSPACE:                         cmd_delete_back (bp);   break;
            case KEY_TAB:                               cmd_tabulate (bp);      break;
            default:
                if ((ch >= 32 && ch < 127) || (ch >= 128 + 32 && ch < 256) || ch == KEY_CR || ch == '\n')
                {
                    insert_ch (bp, ch);
                }
                break;
        }

#if 000
        if (reverse)
        {
            attrset (A_NORMAL);
            reverse = 0;
        }
#endif

        if (ch != KEY_UP && ch != KEY_DOWN && ch != KEY_NPAGE && ch != KEY_PPAGE)
        {
            wish_x = -1;
        }
    }
    return 1;
}

static void
save_buffer (BUFFER * bp, const char * fname)
{
    FILE *  fp;
    int     pos;

    fp = fopen (fname, "w");

    if (fp)
    {
        int ch = '\n';                                                                  // tricky for bp->size == 0

        for (pos = 0; pos < bp->size; pos++)
        {
            ch = char_at (bp, pos);

            if (ch == '\n')
            {
                fputc ('\r', fp);
            }

            fputc (ch, fp);
        }

        if (ch != '\n')
        {
            fputc ('\r', fp);
            fputc ('\n', fp);
        }

        fclose (fp);
    }
}

#ifdef unix
#define cmd_fe  main
#endif

int
cmd_fe (int argc, const char ** argv)
{
    BUFFER * bp;

    if (argc == 2 && argv[1][0])
    {
        const char *    fname;

        fname = argv[1];

        bp = new_buffer (fname);

        if (bp)
        {
            if (bp->buf)
            {
                initscr ();
                setscrreg (TOP_LINE, BOTTOM_LINE);
                edit (bp);

                if (bp->modified)
                {
                    char buf[64];
                    move (PROMPT_LINE, 0);
                    clrtoeol ();
                    addstr ("Save file (y/n)? ");

                    buf[0] = '\0';

                    while (buf[0] != 'y' && buf[0] != 'n')
                    {
                        getnstr (buf, 2);
                    }

                    if (buf[0] == 'y')
                    {
                        move (PROMPT_LINE, 0);
                        clrtoeol ();
                        addstr ("Save file as: ");

                        strcpy (buf, fname);
                        getnstr (buf, 64);
                        save_buffer (bp, buf);
                    }
                }

                endwin ();
            }

            if (paste_buffer)
            {
                free (paste_buffer);
            }

            free_buffer (bp);
        }
        else
        {
            fprintf (stderr, "%s: out of memory", fname);
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}
