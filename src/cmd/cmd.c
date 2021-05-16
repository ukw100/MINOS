/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd.c - command interpreter
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

#include "board-led.h"
#include "button.h"
#include "console.h"
#include "mcurses.h"
#include "stm32_sdcard.h"
#include "stm32f4-rtc.h"
#include "delay.h"
#include "ff.h"
#include "fs.h"
#include "cmd.h"
#include "timer2.h"

#include "nic.h"
#include "nicc.h"
#include "nic.h"
#include "fe.h"

static uint_fast8_t     mounted;
static char             curwd[FS_MAX_PATH_LEN]  = "/";
static FATFS            fs;                                                             // must be static!


#define MAXARGS         32

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * date_time_print () - print date/time
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static void
date_time_print (struct tm * tmp)
{
    printf ("%d-%02d-%02d %02d:%02d:%02d\n", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * date_time_set () - set date/time
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
date_time_set (struct tm * tmp, const char * datestr, const char * timestr)
{
    int rtc = SUCCESS;

    if (strlen (datestr) == 10 && strlen (timestr) == 8)                                                // yyyy-mm-dd hh:mm:ss
    {                                                                                                   // 0123456789 01234567
        tmp->tm_year  = atoi (datestr) - 1900;
        tmp->tm_mon   = atoi (datestr + 5) - 1;
        tmp->tm_mday  = atoi (datestr + 8);
        tmp->tm_hour  = atoi (timestr);
        tmp->tm_min   = atoi (timestr + 3);
        tmp->tm_sec   = atoi (timestr + 6);

        stm32f4_rtc_set (tmp);
    }
    else
    {
        rtc = ERROR;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_cat () - command: cat
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_cat (int argc, const char ** argv)
{
    FILINFO     fno;
    FRESULT     res;
    int         idx;
    int         do_not_echo = 0;
    int         is_dir;
    int         rtc = EXIT_FAILURE;

    while (argc > 1 && argv[1][0] == '-')
    {
        for (idx = 1; argv[1][idx]; idx++)
        {
            if (argv[1][idx] == 'e')
            {
                do_not_echo = 1;
            }
            else
            {
                fprintf (stderr, "usage: %s [-e] [file ...]\n", argv[0]);
                return EXIT_FAILURE;
            }
        }

        argc--;
        argv++;
    }

    if (argc == 1)
    {
        int     local_echo = 0;
        int     ch;
        int     last_ch = -1;

        if (! do_not_echo && ! isatty (fileno (stdout)))
        {
            local_echo = 1;
        }

        while ((ch = console_getc ()) != KEY_CTRL('D'))
        {
            if (local_echo)
            {
                console_putc (ch);

                if (ch == '\r')
                {
                    console_putc ('\n');
                }
            }

            putchar (ch);

            if (ch == '\r')
            {
                putchar ('\n');
            }
            last_ch = ch;
        }

        if (do_not_echo && last_ch != '\r')                                     // echo is off and last character is no Carriage Return?
        {                                                                       // then append CRLF.
            putchar ('\r');
            putchar ('\n');
        }
    }
    else
    {
        for (idx = 1; idx < argc; idx++)
        {
            is_dir = fs_is_dir (argv[idx], &fno);

            if (is_dir == 1)
            {
                fprintf (stderr, "%s: is a directory\n", argv[idx]);
                return EXIT_FAILURE;
            }
            else if (is_dir < 0)
            {
                fprintf (stderr, "%s: no such file\n", argv[idx]);
                return EXIT_FAILURE;
            }
        }

        for (idx = 1; idx < argc; idx++)
        {
            res = fs_cat (argv[idx]);

            if (res != FR_OK)
            {
                fs_perror (argv[idx], res);
                return EXIT_FAILURE;
            }
        }

        rtc = EXIT_SUCCESS;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_cd () - command: cd
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_cd (int argc, const char ** argv)
{
    char        name[FS_MAX_PATH_LEN];
    FRESULT     res;
    int         rtc = EXIT_FAILURE;

    if (argc == 2)
    {
        int             len;

        strcpy (name, argv[1]);
        len = strlen (name);

        // console_printf ("cd name='%s', len=%d\r\n", name, len);

        if (len > 1 && name[len - 1] == '/')                                       // strip trailing but not leading slash
        {
            len--;
            name[len] = '\0';
        }

        // console_printf ("chdir vorher: '%s'\r\n", name);

        res = f_chdir(name);

        // console_printf ("chdir '%s', res=%d\r\n", name, res);

        if (res == FR_OK)
        {
            res = f_getcwd (curwd, FS_MAX_PATH_LEN);
            // console_printf ("curwd '%s', res=%d\r\n", curwd, res);

            if (res == FR_OK)
            {
                rtc = EXIT_SUCCESS;
            }
            else
            {
                fs_perror ("cd", res);
            }
        }
        else
        {
            fs_perror (name, res);
        }
    }
    else
    {
        fprintf (stderr, "usage: %s directory\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_clocks () - command: clocks
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_clocks (int argc, const char ** argv)
{
    int         rtc = EXIT_FAILURE;

    if (argc == 1)
    {
        RCC_ClocksTypeDef RCC_Clocks;

        RCC_GetClocksFreq(&RCC_Clocks);

        printf ("SYS:%lu HCLK:%lu PLCK1:%lu PLCK2:%lu\n",
                      RCC_Clocks.SYSCLK_Frequency,
                      RCC_Clocks.HCLK_Frequency,   // AHB
                      RCC_Clocks.PCLK1_Frequency,  // APB1
                      RCC_Clocks.PCLK2_Frequency); // APB2
    }
    else
    {
        fprintf (stderr, "usage: %s\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_cp () - command: cp
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_cp (int argc, const char ** argv)
{
    FRESULT         res = FR_OK;
    int             is_dir;
    int             idx;
    uint_fast8_t    flags = 0;
    int             rtc = EXIT_FAILURE;

    while (argc > 1 && argv[1][0] == '-')
    {
        for (idx = 1; argv[1][idx]; idx++)
        {
            if (argv[1][idx] == 'v')
            {
                flags |= FS_CP_FLAG_VERBOSE;
            }
            else if (argv[1][idx] == 'f')
            {
                flags |= FS_CP_FLAG_FAST;
            }
            else
            {
                fprintf (stderr, "usage: %s [-f] [-v] source dest\n", argv[0]);
                fprintf (stderr, "  or:  %s [-f] [-v] source ... destdir\n", argv[0]);
                return EXIT_FAILURE;
            }
        }

        argc--;
        argv++;
    }

    if (argc >= 3)
    {
        static FILINFO  fno;

        is_dir = fs_is_dir (argv[argc - 1], &fno);

        if (is_dir == 1)
        {
            char            targetfile[FS_MAX_PATH_LEN];
            const char *    targetdir = argv[argc - 1];
            int             len = strlen (targetdir);

            for (idx = 1; idx < argc - 1; idx++)
            {
                if (! strcmp (targetdir, "/"))
                {
                    sprintf (targetfile, "/%s", fs_basename (argv[idx]));
                }
                else if (len > 0 && targetdir[len - 1] == '/')
                {
                    sprintf (targetfile, "%s%s", targetdir, fs_basename (argv[idx]));       // targetdir already contains slash
                }
                else
                {
                    sprintf (targetfile, "%s/%s", targetdir, fs_basename (argv[idx]));
                }

                res = fs_cp (argv[idx], targetfile, flags);

                if (res != FR_OK)
                {
                    fprintf (stderr, "%s: cp to %s failed\n", argv[idx], targetfile);
                }
            }
        }
        else if (argc == 3)
        {
            res = fs_cp (argv[1], argv[2], flags);
        }
        else
        {
            fprintf (stderr, "%s: is no directory\n", argv[argc - 1]);
            res = -1;
        }

        if (res == FR_OK)
        {
            rtc = EXIT_SUCCESS;
        }
    }
    else
    {
        fprintf (stderr, "usage: %s [-f] source dest\n", argv[0]);
        fprintf (stderr, "  or:  %s [-f] source ... destdir\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_date () - command: date
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_date (int argc, const char ** argv)
{
    struct tm   tm;
    int         rtc = EXIT_SUCCESS;

    if (argc == 1)
    {
        if (stm32f4_rtc_get (&tm) == SUCCESS)
        {
            date_time_print (&tm);
        }
        else
        {
            fputs ("cannot get date/time\n", stderr);
            rtc = EXIT_FAILURE;
        }
    }
    else if (argc == 3)
    {
        if (date_time_set (&tm, argv[1], argv[2]) == SUCCESS)
        {
            date_time_print (&tm);
        }
        else
        {
            fputs ("date/time format error\n", stderr);
            rtc = EXIT_FAILURE;
        }
    }
    else
    {
        fprintf (stderr, "usage: %s [YYYY-MM-DD hh:mm:ss]\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_df () - command: df
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_df (int argc, const char ** argv)
{
    FRESULT     res;
    int         rtc = EXIT_FAILURE;

    if (argc == 1)
    {
        res = fs_df ();

        if (res == FR_OK)
        {
            rtc = EXIT_SUCCESS;
        }
        else
        {
            fputs ("df failed\n", stderr);
        }
    }
    else
    {
        fprintf (stderr, "usage: %s\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_echo () - command: echo
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_echo (int argc, const char ** argv)
{
    int     idx;

    for (idx = 1; idx < argc; idx++)
    {
        fputs (argv[idx], stdout);

        if (idx < argc - 1)
        {
            putchar (' ');
        }
    }

    putchar ('\n');

    return EXIT_SUCCESS;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_find () - command: find
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_find (int argc, const char ** argv)
{
    FRESULT     res;
    int         rtc = EXIT_FAILURE;

    if (argc == 1 || argc == 2)
    {
        const char *    path;

        if (argc == 2)
        {
            path = argv[1];
        }
        else
        {
            path = ".";
        }

        res = fs_find (path);

        if (res == FR_OK)
        {
            rtc = EXIT_SUCCESS;
        }
    }
    else
    {
        fprintf (stderr, "usage: %s [directory]\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_led () - command: led
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_led (int argc, const char ** argv)
{
    int         rtc     = EXIT_SUCCESS;
    int         usage   = 0;

    if (argc == 2)
    {
        if (! strcmp (argv[1], "on"))
        {
            board_led_on ();
        }
        else if (! strcmp (argv[1], "off"))
        {
            board_led_off ();
        }
        else
        {
            usage = 1;
        }
    }
    else
    {
        usage = 1;
    }

    if (usage)
    {
        fprintf (stderr, "usage: %s on|off\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_ls () - command: ls
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_ls (int argc, const char ** argv)
{
    FRESULT         res = FR_OK;
    int             idx;
    const char *    pgm         = argv[0];
    uint_fast8_t    ls_flags    = 0;
    uint_fast8_t    ls_sort     = LS_SORT_FNAME;
    int             rtc         = EXIT_SUCCESS;

    while (argc > 1 && argv[1][0] == '-')
    {
        for (idx = 1; argv[1][idx]; idx++)
        {
            if (argv[1][idx] == 'l')                                                                    // long
            {
                ls_flags |= LS_FLAG_LONG;
            }
            else if (argv[1][idx] == 'a')                                                               // all files
            {
                ls_flags |= LS_FLAG_SHOW_ALL;
            }
            else if (argv[1][idx] == 'U')                                                               // U = Unsorted
            {
                ls_sort = LS_SORT_NONE;
            }
            else if (argv[1][idx] == 'S')                                                               // S = Sort by file size
            {
                ls_sort = LS_SORT_FSIZE;
            }
            else if (argv[1][idx] == 't')                                                               // t = Sort by (m)time
            {
                ls_sort = LS_SORT_FTIME;
            }
            else if (argv[1][idx] == 'r')                                                               // r = Sort reverse
            {
                ls_flags |= LS_FLAG_SORT_REVERSE;
            }
            else
            {
                fprintf (stderr, "usage: %s [-alUStr] [files...]\n", pgm);
                return EXIT_FAILURE;
            }
        }

        argc--;
        argv++;
    }

    if (argc == 1)
    {
        res = fs_ls ("");
    }
    else
    {
        for (idx = 1; idx < argc; idx++)
        {
            res = fs_ls (argv[idx]);

            if (res != FR_OK)
            {
                rtc = EXIT_FAILURE;
            }
        }
    }

    fs_ls_output (ls_flags, ls_sort);

    if (res != FR_OK)
    {
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_mkdir () - command: mkdir
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_mkdir (int argc, const char ** argv)
{
    FRESULT     res;
    int         rtc = EXIT_SUCCESS;

    if (argc > 1)
    {
        int     idx;

        for (idx = 1; idx < argc; idx++)
        {
            res = fs_mkdir (argv[idx]);

            if (res != FR_OK)
            {
                fs_perror (argv[idx], res);
                rtc = EXIT_FAILURE;
            }
        }
    }
    else
    {
        fprintf (stderr, "usage: %s dir ...\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_mv () - command: mv
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_mv (int argc, const char ** argv)
{
    FRESULT         res = FR_OK;
    int             is_dir;
    int             idx;
    uint_fast8_t    flags = 0;
    int             rtc = EXIT_FAILURE;

    while (argc > 1 && argv[1][0] == '-')
    {
        for (idx = 1; argv[1][idx]; idx++)
        {
            if (argv[1][idx] == 'v')
            {
                flags |= FS_MV_FLAG_VERBOSE;
            }
            else
            {
                fprintf (stderr, "usage: %s [-v] source dest\n", argv[0]);
                fprintf (stderr, "  or:  %s [-v] source ... destdir\n", argv[0]);
                return EXIT_FAILURE;
            }
        }

        argc--;
        argv++;
    }

    if (argc >= 3)
    {
        static FILINFO  fno;

        is_dir = fs_is_dir (argv[argc - 1], &fno);

        if (is_dir == 1)
        {
            char            targetfile[FS_MAX_PATH_LEN];
            const char *    targetdir = argv[argc - 1];
            int             len = strlen (targetdir);

            for (idx = 1; idx < argc - 1; idx++)
            {
                if (! strcmp (targetdir, "/"))
                {
                    sprintf (targetfile, "/%s", fs_basename (argv[idx]));
                }
                else if (len > 0 && targetdir[len - 1] == '/')
                {
                    sprintf (targetfile, "%s%s", targetdir, fs_basename (argv[idx]));       // targetdir already contains slash
                }
                else
                {
                    sprintf (targetfile, "%s/%s", targetdir, fs_basename (argv[idx]));
                }

                res = fs_mv (argv[idx], targetfile, flags);

                if (res != FR_OK)
                {
                    fprintf (stderr, "%s: mv to %s failed\n", argv[idx], targetfile);
                }
            }
        }
        else if (argc == 3)
        {
            res = fs_mv (argv[1], argv[2], flags);

            if (res != FR_OK)
            {
                fs_perror ("mv", res);
            }
        }
        else
        {
            fprintf (stderr, "%s: is no directory\n", argv[argc - 1]);
            res = -1;
        }

        if (res == FR_OK)
        {
            rtc = EXIT_SUCCESS;
        }
    }
    else
    {
        fprintf (stderr, "usage: %s source dest\n", argv[0]);
        fprintf (stderr, "  or:  %s source ... destdir\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_pwd () - command: pwd
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_pwd (int argc, const char ** argv)
{
    int         rtc = EXIT_FAILURE;

    if (argc == 1)
    {
        printf ("%s\n", curwd);
    }
    else
    {
        printf ("usage: %s\n", argv[0]);
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_rm () - command: rm
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
cmd_rm (int argc, const char ** argv)
{
    FRESULT     res;
    int         rtc = EXIT_SUCCESS;

    if (argc > 1)
    {
        int     idx;

        for (idx = 1; idx < argc; idx++)
        {
            res = fs_rm (argv[idx]);

            if (res != FR_OK)
            {
                rtc = EXIT_FAILURE;
            }
        }
    }
    else
    {
        fprintf (stderr, "usage: %s file ...\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_rmdir () - command: rmdir
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_rmdir (int argc, const char ** argv)
{
    FRESULT     res;
    int         rtc = EXIT_SUCCESS;

    if (argc > 1)
    {
        int     idx;

        for (idx = 1; idx < argc; idx++)
        {
            res = fs_rmdir (argv[idx]);

            if (res != FR_OK)
            {
                fs_perror (argv[idx], res);
                rtc = EXIT_FAILURE;
            }
        }
    }
    else
    {
        fprintf (stderr, "usage: %s dir ...\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_sleep () - command: sleep
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
static int
cmd_sleep (int argc, const char ** argv)
{
    int rtc;

    if (argc == 2)
    {
        uint32_t seconds = atoi (argv[1]);
        delay_sec (seconds);
        rtc = EXIT_SUCCESS;
    }
    else
    {
        fprintf (stderr, "usage: %s seconds\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}


static int do_mount (void)
{
    FRESULT         res;
    uint_fast8_t    cnt = 0;
    int             rtc;

    if (mounted)
    {
        printf ("SD card already mounted\n");
        rtc = EXIT_SUCCESS;
    }
    else
    {
        do
        {
            res = f_mount (&fs, "", 1);
            if (res == FR_NOT_READY)
            {
                delay_msec (10);
                cnt++;
            }
        } while (res == FR_NOT_READY && cnt < 10);

        if (res == FR_OK)
        {
            printf ("SD card mounted, retry count = %d\n", cnt);
            mounted = 1;
            rtc = EXIT_SUCCESS;
        }
        else
        {
            fs_perror ("mount", res);
            rtc = EXIT_FAILURE;
        }
    }
    return rtc;
}

static void do_umount (void)
{
    FRESULT     res;

    res = f_mount (0, "", 0);
    printf ("SD card umounted, res=%d\n", res);
    strcpy (curwd, "/");
    mounted = 0;
}

static int
cmd_mount (int argc, const char ** argv)
{
    int             rtc;

    if (argc == 1)
    {
        rtc = do_mount ();
    }
    else
    {
        fprintf (stderr, "usage: %s\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

static int
cmd_umount (int argc, const char ** argv)
{
    int     rtc;

    if (argc == 1)
    {
        do_umount ();
        rtc = EXIT_SUCCESS;
    }
    else
    {
        fprintf (stderr, "usage: %s\n", argv[0]);
        rtc = EXIT_FAILURE;
    }

    return rtc;
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd_start () - command: start
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
int
cmd_start (int argc, const char ** argv, const char * stdout_file, int stdout_append, const char * stderr_file, int stderr_append)
{
    FILE *          stdout_fp = NULL;
    FILE *          stderr_fp = NULL;
    const char *    command;
    uint32_t        milli_start = 0;
    uint32_t        milli_end;
    int             rtc;

    if (! strcmp (argv[0], "time"))
    {
        argc--;
        argv++;

        milli_start = milliseconds;
    }


    if (stdout_file && *stdout_file)
    {
        stdout_fp = fopen (stdout_file, stdout_append ? "a" : "w");

        if (stdout_fp)
        {
            fs_stdout_fd = fileno(stdout_fp);
        }
        else
        {
            fprintf (stderr, "%s: cannot open\n", stdout_file);
            return EXIT_FAILURE;
        }
    }

    if (stderr_file && *stderr_file)
    {
        stderr_fp = fopen (stderr_file, stderr_append ? "a" : "w");

        if (stderr_fp)
        {
            fs_stderr_fd = fileno(stderr_fp);
        }
        else
        {
            fprintf (stderr, "%s: cannot open\n", stderr_file);
            return EXIT_FAILURE;
        }
    }

    command = argv[0];

#if 0 && defined DEBUG
    int idx;
    console_printf ("argc = '%d'\r\n", argc);

    for (idx = 0; idx < argc; idx++)
    {
        console_printf ("argv[%d] = '%s'\r\n", idx, argv[idx]);
    }
#endif

    if (! strcmp (command, "cat"))
    {
        rtc = cmd_cat (argc, argv);
    }
    else if (! strcmp (command, "cd"))
    {
        rtc = cmd_cd (argc, argv);
    }
    else if (! strcmp (command, "clocks"))
    {
        rtc = cmd_clocks (argc, argv);
    }
    else if (! strcmp (command, "cp"))
    {
        rtc = cmd_cp (argc, argv);
    }
    else if (! strcmp (command, "date"))
    {
        rtc = cmd_date (argc, argv);
    }
    else if (! strcmp (command, "df"))
    {
        rtc = cmd_df (argc, argv);
    }
    else if (! strcmp (command, "echo"))
    {
        rtc = cmd_echo (argc, argv);
    }
    else if (! strcmp (command, "find"))
    {
        rtc = cmd_find (argc, argv);
    }
    else if (! strcmp (command, "fe"))
    {
        rtc = cmd_fe (argc, argv);
    }
    else if (! strcmp (command, "led"))
    {
        rtc = cmd_led (argc, argv);
    }
    else if (! strcmp (command, "ls"))
    {
        rtc = cmd_ls (argc, argv);
    }
    else if (! strcmp (command, "mkdir"))
    {
        rtc = cmd_mkdir (argc, argv);
    }
    else if (! strcmp (command, "mount"))
    {
        rtc = cmd_mount (argc, argv);
    }
    else if (! strcmp (command, "mv"))
    {
        rtc = cmd_mv (argc, argv);
    }
    else if (! strcmp (command, "nic"))
    {
        rtc = cmd_nic (argc, argv);
    }
    else if (! strcmp (command, "nicc"))
    {
        rtc = cmd_nicc (argc, argv);
    }
    else if (! strcmp (command, "pwd"))
    {
        rtc = cmd_pwd (argc, argv);
    }
    else if (! strcmp (command, "rm"))
    {
        rtc = cmd_rm (argc, argv);
    }
    else if (! strcmp (command, "rmdir"))
    {
        rtc = cmd_rmdir (argc, argv);
    }
    else if (! strcmp (command, "sleep"))
    {
        rtc = cmd_sleep (argc, argv);
    }
    else if (! strcmp (command, "umount"))
    {
        rtc = cmd_umount (argc, argv);
    }
    else
    {
        char fname[FS_MAX_PATH_LEN];
        FILINFO fno;
        FRESULT res;

        strcpy (fname, command);

        res = f_stat (fname, &fno);

        if (res != FR_OK)
        {
            sprintf (fname, "/bin/%s", command);
            res = f_stat (fname, &fno);
        }

        if (res == FR_OK)
        {
            int l = strlen (fname);

            if (l > 4 && ! strcasecmp (fname + l - 4, ".nic"))
            {
                const char *    nic_argv[argc + 1];
                int             nic_argc = argc + 1;
                int             idx;

                nic_argv[0] = "nic";

                for (idx = 0; idx < argc; idx++)
                {
                    nic_argv[idx + 1] = argv[idx];
                }

                rtc = cmd_nic (nic_argc, nic_argv);
            }
            else if (l > 2 && ! strcasecmp (fname + l - 2, ".n"))
            {
                int             nicc_argc       = 2;
                const char *    nicc_argv[3]    = { "nicc", fname, (char *) NULL };

                rtc = cmd_nicc (nicc_argc, nicc_argv);
            }
            else
            {
                char    buf[80];
                FILE *  fp;

                fp  = fopen (fname, "r");

                if (fp)
                {
                    while (fgets (buf, 80, fp))
                    {
                        cmd (buf);
                    }

                    fclose (fp);
                    rtc = EXIT_SUCCESS;
                }
            }
        }
        else
        {
            fprintf (stderr, "%s: command not found\n", argv[0]);
            rtc = EXIT_FAILURE;
        }
    }

    if (stderr_fp)
    {
        fclose (stderr_fp);
        fs_stderr_fd = -1;
    }

    if (stdout_fp)
    {
        fclose (stdout_fp);
        fs_stdout_fd = -1;
    }

    // print time on terminal, ignore redirection of stdout or stderr
    if (milli_start > 0)
    {
        milli_end   = milliseconds;
        fprintf (stderr, "time: %lu msec\n", milli_end - milli_start);
    }

    return rtc;
}

#define MAX_HISTORY         16
#define MAX_HISTORY_BUFLEN  80
static char                 history[MAX_HISTORY][MAX_HISTORY_BUFLEN];
static uint_fast8_t         cur_history;

static int
my_tolower (int ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        ch = ch - 'A' + 'a';
    }
    return ch;
}

static int
cmd_arg_identical (const char * pattern, const char *s2, int minlen)
{
    int n = 0;

    while (*pattern && *s2)
    {
        int chp = *pattern;
        int ch2 = *s2;

        if (my_tolower (chp) != my_tolower (ch2))
        {
            break;
        }
        n++;
        pattern++;
        s2++;
    }

    if (n < minlen)
    {
        n = -1;
    }

    return n;
}

static void
cmd_getnstr (const char * prompt, char * str, uint_fast8_t maxlen)
{
    uint_fast8_t    ch;
    uint_fast8_t    last_ch = '\0';
    uint_fast8_t    curlen = 0;
    uint_fast8_t    curpos = 0;
    uint_fast8_t    idx;
    uint_fast8_t    hist_idx = cur_history;
    uint_fast8_t    hist_offset = 0;
    uint_fast8_t    double_tab = 0;

    history[hist_idx][0] = '\0';

    maxlen--;                               // reserve one byte in order to store '\0' in last position

    console_puts (prompt);

    while ((ch = getch ()) != KEY_CR)
    {
        if (ch == KEY_TAB && last_ch == KEY_TAB)
        {
            double_tab = 1;
            last_ch = '\0';
        }
        else
        {
            double_tab = 0;
            last_ch = ch;
        }

        switch (ch)
        {
            case KEY_LEFT:
                if (curpos > 0)
                {
                    curpos--;
                    console_puts ("\033[D");
                }
                break;
            case KEY_RIGHT:
                if (curpos < curlen)
                {
                    console_puts ("\033[C");
                    curpos++;
                }
                break;
            case KEY_UP:
            case KEY_CTRL ('P'):
                if (hist_offset < MAX_HISTORY - 1)
                {
                    if (hist_offset == 0)
                    {
                        str[curlen] = '\0';
                        strncpy (history[cur_history], str, MAX_HISTORY_BUFLEN - 1);
                    }

                    hist_offset++;

                    if (hist_idx > 0)
                    {
                        hist_idx--;
                    }
                    else
                    {
                        hist_idx = MAX_HISTORY - 1;
                    }

                    if (hist_idx == cur_history)
                    {
                        strncpy (history[hist_idx], str, MAX_HISTORY_BUFLEN - 1);
                    }

                    strncpy (str, history[hist_idx], maxlen);
                    addch ('\r');
                    console_puts (prompt);
                    console_puts (str);
                    clrtoeol ();
                    curlen = strlen (str);
                    curpos = curlen;
                }
                break;
            case KEY_DOWN:
            case KEY_CTRL ('N'):
                if (hist_offset > 0)
                {
                    hist_offset--;

                    if (hist_idx < MAX_HISTORY - 1)
                    {
                        hist_idx++;
                    }
                    else
                    {
                        hist_idx = 0;
                    }

                    strncpy (str, history[hist_idx], maxlen);
                    addch ('\r');
                    console_puts (prompt);
                    console_puts (str);
                    clrtoeol ();
                    curlen = strlen (str);
                    curpos = curlen;
                }
                break;
            case KEY_TAB:
                if (curpos > 0)
                {
                    FRESULT         res;
                    DIR             dir;
                    static FILINFO  fno;
                    char            b[maxlen];
                    char            found_fname[FS_MAX_PATH_LEN];
                    int             found_cnt = 0;
                    int             is_dir = 0;
                    char *          dname;
                    int             l;
                    int             identical;
                    int             min_identical = 0xFF;
                    int             pos = curpos;

                    while (pos > 0 && str[pos - 1] != ' ')
                    {
                        pos--;
                    }

                    strncpy (b, str + pos, curpos - pos);
                    b[curpos - pos] = '\0';
                    dname = fs_dirname (b, 0);
                    strcpy (found_fname, fs_basename (b));

                    res = f_opendir (&dir, dname);

                    // console_printf ("\r\nopendir='%s', res=%d\r\n", dname, res);

                    if (res == FR_OK)
                    {
                        l = strlen (found_fname);

                        for (;;)
                        {
                            res = f_readdir (&dir, &fno);

                            if (res != FR_OK || fno.fname[0] == 0)          // end of directory entries: fname is empty
                            {
                                break;
                            }

                            identical = cmd_arg_identical (found_fname, fno.fname, l);

                            if (identical >= 0 && min_identical >= identical)
                            {
                                if (double_tab)
                                {
                                    if (min_identical == 0xFF)
                                    {
                                        putchar ('\n');
                                    }
                                    printf ("%s\n", fno.fname);
                                }

                                if (min_identical == 0xFF)
                                {
                                    strcpy (found_fname, fno.fname);
                                    min_identical = strlen (fno.fname);
                                }
                                else
                                {
                                    found_fname[identical] = '\0';
                                    min_identical = identical;
                                }
                                found_cnt++;

                                if (found_cnt == 1 && fno.fattrib & AM_DIR)
                                {
                                    is_dir = 1;
                                }
                            }
                        }

                        f_closedir (&dir);

                        if (min_identical != 0xFF)
                        {
                            if (*dname)
                            {
                                if (! strcmp (dname, "/"))
                                {
                                    sprintf (str + pos, "/%s", found_fname);
                                }
                                else
                                {
                                    sprintf (str + pos, "%s/%s", dname, found_fname);
                                }
                            }
                            else
                            {
                                strcpy (str + pos, found_fname);
                            }

                            curlen = strlen (str);

                            if (found_cnt == 1)                                         // exactly one file found?
                            {                                                           // yes, append space or slash
                                if (is_dir)
                                {
                                    str[curlen] = '/';
                                }
                                else
                                {
                                    str[curlen] = ' ';
                                }
                                curlen++;
                                str[curlen] = '\0';
                                last_ch = '\0';                                         // reset last_ch!
                            }

                            curpos = curlen;

                            if (double_tab)
                            {
                                console_puts (prompt);
                            }
                            else
                            {
                                addch ('\r');
                                console_puts (prompt);
                            }

                            console_puts (str);
                            clrtoeol ();
                        }
                    }
                    else
                    {
                        fs_perror (dname, res);
                    }
                }
                break;
            case KEY_HOME:
            case KEY_CTRL ('A'):
                if (curpos != 0)
                {
                    console_printf ("\033[%dD", curpos);
                    curpos = 0;
                }
                break;
            case KEY_END:
            case KEY_CTRL ('E'):
                if (curpos != curlen)
                {
                    console_printf ("\033[%dC", curlen - curpos);
                    curpos = curlen;
                }
                break;
            case KEY_BACKSPACE:
                if (curpos > 0)
                {
                    curpos--;
                    curlen--;
                    addch ('\b');

                    for (idx = curpos; idx < curlen; idx++)
                    {
                        str[idx] = str[idx + 1];
                    }
                    str[idx] = '\0';
                    delch();
                }
                break;

            case KEY_DC:
            case KEY_CTRL ('D'):
                if (curlen > 0)
                {
                    curlen--;
                    for (idx = curpos; idx < curlen; idx++)
                    {
                        str[idx] = str[idx + 1];
                    }
                    str[idx] = '\0';
                    delch();
                }
                break;

            case KEY_CTRL ('K'):
                curlen = curpos;
                clrtoeol ();
                break;

            default:
                if (curlen < maxlen && (ch & 0x7F) >= 32 && (ch & 0x7F) < 127)      // printable ascii 7bit or printable 8bit ISO8859
                {
                    for (idx = curlen; idx > curpos; idx--)
                    {
                        str[idx] = str[idx - 1];
                    }
                    insch (ch);
                    str[curpos] = ch;
                    curpos++;
                    curlen++;
                }
        }
    }
    str[curlen] = '\0';

    if (*str)
    {
        strncpy (history[cur_history], str, MAX_HISTORY_BUFLEN - 1);

        if (cur_history < MAX_HISTORY - 1)
        {
            cur_history++;
        }
        else
        {
            cur_history = 0;
        }
    }
}

static void
strip_crnl (char * s)
{
    char *      p;

    p = strchr (s, '\r');

    if (p)
    {
        *p = '\0';
    }

    p = strchr (s, '\n');

    if (p)
    {
        *p = '\0';
    }
}

/*---------------------------------------------------------------------------------------------------------------------------------------------------
 * cmd () - command line parser
 *---------------------------------------------------------------------------------------------------------------------------------------------------
 */
void
cmd (char * cmdline)
{
    static FILE *   fp_boot;
    static int      already_called = 0;

    char            buf[80];
    char *          argv[MAXARGS];
    uint_fast8_t    alloc[MAXARGS];
    int             argc;
    int             valid;
    int             idx;
    int             stdout_append = 0;
    int             stderr_append = 0;
    char *          stdout_file  = (char *) NULL;
    char *          stderr_file  = (char *) NULL;
    char *          p = strchr (buf, '\n');

    if (! already_called)
    {
        already_called = 1;

        do_mount ();

        fp_boot = fopen ("boot", "r");
    }

    if (fp_boot)
    {
        if (fgets (buf, 80, fp_boot))
        {
            strip_crnl (buf);
        }
        else
        {
            fclose (fp_boot);
            fp_boot = (FILE *) NULL;
            buf[0] = '\0';
        }
    }

    if (! fp_boot)
    {
        if (cmdline)
        {
            strcpy (buf, cmdline);
            strip_crnl (buf);
        }
        else
        {
            fs_close_all_open_files ();

            cmd_getnstr ("$ ", buf, 80);
            console_puts ("\r\n");
        }
    }

    p = strchr (buf, '\n');

    if (p)
    {
        *p = 0;
    }

    valid       = 1;
    argc        = 0;
    argv[argc]  = buf;
    alloc[argc] = 0;
    argc++;

    for (p = buf; *p && valid; p++)
    {
        if (*p == ' ')
        {
            *p++ = '\0';

            while (*p == ' ')
            {
                p++;
            }

            if (*p)
            {
                char *  spacep;

                spacep = strchr (p, ' ');

                if (spacep)
                {
                    *spacep = '\0';                                                             // terminate next argument;
                }

                if (strchr (p, '*') || strchr (p, '?'))                                         // wildcard in argument?
                {
                    FRESULT     fr;
                    DIR         dj;
                    FILINFO     fno;
                    int         found = 0;

                    char * path;
                    char * slash = (char *) NULL;
                    char * pp;
                    char * pattern;

                    pp = p;

                    while (pp)
                    {
                        pp = strchr (pp, '/');

                        if (pp)
                        {
                            slash = pp;
                            pp++;
                        }
                    }

                    if (slash)
                    {
                        *slash  = '\0';
                        path    = p;
                        pattern = slash + 1;
                    }
                    else
                    {
                        path    = "";
                        pattern = p;
                    }

                    fr = f_findfirst(&dj, &fno, path, pattern);

                    // console_printf ("\r\n findfirst: path='%s' pattern='%s' fr=%d, fno.fname='%s'", path, pattern, fr, fno.fname);

                    if (fr != FR_OK)
                    {
                        fs_perror (path, fr);
                    }

                    while (fr == FR_OK && fno.fname[0])
                    {
                        if (argc < MAXARGS - 1)
                        {
                            char * m;

                            m = malloc (strlen (path) + strlen (fno.fname) + 2);

                            if (m)
                            {
                                if (*path)
                                {
                                    sprintf (m, "%s/%s", path, fno.fname);
                                }
                                else
                                {
                                    strcpy (m, fno.fname);
                                }

                                argv[argc] = m;
                                alloc[argc] = 1;
                                argc++;
                            }
                            else
                            {
                                console_puts ("not enough memory\r\n");
                                valid = 0;
                                break;
                            }
                        }
                        else
                        {
                            console_puts ("too many arguments\r\n");
                            valid = 0;
                            break;
                        }

                        found++;

                        fr = f_findnext(&dj, &fno);
                        // console_printf ("\r\n findnext: fno.fname='%s'", path, pattern, fr, fno.fname);
                    }

                    f_closedir (&dj);

                    if (slash)
                    {
                        *slash  = '/';
                    }

                    if (valid && ! found)
                    {
                        if (argc < MAXARGS - 1)
                        {
                            argv[argc] = p;
                            alloc[argc] = 0;
                            argc++;
                        }
                        else
                        {
                            console_puts ("too many arguments\r\n");
                            valid = 0;
                        }
                    }

                    if (spacep)
                    {
                        *spacep = ' ';                                                          // repair termination
                    }
                }
                else
                {
                    if (spacep)
                    {
                        *spacep = ' ';                                                          // repair termination
                    }

                    if (*p == '>')
                    {
                        p++;

                        if (*p == '>')
                        {
                            p++;
                            stdout_append = 1;
                        }
                        else
                        {
                            stdout_append = 0;
                        }

                        while (*p == ' ')
                        {
                            p++;
                        }

                        if (*p)
                        {
                            stdout_file = p;
                        }
                        else
                        {
                            console_puts ("no redirect filename found\r\n");
                            valid = 0;
                        }
                    }
                    else if (*p == '2' && *(p + 1) == '>')
                    {
                        p += 2;

                        if (*p == '>')
                        {
                            p++;
                            stderr_append = 1;
                        }
                        else
                        {
                            stderr_append = 0;
                        }

                        while (*p == ' ')
                        {
                            p++;
                        }

                        if (*p)
                        {
                            stderr_file = p;
                        }
                        else
                        {
                            console_puts ("no redirect filename found\r\n");
                            valid = 0;
                        }
                    }
                    else
                    {
                        if (argc < MAXARGS - 1)
                        {
                            argv[argc] = p;
                            alloc[argc] = 0;
                            argc++;
                        }
                        else
                        {
                            console_puts ("too many arguments\r\n");
                            valid = 0;
                        }
                    }
                }
            }
        }
    }

    if (valid)
    {
        if (argv[0][0])
        {
            cmd_start (argc, (const char **) argv, stdout_file, stdout_append, stderr_file, stderr_append);
        }
    }

    for (idx = 0; idx < argc; idx++)
    {
        if (alloc[idx])
        {
            free (argv[idx]);
            alloc[idx] = 0;
        }
    }
}
