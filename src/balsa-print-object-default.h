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

#ifndef __BALSA_PRINT_OBJECT_DEFAULT_H__
#define __BALSA_PRINT_OBJECT_DEFAULT_H__

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_DEFAULT	\
    (balsa_print_object_default_get_type())
#define BALSA_PRINT_OBJECT_DEFAULT(obj)				\
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_DEFAULT, BalsaPrintObjectDefault)
#define BALSA_PRINT_OBJECT_DEFAULT_CLASS(klass)			\
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_DEFAULT, BalsaPrintObjectDefaultClass)
#define BALSA_IS_PRINT_OBJECT_DEFAULT(obj)			\
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_DEFAULT)


typedef struct _BalsaPrintObjectDefaultClass BalsaPrintObjectDefaultClass;
typedef struct _BalsaPrintObjectDefault BalsaPrintObjectDefault;


struct _BalsaPrintObjectDefault {
    BalsaPrintObject parent;

    gint p_label_width;
    gdouble c_image_width;
    gdouble c_image_height;
    gdouble c_text_height;
    gchar *description;
    GdkPixbuf *pixbuf;
};


struct _BalsaPrintObjectDefaultClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_default_get_type(void);
GList *balsa_print_object_default(GList * list,
				  GtkPrintContext *context,
				  LibBalsaMessageBody *body,
				  BalsaPrintSetup *psetup);


G_END_DECLS

#endif				/* __BALSA_PRINT_OBJECT_DEFAULT_H__ */
