/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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


#ifndef __LIBBALSA_CELL_RENDERER_BUTTON_H__
#define __LIBBALSA_CELL_RENDERER_BUTTON_H__

#include <gtk/gtk.h>


G_BEGIN_DECLS
#define LIBBALSA_TYPE_CELL_RENDERER_BUTTON                              \
    (libbalsa_cell_renderer_button_get_type())
#define LIBBALSA_CELL_RENDERER_BUTTON(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                                  \
                                LIBBALSA_TYPE_CELL_RENDERER_BUTTON,     \
                                LibBalsaCellRendererButton))
#define LIBBALSA_CELL_RENDERER_BUTTON_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                                   \
                             LIBBALSA_TYPE_CELL_RENDERER_BUTTON,        \
                             LibBalsaCellRendererButtonClass))
#define LIBBALSA_IS_CELL_RENDERER_BUTTON(obj)                           \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj),                                  \
                                LIBBALSA_TYPE_CELL_RENDERER_BUTTON))
#define LIBBALSA_IS_CELL_RENDERER_BUTTON_CLASS(klass)                   \
    (G_TYPE_CHECK_CLASS_TYPE((klass),                                   \
                             LIBBALSA_TYPE_CELL_RENDERER_BUTTON))
#define LIBBALSA_CELL_RENDERER_BUTTON_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                                   \
                               LIBBALSA_TYPE_CELL_RENDERER_BUTTON,      \
                               LibBalsaCellRendererButtonClass))

typedef struct _LibBalsaCellRendererButton LibBalsaCellRendererButton;
typedef struct _LibBalsaCellRendererButtonClass
    LibBalsaCellRendererButtonClass;

struct _LibBalsaCellRendererButton {
    GtkCellRendererPixbuf parent;
};

struct _LibBalsaCellRendererButtonClass {
    GtkCellRendererPixbufClass parent_class;
};

GType            libbalsa_cell_renderer_button_get_type(void) G_GNUC_CONST;
GtkCellRenderer *libbalsa_cell_renderer_button_new(void);


G_END_DECLS
#endif                          /* __LIBBALSA_CELL_RENDERER_BUTTON_H__ */
