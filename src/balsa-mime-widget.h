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

#ifndef __BALSA_MIME_WIDGET_H__
#define __BALSA_MIME_WIDGET_H__

#include "libbalsa.h"
#include "balsa-message.h"

G_BEGIN_DECLS


/* define some constants to simplify the layout */
#define BMW_CONTAINER_BORDER        10
#define BMW_VBOX_SPACE               2
#define BMW_HBOX_SPACE               6
#define BMW_MESSAGE_PADDING          6
#define BMW_BUTTON_PACK_SPACE        5
#define BMW_HEADER_MARGIN_LEFT       2
#define BMW_HEADER_MARGIN_RIGHT     15

/*
 * GObject class definitions
 */

struct _BalsaMimeWidgetClass {
    GtkBoxClass parent_class;
};

#define BALSA_TYPE_MIME_WIDGET balsa_mime_widget_get_type()

G_DECLARE_DERIVABLE_TYPE(BalsaMimeWidget, balsa_mime_widget, BALSA, MIME_WIDGET, GtkBox)

/*
 * Method definitions.
 */

BalsaMimeWidget *balsa_mime_widget_new(BalsaMessage * bm,
				       LibBalsaMessageBody * mime_body,
				       gpointer data);
void balsa_mime_widget_schedule_resize(GtkWidget * widget);

/*
 * Getters
 */

GtkWidget *balsa_mime_widget_get_container    (BalsaMimeWidget * mw);
GtkWidget *balsa_mime_widget_get_header_widget(BalsaMimeWidget * mw);

/*
 * Setters
 */

void balsa_mime_widget_set_container    (BalsaMimeWidget * mw, GtkWidget * widget);
void balsa_mime_widget_set_header_widget(BalsaMimeWidget * mw, GtkWidget * widget);

G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_H__ */
