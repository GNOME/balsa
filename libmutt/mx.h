/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 1999 Thomas Roessler <roessler@guug.de>
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
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */ 

/*
 * This header file contains prototypes for internal functions used by the
 * generic mailbox api.  None of these functions should be called directly.
 */

#ifndef _MX_H
#define _MX_H

#include "mailbox.h"

/* supported mailbox formats */
enum
{
  M_MBOX = 1,
  M_MMDF,
  M_KENDRA,
  M_MH,
  M_MAILDIR
#ifdef USE_IMAP
  , M_IMAP
#endif
};

WHERE short DefaultMagic INITVAL (M_MBOX);

#define MMDF_SEP "\001\001\001\001\n"
#define KENDRA_SEP "\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\001\n"
#define MAXLOCKATTEMPT 5

int mbox_sync_mailbox (CONTEXT *);
int mbox_open_mailbox (CONTEXT *);
int mbox_check_mailbox (CONTEXT *, int *);
int mbox_close_mailbox (CONTEXT *);
int mbox_lock_mailbox (CONTEXT *, int, int);
int mbox_parse_mailbox (CONTEXT *);
int mmdf_parse_mailbox (CONTEXT *);

int mh_read_dir (CONTEXT *, const char *);
int mh_sync_mailbox (CONTEXT *);
int mh_check_mailbox (CONTEXT *, int *);
int mh_parse_sequences (CONTEXT *, const char *);

int maildir_read_dir (CONTEXT *);

int maildir_commit_message (CONTEXT *, MESSAGE *, HEADER *);
int mh_commit_message (CONTEXT *, MESSAGE *, HEADER *);

int maildir_open_new_message (MESSAGE *, CONTEXT *, HEADER *);
int mh_open_new_message (MESSAGE *, CONTEXT *, HEADER *);

int mbox_strict_cmp_headers (const HEADER *, const HEADER *);
int mutt_reopen_mailbox (CONTEXT *, int *);

void mx_alloc_memory (CONTEXT *);
void mx_update_context (CONTEXT *);
void mx_update_tables (CONTEXT *, int);

FILE *mx_open_file_lock (const char *, const char *);


int mx_lock_file (const char *, int, int, int, int);
int mx_unlock_file (const char *path, int fd, int dot);


#endif
