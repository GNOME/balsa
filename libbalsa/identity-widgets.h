/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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


#ifndef __LIBBALSA_IDENTITY_WIDGETS_H__
#define __LIBBALSA_IDENTITY_WIDGETS_H__

#ifndef BALSA_VERSION
#   error "Include config.h before this file."
#endif

#include <gtk/gtk.h>
#include "identity.h"

G_BEGIN_DECLS

void libbalsa_identity_config_dialog(GtkWindow         *parent,
                                     GList            **identities,
                                     LibBalsaIdentity **current,
                                     GSList            *smtp_servers,
                                     void (            *changed_cb)(gpointer));

typedef void (*LibBalsaIdentityCallback) (gpointer          data,
                                          LibBalsaIdentity *identity);

void libbalsa_identity_select_dialog(GtkWindow               *parent,
                                     const gchar             *prompt,
                                     GList                   *identities,
                                     LibBalsaIdentity        *initial_id,
                                     LibBalsaIdentityCallback update,
                                     gpointer                 data);

GtkWidget *libbalsa_identity_combo_box(GList       *identities,
                                       const gchar *active_name,
                                       GCallback    changed_cb,
                                       gpointer     changed_data);

G_END_DECLS

#endif /* __LIBBALSA_IDENTITY_WIDGETS_H__ */
