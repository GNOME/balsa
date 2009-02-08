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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __BALSA_PRINT_OBJECT_TEXT_H__
#define __BALSA_PRINT_OBJECT_TEXT_H__

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_TEXT	\
    (balsa_print_object_text_get_type())
#define BALSA_PRINT_OBJECT_TEXT(obj)				\
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_TEXT, BalsaPrintObjectText)
#define BALSA_PRINT_OBJECT_TEXT_CLASS(klass)			\
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_TEXT, BalsaPrintObjectTextClass)
#define BALSA_IS_PRINT_OBJECT_TEXT(obj)			\
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_TEXT)


typedef struct _BalsaPrintObjectTextClass BalsaPrintObjectTextClass;
typedef struct _BalsaPrintObjectText BalsaPrintObjectText;


struct _BalsaPrintObjectText {
    BalsaPrintObject parent;

    gint p_label_width;
    gchar *text;
    guint cite_level;
    GList *attributes;
};


struct _BalsaPrintObjectTextClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_text_get_type(void);
GList *balsa_print_object_text_plain(GList *list,
				     GtkPrintContext * context,
				     LibBalsaMessageBody * body,
				     BalsaPrintSetup * psetup);
GList *balsa_print_object_text(GList *list,
			       GtkPrintContext * context,
			       LibBalsaMessageBody * body,
			       BalsaPrintSetup * psetup);
GList *balsa_print_object_text_vcard(GList *list,
				     GtkPrintContext * context,
				     LibBalsaMessageBody * body,
				     BalsaPrintSetup * psetup);
GList *balsa_print_object_text_calendar(GList *list,
                                        GtkPrintContext * context,
                                        LibBalsaMessageBody * body,
                                        BalsaPrintSetup * psetup);


G_END_DECLS

#endif				/* __BALSA_PRINT_OBJECT_TEXT_H__ */
