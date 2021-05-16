/*-------------------------------------------------------------------------------------------------------------------------------------------
 * fs.h - filesystem functions
 *-------------------------------------------------------------------------------------------------------------------------------------------
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
 *-------------------------------------------------------------------------------------------------------------------------------------------
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>

#include "stm32_sdcard.h"
#include "ff.h"

#define FS_MAX_PATH_LEN         64                                             // max path len

#define LS_FLAG_LONG            0x01
#define LS_FLAG_SHOW_ALL        0x02
#define LS_FLAG_SORT_REVERSE    0x20

#define LS_SORT_NONE            0
#define LS_SORT_FNAME           1
#define LS_SORT_FTIME           2
#define LS_SORT_FSIZE           3

#define FS_CP_FLAG_VERBOSE      0x01
#define FS_CP_FLAG_FAST         0x02

#define FS_MV_FLAG_VERBOSE      0x01

extern int                      fs_stdout_fd;
extern int                      fs_stderr_fd;

extern void                     fs_perror (const char *, FRESULT);
extern const char *             fs_basename (const char *);
extern char *                   fs_dirname (const char *, uint_fast8_t);
extern int                      fs_is_dir (const char *, FILINFO  *);
extern int                      fs_ls (const char *);
extern void                     fs_ls_output (uint_fast8_t, uint_fast8_t);

extern int                      fs_cat (const char *);
extern int                      fs_cp (const char *, const char *, uint_fast8_t);
extern int                      fs_df (void);
extern int                      fs_find (const char *);
extern int                      fs_mv (const char *, const char *, uint_fast8_t);
extern int                      fs_mkdir (const char *);
extern int                      fs_rm (const char *);
extern int                      fs_rmdir (const char *);

extern void                     fs_close_all_open_files (void);
