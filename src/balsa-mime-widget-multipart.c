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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-mime-widget-multipart.h"

#include "balsa-app.h"
#include <glib/gi18n.h>
#include "balsa-mime-widget.h"
#include "balsa-mime-widget-crypto.h"


BalsaMimeWidget *
balsa_mime_widget_new_multipart(BalsaMessage * bm,
				LibBalsaMessageBody * mime_body,
				const gchar * content_type, gpointer data)
{
    BalsaMimeWidget *mw;

    g_return_val_if_fail(mime_body != NULL, NULL);
    g_return_val_if_fail(content_type != NULL, NULL);

    mw = g_object_new(BALSA_TYPE_MIME_WIDGET, NULL);
    mw->widget = mw->container = gtk_box_new(GTK_ORIENTATION_VERTICAL, BMW_MESSAGE_PADDING);

#ifdef HAVE_GPGME
    if (!g_ascii_strcasecmp("multipart/signed", content_type) &&
	mime_body->parts && mime_body->parts->next &&
	mime_body->parts->next->sig_info)
	mw->widget = 
	    balsa_mime_widget_crypto_frame(mime_body->parts->next, mw->widget,
					   mime_body->was_encrypted, FALSE, NULL);
#endif

    return mw;
}
