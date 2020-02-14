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

#ifndef __BALSA_MIME_WIDGET_MULTIPART_H__
#define __BALSA_MIME_WIDGET_MULTIPART_H__

#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-mime-widget.h"

G_BEGIN_DECLS


BalsaMimeWidget *balsa_mime_widget_new_multipart(BalsaMessage * bm,
						 LibBalsaMessageBody * mime_body,
						 const gchar * content_type, gpointer data);


G_END_DECLS

#endif				/* __BALSA_MIME_WIDGET_IMAGE_H__ */
