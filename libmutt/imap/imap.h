/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
 * Copyright (C) 2000-1 Brendan Cully <brendan@kublai.com>
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

#ifndef _IMAP_H
#define _IMAP_H 1

#include "account.h"
#include "browser.h"
#include "mailbox.h"

/* -- data structures -- */
typedef struct
{
  ACCOUNT account;
  char* mbox;
} IMAP_MBOX;

/* imap.c */
int imap_access (const char*, int);
int imap_check_mailbox (CONTEXT *ctx, int *index_hint);
int imap_close_connection (CONTEXT *ctx);
int imap_delete_mailbox (CONTEXT* idata, IMAP_MBOX mx);
int imap_open_mailbox (CONTEXT *ctx);
int imap_open_mailbox_append (CONTEXT *ctx);
int imap_sync_mailbox (CONTEXT *ctx, int expunge, int *index_hint);
void imap_close_mailbox (CONTEXT *ctx);
int imap_buffy_check (char *path);
#ifdef LIBMUTT
int imap_mailbox_check(char *path, int new,
		       int imap_check_test(const char *mbox));
#else
int imap_mailbox_check (char *path, int new);
#endif
int imap_subscribe (const char *path, int subscribe);
int imap_complete (char* dest, size_t dlen, char* path);

void imap_allow_reopen (CONTEXT *ctx);
void imap_disallow_reopen (CONTEXT *ctx);

/* browse.c */
int imap_browse (char* path, struct browser_state* state);
/* LIBMUTT - BALSA: changed prototype for imap_mailbox_create: */
int imap_mailbox_create (const char* folder, const char* subfolder, int subscribe);
/* BALSA: prototype for new function imap_mailbox_rename: */
int imap_mailbox_rename (const char* url, const char* parent, 
                         const char* subfolder, int subscribe);
/* BALSA: prototype for new function imap_mailbox_delete: */
int imap_mailbox_delete (const char* path);


/* message.c */
int imap_append_message (CONTEXT* ctx, MESSAGE* msg);
int imap_copy_messages (CONTEXT* ctx, HEADER* h, char* dest, int delete);
int imap_fetch_message (MESSAGE* msg, CONTEXT* ctx, int msgno);
void imap_update_header_info(MESSAGE*msg, CONTEXT*ctx, int msgno);

/* socket.c */
void imap_logout_all (void);

/* util.c */
int imap_expand_path (char* path, size_t len);
int imap_parse_path (const char* path, IMAP_MBOX* mx);
void imap_pretty_mailbox (char* path);

int imap_wait_keepalive (pid_t pid);
void imap_keepalive (void);

#endif
