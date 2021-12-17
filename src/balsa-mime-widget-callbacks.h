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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_MIME_WIDGET_CALLBACKS_H__
#define __BALSA_MIME_WIDGET_CALLBACKS_H__

#include "balsa-app.h"
#include "balsa-message.h"


G_BEGIN_DECLS


void balsa_mime_widget_ctx_menu_launch_app(const gchar         * app,
                                           LibBalsaMessageBody * mime_body);
void balsa_mime_widget_ctx_menu_cb(GtkWidget * button,
                                   gpointer    user_data);
void balsa_mime_widget_ctx_menu_save(GtkWidget * parent_widget,
                                     LibBalsaMessageBody * mime_body);
gboolean balsa_mime_widget_key_pressed(GtkEventControllerKey *controller,
                                       guint                  keyval,
                                       guint                  keycode,
                                       GdkModifierType        state,
                                       gpointer               user_data);
void balsa_mime_widget_limit_focus(GtkEventControllerKey *key_controller,
                                   gpointer               user_data);
void balsa_mime_widget_unlimit_focus(GtkEventControllerKey *key_controller,
                                     gpointer               user_data);


G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_CALLBACKS_H__ */
