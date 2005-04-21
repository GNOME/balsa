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
#ifndef __IMAP_SERVER_H__
#define __IMAP_SERVER_H__

#define LIBBALSA_TYPE_IMAP_SERVER \
    (libbalsa_imap_server_get_type())
#define LIBBALSA_IMAP_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_IMAP_SERVER, \
                                LibBalsaImapServer))
#define LIBBALSA_IMAP_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_IMAP_SERVER, \
                             LibBalsaImapServerClass))
#define LIBBALSA_IS_IMAP_SERVER(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_IMAP_SERVER))
#define LIBBALSA_IS_IMAP_SERVER_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_IMAP_SERVER))

GType libbalsa_imap_server_get_type(void);
typedef struct LibBalsaImapServer_ LibBalsaImapServer;

LibBalsaImapServer* libbalsa_imap_server_new(const gchar *username,
                                             const gchar *host);
LibBalsaImapServer* libbalsa_imap_server_new_from_config(void);
void libbalsa_imap_server_save_config(LibBalsaImapServer *server);
struct _ImapMboxHandle* libbalsa_imap_server_get_handle
                          (LibBalsaImapServer *server, GError **err);
struct _ImapMboxHandle* libbalsa_imap_server_get_handle_with_user
                          (LibBalsaImapServer *imap_server,
                           gpointer user, GError **err);
void libbalsa_imap_server_release_handle(LibBalsaImapServer *server,
                                         struct _ImapMboxHandle* handle);
void libbalsa_imap_server_set_max_connections(LibBalsaImapServer *server,
                                              int max);
int  libbalsa_imap_server_get_max_connections(LibBalsaImapServer *server);
void libbalsa_imap_server_enable_persistent_cache(LibBalsaImapServer *server,
                                                  gboolean enable);
gboolean libbalsa_imap_server_has_persistent_cache(LibBalsaImapServer *srv);
void libbalsa_imap_server_force_disconnect(LibBalsaImapServer *server);
void libbalsa_imap_server_close_all_connections(void);
gboolean libbalsa_imap_server_has_free_handles(LibBalsaImapServer *server);
gboolean libbalsa_imap_server_is_offline(LibBalsaImapServer *server);
void libbalsa_imap_server_set_offline_mode(LibBalsaImapServer *server,
                                           gboolean offline);

typedef enum {
    ISBUG_FETCH /* Some servers cannot fetch message parts properly
                 * we will fetch entire messages instead. */
} LibBalsaImapServerBug;

void libbalsa_imap_server_set_bug(LibBalsaImapServer *server,
                                  LibBalsaImapServerBug bug, gboolean hasp);

gboolean libbalsa_imap_server_has_bug(LibBalsaImapServer *server,
                                      LibBalsaImapServerBug bug);
void libbalsa_imap_server_set_use_status(LibBalsaImapServer *server,
                                         gboolean use_status);
gboolean libbalsa_imap_server_get_use_status(LibBalsaImapServer *server);

extern gint ImapDebug;
#endif /* __IMAP_SERVER_H__ */
