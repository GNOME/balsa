/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 * 
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#ifndef _MAILBOX_H
#define _MAILBOX_H

/* flags for mutt_open_mailbox() */
#define M_NOSORT	(1<<0) /* do not sort the mailbox after opening it */
#define M_APPEND	(1<<1) /* open mailbox for appending messages */
#define M_READONLY	(1<<2) /* open in read-only mode */
#define M_QUIET		(1<<3) /* do not print any messages */
#define M_NEWFOLDER	(1<<4) /* create a new folder - same as M_APPEND, but uses
				* safe_fopen() for mbox-style folders.
				*/

/* mx_open_new_message() */
#define M_ADD_FROM	1	/* add a From_ line */

/* return values from mx_check_mailbox() */
enum
{
  M_NEW_MAIL = 1,	/* new mail received in mailbox */
  M_LOCKED,		/* couldn't lock the mailbox */
  M_REOPENED,		/* mailbox was reopened */
  M_FLAGS               /* nondestructive flags change (IMAP) */
};

typedef struct
{
  FILE *fp;	/* pointer to the message data */
  char *path;	/* path to temp file */
  short magic;	/* type of mailbox this message belongs to */
  short write;	/* nonzero if message is open for writing */
  struct {
    unsigned read : 1;
    unsigned flagged : 1;
    unsigned replied : 1;
  } flags;
} MESSAGE;

CONTEXT *mx_open_mailbox (const char *, int, CONTEXT *);

MESSAGE *mx_open_message (CONTEXT *, int);
MESSAGE *mx_open_new_message (CONTEXT *, HEADER *, int);

void mx_fastclose_mailbox (CONTEXT *);

int mx_close_mailbox (CONTEXT *, int *);
int mx_sync_mailbox (CONTEXT *ctx, int *);
int mx_commit_message (MESSAGE *, CONTEXT *);
int mx_close_message (MESSAGE **);
int mx_get_magic (const char *);
int mx_set_magic (const char *);
int mx_check_mailbox (CONTEXT *ctx, int *, int );
#ifdef USE_IMAP
int mx_is_imap (const char *);
#endif
#ifdef USE_POP
int mx_is_pop (const char *);
#endif
 
int mx_access (const char*, int);

#endif
