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

#include "balsa-druid-page-welcome.h"

static void balsa_druid_page_welcome_prepare(GnomeDruidPage * page,
                                             GnomeDruid * druid);

void
balsa_druid_page_welcome(GnomeDruid * druid, GdkPixbuf * default_logo)
{
    GnomeDruidPageEdge *page;
    static const gchar title[] = N_("Welcome to Balsa!");
    static const gchar text[] =
        N_
        ("You seem to be running Balsa for the first time. "
         "The following steps will set up Balsa by asking "
         "a few simple questions. Once you have completed "
         "these steps, you can always change them later in "
         "Balsa's preferences. If any files or directories "
         "need to be created, it will be done so automatically.\n"
         "  Please check the about box in Balsa's main window "
         "for more information about contacting the authors "
         "or reporting bugs.");

    page =
        GNOME_DRUID_PAGE_EDGE(gnome_druid_page_edge_new
                              (GNOME_EDGE_START));
    gnome_druid_page_edge_set_title(page, _(title));
    gnome_druid_page_edge_set_text(page, _(text));
    gnome_druid_page_edge_set_logo(page, default_logo);
    gnome_druid_page_edge_set_watermark(page,
                                        balsa_init_get_png
                                        ("balsa-watermark.png"));
    g_signal_connect(G_OBJECT(page), "prepare",
                     G_CALLBACK(balsa_druid_page_welcome_prepare),
                     NULL);
    gnome_druid_append_page(druid, GNOME_DRUID_PAGE(page));
    gnome_druid_set_page(druid, GNOME_DRUID_PAGE(page));
}

static void
balsa_druid_page_welcome_prepare(GnomeDruidPage * page, GnomeDruid * druid)
{
    /* FIXME: provide help */
    gnome_druid_set_buttons_sensitive(druid, FALSE, TRUE, TRUE, FALSE);
    gnome_druid_set_show_finish(druid, FALSE);
}
