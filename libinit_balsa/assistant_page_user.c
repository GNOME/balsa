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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
           "application in the “Preferences” and “Identities” "
           "menu items)");
#if 0
    static const char *header21 =
        N_(" Whoever provides your email account should be able "
           "to give you the following information (if you have "
           "a Network Administrator, they may already have set "
           "this up for you):");
#endif
    static const char* server_types[] = { "POP3", "IMAP", NULL };
    static const gchar *security_modes[] = {
    	N_("SSL"),
		N_("TLS required"),
		N_("TLS if possible (not recommended)"),
		N_("None (not recommended)"),
		NULL };
    static const char* remember_passwd[] = {
        N_("Yes, remember it"), N_("No, type it in every time"), NULL };
    GtkGrid *grid;
    GtkLabel *label;
    gchar *preset;
    int row = 0;

    user->emaster.setbits = 0;
    user->emaster.numentries = 0;
    user->emaster.donemask = 0;
    user->ed0.master = &(user->emaster);
    user->ed1.master = &(user->emaster);
    user->ed2.master = &(user->emaster);
    user->ed3.master = &(user->emaster);
    user->ed4.master = &(user->emaster);
    label = GTK_LABEL(gtk_label_new(_(header2)));
    gtk_label_set_line_wrap(label, TRUE);
    gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(label));

    grid = GTK_GRID(gtk_grid_new());
    gtk_grid_set_row_spacing(grid, 2);
    gtk_grid_set_column_spacing(grid, 5);

#if 0
    label = GTK_LABEL(gtk_label_new(_(header21)));
    gtk_label_set_justify(label, GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(label, TRUE);
    gtk_grid_attach(grid, GTK_WIDGET(label), 0, 2, 0, 1,
                     GTK_EXPAND | GTK_FILL, GTK_EXPAND | GTK_FILL, 8, 4);
#endif
    /* 2.1 */
    balsa_init_add_grid_entry(grid, row++,
                               _("Name of mail server for incoming _mail:"),
                               "", /* no guessing here */
                               NULL, druid, page, &(user->incoming_srv));

    balsa_init_add_grid_option(grid, row++,
                                _("_Type of mail server:"),
                               server_types, druid, &(user->incoming_type));

    balsa_init_add_grid_option(grid, row++,
    						   _("Connection _Security:"),
							   security_modes, druid, &(user->security));
    gtk_combo_box_set_active(GTK_COMBO_BOX(user->security), NET_CLIENT_CRYPT_STARTTLS - 1);

    balsa_init_add_grid_entry(grid, row++, _("Your email _login name:"),
                               g_get_user_name(),
                               NULL, druid, page, &(user->login));
    balsa_init_add_grid_entry(grid, row++, _("Your _password:"),
                               "",
                               NULL, druid, page, &(user->passwd));
    gtk_entry_set_visibility(GTK_ENTRY(user->passwd), FALSE);
    /* separator line here */

    preset = "localhost:25";
    balsa_init_add_grid_entry(grid, row++, _("_SMTP Server:"), preset,
                               &(user->ed2), druid, page, &(user->smtp));

    /* 2.1 */
    balsa_init_add_grid_entry(grid, row++, _("Your real _name:"),
                               g_get_real_name(),
                               &(user->ed0), druid, page, &(user->name));

    preset = libbalsa_guess_email_address();
    balsa_init_add_grid_entry
        (grid, row++, _("Your _email address for this email account:"),
         preset, &(user->ed1), druid, page, &(user->email));
    g_free(preset);

    balsa_init_add_grid_option(grid, row++,
                                _("_Remember your password:"),
                                remember_passwd, druid,
                                &(user->remember_passwd));

    preset = g_strconcat(g_get_home_dir(), "/mail", NULL);
    balsa_init_add_grid_entry(grid, row++, _("_Local mail directory:"),
                               preset,
                               &(user->ed4), druid, page,
                               &(user->localmaildir));
    g_free(preset);
    gtk_widget_set_margin_top(GTK_WIDGET(grid), 3);
    gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(grid));

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

    g_signal_connect(G_OBJECT(druid), "prepare",
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
                                    ENTRY_MASTER_DONE(user->emaster));

    gtk_widget_grab_focus(user->incoming_srv);
    user->need_set = TRUE;
}

static LibBalsaMailbox*
create_pop3_mbx(const gchar *name, const gchar* host, gint security,
                const gchar *login, const gchar *passwd,
                gboolean remember)
{
    LibBalsaMailboxPop3 *pop = libbalsa_mailbox_pop3_new();
    LibBalsaMailbox *mbx   = LIBBALSA_MAILBOX(pop);
    LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(pop);

    libbalsa_server_set_username(server, login);
    libbalsa_server_set_password(server, passwd);
    libbalsa_server_set_host(server, host, FALSE);
    server->security        = security;
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
create_imap_mbx(const gchar *name, const gchar* host, gint security,
                const gchar *login, const gchar *passwd,
                gboolean remember)
{
    BalsaMailboxNode *mbnode;
    LibBalsaServer *server =
        LIBBALSA_SERVER(libbalsa_imap_server_new(login, host));
    libbalsa_server_set_username(server, login);
    libbalsa_server_set_password(server, passwd);
    libbalsa_server_set_host(server, host, security == NET_CLIENT_CRYPT_ENCRYPTED);
    switch (security) {
    case NET_CLIENT_CRYPT_STARTTLS:
    	server->tls_mode   = LIBBALSA_TLS_REQUIRED;
    	break;
    case NET_CLIENT_CRYPT_STARTTLS_OPT:
    	server->tls_mode   = LIBBALSA_TLS_ENABLED;
    	break;
    default:
    	server->tls_mode   = LIBBALSA_TLS_DISABLED;
    }
    server->remember_passwd = remember;
    mbnode = balsa_mailbox_node_new_imap_folder(server, NULL);
    mbnode->name = g_strdup(name && *name ? name : host);

    config_folder_add(mbnode, NULL);
    /* memory leak? */
    g_object_unref(mbnode);
}

static void
balsa_druid_page_user_next(GtkAssistant * druid, GtkWidget * page,
                           BalsaDruidPageUser * user)
{
    const gchar *host, *mailbox;
    gchar *uhoh;
    LibBalsaIdentity *ident;
    InternetAddress *ia;
    LibBalsaSmtpServer *smtp_server;
    
#if 0
    printf("USER next ENTER %p %p\n", page, user->page);
    if(page != user->page)
        return;
#endif

    /* incoming mail */
    host = gtk_entry_get_text(GTK_ENTRY(user->incoming_srv));
    if(host && *host) {
        LibBalsaMailbox *mbx = NULL;
        const gchar *login = gtk_entry_get_text(GTK_ENTRY(user->login));
        const gchar *passwd = gtk_entry_get_text(GTK_ENTRY(user->passwd));
        gint security = balsa_option_get_active(user->security) + NET_CLIENT_CRYPT_ENCRYPTED;
        gboolean remember = 
            balsa_option_get_active(user->remember_passwd) == 0;
        switch(balsa_option_get_active(user->incoming_type)) {
        case 0: /* POP */
            mbx = create_pop3_mbx(host, host, security, login, passwd, remember);
            if(mbx)
                config_mailbox_add(mbx, NULL);
            break;
        case 1: /* IMAP */
            create_imap_mbx(host, host, security, login, passwd, remember);
            break; 
        default: /* hm */;
        }
    }

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
    g_object_unref(ia);

    /* outgoing mail */
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
        return; /* FIXME! Do not go to the next page! */
    }

    balsa_app.current_ident = ident;
}
