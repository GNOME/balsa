/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option) 
 * any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the  
 * GNU General Public License for more details.
 *  
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __LIBBALSA_NOTIFY_H__
#define __LIBBALSA_NOTIFY_H__


#ifdef BALSA_ENABLE_DEPRECATED
/* Initialize the notification system */
void libbalsa_notify_init(void);

/* Add a mailbox to the notification system */
void libbalsa_notify_register_mailbox(LibBalsaMailbox * mailbox);
void libbalsa_notify_unregister_mailbox(LibBalsaMailbox * mailbox);
#else
#define libbalsa_notify_init()
#define libbalsa_notify_register_mailbox(mailbox)
#define libbalsa_notify_unregister_mailbox(mailbox)
#endif

/* Call libbalsa_notify_start_check before checking each mailbox */
void libbalsa_notify_start_check(gboolean imap_check_test(const gchar *path));

/* Used by the mailboxes. */
gint libbalsa_notify_check_mailbox(LibBalsaMailbox * mailbox);

#endif				/* __LIBBALSA_NOTIFY_H__ */
