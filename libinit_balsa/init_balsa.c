/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#include "config.h"

#include <gtk/gtk.h>

#include "init_balsa.h"
#ifdef HAVE_GNOME
#include "helper.h"
#include "balsa-initdruid.h"
#include "balsa-druid-page-welcome.h"
#endif /* HAVE_GNOME */

#include <glib/gi18n.h>	/* Must come after helper.h. */

static gboolean
dismiss_the_wizard(GtkWidget *wizard)
{
    gtk_widget_destroy(wizard);
    return FALSE;
}

void
balsa_init_begin(void)
{
    GtkWidget *window;

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), _("Configure Balsa"));
    gtk_window_set_wmclass(GTK_WINDOW(window), "druid", "Balsa");

#ifdef HAVE_GNOME
    balsa_initdruid(GTK_WINDOW(window));
    gtk_widget_show_all(window);

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
#endif

    /* we do not want to destroy wizard immediately to avoid confusing
       delay between the wizard that left and balsa that entered. */
    g_idle_add((GSourceFunc)dismiss_the_wizard, window);
}
