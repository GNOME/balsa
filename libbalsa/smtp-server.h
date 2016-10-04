/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#if ENABLE_ESMTP
#ifndef __SMTP_SERVER_H__
#define __SMTP_SERVER_H__

#include <gtk/gtk.h>
#include <auth-client.h>
#include "libbalsa.h"

#define LIBBALSA_TYPE_SMTP_SERVER				\
    (libbalsa_smtp_server_get_type())
#define LIBBALSA_SMTP_SERVER(obj)				\
    (G_TYPE_CHECK_INSTANCE_CAST(obj, LIBBALSA_TYPE_SMTP_SERVER,	\
                                LibBalsaSmtpServer))
#define LIBBALSA_SMTP_SERVER_CLASS(klass)			\
    (G_TYPE_CHECK_CLASS_CAST(klass, LIBBALSA_TYPE_SMTP_SERVER,	\
                             LibBalsaSmtpServerClass))
#define LIBBALSA_IS_SMTP_SERVER(obj)				\
    (G_TYPE_CHECK_INSTANCE_TYPE(obj, LIBBALSA_TYPE_SMTP_SERVER))
#define LIBBALSA_IS_SMTP_SERVER_CLASS(klass)			\
    (G_TYPE_CHECK_CLASS_TYPE(klass, LIBBALSA_TYPE_SMTP_SERVER))

GType libbalsa_smtp_server_get_type(void);

LibBalsaSmtpServer *libbalsa_smtp_server_new(void);
LibBalsaSmtpServer *libbalsa_smtp_server_new_from_config(const gchar *
                                                         name);
void libbalsa_smtp_server_save_config(LibBalsaSmtpServer * server);
void libbalsa_smtp_server_set_name(LibBalsaSmtpServer * smtp_server,
                                   const gchar * name);
const gchar *libbalsa_smtp_server_get_name(LibBalsaSmtpServer *
                                           smtp_server);
void libbalsa_smtp_server_set_cert_passphrase(LibBalsaSmtpServer *
                                              smtp_server,
                                              const gchar * passphrase);
const gchar *libbalsa_smtp_server_get_cert_passphrase(LibBalsaSmtpServer *
                                                      smtp_server);
auth_context_t libbalsa_smtp_server_get_authctx(LibBalsaSmtpServer *
                                                smtp_server);
guint libbalsa_smtp_server_get_big_message(LibBalsaSmtpServer *
                                           smtp_server);
void libbalsa_smtp_server_add_to_list(LibBalsaSmtpServer * smtp_server,
                                      GSList ** server_list);

typedef void (*LibBalsaSmtpServerUpdate) (LibBalsaSmtpServer * smtp_server,
                                          GtkResponseType response,
                                          const gchar * old_name);
void libbalsa_smtp_server_dialog(LibBalsaSmtpServer * smtp_server,
                                 GtkWindow * parent,
                                 LibBalsaSmtpServerUpdate update);

#endif                          /* __SMTP_SERVER_H__ */
#endif                          /* ENABLE_ESMTP */
