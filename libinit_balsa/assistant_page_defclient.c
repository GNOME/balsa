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
#include "assistant_page_defclient.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <gio/gio.h>

#include <glib/gi18n.h>
#include "balsa-app.h"

/* here are local prototypes */

static void balsa_druid_page_defclient_init(BalsaDruidPageDefclient *defclient,
                                            GtkWidget *page,
                                            GtkAssistant *druid);
static void balsa_druid_page_defclient_toggle(GtkWidget * page,
                                          BalsaDruidPageDefclient * defclient);

static void
balsa_druid_page_defclient_init(BalsaDruidPageDefclient * defclient,
                                GtkWidget * page,
                                GtkAssistant * druid)
{
    GtkLabel *label;
    GtkWidget *yes, *no;
    GtkWidget *widget;

    defclient->default_client = 1;
    balsa_app.default_client = defclient->default_client;

    label =
        GTK_LABEL(gtk_label_new
                  (_("Use Balsa as default email client?")));
    gtk_label_set_justify(label, GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(label, TRUE);

    yes = gtk_radio_button_new_with_mnemonic(NULL, _("_Yes"));
    no = gtk_radio_button_new_with_mnemonic_from_widget(GTK_RADIO_BUTTON(yes),
                                                         _("_No"));

    g_signal_connect(yes, "toggled",
                       G_CALLBACK(balsa_druid_page_defclient_toggle),
                       defclient);

    widget = GTK_WIDGET(label);
    gtk_widget_set_vexpand(widget, FALSE);
    gtk_widget_set_valign(widget, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(widget, HIG_PADDING + 2);
    gtk_container_add(GTK_CONTAINER(page), widget);

    widget = GTK_WIDGET(yes);
    gtk_widget_set_vexpand(widget, FALSE);
    gtk_widget_set_valign(widget, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(widget, 2);
    gtk_container_add(GTK_CONTAINER(page), widget);

    widget = GTK_WIDGET(no);
    gtk_widget_set_vexpand(widget, FALSE);
    gtk_widget_set_valign(widget, GTK_ALIGN_FILL);
    libbalsa_set_vmargins(widget, 2);
    gtk_container_add(GTK_CONTAINER(page), widget);

    return;
}

void
balsa_druid_page_defclient(GtkAssistant *druid)
{
    GAppInfo *info;
    BalsaDruidPageDefclient *defclient;
    GtkWidget *page;

    info = g_app_info_get_default_for_uri_scheme("mailto");
    if (info) {
        gboolean set_to_balsa_already;

        set_to_balsa_already = !strcmp(g_app_info_get_name(info), "Balsa");
        g_object_unref(info);

        if(set_to_balsa_already)
            return;
    }

    defclient = g_new0(BalsaDruidPageDefclient, 1);
    page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_assistant_append_page(druid, page);
    gtk_assistant_set_page_title(druid, page, _("Default Client"));
    balsa_druid_page_defclient_init(defclient, page, druid);
    /* This one is ready to pass through. */
    gtk_assistant_set_page_complete(druid, page, TRUE);
    g_object_weak_ref(G_OBJECT(druid), (GWeakNotify)g_free, defclient);
}

static void
balsa_druid_page_defclient_toggle(GtkWidget * page, 
                                  BalsaDruidPageDefclient * defclient)
{
    defclient->default_client = ! (defclient->default_client);
    balsa_app.default_client = defclient->default_client;
}
