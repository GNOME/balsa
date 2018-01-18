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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "cell-renderer-button.h"

#include <gtk/gtk.h>

enum {
    ACTIVATED,
    LAST_SIGNAL
};

static guint cell_button_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(LibBalsaCellRendererButton, libbalsa_cell_renderer_button,
              GTK_TYPE_CELL_RENDERER_PIXBUF)

static gboolean
libbalsa_cell_renderer_button_activate(GtkCellRenderer    * cell,
                                       GdkEvent           * event,
                                       GtkWidget          * widget,
                                       const gchar        * path,
                                       const GdkRectangle * background_area,
                                       const GdkRectangle * cell_area,
                                       GtkCellRendererState flags)
{
    g_signal_emit(cell, cell_button_signals[ACTIVATED], 0, path);
    return TRUE;
}

static void
libbalsa_cell_renderer_button_init(LibBalsaCellRendererButton * button)
{
}

static void
libbalsa_cell_renderer_button_class_init(LibBalsaCellRendererButtonClass *
                                         klass)
{
    GtkCellRendererClass *cell_class   = GTK_CELL_RENDERER_CLASS(klass);

    cell_class->activate = libbalsa_cell_renderer_button_activate;

  /**
   * LibBalsaCellRendererButton::activated:
   * @cell_renderer: the object which received the signal
   * @path:          string representation of #GtkTreePath describing the
   *                 event location
   *
   * The ::activated signal is emitted when the cell is activated.
   **/
    cell_button_signals[ACTIVATED] =
        g_signal_new("activated",
                     G_OBJECT_CLASS_TYPE(klass),
                     0, 0, NULL, NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE,
                     1, G_TYPE_STRING);
}

/**
 * libbalsa_cell_renderer_button_new:
 * 
 * Creates a new #LibBalsaCellRendererButton.
 * 
 * Return value: the new cell renderer
 **/
GtkCellRenderer *
libbalsa_cell_renderer_button_new(void)
{
    return g_object_new(LIBBALSA_TYPE_CELL_RENDERER_BUTTON,
                        "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
                        NULL);
}
