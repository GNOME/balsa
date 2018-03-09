/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others
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

#ifndef __BALSA_PRINT_OBJECT_IMAGE_H__
#define __BALSA_PRINT_OBJECT_IMAGE_H__

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_IMAGE	\
    (balsa_print_object_image_get_type())
#define BALSA_PRINT_OBJECT_IMAGE(obj)				\
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_IMAGE, BalsaPrintObjectImage)
#define BALSA_PRINT_OBJECT_IMAGE_CLASS(klass)			\
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_IMAGE, BalsaPrintObjectImageClass)
#define BALSA_IS_PRINT_OBJECT_IMAGE(obj)			\
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_IMAGE)


typedef struct _BalsaPrintObjectImageClass BalsaPrintObjectImageClass;
typedef struct _BalsaPrintObjectImage BalsaPrintObjectImage;


struct _BalsaPrintObjectImage {
    BalsaPrintObject parent;

    GdkPixbuf *pixbuf;
    gdouble c_img_offs;
    gdouble scale;
};


struct _BalsaPrintObjectImageClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_image_get_type(void);
GList *balsa_print_object_image(GList *list,
				GtkPrintContext *context,
				LibBalsaMessageBody *body,
				BalsaPrintSetup *psetup);


G_END_DECLS

#endif				/* __BALSA_PRINT_OBJECT_IMAGE_H__ */
