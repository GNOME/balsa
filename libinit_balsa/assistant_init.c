/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "assistant_init.h"

#include <stdlib.h>

#include <glib/gi18n.h>
#include "libbalsa-conf.h"
#include "misc.h"
#include "save-restore.h"
#include "balsa-app.h"

#include "assistant_page_welcome.h"
#include "assistant_page_user.h"
#include "assistant_page_server.h"
#include "assistant_page_directory.h"
#include "assistant_page_defclient.h"
#include "assistant_page_finish.h"

static void balsa_initdruid_cancel_response(GtkDialog *self,
                                            gint       response_id,
                                            gpointer   user_data);

static void
balsa_initdruid_cancel(GtkAssistant * druid,
                       gpointer       user_data)
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
    g_signal_connect(dialog, "response",
                     G_CALLBACK(balsa_initdruid_cancel_response), NULL);
    gtk_widget_show_all(dialog);
}

static void
balsa_initdruid_cancel_response(GtkDialog *self,
                                gint       response_id,
                                gpointer   user_data)
{
    GtkResponseType reply = response_id;

    gtk_widget_destroy(GTK_WIDGET(self));

    if (reply == GTK_RESPONSE_YES) {
        libbalsa_conf_drop_all();
        exit(0);
    }
}

static void
balsa_initdruid_apply(GtkAssistant * druid,
                      gpointer       user_data)
{
    gchar *address_book;
    LibBalsaAddressBook *ab = NULL;

    address_book = g_build_filename(g_get_home_dir(), "GnomeCard.gcrd", NULL);
    if (g_file_test(address_book, G_FILE_TEST_EXISTS))
        ab = libbalsa_address_book_vcard_new(_("GnomeCard Address Book"),
                                             address_book);
    g_free(address_book);
    if(!ab) {
        address_book = g_build_filename(g_get_home_dir(), 
                                   ".addressbook.ldif", NULL);
        if (g_file_test(address_book, G_FILE_TEST_EXISTS))
            ab = libbalsa_address_book_ldif_new(_("Address Book"),
                                                address_book);
        g_free(address_book);
    }
    if(!ab) {
        /* This will be the default address book and its location */
        address_book = g_build_filename(g_get_user_config_dir(),
                                        "balsa", "addressbook.ldif", NULL);
        ab = libbalsa_address_book_ldif_new(_("Address Book"),
                                            address_book); 
        g_free(address_book);
   }

    balsa_app.address_book_list =
        g_list_prepend(balsa_app.address_book_list, ab);
    balsa_app.default_address_book = ab;

    g_signal_handlers_disconnect_by_func(druid,
                                         G_CALLBACK(exit), NULL);
    libbalsa_conf_push_group("Notifications");
    libbalsa_conf_set_bool("GtkUIManager", TRUE);
    libbalsa_conf_set_bool("LibBalsaAddressView", TRUE);
    libbalsa_conf_pop_group();
    config_save();
    gtk_main_quit();
}

static void
balsa_initdruid(GtkAssistant * assistant)
{
    g_signal_connect(assistant, "cancel",
                     G_CALLBACK(balsa_initdruid_cancel), NULL);
    g_signal_connect(assistant, "close",
                     G_CALLBACK(balsa_initdruid_apply), NULL);

    balsa_druid_page_welcome(assistant);
    balsa_druid_page_user(assistant);
    balsa_druid_page_server(assistant);
    balsa_druid_page_directory(assistant);
    balsa_druid_page_defclient(assistant);
    balsa_druid_page_finish(assistant);
}


/* The external interface code */
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
    gtk_window_set_default_size(GTK_WINDOW(assistant), 1, 1);

    balsa_initdruid(GTK_ASSISTANT(assistant));
    gtk_widget_show_all(assistant);

    gtk_main();

    /* we do not want to destroy wizard immediately to avoid confusing
       delay between the wizard that left and balsa that entered. */
    g_idle_add((GSourceFunc)dismiss_the_wizard, assistant);
}
