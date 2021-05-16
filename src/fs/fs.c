/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs.c - file system functions
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "console.h"
#include "fs.h"

#define FS_BUFSIZE              512
#define FS_MAX_OPEN_FILES       8
#define FS_FDNO_FLAG_IS_OPEN    0x01

typedef struct
{
    FIL         fil;
    uint32_t    flags;
} FS_FDNO_SLOT;

static int              fs_errno = FR_OK;
static FS_FDNO_SLOT     fs_fdno[FS_MAX_OPEN_FILES];

int                     fs_stdout_fd = -1;
int                     fs_stderr_fd = -1;

void
fs_perror (const char * path, FRESULT res)
{
    if (path && *path)
    {
        fprintf (stderr, "%s: ", path);
    }

    switch (res)
    {
        case FR_OK:                     fputs ("succeeded\n", stderr); break;
        case FR_DISK_ERR:               fputs ("a hard error occurred in the low level disk I/O layer\n", stderr); break;
        case FR_INT_ERR:                fputs ("assertion failed\n", stderr); break;
        case FR_NOT_READY:              fputs ("physical drive cannot work\n", stderr); break;
        case FR_NO_FILE:                fputs ("no such file\n", stderr); break;
        case FR_NO_PATH:                fputs ("no such file or directory\n", stderr); break;
        case FR_INVALID_NAME:           fputs ("path name format is invalid\n", stderr); break;
        case FR_DENIED:                 fputs ("access denied due to prohibited access or directory full\n", stderr); break;
        case FR_EXIST:                  fputs ("access denied due to prohibited access\n", stderr); break;
        case FR_INVALID_OBJECT:         fputs ("file/directory object is invalid\n", stderr); break;
        case FR_WRITE_PROTECTED:        fputs ("physical drive is write protected\n", stderr); break;
        case FR_INVALID_DRIVE:          fputs ("logical drive number is invalid\n", stderr); break;
        case FR_NOT_ENABLED:            fputs ("volume has no work area\n", stderr); break;
        case FR_NO_FILESYSTEM:          fputs ("there is no valid FAT volume\n", stderr); break;
        case FR_MKFS_ABORTED:           fputs ("f_mkfs() aborted due to any problem\n", stderr); break;
        case FR_TIMEOUT:                fputs ("could not get a grant to access the volume within defined period\n", stderr); break;
        case FR_LOCKED:                 fputs ("operation is rejected according to the file sharing policy\n", stderr); break;
        case FR_NOT_ENOUGH_CORE:        fputs ("lFN working buffer could not be allocated\n", stderr); break;
        case FR_TOO_MANY_OPEN_FILES:    fputs ("number of open files > FF_FS_LOCK\n", stderr); break;
        case FR_INVALID_PARAMETER:      fputs ("given parameter is invalid\n", stderr); break;
        default:                        fputs ("unknown error\n", stderr); break;
    }
}

static void
fs_std_perror (const char * path)
{
    switch (errno)
    {
        case EBADF:         fprintf (stderr, "%s: bad file number\n", path);          break;
        case ENFILE:        fprintf (stderr, "%s: too many open files\n", path);      break;
        case __ELASTERROR:  fs_perror (path, fs_errno);                               break;
        default:            fprintf (stderr, "%s: unknown error\n", path);            break;
    }
}

static void
fs_set_errno (int res)
{
    fs_errno = res;
    errno = __ELASTERROR;                                                       // range beginning with user defined errors
}

const char *
fs_basename (const char * path)
{
    const char *  base_p;

    if (! path)
    {
        return ((char *) 0);
    }

    for (base_p = path; *path; path++)
    {
        if (*path == '/')
        {
            base_p = path + 1;
        }
    }
    return (base_p);
}

char *
fs_dirname (const char * path, uint_fast8_t strip_trailing_slash)
{
    static char     dname[128];
    int             found = 0;
    int             pos;

    if (strchr (path, '/'))
    {
        strcpy (dname, path);
        pos = strlen (dname) - 1;

        if (strip_trailing_slash && pos > 0 && dname[pos] == '/')                       // strip trailing but not leading slash
        {
            dname[pos--] = '\0';
        }

        while (pos > 0)
        {
            if (dname[pos] == '/')
            {
                dname[pos] = '\0';
                found = 1;
                break;
            }
            pos--;
        }

        if (! found && dname[0] == '/')
        {
            dname[1] = '\0';
        }
    }
    else
    {
        dname[0] = '\0';
    }

    return (dname);
}

