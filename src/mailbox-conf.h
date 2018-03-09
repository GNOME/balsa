/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#ifndef __MAILBOX_CONF_H__
#define __MAILBOX_CONF_H__

#include "mailbox-node.h"

#include "server.h"

typedef struct _BalsaMailboxConfView BalsaMailboxConfView;

void mailbox_conf_new(GType mailbox_type);
void mailbox_conf_edit(BalsaMailboxNode *mbnode);
void mailbox_conf_delete(BalsaMailboxNode *mbnode);

/* callbacks used also by the main window menu */
void mailbox_conf_add_mbox_cb(GtkWidget *widget,
                              gpointer   data);
void mailbox_conf_add_maildir_cb(GtkWidget *widget,
                                 gpointer   data);
void mailbox_conf_add_mh_cb(GtkWidget *widget,
                            gpointer   data);
void mailbox_conf_add_imap_cb(GtkWidget *widget,
                              gpointer   data);
void mailbox_conf_delete_cb(GtkWidget *widget,
                            gpointer   data);
void mailbox_conf_edit_cb(GtkWidget *widget,
                          gpointer   data);

/* Helpers for dialogs. */
BalsaMailboxConfView *mailbox_conf_view_new(LibBalsaMailbox *mailbox,
                                            GtkWindow       *window,
                                            GtkWidget       *grid,
                                            gint             row,
                                            GCallback        callback);
void mailbox_conf_view_check(BalsaMailboxConfView *mcc,
                             LibBalsaMailbox      *mailbox);


typedef struct {
    GtkWidget *use_ssl;
    GtkWidget *tls_mode;
    GtkGrid *grid;         /* internal */
    GtkWidget *tls_option; /* internal */
    GtkWidget *server;     /* internal */
    GtkWidget *need_client_cert;
    GtkWidget *client_cert_file;
    GtkWidget *client_cert_passwd;
    const gchar *default_ports;
    unsigned used_rows;    /* internal */
} BalsaServerConf;
#define IMAP_DEFAULT_PORTS "143 993 imap imaps"

GtkWidget *balsa_server_conf_get_advanced_widget(BalsaServerConf *bsc,
                                                 LibBalsaServer  *s,
                                                 int              extra_rows);
GtkWidget *balsa_server_conf_add_checkbox(BalsaServerConf *bsc,
                                          const char      *label);
GtkWidget *balsa_server_conf_add_spinner(BalsaServerConf *bsc,
                                         const char      *label,
                                         gint             lo,
                                         gint             hi,
                                         gint             step,
                                         gint             initial_value);
void            balsa_server_conf_set_values(BalsaServerConf *bsc,
                                             LibBalsaServer  *server);
gboolean        balsa_server_conf_get_use_ssl(BalsaServerConf *bsc);
LibBalsaTlsMode balsa_server_conf_get_tls_mode(BalsaServerConf *bsc);

#endif                          /* __MAILBOX_CONF_H__ */
