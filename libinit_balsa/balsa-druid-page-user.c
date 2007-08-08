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

#include "balsa-druid-page-user.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "i18n.h"
#include "imap-server.h"
#include "smtp-server.h"
#include "server.h"
#include "balsa-app.h"
#include "save-restore.h"

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
srv_changed_cb(GtkEditable *incoming_srv, gpointer data)
{
    BalsaDruidPageUser * user = (BalsaDruidPageUser*)data;
    const gchar *prev_name;

    prev_name = g_object_get_data(G_OBJECT(incoming_srv), "prev-name");
    if(!prev_name ||
       strcmp(prev_name, 
              gtk_entry_get_text(GTK_ENTRY(user->account_name))) == 0) {
        const gchar *aname = gtk_entry_get_text(GTK_ENTRY(incoming_srv));
        gtk_entry_set_text(GTK_ENTRY(user->account_name), aname);
        g_object_set_data_full(G_OBJECT(incoming_srv), "prev-name",
                               g_strdup(aname), g_free);
    }
}

static void
balsa_druid_page_user_init(BalsaDruidPageUser * user,
                           GnomeDruidPageStandard * page,
                           GnomeDruid * druid)
{
    static const char *header2 =
        N_("The following settings are also needed "
           "(and you can find them later, if need be, in the Email "
           "application in the 'Preferences' and 'Identities' "
           "commands on the 'Tools' menu)");
    static const char *header21 =
        N_(" Whoever provides your email account should be able "
           "to give you the following information (if you have "
           "a Network Administrator, they may already have set "
           "this up for you):");
    static const char* server_types[] = { "POP3", "IMAP", NULL };
    static const char* remember_passwd[] = {
        N_("Yes, remember it"), N_("No, type it in every time"), NULL };
    GtkTable *table;
    GtkLabel *label;
    gchar *preset;
    int row = 0;

    user->emaster.setbits = 0;
    user->emaster.numentries = 0;
    user->emaster.donemask = 0;
    user->ed0.master = &(user->emaster);
    user->ed1.master = &(user->emaster);
#if ENABLE_ESMTP
    user->ed2.master = &(user->emaster);
#endif
    user->ed3.master = &(user->emaster);
#if !defined(ENABLE_TOUCH_UI)
    user->ed4.master = &(user->emaster);
#endif
    label = GTK_LABEL(gtk_label_new(_(header2)));
    gtk_label_set_line_wrap(label, TRUE);
    gtk_box_pack_start_defaults(GTK_BOX(page->vbox), GTK_WIDGET(label));

    table = GTK_TABLE(gtk_table_new(10, 2, FALSE));

    label = GTK_LABEL(gtk_label_new(_(header21)));
    gtk_label_set_justify(label, GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(label, TRUE);
    gtk_table_attach(table, GTK_WIDGET(label), 0, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);

    /* 2.1 */
    balsa_init_add_table_entry(table, row++,
                               _("Name of mail server for incoming _mail:"),
                               "", /* no guessing here */
                               NULL, druid, &(user->incoming_srv));

    balsa_init_add_table_option(table, row++,
                                _("_Type of mail server:"),
                               server_types, druid, &(user->incoming_type));

    balsa_init_add_table_checkbox(table, row++,
                                  _("Connect using _SSL:"), FALSE,
                                  druid, &(user->using_ssl));

    balsa_init_add_table_entry(table, row++, _("Your email _login name:"),
                               g_get_user_name(),
                               NULL, druid, &(user->login));
    balsa_init_add_table_entry(table, row++, _("Your _password:"),
                               "",
                               NULL, druid, &(user->passwd));
    gtk_entry_set_visibility(GTK_ENTRY(user->passwd), FALSE);
    /* separator line here */

#if ENABLE_ESMTP
    preset = "localhost:25";
    balsa_init_add_table_entry(table, row++, _("_SMTP Server:"), preset,
                               &(user->ed2), druid, &(user->smtp));
#endif

    /* 2.1 */
    balsa_init_add_table_entry(table, row++, _("Your real _name:"),
                               g_get_real_name(),
                               &(user->ed0), druid, &(user->name));

    preset = libbalsa_guess_email_address();
    balsa_init_add_table_entry
        (table, row++, _("Your _Email Address, for this email account:"),
         preset, &(user->ed1), druid, &(user->email));
    g_free(preset);

    balsa_init_add_table_option(table, row++,
                                _("_Remember your password:"),
                               remember_passwd, druid,
                                &(user->remember_passwd));
    balsa_init_add_table_entry(table, row, _("_Refer to this account as:"),
                               "",
                               NULL, druid, &(user->account_name));
    gtk_table_set_row_spacing(table, row++, 10);
    g_signal_connect(user->incoming_srv, "changed",
                     (GCallback)srv_changed_cb, user);
#if !defined(ENABLE_TOUCH_UI)
    preset = g_strconcat(g_get_home_dir(), "/mail", NULL);
    balsa_init_add_table_entry(table, row++, _("_Local mail directory:"),
                               preset,
                               &(user->ed4), druid, &(user->localmaildir));
    g_free(preset);
#endif
    gtk_box_pack_start(GTK_BOX(page->vbox), GTK_WIDGET(table), TRUE, TRUE,
                       8);
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
    gtk_widget_grab_focus(user->incoming_srv);
}

static LibBalsaMailbox*
create_pop3_mbx(const gchar *name, const gchar* host, gboolean ssl, 
                const gchar *login, const gchar *passwd,
                gboolean remember)
{
    LibBalsaMailboxPop3 *pop = libbalsa_mailbox_pop3_new();
    LibBalsaMailbox *mbx   = LIBBALSA_MAILBOX(pop);
    LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(pop);

    libbalsa_server_set_username(server, login);
    libbalsa_server_set_password(server, passwd);
    libbalsa_server_set_host(server, host, ssl);
    server->tls_mode        = LIBBALSA_TLS_ENABLED;
    server->remember_passwd = remember;
    mbx->name               = g_strdup(name && *name ? name : host);
    pop->check              = TRUE;
    pop->disable_apop       = FALSE;
    pop->delete_from_server = TRUE;
    pop->filter             = FALSE;
    pop->filter_cmd         = g_strdup("procmail -f -");
    
    return mbx;
}

static void
create_imap_mbx(const gchar *name, const gchar* host, gboolean ssl,
                const gchar *login, const gchar *passwd,
                gboolean remember)
{
    BalsaMailboxNode *mbnode;
    LibBalsaServer *server =
        LIBBALSA_SERVER(libbalsa_imap_server_new(login, host));
    libbalsa_server_set_username(server, login);
    libbalsa_server_set_password(server, passwd);
    libbalsa_server_set_host(server, host, ssl);
    server->tls_mode        = LIBBALSA_TLS_ENABLED;
    server->remember_passwd = remember;
    mbnode = balsa_mailbox_node_new_imap_folder(server, NULL);
    mbnode->name = g_strdup(name && *name ? name : host);

    config_folder_add(mbnode, NULL);
    /* memory leak? */
    g_object_unref(mbnode);
}

static gboolean
balsa_druid_page_user_next(GnomeDruidPage * page, GnomeDruid * druid,
                           BalsaDruidPageUser * user)
{
    const gchar *host;
    gchar *uhoh;
    LibBalsaIdentity *ident;
#if ENABLE_ESMTP
    LibBalsaSmtpServer *smtp_server;
#endif /* ENABLE_ESMTP */
    
    /* incoming mail */
    host = gtk_entry_get_text(GTK_ENTRY(user->incoming_srv));
    if(host && *host) {
        LibBalsaMailbox *mbx = NULL;
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(user->account_name));
        const gchar *login = gtk_entry_get_text(GTK_ENTRY(user->login));
        const gchar *passwd = gtk_entry_get_text(GTK_ENTRY(user->passwd));
        gboolean ssl = 
            gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(user->using_ssl));
        gboolean remember = 
            balsa_option_get_active(user->remember_passwd) == 0;
        switch(balsa_option_get_active(user->incoming_type)) {
        case 0: /* POP */
            mbx = create_pop3_mbx(name, host, ssl, login, passwd, remember);
            if(mbx)
                config_mailbox_add(mbx, NULL);
            break;
        case 1: /* IMAP */
            create_imap_mbx(name, host, ssl, login, passwd, remember);
            break; 
        default: /* hm */;
        }
    }

    /* identity */
    if (balsa_app.identities == NULL) {
        ident = LIBBALSA_IDENTITY(libbalsa_identity_new());
        balsa_app.identities = g_list_append(NULL, ident);
    } else {
        ident = balsa_app.current_ident;
    }
    internet_address_set_name(ident->ia,
                              gtk_entry_get_text(GTK_ENTRY(user->name)));
    internet_address_set_addr(ident->ia,
                              gtk_entry_get_text(GTK_ENTRY(user->email)));

    /* outgoing mail */
#if ENABLE_ESMTP
    if (balsa_app.smtp_servers == NULL) {
	smtp_server = libbalsa_smtp_server_new();
        libbalsa_smtp_server_set_name(smtp_server,
                                      libbalsa_smtp_server_get_name(NULL));
	balsa_app.smtp_servers = g_slist_prepend(NULL, smtp_server);
    } else {
	smtp_server = balsa_app.smtp_servers->data;
    }
    libbalsa_server_set_host(LIBBALSA_SERVER(smtp_server),
                             gtk_entry_get_text(GTK_ENTRY(user->smtp)),
                             FALSE);
#endif

    g_free(balsa_app.local_mail_directory);
#if defined(ENABLE_TOUCH_UI)
    balsa_app.local_mail_directory = 
        g_strconcat(g_get_home_dir(), "/mail", NULL);
#else
    balsa_app.local_mail_directory =
        gtk_editable_get_chars(GTK_EDITABLE(user->localmaildir), 0, -1);
#endif /* ENABLE_TOUCH_UI */

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