int
fs_is_dir (const char * path, FILINFO  * fnop)
{
    FRESULT         res;
    int             rtc = 0;

    if (! strcmp (path, "/"))
    {
        rtc = 1;
    }
    else
    {
        // fatfs () doesn't like '/' at end of directory name

        int len = strlen (path);

        if (len > 0 && path[len - 1] == '/')
        {
            char new_path[len + 1];
            strcpy (new_path, path);
            new_path[len - 1] = '\0';

            res = f_stat (new_path, fnop);
        }
        else
        {
            res = f_stat (path, fnop);
        }

        if (res == FR_OK)
        {
            if (fnop->fattrib & AM_DIR)
            {
                rtc = 1;
            }
        }
        else
        {
            rtc = -1;
        }
    }
    return rtc;
}

static int                  g_ls_sort;
static int                  g_ls_flags;

typedef struct
{
    char *                  fname;
    FSIZE_t                 fsize;
    WORD                    fdate;
    WORD                    ftime;
    BYTE                    fattrib;
} LS_DIRENTRY;

static LS_DIRENTRY *        ls_direntries;
static int                  ls_direntries_used;
static int                  ls_direntries_allocated;

#define LS_DIRENTRIES_GRANULARITY     20

static int
new_ls_direntry (FILINFO * fnop)
{
    if (! ls_direntries_allocated)
    {
        ls_direntries             = calloc (LS_DIRENTRIES_GRANULARITY, sizeof (LS_DIRENTRY));
        ls_direntries_allocated   = LS_DIRENTRIES_GRANULARITY;
        ls_direntries_used        = 0;
    }
    else
    {
        ls_direntries             = realloc (ls_direntries, (ls_direntries_allocated + LS_DIRENTRIES_GRANULARITY) * sizeof (LS_DIRENTRY));
        ls_direntries_allocated   += LS_DIRENTRIES_GRANULARITY;
    }

    ls_direntries[ls_direntries_used].fname     = malloc (strlen (fnop->fname) + 1);
    strcpy (ls_direntries[ls_direntries_used].fname, fnop->fname);
    ls_direntries[ls_direntries_used].fsize     = fnop->fsize;
    ls_direntries[ls_direntries_used].fdate     = fnop->fdate;
    ls_direntries[ls_direntries_used].ftime     = fnop->ftime;
    ls_direntries[ls_direntries_used].fattrib   = fnop->fattrib;

    ls_direntries_used++;
    return ls_direntries_used;
}

static void
free_ls_direntries (void)
{
    int     idx;

    for (idx = 0; idx < ls_direntries_used; idx++)
    {
        free (ls_direntries[idx].fname);
    }

    if (ls_direntries)
    {
        free (ls_direntries);
    }

    ls_direntries             = (LS_DIRENTRY *) NULL;
    ls_direntries_used        = 0;
    ls_direntries_allocated   = 0;
}

static int
ls_dircmp (const void * e1, const void * e2)
{
    const LS_DIRENTRY *     de1 = e1;
    const LS_DIRENTRY *     de2 = e2;
    int                     rtc;

    switch (g_ls_sort)
    {
        case LS_SORT_FTIME:
            rtc = de2->fdate - de1->fdate;                                  // 2 - 1 : newest first

            if (rtc == 0)
            {
                rtc = de2->ftime - de1->ftime;
            }
            break;
        case LS_SORT_FSIZE:
            rtc = de2->fsize - de1->fsize;                                  // 2 - 1: biggest first
            break;
        default: // LS_SORT_FNAME
            rtc = strcmp (de1->fname, de2->fname);                          // 1 - 2: alphabetical order
            break;
    }

    if (g_ls_flags & LS_FLAG_SORT_REVERSE)
    {
        rtc = -rtc;
    }

    return rtc;
}

