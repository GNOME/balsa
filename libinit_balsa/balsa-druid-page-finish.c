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

#include "balsa-druid-page-finish.h"

#include "save-restore.h"
#include "balsa-app.h"

/* here are local prototypes */
static void balsa_druid_page_finish_prepare(GnomeDruidPage * page,
                                            GnomeDruid * druid);
static void balsa_druid_page_finish_finish(GnomeDruidPage * page,
                                           GnomeDruid * druid);

#if BALSA_MAJOR < 2
void
balsa_druid_page_finish(GnomeDruid * druid, GdkImlibImage * default_logo)
#else
void
balsa_druid_page_finish(GnomeDruid * druid, GdkPixbuf * default_logo)
#endif                          /* BALSA_MAJOR < 2 */
{
    static const gchar bye[] =
        N_("You've successfully set up Balsa. Have fun!\n"
           "   -- The Balsa development team");
#if BALSA_MAJOR < 2
    GtkWidget *text;
    GnomeDruidPageStandard *page;

    page = GNOME_DRUID_PAGE_STANDARD(gnome_druid_page_standard_new());
    gnome_druid_page_standard_set_title(page, _("All Done!"));
    gnome_druid_page_standard_set_logo(page, default_logo);

    text = gtk_label_new(_(bye));
    gtk_label_set_justify(GTK_LABEL(text), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(page->vbox), GTK_WIDGET(text), TRUE, TRUE,
                       8);

    gtk_signal_connect(GTK_OBJECT(page), "prepare",
                       GTK_SIGNAL_FUNC(balsa_druid_page_finish_prepare),
                       NULL);
    gtk_signal_connect(GTK_OBJECT(page), "finish",
                       GTK_SIGNAL_FUNC(balsa_druid_page_finish_finish),
                       NULL);
#else
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
#endif                          /* BALSA_MAJOR < 2 */

    gnome_druid_append_page(druid, GNOME_DRUID_PAGE(page));
}

static void
balsa_druid_page_finish_prepare(GnomeDruidPage * page, GnomeDruid * druid)
{
#if BALSA_MAJOR < 2
    gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE);
#else
    gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE, FALSE);
#endif                          /* BALSA_MAJOR < 2 */
    gnome_druid_set_show_finish(druid, TRUE);
}

static void
balsa_druid_page_finish_finish(GnomeDruidPage * page, GnomeDruid * druid)
{
    gchar *address_book;

    address_book = gnome_util_home_file("GnomeCard.gcrd");
    if (g_file_test(address_book, G_FILE_TEST_EXISTS)) {
        LibBalsaAddressBook *ab =
            libbalsa_address_book_vcard_new(_("GnomeCard Address Book"),
                                            address_book);

        balsa_app.address_book_list =
            g_list_prepend(balsa_app.address_book_list, ab);
        balsa_app.default_address_book = ab;
    }
    g_free(address_book);

    config_save();
    gtk_main_quit();
}
