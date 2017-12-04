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

#ifndef __BALSA_MIME_WIDGET_TEXT_H__
#define __BALSA_MIME_WIDGET_TEXT_H__

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"

G_BEGIN_DECLS

/*
 * Type declaration.
 */
GType balsa_mime_widget_text_get_type(void);
#define BALSA_TYPE_MIME_WIDGET_TEXT balsa_mime_widget_text_get_type()
#define BALSA_MIME_WIDGET_TEXT(obj) \
    G_TYPE_CHECK_INSTANCE_CAST (obj, BALSA_TYPE_MIME_WIDGET_TEXT, BalsaMimeWidgetText)
typedef struct _BalsaMimeWidgetText BalsaMimeWidgetText;
typedef struct _BalsaMimeWidgetTextClass BalsaMimeWidgetTextClass;

/*
 * Method definitions.
 */

BalsaMimeWidget *balsa_mime_widget_new_text(BalsaMessage * bm,
					    LibBalsaMessageBody * mime_body,
					    const gchar * content_type, gpointer data);


G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_TEXT_H__ */
