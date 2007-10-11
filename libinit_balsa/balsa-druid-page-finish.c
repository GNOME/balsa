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

#include "balsa-druid-page-directory.h"
#include "balsa-druid-page-finish.h"

#include <glib/gi18n.h>
#include "save-restore.h"
#include "balsa-app.h"

/* here are local prototypes */
static void balsa_druid_page_finish_prepare(GnomeDruidPage * page,
                                            GnomeDruid * druid);
static void balsa_druid_page_finish_finish(GnomeDruidPage * page,
                                           GnomeDruid * druid);

void
balsa_druid_page_finish(GnomeDruid * druid, GdkPixbuf * default_logo)
{
    static const gchar bye[] =
        N_("You've successfully set up Balsa. Have fun!\n"
           "   -- The Balsa development team");
    GnomeDruidPageEdge *page =
        GNOME_DRUID_PAGE_EDGE(gnome_druid_page_edge_new
                              (GNOME_EDGE_FINISH));

    gnome_druid_page_edge_set_title(page, _("All Done!"));
    gnome_druid_page_edge_set_logo(page, default_logo);
    gnome_druid_page_edge_set_text(page, _(bye));

    g_signal_connect(G_OBJECT(page), "prepare",
                     G_CALLBACK(balsa_druid_page_finish_prepare), NULL);
    g_signal_connect(G_OBJECT(page), "finish",
                     G_CALLBACK(balsa_druid_page_finish_finish), NULL);

    gnome_druid_append_page(druid, GNOME_DRUID_PAGE(page));
}

static void
balsa_druid_page_finish_prepare(GnomeDruidPage * page, GnomeDruid * druid)
{
    gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE, FALSE);
    gnome_druid_set_show_finish(druid, TRUE);
}

static void
balsa_druid_page_finish_finish(GnomeDruidPage * page, GnomeDruid * druid)
{
    gchar *address_book;
    LibBalsaAddressBook *ab = NULL;

#if defined(ENABLE_TOUCH_UI)
    balsa_druid_page_directory_later(GTK_WIDGET(druid));
#endif
    address_book = gnome_util_home_file("GnomeCard.gcrd");
    if (g_file_test(address_book, G_FILE_TEST_EXISTS))
        ab = libbalsa_address_book_vcard_new(_("GnomeCard Address Book"),
                                             address_book);
    g_free(address_book);
    if(!ab) {
        address_book = g_strconcat(g_get_home_dir(), 
                                   "/.addressbook.ldif", NULL);
        if (g_file_test(address_book, G_FILE_TEST_EXISTS))
            ab = libbalsa_address_book_ldif_new(_("Address Book"),
                                                address_book);
        g_free(address_book);
    }
    if(!ab) {
        /* This will be the default address book and its location */
        address_book = g_strconcat(g_get_home_dir(), 
                                   "/.balsa/addressbook.ldif", NULL);
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
