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
#include "assistant_page_user.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <glib/gi18n.h>
#include "imap-server.h"
#include "smtp-server.h"
#include "server.h"
#include "balsa-app.h"
#include "save-restore.h"

/* here are local prototypes */

static void balsa_druid_page_user_init(BalsaDruidPageUser * user,
                                       GtkWidget * page,
                                       GtkAssistant * druid);
static void balsa_druid_page_user_prepare(GtkAssistant * druid,
                                          GtkWidget * page,
                                          BalsaDruidPageUser * user);
static void balsa_druid_page_user_next(GtkAssistant * druid,
                                       GtkWidget * page,
                                       BalsaDruidPageUser * user);

static void
balsa_druid_page_user_init(BalsaDruidPageUser * user,
                           GtkWidget * page,
                           GtkAssistant * druid)
{
    static const char *header2 =
        N_("The following settings are also needed "
           "(and you can find them later, if need be, in the Email "
           "application in the “Edit \342\217\265 Preferences \342\217\265 Identities…” "
           "menu item)");
    GtkGrid *grid;
    GtkLabel *label;
    gchar *preset;
    int row = 0;

    user->econtroller.setbits = 0;
    user->econtroller.numentries = 0;
    user->econtroller.donemask = 0;
    user->ed0.controller = &(user->econtroller);
    user->ed1.controller = &(user->econtroller);
    user->ed2.controller = &(user->econtroller);
    label = GTK_LABEL(gtk_label_new(_(header2)));
    gtk_label_set_line_wrap(label, TRUE);
    gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(label), GTK_ALIGN_START);
    gtk_widget_set_margin_bottom(GTK_WIDGET(label), 12);
    gtk_container_add(GTK_CONTAINER(page), GTK_WIDGET(label));

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 2);
    gtk_grid_set_column_spacing(grid, 5);
    gtk_widget_set_valign(GTK_WIDGET(grid), GTK_ALIGN_START);

    /* 2.1 */
    balsa_init_add_grid_entry(grid, row++, _("Your real _name:"),
                               g_get_real_name(),
                               &(user->ed0), druid, page, &(user->name));

    balsa_init_add_grid_entry
        (grid, row++, _("Your _email address for this email account:"),
         "", &(user->ed1), druid, page, &(user->email));

    preset = g_build_filename(g_get_home_dir(), "mail", NULL);
    balsa_init_add_grid_entry(grid, row++, _("_Local mail directory:"),
                               preset,
                               &(user->ed2), druid, page,
                               &(user->localmaildir));
    g_free(preset);

    libbalsa_set_vmargins(GTK_WIDGET(grid), 3);
    gtk_container_add(GTK_CONTAINER(page), GTK_WIDGET(grid));

    user->need_set = FALSE;
}

void
balsa_druid_page_user(GtkAssistant * druid)
{
    BalsaDruidPageUser *user;

    user = g_new0(BalsaDruidPageUser, 1);
    user->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_assistant_append_page(druid, user->page);
    gtk_assistant_set_page_title(druid, user->page, _("User Settings"));
    balsa_druid_page_user_init(user, user->page, druid);

    g_signal_connect(druid, "prepare",
                     G_CALLBACK(balsa_druid_page_user_prepare),
                     user);
    g_object_weak_ref(G_OBJECT(druid), (GWeakNotify)g_free, user);
}

static void
balsa_druid_page_user_prepare(GtkAssistant * druid, GtkWidget * page,
                              BalsaDruidPageUser * user)
{
    if(page != user->page) {
        if(user->need_set) {
            balsa_druid_page_user_next(druid, page, user);
            user->need_set = FALSE;
        }
        return;
    }

    /* Don't let them continue unless all entries have something. */
    gtk_assistant_set_page_complete(druid, page,
                                    ENTRY_CONTROLLER_DONE(&user->econtroller));

    gtk_widget_grab_focus(user->email);
    user->need_set = TRUE;
}

static void
balsa_druid_page_user_next(GtkAssistant * druid, GtkWidget * page,
                           BalsaDruidPageUser * user)
{
    const gchar *mailbox;
    gchar *uhoh;
    LibBalsaIdentity *ident;
    InternetAddress *ia;
    

    /* identity */
    mailbox = gtk_entry_get_text(GTK_ENTRY(user->name));
    if (balsa_app.identities == NULL) {
	gchar *domain = strrchr(mailbox, '@');
        ident = LIBBALSA_IDENTITY(libbalsa_identity_new_with_name
				  (_("Default Identity")));
        balsa_app.identities = g_list_append(NULL, ident);
        balsa_app.current_ident = ident;
	if(domain)
	    libbalsa_identity_set_domain(ident, domain+1);
    } else {
        ident = balsa_app.current_ident;
    }
    
    ia = internet_address_mailbox_new (mailbox, gtk_entry_get_text(GTK_ENTRY(user->email)));
    libbalsa_identity_set_address (ident, ia);

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
        g_free(uhoh);
        /* Do not go to the next page!
         * Go back to the previous page, so that we reappear on this one */
        gtk_assistant_previous_page(druid);

        g_signal_connect(err, "response",
                         G_CALLBACK(gtk_widget_destroy), NULL);
        gtk_widget_show_all(err);

        return;
    }

    balsa_app.current_ident = ident;
}
