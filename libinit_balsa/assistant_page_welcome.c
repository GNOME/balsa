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

#include "assistant_page_welcome.h"
#include <glib/gi18n.h>

void
balsa_druid_page_welcome(GtkAssistant * druid)
{
    GtkWidget *page;
    static const gchar title[] = N_("Welcome to Balsa!");
    static const gchar text[] =
        N_
        ("Before you can send or receive email:\n\n"
         "• either you should already have Internet access and an "
         "email account, provided by an Internet Service Provider, "
         "and you should have made that Internet connection on your "
         "computer\n\n"
         "• or your network administrator at your place of "
         "work/study/similar may have set up your computer to "
         "connect to the network.");

    page = gtk_label_new(_(text));
    gtk_label_set_line_wrap(GTK_LABEL(page), TRUE);
    gtk_widget_set_valign(page, GTK_ALIGN_START);
    
    gtk_assistant_append_page(druid, page);
    gtk_assistant_set_page_title(druid, page, _(title));
    gtk_assistant_set_page_type(druid, page, GTK_ASSISTANT_PAGE_INTRO);
    gtk_assistant_set_page_complete(druid, page, TRUE);
}
