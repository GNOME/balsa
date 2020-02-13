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
#include "assistant_page_finish.h"

#include "assistant_page_directory.h"

#include <glib/gi18n.h>
#include "save-restore.h"

#if 0
/* here are local prototypes */
static void balsa_druid_page_finish_prepare(GtkWidget * page,
                                            GtkAssistant * druid);
static void balsa_druid_page_finish_finish(GtkWidget * page,
                                           GtkAssistant * druid);
#endif

void
balsa_druid_page_finish(GtkAssistant * druid)
{
    static const gchar bye[] =
        N_("You’ve successfully set up Balsa. Have fun!\n"
           "   -- The Balsa development team");
    GtkWidget *page = gtk_label_new(_(bye));

    gtk_assistant_append_page(druid, page);
    gtk_assistant_set_page_title(druid, page, _("All Done!"));
    gtk_assistant_set_page_type(druid, page, GTK_ASSISTANT_PAGE_SUMMARY);
}

#if 0
static void
balsa_druid_page_finish_prepare(GnomeDruidPage * page, GnomeDruid * druid)
{
    gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE, FALSE);
    gnome_druid_set_show_finish(druid, TRUE);
}

#endif
