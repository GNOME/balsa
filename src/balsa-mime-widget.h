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


#define BALSA_TYPE_MIME_WIDGET          (balsa_mime_widget_get_type ())
#define BALSA_MIME_WIDGET(obj)          G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_MIME_WIDGET, BalsaMimeWidget)
#define BALSA_MIME_WIDGET_CLASS(klass)  G_TYPE_CHECK_CLASS_CAST (klass, BALSA_TYPE_MIME_WIDGET, BalsaMimeWidgetClass)
#define BALSA_IS_MIME_WIDGET(obj)       G_TYPE_CHECK_INSTANCE_TYPE (obj, BALSA_TYPE_MIME_WIDGET)


typedef struct _BalsaMimeWidgetClass BalsaMimeWidgetClass;


struct _BalsaMimeWidget {
    GObject parent;

    /* display widget */
    GtkWidget *widget;

    /* container widget if more sub-parts can be added */
    GtkWidget *container;

    /* headers */
    GtkWidget *header_widget;
};


struct _BalsaMimeWidgetClass {
    GObjectClass parent;
};


GType balsa_mime_widget_get_type (void);
BalsaMimeWidget *balsa_mime_widget_new(BalsaMessage * bm,
				       LibBalsaMessageBody * mime_body,
				       gpointer data);
void balsa_mime_widget_schedule_resize(GtkWidget * widget);


G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_H__ */
