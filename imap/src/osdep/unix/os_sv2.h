/*
 * Program:	Operating-system dependent routines -- SVR2 version
 *
 * Author:	Mark Crispin
 *		Networks and Distributed Computing
 *		Computing & Communications
 *		University of Washington
 *		Administration Building, AG-44
 *		Seattle, WA  98195
 *		Internet: MRC@CAC.Washington.EDU
 *
 * Date:	10 April 1992
 * Last Edited:	3 June 1998
 *
 * Copyright 1998 by the University of Washington
 *
 *  Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appears in all copies and that both the
 * above copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the University of Washington not be
 * used in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  This software is made
 * available "as is", and
 * THE UNIVERSITY OF WASHINGTON DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED,
 * WITH REGARD TO THIS SOFTWARE, INCLUDING WITHOUT LIMITATION ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, AND IN
 * NO EVENT SHALL THE UNIVERSITY OF WASHINGTON BE LIABLE FOR ANY SPECIAL,
 * INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, TORT
 * (INCLUDING NEGLIGENCE) OR STRICT LIABILITY, ARISING OUT OF OR IN CONNECTION
 * WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <unistd.h>
#include <string.h>
#define char void
#include <memory.h>
#undef char
#include <sys/types.h>
#include <sys/dir.h>
#include <fcntl.h>
#include <syslog.h>
#include <sys/file.h>
#include <ustat.h>


/* Many versions of SysV get this wrong */

#define setpgrp(a,b) Setpgrp(a,b)


/* Different names between BSD and SVR4 */

#define L_SET SEEK_SET
#define L_INCR SEEK_CUR
#define L_XTND SEEK_END

#define lstat stat
#define random lrand48

#define SIGSTOP SIGQUIT

#define S_IFLNK 0120000


/* syslog() emulation */

#define LOG_MAIL        (2<<3)  /* mail system */
#define LOG_AUTH        (4<<3)  /* security/authorization messages */


/* For flock() emulation */

#define flock bsd_flock

#define LOCK_SH 1
#define LOCK_EX 2
#define LOCK_NB 4
#define LOCK_UN 8


/* For setitimer() emulation */

#define ITIMER_REAL	0


/* For opendir() emulation */

typedef struct _dirdesc {
  int dd_fd;
  long dd_loc;
  long dd_size;
  char *dd_buf;
} DIR;

#include "env_unix.h"
#include "fs.h"
#include "ftl.h"
#include "nl.h"
#include "tcp.h"
#include "lockfix.h"

struct passwd *getpwent (void);
struct passwd *getpwuid (int uid);
struct passwd *getpwnam (char *name);

char *getenv (char *name);
long gethostid (void);
void *memmove (void *s,void *ct,size_t n);
char *strstr (char *cs,char *ct);
char *strerror (int n);
unsigned long strtoul (char *s,char **endp,int base);
DIR *opendir (char * name);
int closedir (DIR *d);
struct direct *readdir (DIR *d);
typedef int (*select_t) (struct direct *name);
typedef int (*compar_t) (void *d1,void *d2);
int scandir (char *dirname,struct direct ***namelist,select_t select,
	     compar_t compar);
int bsd_flock (int fd,int operation);
int fsync (int fd);
int openlog (ident,logopt,facility);
int syslog (priority,message,parameters ...);
void *malloc (size_t byteSize);
void free (void *ptr);
void *realloc (void *oldptr,size_t newsize);