static void
fs_ls_entry (char * fname, FSIZE_t fsize, WORD fdate, WORD ftime, BYTE fattrib, uint_fast8_t ls_flags)
{
    struct tm       tm;

    if ((ls_flags & LS_FLAG_SHOW_ALL) || ! ((fattrib & AM_HID) || (fattrib & AM_SYS)))
    {
        if (ls_flags & LS_FLAG_LONG)
        {
            tm.tm_year  = ((fdate & 0xFE00) >>  9) + 1980 - 1900;
            tm.tm_mon   = ((fdate & 0x01E0) >>  5) - 1;
            tm.tm_mday  = ((fdate & 0x001F) >>  0);
            tm.tm_hour  = ((ftime & 0xF800) >> 11);
            tm.tm_min   = ((ftime & 0x07E0) >>  5);
            tm.tm_sec   = ((ftime & 0x001F) <<  1);

            putchar ((fattrib & AM_DIR) ?  'd' : '-');
            putchar ('r');
            putchar ((fattrib & AM_RDO) ? '-' : 'w');
            putchar ((fattrib & AM_HID) ? 'h' : '-');
            putchar ((fattrib & AM_SYS) ? 's' : '-');

            printf ("%10lu  %d-%02d-%02d %02d:%02d:%02d  ", fsize,
                    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

        }

        fputs (fname, stdout);
        putchar ('\n');
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_ls () - list files
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_ls (const char * path)
{
    char            name[128];
    FRESULT         res;
    DIR             dir;
    int             len;
    int             is_dir = 0;
    static FILINFO  fno;

    strcpy (name, path);
    len = strlen (name);

    if (len > 1 && name[len - 1] == '/')                                       // strip trailing but not leading slash
    {
        len--;
        name[len] = '\0';
    }

    if (*name)
    {
        is_dir = fs_is_dir (name, &fno);
        // printf ("is_dir=%d\n", is_dir);
    }
    else
    {
        is_dir = 1;
        res = FR_OK;
    }

    if (is_dir == 1)
    {
        res = f_opendir (&dir, name);

        if (res == FR_OK)
        {
            for (;;)
            {
                res = f_readdir (&dir, &fno);

                if (res != FR_OK || fno.fname[0] == 0)                                      // end of directory entries: fname is empty
                {
                    break;
                }

                new_ls_direntry (&fno);
            }

            f_closedir (&dir);
        }
    }
    else if (is_dir == 0)
    {
        new_ls_direntry (&fno);
    }
    else
    {
        fprintf (stderr, "%s: no such file or directory\n", name);
        res = -1;
    }

    return res;
}

void
fs_ls_output (uint_fast8_t ls_flags, uint_fast8_t ls_sort)
{
    int idx;

    if (ls_sort != LS_SORT_NONE)
    {
        g_ls_sort   = ls_sort;
        g_ls_flags  = ls_flags;

        qsort (ls_direntries, ls_direntries_used, sizeof (LS_DIRENTRY), ls_dircmp);
    }

    for (idx = 0; idx < ls_direntries_used; idx++)
    {
        fs_ls_entry (ls_direntries[idx].fname, ls_direntries[idx].fsize,
                     ls_direntries[idx].fdate, ls_direntries[idx].ftime,
                     ls_direntries[idx].fattrib, ls_flags);
    }

    free_ls_direntries ();
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_cat () - cat files
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
#define CAT_BUFFER_LEN  128

int
fs_cat (const char * fname)
{
    FILE *  fp;
    int     ch;

    fp = fopen (fname, "r");

    if (fp)
    {
        while ((ch = getc(fp)) != EOF)
        {
            putchar (ch);
        }

        fclose (fp);
    }
    else
    {
        fs_std_perror (fname);
        return -1;
    }

    return 0;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_cp () - copy file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_cp (const char * src, const char * dst, uint_fast8_t flags)
{
    if (flags & FS_CP_FLAG_FAST)
    {
        FIL     fsrc;
        FIL     fdst;
        BYTE    buffer[FS_BUFSIZE];
        FRESULT fr;
        UINT    br;
        UINT    bw;

        if (flags & FS_CP_FLAG_VERBOSE)
        {
            printf ("%s -> %s\n", src, dst);
        }

        fr = f_open (&fsrc, src, FA_READ);

        if (fr == FR_OK)
        {
            fr = f_open (&fdst, dst, FA_WRITE | FA_CREATE_ALWAYS);

            if (fr == FR_OK)
            {
                for (;;)
                {
                    fr = f_read (&fsrc, buffer, sizeof buffer, &br);                                                // read a chunk of source file

                    if (fr || br == 0)
                    {
                        break;                                                                                      // error or eof
                    }

                    fr = f_write (&fdst, buffer, br, &bw);                                                          // write it to the destination file

                    if (fr)
                    {                                                                                               // error
                        break;
                    }

                    if (bw < br)
                    {                                                                                               // error or disk full?
                        fr = -1;                                                                                    // indicate error
                        break;
                    }
                }

                f_close(&fdst);
            }
            else
            {
                fs_perror (dst, fr);
            }

            f_close(&fsrc);
        }
        else
        {
            fs_perror (src, fr);
        }

        return (int) fr;
    }
    else
    {
        FILE *  fpsrc;
        FILE *  fpdst;
        int     ch;
        int     rtc = -1;

        if (flags & FS_CP_FLAG_VERBOSE)
        {
            printf ("%s -> %s\n", src, dst);
        }

        fpsrc = fopen (src, "r");

        if (fpsrc)
        {
            fpdst = fopen (dst, "w");

            if (fpdst)
            {
                while ((ch = fgetc (fpsrc)) != EOF)
                {
                    fputc (ch, fpdst);
                }

                fclose (fpdst);
                rtc = 0;
            }
            else
            {
                fs_std_perror (dst);
            }

            fclose (fpsrc);
        }
        else
        {
            fs_std_perror (src);
        }

        return rtc;
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_df () - print disk free info
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_df (void)
{
    DWORD       fre_clust = 0;
    DWORD       fre_sect;
    DWORD       tot_sect;
    FATFS *     fsp = (FATFS *) NULL;
    FRESULT     res;

    res = f_getfree("0:", &fre_clust, &fsp);

    if (res == FR_OK)
    {
        tot_sect = (fsp->n_fatent - 2) * fsp->csize;
        fre_sect = fre_clust * fsp->csize;

        // assuming 512 bytes/sector
        printf ("total drive space: %lu KiB, available: %lu KiB, used: %lu KiB\n", tot_sect / 2, fre_sect / 2, (tot_sect - fre_sect) / 2);
    }
    else
    {
        fs_perror ((const char *) NULL, res);
    }

    return res;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_find () - find files
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_find (const char * path)
{
    FRESULT         res;
    DIR             dir;
    static FILINFO  fno;

    if (! strcmp (path, "/"))
    {
        path = "";                                                                      // avoid double slash
    }

    res = f_opendir (&dir, path);

    if (res == FR_OK)
    {
        for (;;)
        {
            res = f_readdir (&dir, &fno);

            if (res != FR_OK || fno.fname[0] == 0)                                      // end of directory entries: fname is empty
            {
                break;
            }

            if (fno.fattrib & AM_DIR)
            {                                                                           // it is a directory
                char    subpath[256];
                int     len;

                len = strlen (path) + 1 + strlen (fno.fname);

                if (len < 256 - 1)
                {
                    sprintf (subpath, "%s/%s", path, fno.fname);

                    res = fs_find (subpath);                                             // enter the directory

                    if (res != FR_OK)
                    {
                        break;
                    }
                }
                else
                {
                    fputs ("path too long\n", stderr);
                    break;
                }
            }
            else
            {                                                                           // it is a file
                printf ("%s/%s\n", path, fno.fname);
            }
        }
        f_closedir (&dir);
    }
    else
    {
        fs_perror (path, res);
    }

    return res;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_mkdir () - make directory
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_mkdir (const char * dir)
{
    FRESULT     res;

    res = f_mkdir (dir);

    return res;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_mv () - move file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_mv (const char * src, const char * dst, uint_fast8_t flags)
{
    FRESULT     res;

    if (flags & FS_MV_FLAG_VERBOSE)
    {
        printf ("%s -> %s\n", src, dst);
    }

    res = f_rename (src, dst);

    return res;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_rm () - remove file
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_rm (const char * file)
{
    FRESULT     res;
    FILINFO     fno;

    res = f_stat (file, &fno);

    if (res == FR_OK)
    {
        if (! (fno.fattrib & AM_DIR))
        {
            res = f_unlink (file);
        }
        else
        {
            fprintf (stderr, "%s: is a directory\n", file);
        }
    }
    else
    {
        fs_perror (file, res);
        res = -1;
    }
    return res;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * fs_rmdir () - remove directory
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
fs_rmdir (const char * dir)
{
    FRESULT     res;
    FILINFO     fno;

    res = f_stat (dir, &fno);

    if (res == FR_OK)
    {
        if (fno.fattrib & AM_DIR)
        {
            res = f_unlink (dir);

            if (res != FR_OK)
            {
                fs_perror (dir, res);
            }
        }
        else
        {
            fprintf (stderr, "%s: is no directory\n", dir);
        }
    }
    else
    {
        fs_perror (dir, res);
        res = -1;
    }
    return res;
}

int
_open (char * path, int flags, ...)
{
    FRESULT res;
    BYTE    mode;
    int     fd;

    // printf ("open: path=%s, flags=0x%02x\n", path, flags);

    switch (flags & (O_RDONLY | O_WRONLY | O_RDWR))
    {
        case O_RDONLY:
        {
            mode = FA_READ;
            break;
        }
        case O_WRONLY:
        {
            mode = FA_WRITE;

            if (flags & O_CREAT)
            {
                if (flags & O_TRUNC)
                {
                    mode = FA_WRITE | FA_CREATE_ALWAYS;
                }
                else
                {
                    mode = FA_WRITE | FA_OPEN_ALWAYS;
                }
            }
            else if (flags & O_TRUNC)
            {
                mode = FA_WRITE | FA_CREATE_ALWAYS;                                         // that's not correct
            }
            if (flags & O_APPEND)
            {
                mode = FA_WRITE | FA_OPEN_APPEND;
            }
            break;
        }
        case O_RDWR:
        {
            mode = FA_READ | FA_WRITE;

            if (flags & O_CREAT)
            {
                if (flags & O_TRUNC)
                {
                    mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;
                }
                else
                {
                    mode = FA_READ | FA_WRITE | FA_OPEN_ALWAYS;
                }
            }
            else if (flags & O_TRUNC)
            {
                mode = FA_READ | FA_WRITE | FA_CREATE_ALWAYS;                                       // that's not correct
            }
            if (flags & O_APPEND)
            {
                mode = FA_READ | FA_WRITE | FA_OPEN_APPEND;
            }
            break;
        }
        default:
        {
            return -1;
        }
    }

    for (fd = 0; fd < FS_MAX_OPEN_FILES; fd++)
    {
        if (! (fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
        {
            break;
        }
    }

    if (fd < FS_MAX_OPEN_FILES)
    {
        // printf ("f_open: mode=0x%02x\n", mode);

        res = f_open (&(fs_fdno[fd].fil), path, mode);

        if (res == FR_OK)
        {
            fs_fdno[fd].flags |= FS_FDNO_FLAG_IS_OPEN;
            // printf ("open: rtc=%d\n", fd + 3);
            return fd + 3;
        }

        fs_set_errno (res);
        return -1;
    }

    // fputs ("too many open files\n", stdout);
    errno = ENFILE;
    return -1;
}

int
_close (int fd)
{
    int rtc = -1;

    // printf ("close: fd=%d\n", fd);

    if (fd >= 3)
    {
        fd -= 3;

        if (fd < FS_MAX_OPEN_FILES)
        {
            if ((fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
            {
                f_close (&(fs_fdno[fd].fil));
                fs_fdno[fd].flags &= ~FS_FDNO_FLAG_IS_OPEN;
                // fputs ("closed\n", stdout);
                rtc = 0;
            }
            else
            {
                errno = EBADF;
            }
        }
        else
        {
            errno = EBADF;
        }
    }
    else
    {
        errno = EBADF;
    }

    // printf ("close: rtc=%d\n", rtc);
    return rtc;
}

int
_read (int fd, char * ptr, int len)
{
    int         rtc = -1;

    // printf ("read: fd=%d, len=%d\n", fd, len);

    if (fd >= 3)
    {
        fd -= 3;

        if (fd < FS_MAX_OPEN_FILES && (fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
        {
            FIL *       fp = &(fs_fdno[fd].fil);
            FRESULT     res;
            UINT        br;

            rtc = 0;

            while (len > FS_BUFSIZE)
            {
                res = f_read (fp, ptr, FS_BUFSIZE, &br);
                // printf ("read1: res=%d len=%d br=%d\n", res, len, br);

                if (res != FR_OK)
                {
                    fs_set_errno (res);
                    return -1;
                }

                len -= br;
                ptr += br;
                rtc += br;

                if (br < FS_BUFSIZE)                                        // EOF
                {                                                           // stop reading
                    len = 0;
                }
            }

            if (len > 0)
            {
                res = f_read (fp, ptr, len, &br);
                // printf ("read2: res=%d len=%d br=%d\n", res, len, br);

                if (res != FR_OK)
                {
                    fs_set_errno (res);
                    return -1;
                }

                len -= br;
                ptr += br;
                rtc += br;
            }
        }
        else
        {
            errno = EBADF;
        }
    }

    // printf ("read: rtc=%d\n", rtc);
    return rtc;
}

int
_write (int fd, char * ptr, int len)
{
    int         rtc = -1;

    if (fd == STDOUT_FILENO && fs_stdout_fd >= 0)                                           // redirection of stdout
    {
        fd = fs_stdout_fd;
    }

    if (fd == STDERR_FILENO && fs_stderr_fd >= 0)                                           // redirection of stderr
    {
        fd = fs_stderr_fd;
    }

    // printf ("write: fd=%d, len=%d\n", fd, len);

    if (fd == STDIN_FILENO)
    {
        return rtc;
    }
    else if (fd == STDOUT_FILENO)
    {
        static int  last_ch;
        int idx;

        for (idx = 0; idx < len; idx++)
        {
            if (*ptr == '\n' && last_ch != '\r')
            {
                console_putc ('\r');
            }

            console_putc (*ptr);
            last_ch = *ptr;
            ptr++;
        }

        rtc = len;
    }
    else if (fd == STDERR_FILENO)
    {
        static int  last_ch;
        int idx;

        for (idx = 0; idx < len; idx++)
        {
            if (*ptr == '\n' && last_ch != '\r')
            {
                console_putc ('\r');
            }

            console_putc (*ptr);
            last_ch = *ptr;
            ptr++;
        }

        rtc = len;
    }
    else
    {
        fd -= 3;

        if (fd < FS_MAX_OPEN_FILES && (fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
        {
            FIL *       fp = &(fs_fdno[fd].fil);
            FRESULT     res;
            UINT        bw;

            rtc = 0;

            while (len > FS_BUFSIZE)
            {
                res = f_write (fp, ptr, FS_BUFSIZE, &bw);
                // printf ("write1: res=%d bw=%d\n", res, bw);

                if (res != FR_OK)
                {
                    fs_set_errno (res);
                    return -1;
                }

                len -= bw;
                ptr += bw;
                rtc += bw;

                if (bw < FS_BUFSIZE)                                        // EOF or FS full
                {                                                           // stop writing
                    len = 0;
                }
            }

            if (len > 0)
            {
                res = f_write (fp, ptr, len, &bw);
                // printf ("write2: res=%d bw=%d\n", res, bw);

                if (res != FR_OK)
                {
                    fs_set_errno (res);
                    return -1;
                }

                len -= bw;
                ptr += bw;
                rtc += bw;
            }
        }
        else
        {
            errno = EBADF;
        }
    }

    // printf ("write: rtc=%d\n", rtc);
    return rtc;
}

int
_lseek(int fd, int ptr, int whence)
{
    int     rtc = -1;

    // printf ("lseek: fd=%d, ptr=%d whence=%d\n", fd, ptr, whence);

    if (fd >= 3)
    {
        fd -= 3;

        if (fd < FS_MAX_OPEN_FILES && (fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
        {
            int     res = FR_OK;
            FIL *   fp  = &(fs_fdno[fd].fil);
            int     newpos;

            switch (whence)
            {
                case SEEK_SET:
                {
                    newpos = ptr;
                    break;
                }
                case SEEK_CUR:
                {
                    newpos = f_tell(fp) + ptr;
                    break;
                }
                case SEEK_END:
                {
                    newpos = f_size(fp) + ptr;
                    break;
                }
                default:
                {
                    newpos = -1;
                    errno = EINVAL;
                    break;
                }
            }

            if (newpos >= 0)
            {
                res = f_lseek (fp, newpos);

                if (res == FR_OK)
                {
                    rtc = newpos;
                }
                else
                {
                    fs_set_errno (res);
                }
            }
        }
        else
        {
            errno = EBADF;
        }
    }
    else
    {
        errno = EBADF;
    }

    // printf ("lseek: rtc=%d\n", rtc);
    return rtc;
}

int _isatty(int fd)
{
    if (fd == STDOUT_FILENO && fs_stdout_fd >= 0)                                           // redirection of stdout
    {
        fd = fs_stdout_fd;
    }

    if (fd == STDERR_FILENO && fs_stderr_fd >= 0)                                           // redirection of stderr
    {
        fd = fs_stderr_fd;
    }

    if ((fd == STDOUT_FILENO) || (fd == STDIN_FILENO) || (fd == STDERR_FILENO))
    {
        return 1;
    }

    return 0;
}

void
fs_close_all_open_files (void)
{
    int fd;

    for (fd = 0; fd < FS_MAX_OPEN_FILES; fd++)
    {
        if ((fs_fdno[fd].flags & FS_FDNO_FLAG_IS_OPEN))
        {
            fprintf (stderr, "error: fd %d not closed\n", fd + 3);
            _close (fd + 3);
        }
    }
}
