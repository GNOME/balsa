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

#ifndef __BALSA_PRINT_OBJECT_DECOR_H__
#define __BALSA_PRINT_OBJECT_DECOR_H__

#include "balsa-print-object.h"

G_BEGIN_DECLS

#define BALSA_TYPE_PRINT_OBJECT_DECOR   \
    (balsa_print_object_decor_get_type())
#define BALSA_PRINT_OBJECT_DECOR(obj)                           \
    G_TYPE_CHECK_INSTANCE_CAST(obj, BALSA_TYPE_PRINT_OBJECT_DECOR, BalsaPrintObjectDecor)
#define BALSA_PRINT_OBJECT_DECOR_CLASS(klass)                   \
    G_TYPE_CHECK_CLASS_CAST(klass, BALSA_TYPE_PRINT_OBJECT_DECOR, BalsaPrintObjectDecorClass)
#define BALSA_IS_PRINT_OBJECT_DECOR(obj)                        \
    G_TYPE_CHECK_INSTANCE_TYPE(obj, BALSA_TYPE_PRINT_OBJECT_DECOR)

typedef struct _BalsaPrintObjectDecorClass BalsaPrintObjectDecorClass;
typedef struct _BalsaPrintObjectDecor BalsaPrintObjectDecor;

typedef enum {
    BALSA_PRINT_DECOR_FRAME_BEGIN,
    BALSA_PRINT_DECOR_FRAME_END,
    BALSA_PRINT_DECOR_SEPARATOR
} BalsaPrintDecorType;

struct _BalsaPrintObjectDecor {
    BalsaPrintObject parent;

    BalsaPrintDecorType mode;
    gchar *label;
};


struct _BalsaPrintObjectDecorClass {
    BalsaPrintObjectClass parent;
};


GType balsa_print_object_decor_get_type(void);
GList *balsa_print_object_separator(GList           *list,
                                    BalsaPrintSetup *psetup);
GList *balsa_print_object_frame_begin(GList           *list,
                                      const gchar     *label,
                                      BalsaPrintSetup *psetup);
GList *balsa_print_object_frame_end(GList           *list,
                                    BalsaPrintSetup *psetup);


G_END_DECLS

#endif                          /* __BALSA_PRINT_OBJECT_SEPARATOR_H__ */
