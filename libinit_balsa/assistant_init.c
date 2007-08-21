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

#include "assistant_init.h"

#include "i18n.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include "save-restore.h"
#include "balsa-app.h"

#include "assistant_page_welcome.h"
#include "assistant_page_user.h"
#include "assistant_page_directory.h"
#include "assistant_page_defclient.h"
#include "assistant_page_finish.h"

static void
balsa_initdruid_cancel(GtkAssistant * druid)
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
        libbalsa_conf_drop_all();
        exit(0);
    }
}

static void
balsa_initdruid_apply(GtkAssistant * druid)
{
    gchar *address_book;
    LibBalsaAddressBook *ab = NULL;

#if defined(ENABLE_TOUCH_UI)
    balsa_druid_page_directory_later(GTK_WIDGET(druid));
#endif
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
        address_book = g_build_filename(g_get_home_dir(), 
                                        ".balsa", "addressbook.ldif", NULL);
        ab = libbalsa_address_book_ldif_new(_("Address Book"),
                                            address_book); 
        g_free(address_book);
        libbalsa_assure_balsa_dir();
   }

    balsa_app.address_book_list =
        g_list_prepend(balsa_app.address_book_list, ab);
    balsa_app.default_address_book = ab;

    g_signal_handlers_disconnect_by_func(G_OBJECT(druid),
                                         G_CALLBACK(exit), NULL);
    config_save();
    gtk_main_quit();
}

void
balsa_initdruid(GtkAssistant * assistant)
{
    GdkPixbuf *default_logo = balsa_init_get_png("balsa-logo.png");

    g_return_if_fail(assistant != NULL);
    g_return_if_fail(GTK_IS_ASSISTANT(assistant));

    g_signal_connect(G_OBJECT(assistant), "cancel",
                     G_CALLBACK(balsa_initdruid_cancel), NULL);
    g_signal_connect(G_OBJECT(assistant), "close",
                     G_CALLBACK(balsa_initdruid_apply), NULL);

    balsa_druid_page_welcome(assistant, default_logo);
    balsa_druid_page_user(assistant, default_logo);
#if !defined(ENABLE_TOUCH_UI)
    balsa_druid_page_directory(assistant, default_logo);
    balsa_druid_page_defclient(assistant, default_logo);
#endif
    balsa_druid_page_finish(assistant, default_logo);
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
