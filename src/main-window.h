/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __main_window_h__
#define __main_window_h__

#include <gnome.h>

typedef struct _MainWindow MainWindow;
struct _MainWindow
  {
    GtkWidget *window;

    GtkWidget *menubar;
    GtkMenuFactory *factory;
    GtkMenuFactory *subfactories[1];

    GtkWidget *toolbar;
    GtkWidget *mailbox_option_menu;
    GtkWidget *mailbox_menu;
    GtkWidget *move_menu;

    GtkWidget *index;

    GtkWidget *message_area;
  };

MainWindow *create_main_window ();

#endif /* __main_window_h__ */
