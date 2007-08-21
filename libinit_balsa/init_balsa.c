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

#include "assistant_helper.h"
#include "balsa-initdruid.h"
#include "balsa-druid-page-welcome.h"

#include "i18n.h"	/* Must come after assistant_helper.h. */

static gboolean
dismiss_the_wizard(GtkWidget *wizard)
{
    gtk_widget_destroy(wizard);
    return FALSE;
}

void
balsa_init_begin(void)
{
    GtkWidget *assistant;

    assistant = gtk_assistant_new();
    gtk_window_set_title(GTK_WINDOW(assistant), _("Configure Balsa"));
    gtk_window_set_wmclass(GTK_WINDOW(assistant), "druid", "Balsa");
    gtk_widget_set_size_request(assistant, 780, 580);

    balsa_initdruid(GTK_ASSISTANT(assistant));
    gtk_widget_show_all(assistant);

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();

    /* we do not want to destroy wizard immediately to avoid confusing
       delay between the wizard that left and balsa that entered. */
    g_idle_add((GSourceFunc)dismiss_the_wizard, assistant);
}
