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

#include "balsa-druid-page-user.h"

#include <sys/types.h>
#include <sys/stat.h>

#include "balsa-app.h"

/* here are local prototypes */

static void balsa_druid_page_user_init(BalsaDruidPageUser * user,
                                       GnomeDruidPageStandard * page,
                                       GnomeDruid * druid);
static void balsa_druid_page_user_prepare(GnomeDruidPage * page,
                                          GnomeDruid * druid,
                                          BalsaDruidPageUser * user);
static gboolean balsa_druid_page_user_next(GnomeDruidPage * page,
                                           GnomeDruid * druid,
                                           BalsaDruidPageUser * user);

static void
balsa_druid_page_user_init(BalsaDruidPageUser * user,
                           GnomeDruidPageStandard * page,
                           GnomeDruid * druid)
{
    GtkTable *table;
    GtkLabel *label;
    gchar *preset;

    user->emaster.setbits = 0;
    user->emaster.numentries = 0;
    user->emaster.donemask = 0;
    user->ed0.master = &(user->emaster);
    user->ed1.master = &(user->emaster);
    user->ed2.master = &(user->emaster);
#if ENABLE_ESMTP
    user->ed3.master = &(user->emaster);

    table = GTK_TABLE(gtk_table_new(5, 2, FALSE));
#else
    table = GTK_TABLE(gtk_table_new(4, 2, FALSE));
#endif


    label =
        GTK_LABEL(gtk_label_new
                  (_("Please enter information about yourself.")));
    gtk_label_set_justify(label, GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(label, TRUE);
    gtk_table_attach(table, GTK_WIDGET(label), 0, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);

    preset = g_strdup(g_get_real_name());
    balsa_init_add_table_entry(table, 0, _("_Name:"), preset,
                               &(user->ed0), druid, &(user->name));
    g_free(preset);

    preset = libbalsa_guess_email_address();
    balsa_init_add_table_entry(table, 1, _("_Email Address:"), preset,
                               &(user->ed1), druid, &(user->email));
    g_free(preset);

    preset = g_strconcat(g_get_home_dir(), "/mail", NULL);
    balsa_init_add_table_entry(table, 2, _("_Local Mail Directory:"),
                               preset, &(user->ed2),
                               druid, &(user->localmaildir));
    g_free(preset);

#if ENABLE_ESMTP
    preset = "localhost:25";
    balsa_init_add_table_entry(table, 3, _("_SMTP Server:"), preset,
                               &(user->ed3), druid, &(user->smtp));
#endif

    gtk_box_pack_start(GTK_BOX(page->vbox), GTK_WIDGET(table), TRUE, TRUE,
                       8);

    return;
}

void
balsa_druid_page_user(GnomeDruid * druid, GdkPixbuf * default_logo)
{
    BalsaDruidPageUser *user;
    GnomeDruidPageStandard *page;

    user = g_new0(BalsaDruidPageUser, 1);
    page = GNOME_DRUID_PAGE_STANDARD(gnome_druid_page_standard_new());
    gnome_druid_page_standard_set_title(page, _("User Settings"));
    gnome_druid_page_standard_set_logo(page, default_logo);
    balsa_druid_page_user_init(user, page, druid);
    gnome_druid_append_page(druid, GNOME_DRUID_PAGE(page));
    g_signal_connect(G_OBJECT(page), "prepare",
                     G_CALLBACK(balsa_druid_page_user_prepare),
                     user);
    g_signal_connect(G_OBJECT(page), "next",
                     G_CALLBACK(balsa_druid_page_user_next), user);
}

static void
balsa_druid_page_user_prepare(GnomeDruidPage * page, GnomeDruid * druid,
                              BalsaDruidPageUser * user)
{
    /* Don't let them continue unless all entries have something. */

    if (ENTRY_MASTER_DONE(user->emaster)) {
        gnome_druid_set_buttons_sensitive(druid, TRUE, TRUE, TRUE, FALSE);
    } else {
        gnome_druid_set_buttons_sensitive(druid, TRUE, FALSE, TRUE, FALSE);
    }

    gnome_druid_set_show_finish(druid, FALSE);
}

static gboolean
balsa_druid_page_user_next(GnomeDruidPage * page, GnomeDruid * druid,
                           BalsaDruidPageUser * user)
{
    gchar *uhoh;
    LibBalsaIdentity *ident;

    if (balsa_app.identities == NULL) {
        ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
        balsa_app.identities = g_list_append(NULL, ident);
    } else {
        ident = balsa_app.current_ident;
    }
    g_free(ident->address->full_name);
    ident->address->full_name =
        gtk_editable_get_chars(GTK_EDITABLE(user->name), 0, -1);

    g_list_foreach(ident->address->address_list, (GFunc) g_free, NULL);
    g_list_free(ident->address->address_list);
    ident->address->address_list =
        g_list_prepend(NULL,
                       gtk_editable_get_chars(GTK_EDITABLE(user->email), 0,
                                              -1));

#if ENABLE_ESMTP
    g_free(balsa_app.smtp_server);
    balsa_app.smtp_server =
        gtk_editable_get_chars(GTK_EDITABLE(user->smtp), 0, -1);
#endif
    g_free(balsa_app.local_mail_directory);
    balsa_app.local_mail_directory =
        gtk_editable_get_chars(GTK_EDITABLE(user->localmaildir), 0, -1);

    if (balsa_init_create_to_directory
        (balsa_app.local_mail_directory, &uhoh)) {
        GtkWidget* err = 
            gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_ancestor
                                          (GTK_WIDGET(druid), 
                                           GTK_TYPE_WINDOW)),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_OK,
                                   _("Local Mail Problem\n%s"), uhoh);
        gtk_dialog_run(GTK_DIALOG(err));
        gtk_widget_destroy(err);
        g_free(uhoh);
        return TRUE;
    }

    balsa_app.current_ident = ident;
    return FALSE;
}
