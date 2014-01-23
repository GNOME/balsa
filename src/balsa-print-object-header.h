/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others
 * Written by (C) Albrecht Dreﬂ <albrecht.dress@arcor.de> 2007
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

#ifndef __BALSA_PRINT_OBJECT_HEADER_H__
#define __BALSA_PRINT_OBJECT_HEADER_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_HEADER	\
    (balsa_print_object_header_get_type())
#define BALSA_PRINT_OBJECT_HEADER(obj)				\
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_HEADER, BalsaPrintObjectHeader)
#define BALSA_PRINT_OBJECT_HEADER_CLASS(klass)			\
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_HEADER, BalsaPrintObjectHeaderClass)
#define BALSA_IS_PRINT_OBJECT_HEADER(obj)			\
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_HEADER)


typedef struct _BalsaPrintObjectHeaderClass BalsaPrintObjectHeaderClass;
typedef struct _BalsaPrintObjectHeader BalsaPrintObjectHeader;


struct _BalsaPrintObjectHeader {
    BalsaPrintObject parent;

    gint p_label_width;
    gint p_layout_width;
    gchar *headers;
    GdkPixbuf *face;
};


struct _BalsaPrintObjectHeaderClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_header_get_type(void);
GList *balsa_print_object_header_from_message(GList *list,
					      GtkPrintContext * context,
					      LibBalsaMessage * message,
					      const gchar * subject,
					       BalsaPrintSetup * psetup);
GList *balsa_print_object_header_from_body(GList *list,
					   GtkPrintContext * context,
					   LibBalsaMessageBody * body,
					   BalsaPrintSetup * psetup);
#ifdef HAVE_GPGME
GList *balsa_print_object_header_crypto(GList *list,
					GtkPrintContext * context,
					LibBalsaMessageBody * body,
					const gchar * label,
					BalsaPrintSetup * psetup);
#endif


G_END_DECLS

#endif				/* __BALSA_PRINT_OBJECT_HEADER_H__ */
