/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __BALSA_MIME_WIDGET_IMAGE_H__
#define __BALSA_MIME_WIDGET_IMAGE_H__

#include <glib-object.h>
#include "balsa-message.h"
#include "balsa-mime-widget.h"

G_BEGIN_DECLS

/*
 * GObject class definitions
 */
#define BALSA_TYPE_MIME_WIDGET_IMAGE                                    \
    (balsa_mime_widget_image_get_type())

#define BALSA_MIME_WIDGET_IMAGE(obj)                                    \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                                  \
                                BALSA_TYPE_MIME_WIDGET_IMAGE,           \
                                BalsaMimeWidgetImage))

#define BALSA_IS_MIME_WIDGET_IMAGE(obj)                                 \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), BALSA_TYPE_MIME_WIDGET_IMAGE))

#define BALSA_MIME_WIDGET_IMAGE_CLASS(klass)                            \
    (G_TYPE_CHECK_CLASS_CAST((klass),                                   \
                             BALSA_TYPE_MIME_WIDGET_IMAGE,              \
                             BalsaMimeWidgetImageClass))

#define BALSA_IS_MIME_WIDGET_IMAGE_CLASS(klass)                         \
    (G_TYPE_CHECK_CLASS_TYPE((klass), BALSA_TYPE_MIME_WIDGET_IMAGE))

#define BALSA_MIME_WIDGET_IMAGE_GET_CLASS(obj)                          \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                                   \
                               BALSA_TYPE_MIME_WIDGET_IMAGE,            \
                               BalsaMimeWidgetImageClass))

GType balsa_mime_widget_image_get_type(void);

typedef struct _BalsaMimeWidgetImage        BalsaMimeWidgetImage;
typedef struct _BalsaMimeWidgetImageClass   BalsaMimeWidgetImageClass;
/*
 * End of GObject class definitions
 */

BalsaMimeWidget *balsa_mime_widget_new_image(BalsaMessage * bm,
					     LibBalsaMessageBody * mime_body,
					     const gchar * content_type, gpointer data);
void balsa_mime_widget_image_resize_all(GtkWidget * widget, gpointer user_data);


G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_IMAGE_H__ */
