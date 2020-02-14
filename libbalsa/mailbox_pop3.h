/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __LIBBALSA_MAILBOX_POP3_H__
#define __LIBBALSA_MAILBOX_POP3_H__

#define LIBBALSA_TYPE_MAILBOX_POP3 (libbalsa_mailbox_pop3_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaMailboxPOP3,
                     libbalsa_mailbox_pop3,
                     LIBBALSA,
                     MAILBOX_POP3,
                     LibBalsaMailboxRemote)

LibBalsaMailboxPOP3 *libbalsa_mailbox_pop3_new(void);
void libbalsa_mailbox_pop3_set_inbox(LibBalsaMailbox *mailbox,
                                     LibBalsaMailbox *inbox);

/*
 * Getters
 */
gboolean libbalsa_mailbox_pop3_get_delete_from_server(LibBalsaMailboxPOP3 *mailbox_pop3);
gboolean libbalsa_mailbox_pop3_get_check(LibBalsaMailboxPOP3 *mailbox_pop3);
gboolean libbalsa_mailbox_pop3_get_filter(LibBalsaMailboxPOP3 *mailbox_pop3);
const gchar * libbalsa_mailbox_pop3_get_filter_cmd(LibBalsaMailboxPOP3 *mailbox_pop3);
gboolean libbalsa_mailbox_pop3_get_disable_apop(LibBalsaMailboxPOP3 *mailbox_pop3);
gboolean libbalsa_mailbox_pop3_get_enable_pipe(LibBalsaMailboxPOP3 *mailbox_pop3);

/*
 * Setters
 */
void libbalsa_mailbox_pop3_set_msg_size_limit(LibBalsaMailboxPOP3 *mailbox,
                                              gint sz_limit);
void libbalsa_mailbox_pop3_set_check(LibBalsaMailboxPOP3 *mailbox_pop3,
                                     gboolean check);
void libbalsa_mailbox_pop3_set_disable_apop(LibBalsaMailboxPOP3 *mailbox_pop3,
                                            gboolean disable_apop);
void libbalsa_mailbox_pop3_set_delete_from_server(LibBalsaMailboxPOP3 *mailbox_pop3,
                                             gboolean delete_from_server);
void libbalsa_mailbox_pop3_set_filter(LibBalsaMailboxPOP3 *mailbox_pop3,
                                 gboolean filter);
void libbalsa_mailbox_pop3_set_filter_cmd(LibBalsaMailboxPOP3 *mailbox_pop3,
                                     const gchar * filter_cmd);
void libbalsa_mailbox_pop3_set_enable_pipe(LibBalsaMailboxPOP3 *mailbox_pop3,
                                             gboolean enable_pipe);

#endif				/* __LIBBALSA_MAILBOX_POP3_H__ */
