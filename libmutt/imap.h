/*
 * Copyright (C) 1996-8 Michael R. Elkins <me@cs.hmc.edu>
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

#ifndef _IMAP_H
#define _IMAP_H 1

int imap_append_message (CONTEXT *ctx, MESSAGE *msg);
int imap_check_mailbox (CONTEXT *ctx, int *index_hint);
int imap_fetch_message (MESSAGE *msg, CONTEXT *ctx, int msgno);
int imap_open_mailbox (CONTEXT *ctx);
int imap_open_mailbox_append (CONTEXT *ctx);
int imap_sync_mailbox (CONTEXT *ctx);
void imap_fastclose_mailbox (CONTEXT *ctx);
int imap_buffy_check (char *path);

#endif
