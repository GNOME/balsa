/*
 * macosx-helpers.h
 *  
 * Helper functions for managing the IGE Mac Integration stuff
 *
 * Copyright (C) 2004 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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

#if HAVE_MACOSX_DESKTOP

#include <gtk/gtk.h>
#include <ige-mac-integration.h>
#include "macosx-helpers.h"


static gboolean update_osx_menubar(GtkWidget *widget, GdkEventFocus *event, GtkWindow *window);


void
libbalsa_macosx_menu(GtkWidget *window, GtkMenuShell *menubar)
{
    g_object_set_data(G_OBJECT(window), "osx-menubar", menubar);
    g_signal_connect(G_OBJECT(window), "focus-in-event",
		     G_CALLBACK(update_osx_menubar), window);
}


void
libbalsa_macosx_menu_for_parent(GtkWidget *window, GtkWindow *parent)
{
    if(parent)
	g_signal_connect(G_OBJECT(window), "focus-in-event",
			 G_CALLBACK(update_osx_menubar), parent);
    else
	g_message("called %s for widget %p with NULL parent", __func__, window);
}


/* window "focus-in-event" callback for a window
 * get the "osx-menubar" from the user data object, and set it as OS X main menu
 */
static gboolean
update_osx_menubar(GtkWidget *widget,  GdkEventFocus *event, GtkWindow *window)
{
    GtkMenuShell *menubar;
    
    g_return_val_if_fail(window != NULL, FALSE);
    menubar = GTK_MENU_SHELL(g_object_get_data(G_OBJECT(window), "osx-menubar"));
    g_return_val_if_fail(menubar != NULL, FALSE);
    ige_mac_menu_set_menu_bar(menubar);
    return FALSE;
}

#endif  /* HAVE_MACOSX_DESKTOP */
