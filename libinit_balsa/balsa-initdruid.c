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

#include "balsa-initdruid.h"

#include "save-restore.h"

#include "balsa-druid-page-welcome.h"
#include "balsa-druid-page-user.h"
#include "balsa-druid-page-directory.h"
#include "balsa-druid-page-defclient.h"
#include "balsa-druid-page-finish.h"

/* here are local prototypes */
static void balsa_initdruid_init(GnomeDruid * druid);
static void balsa_initdruid_cancel(GnomeDruid * druid);

static void
balsa_initdruid_init(GnomeDruid * druid)
{
    GdkPixbuf *default_logo = balsa_init_get_png("balsa-logo.png");

    balsa_druid_page_welcome(druid, default_logo);
    balsa_druid_page_user(druid, default_logo);
    balsa_druid_page_directory(druid, default_logo);
    balsa_druid_page_defclient(druid, default_logo);
    balsa_druid_page_finish(druid, default_logo);
}

void
balsa_initdruid(GtkWindow * window)
{
    GnomeDruid *druid;

    g_return_if_fail(window != NULL);
    g_return_if_fail(GTK_IS_WINDOW(window));

    druid = GNOME_DRUID(gnome_druid_new());
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(druid));
    g_signal_connect(G_OBJECT(druid), "cancel",
                     G_CALLBACK(balsa_initdruid_cancel), NULL);
    g_object_ref(G_OBJECT(window));

    balsa_initdruid_init(druid);
}

static void
balsa_initdruid_cancel(GnomeDruid * druid)
{
    GtkWidget *dialog =
        gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_ancestor
                                          (GTK_WIDGET(druid), 
                                           GTK_TYPE_WINDOW)),
                               GTK_DIALOG_MODAL,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("This will exit Balsa.\n"
                                 "Do you really want to do this?"));
    GtkResponseType reply = 
        gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (reply == GTK_RESPONSE_YES) {
        gnome_config_drop_all();
        exit(0);
    }
}
