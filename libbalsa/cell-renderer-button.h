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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */


#ifndef __LIBBALSA_CELL_RENDERER_BUTTON_H__
#define __LIBBALSA_CELL_RENDERER_BUTTON_H__

#include <gtk/gtk.h>


G_BEGIN_DECLS

#define LIBBALSA_TYPE_CELL_RENDERER_BUTTON (libbalsa_cell_renderer_button_get_type())

G_DECLARE_FINAL_TYPE(LibBalsaCellRendererButton,
                     libbalsa_cell_renderer_button,
                     LIBBALSA,
                     CELL_RENDERER_BUTTON,
                     GtkCellRendererPixbuf)

GtkCellRenderer *libbalsa_cell_renderer_button_new(void);

G_END_DECLS
#endif                          /* __LIBBALSA_CELL_RENDERER_BUTTON_H__ */
