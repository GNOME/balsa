/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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

/*
 * LibBalsaSmtpServer is a subclass of LibBalsaServer.
 */
#include "server.h"

#include <string.h>

#include "libbalsa.h"
#include "smtp-server.h"
#include "libbalsa-conf.h"
#include "misc.h"
#include <glib/gi18n.h>
#include "net-client.h"

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

static LibBalsaServerClass *parent_class = NULL;

struct _LibBalsaSmtpServer {
    LibBalsaServer server;

    gchar *name;
    guint big_message; /* size of partial messages; in kB; 0 disables splitting */
    gint lock_state;	/* 0 means unlocked; access via atomic operations */
};

typedef struct _LibBalsaSmtpServerClass {
    LibBalsaServerClass parent_class;
} LibBalsaSmtpServerClass;

/* Server class methods */

/* Object class method */

static void
libbalsa_smtp_server_finalize(GObject * object)
{
    LibBalsaSmtpServer *smtp_server;

    g_return_if_fail(LIBBALSA_IS_SMTP_SERVER(object));

    smtp_server = LIBBALSA_SMTP_SERVER(object);

    g_free(smtp_server->name);

    G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void
libbalsa_smtp_server_class_init(LibBalsaSmtpServerClass * klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_smtp_server_finalize;
}

static void
libbalsa_smtp_server_init(LibBalsaSmtpServer * smtp_server)
{
    libbalsa_server_set_protocol(LIBBALSA_SERVER(smtp_server), "smtp");
}

/* Class boilerplate */

GType
libbalsa_smtp_server_get_type(void)
{
    static GType server_type = 0;

    if (!server_type) {
        static const GTypeInfo server_info = {
            sizeof(LibBalsaSmtpServerClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) libbalsa_smtp_server_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(LibBalsaSmtpServer),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) libbalsa_smtp_server_init
        };

        server_type =
            g_type_register_static(LIBBALSA_TYPE_SERVER,
                                   "LibBalsaSmtpServer", &server_info, 0);
    }

    return server_type;
}

/* Public methods */

/**
 * libbalsa_smtp_server_new:
 * @username: username to use to login
 * @host: hostname of server
 *
 * Creates or recycles a #LibBalsaSmtpServer matching the host+username pair.
 *
 * Return value: A #LibBalsaSmtpServer
 */
LibBalsaSmtpServer *
libbalsa_smtp_server_new(void)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = g_object_new(LIBBALSA_TYPE_SMTP_SERVER, NULL);

    /* Change the default. */
    libbalsa_server_set_remember_passwd(LIBBALSA_SERVER(smtp_server), TRUE);

    return smtp_server;
}

LibBalsaSmtpServer *
libbalsa_smtp_server_new_from_config(const gchar * name)
{
    LibBalsaSmtpServer *smtp_server;

    smtp_server = libbalsa_smtp_server_new();
    smtp_server->name = g_strdup(name);

    libbalsa_server_load_config(LIBBALSA_SERVER(smtp_server));

    smtp_server->big_message = libbalsa_conf_get_int("BigMessage=0");

    return smtp_server;
}

void
libbalsa_smtp_server_save_config(LibBalsaSmtpServer * smtp_server)
{
    libbalsa_server_save_config(LIBBALSA_SERVER(smtp_server));

    libbalsa_conf_set_int("BigMessage", smtp_server->big_message);
}

void
libbalsa_smtp_server_set_name(LibBalsaSmtpServer * smtp_server,
                              const gchar * name)
{
    g_free(smtp_server->name);
    smtp_server->name = g_strdup(name);
}

const gchar *
libbalsa_smtp_server_get_name(LibBalsaSmtpServer * smtp_server)
{
    return smtp_server ? smtp_server->name : _("Default");
}

guint
libbalsa_smtp_server_get_big_message(LibBalsaSmtpServer * smtp_server)
{
    /* big_message is stored in kB, but we want the value in bytes. */
    return smtp_server->big_message * 1024;
}

static gint
smtp_server_compare(gconstpointer a, gconstpointer b)
{
    const LibBalsaSmtpServer *smtp_server_a = a;
    const LibBalsaSmtpServer *smtp_server_b = b;

    return g_strcmp0(smtp_server_a->name, smtp_server_b->name);
}

void
libbalsa_smtp_server_add_to_list(LibBalsaSmtpServer * smtp_server,
                                 GSList ** server_list)
{
    GSList *list;

    if ((list =
         g_slist_find_custom(*server_list, smtp_server,
                             smtp_server_compare)) != NULL) {
        g_object_unref(list->data);
        *server_list = g_slist_delete_link(*server_list, list);
    }

    *server_list = g_slist_prepend(*server_list, smtp_server);
}

gboolean
libbalsa_smtp_server_trylock(LibBalsaSmtpServer *smtp_server)
{
	gint prev_state;
	gboolean result;

	prev_state = g_atomic_int_add(&smtp_server->lock_state, 1);
	if (prev_state == 0) {
		result = TRUE;
	} else {
		result = FALSE;
		(void) g_atomic_int_dec_and_test(&smtp_server->lock_state);
	}
	g_debug("%s: lock %s: %d", __func__, libbalsa_smtp_server_get_name(smtp_server), result);
	return result;
}

void
libbalsa_smtp_server_unlock(LibBalsaSmtpServer *smtp_server)
{
	(void) g_atomic_int_dec_and_test(&smtp_server->lock_state);
	g_debug("%s: unlock %s", __func__, libbalsa_smtp_server_get_name(smtp_server));
}

/* SMTP server dialog */

#define LIBBALSA_SMTP_SERVER_DIALOG_KEY "libbalsa-smtp-server-dialog"

struct smtp_server_dialog_info {
    LibBalsaSmtpServer *smtp_server;
    gchar *old_name;
    LibBalsaSmtpServerUpdate update;
    GtkWidget *dialog;
    GtkWidget *name;
    GtkWidget *host;
    GtkWidget *user;
    GtkWidget *pass;
    GtkWidget *tlsm;
    GtkWidget *auth_button;
    GtkWidget *cert_button;
    GtkWidget *cert_file;
    GtkWidget *cert_pass;
    GtkWidget *split_button;
    GtkWidget *big_message;
};

/* GDestroyNotify for smtp_server_dialog_info. */
static void
smtp_server_destroy_notify(struct smtp_server_dialog_info *sdi)
{
    g_free(sdi->old_name);
    if (sdi->dialog)
        gtk_widget_destroy(sdi->dialog);
    g_free(sdi);
}

/* GWeakNotify for dialog. */
static void 
smtp_server_weak_notify(struct smtp_server_dialog_info *sdi, GObject *dialog)
{
    sdi->dialog = NULL;
    g_object_set_data(G_OBJECT(sdi->smtp_server),
                      LIBBALSA_SMTP_SERVER_DIALOG_KEY, NULL);
}

static void
smtp_server_add_widget(GtkWidget * grid, gint row, const gchar * text,
                       GtkWidget * widget)
{
    GtkWidget *label = libbalsa_create_grid_label(text, grid, row);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
}

static
GtkWidget *
smtp_server_tls_widget(LibBalsaSmtpServer * smtp_server)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    GtkWidget *combo_box = gtk_combo_box_text_new();

    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("SMTP over SSL (SMTPS)"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS required"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS if possible (not recommended)"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("None (not recommended)"));

    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), (gint) libbalsa_server_get_security(server) - 1);

    return combo_box;
}

static void
smtp_server_response(GtkDialog * dialog, gint response,
                     struct smtp_server_dialog_info *sdi)
{
    LibBalsaServer *server = LIBBALSA_SERVER(sdi->smtp_server);
    GError *error = NULL;

    switch (response) {
    case GTK_RESPONSE_HELP:
        gtk_show_uri_on_window(GTK_WINDOW(dialog),
                               "help:balsa/preferences-mail-options#smtp-server-config",
                               gtk_get_current_event_time(), &error);
        if (error) {
            libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                                 _("Error displaying server help: %s\n"),
                                 error->message);
            g_error_free(error);
        }
        return;
    case GTK_RESPONSE_OK:
        libbalsa_smtp_server_set_name(sdi->smtp_server,
                                      gtk_entry_get_text(GTK_ENTRY
                                                         (sdi->name)));
        libbalsa_server_set_host(server,
                                 gtk_entry_get_text(GTK_ENTRY(sdi->host)),
                                 FALSE);
        libbalsa_server_set_try_anonymous
            (server,
             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->auth_button)) ? 0U : 1U);
        libbalsa_server_set_username(server,
                                     gtk_entry_get_text(GTK_ENTRY
                                                        (sdi->user)));
        libbalsa_server_set_password(server,
                                     gtk_entry_get_text(GTK_ENTRY
                                                        (sdi->pass)));
        libbalsa_server_set_security
            (server,
             (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(sdi->tlsm)) + 1));
        libbalsa_server_set_client_cert
            (server,
             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->cert_button)));
        libbalsa_server_set_cert_file
            (server,
             gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(sdi->cert_file)));
        libbalsa_server_set_cert_passphrase
            (server,
             gtk_editable_get_chars(GTK_EDITABLE(sdi->cert_pass), 0, -1));
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->split_button))) {
            /* big_message is stored in kB, but the widget is in MB. */
        	sdi->smtp_server->big_message =
                gtk_spin_button_get_value(GTK_SPIN_BUTTON(sdi->big_message)) * 1024.0;
        } else {
        	sdi->smtp_server->big_message = 0U;
        }
        break;
    default:
        break;
    }

    /* The update may unref the server, so we temporarily ref it;
     * we use server instead of sdi->smtp_server, as sdi is deallocated
     * when the object data is cleared. */
    g_object_ref(server);
    sdi->update(sdi->smtp_server, response, sdi->old_name);
    g_object_unref(server);

    gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
smtp_server_changed(GtkWidget G_GNUC_UNUSED *widget,
                    struct smtp_server_dialog_info *sdi)
{
	gboolean sensitive;
	gboolean enable_ok = FALSE;

	/* enable ok button only if a name and a host have been given */
    if ((sdi->name != NULL) && (sdi->host != NULL)) {
    	enable_ok = (*gtk_entry_get_text(GTK_ENTRY(sdi->name)) != '\0')
        	&& (*gtk_entry_get_text(GTK_ENTRY(sdi->host)) != '\0');
    }

	/* user name/password only if authentication is required */
	if ((sdi->auth_button != NULL) && (sdi->user != NULL) && (sdi->pass != NULL)) {
		sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->auth_button));
		gtk_widget_set_sensitive(sdi->user, sensitive);
		gtk_widget_set_sensitive(sdi->pass, sensitive);

		/* disable ok if authentication is required, but no user name given */
		if (sensitive && (*gtk_entry_get_text(GTK_ENTRY(sdi->user)) == '\0')) {
			enable_ok = FALSE;
		}
	}

	/* client certificate and passphrase stuff only if TLS/SSL is enabled */
	if ((sdi->tlsm != NULL) && (sdi->cert_button != NULL) && (sdi->cert_file != NULL) && (sdi->cert_pass != NULL)) {
		sensitive = (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(sdi->tlsm)) + 1) != NET_CLIENT_CRYPT_NONE;
		gtk_widget_set_sensitive(sdi->cert_button, sensitive);
		if (sensitive) {
			sensitive = sensitive && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->cert_button));
		}

		gtk_widget_set_sensitive(sdi->cert_file, sensitive);
		gtk_widget_set_sensitive(sdi->cert_pass, sensitive);

		/* disable ok if a certificate is required, but no file name given */
		if (sensitive) {
			gchar *cert_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(sdi->cert_file));

			if ((cert_file == NULL) || (cert_file[0] == '\0')) {
				enable_ok = FALSE;
			}
			g_free(cert_file);
		}
	}

	/* split big messages */
	if ((sdi->big_message != NULL) && (sdi->split_button != NULL)) {
		sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sdi->split_button));
	    gtk_widget_set_sensitive(sdi->big_message, sensitive);
	}

    gtk_dialog_set_response_sensitive(GTK_DIALOG(sdi->dialog), GTK_RESPONSE_OK, enable_ok);
    gtk_dialog_set_default_response(GTK_DIALOG(sdi->dialog),
    	enable_ok ? GTK_RESPONSE_OK : GTK_RESPONSE_CANCEL);
}

void
libbalsa_smtp_server_dialog(LibBalsaSmtpServer * smtp_server,
                            GtkWindow * parent,
                            LibBalsaSmtpServerUpdate update)
{
    LibBalsaServer *server = LIBBALSA_SERVER(smtp_server);
    struct smtp_server_dialog_info *sdi;
    GtkWidget *dialog;
    GtkWidget *notebook;
    GtkWidget *grid;
    gint row;
    GtkWidget *label, *hbox;

    /* Show only one dialog at a time. */
    sdi = g_object_get_data(G_OBJECT(smtp_server),
                            LIBBALSA_SMTP_SERVER_DIALOG_KEY);
    if (sdi != NULL) {
        gtk_window_present(GTK_WINDOW(sdi->dialog));
        return;
    }

    sdi = g_new0(struct smtp_server_dialog_info, 1U);
    g_object_set_data_full(G_OBJECT(smtp_server),
                           LIBBALSA_SMTP_SERVER_DIALOG_KEY, sdi,
                           (GDestroyNotify) smtp_server_destroy_notify);

    sdi->smtp_server = smtp_server;
    sdi->old_name = g_strdup(libbalsa_smtp_server_get_name(smtp_server));
    sdi->update = update;
    sdi->dialog = dialog =
        gtk_dialog_new_with_buttons(_("SMTP Server"),
                                    parent,
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_OK"),     GTK_RESPONSE_OK,
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_Help"),   GTK_RESPONSE_HELP,
                                    NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    g_object_weak_ref(G_OBJECT(dialog),
		    (GWeakNotify) smtp_server_weak_notify, sdi);
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK(smtp_server_response), sdi);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                    GTK_RESPONSE_CANCEL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK,
                                      FALSE);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
                       notebook);

#define HIG_PADDING 12

    /* notebook page with basic options */
    grid = libbalsa_create_grid();
    row = 0;
    g_object_set(G_OBJECT(grid), "margin", HIG_PADDING, NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid,
                             gtk_label_new_with_mnemonic(_("_Basic")));

    /* server descriptive name */
    sdi->name = gtk_entry_new();
    gtk_widget_set_hexpand(sdi->name, TRUE);
    smtp_server_add_widget(grid, row, _("_Descriptive Name:"), sdi->name);
    if (smtp_server->name != NULL) {
        gtk_entry_set_text(GTK_ENTRY(sdi->name), smtp_server->name);
    }
    g_signal_connect(sdi->name, "changed", G_CALLBACK(smtp_server_changed), sdi);

    /* host and port */
    sdi->host = gtk_entry_new();
    smtp_server_add_widget(grid, ++row, _("_Server:"), sdi->host);
    if (libbalsa_server_get_host(server) != NULL) {
        gtk_entry_set_text(GTK_ENTRY(sdi->host), libbalsa_server_get_host(server));
    }
    g_signal_connect(sdi->host, "changed", G_CALLBACK(smtp_server_changed), sdi);

    /* security settings */
    sdi->tlsm = smtp_server_tls_widget(smtp_server);
    smtp_server_add_widget(grid, ++row, _("Se_curity:"), sdi->tlsm);
    g_signal_connect(sdi->tlsm, "changed", G_CALLBACK(smtp_server_changed), sdi);

    /* authentication or anonymous access */
    sdi->auth_button = gtk_check_button_new_with_mnemonic(_("Server requires authentication"));
    smtp_server_add_widget(grid, ++row, _("_Authentication:"), sdi->auth_button);
    g_signal_connect(sdi->auth_button, "toggled", G_CALLBACK(smtp_server_changed), sdi);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->auth_button),
                                 libbalsa_server_get_try_anonymous(server) == 0U);

    /* user name and password */
    sdi->user = gtk_entry_new();
    smtp_server_add_widget(grid, ++row, _("_User Name:"), sdi->user);
    if (libbalsa_server_get_user(server) != NULL) {
        gtk_entry_set_text(GTK_ENTRY(sdi->user), libbalsa_server_get_user(server));
    }
    g_signal_connect(sdi->user, "changed", G_CALLBACK(smtp_server_changed), sdi);

    sdi->pass = gtk_entry_new();
    smtp_server_add_widget(grid, ++row, _("_Pass Phrase:"), sdi->pass);
    g_object_set(G_OBJECT(sdi->pass), "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(sdi->pass), FALSE);
    if (libbalsa_server_get_passwd(server) != NULL) {
        gtk_entry_set_text(GTK_ENTRY(sdi->pass), libbalsa_server_get_passwd(server));
    }
    g_signal_connect(sdi->pass, "changed", G_CALLBACK(smtp_server_changed), sdi);

    /* notebook page with advanced options */
    grid = libbalsa_create_grid();
    row = 0;
    g_object_set(G_OBJECT(grid), "margin", HIG_PADDING, NULL);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), grid,
                             gtk_label_new_with_mnemonic(_("_Advanced")));

    /* client certificate and passphrase */
    sdi->cert_button = gtk_check_button_new_with_mnemonic(_("Server requires client certificate"));
    smtp_server_add_widget(grid, row, _("_Client Certificate:"), sdi->cert_button);
    g_signal_connect(sdi->cert_button, "toggled", G_CALLBACK(smtp_server_changed), sdi);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->cert_button), libbalsa_server_get_client_cert(server));

    sdi->cert_file = gtk_file_chooser_button_new(_("Choose Client Certificate"), GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_widget_set_hexpand(sdi->cert_file, TRUE);
    smtp_server_add_widget(grid, ++row, _("Certificate _File:"), sdi->cert_file);
    if (libbalsa_server_get_cert_file(server) != NULL) {
    	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(sdi->cert_file), libbalsa_server_get_cert_file(server));
    }
    g_signal_connect(sdi->cert_file, "file-set", G_CALLBACK(smtp_server_changed), sdi);

	sdi->cert_pass = gtk_entry_new();
    smtp_server_add_widget(grid, ++row, _("Certificate _Pass Phrase:"), sdi->cert_pass);
    g_object_set(G_OBJECT(sdi->cert_pass), "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(sdi->cert_pass), FALSE);
    if (libbalsa_server_get_cert_passphrase(server) != NULL) {
        gtk_entry_set_text(GTK_ENTRY(sdi->cert_pass), libbalsa_server_get_cert_passphrase(server));
    }
    g_signal_connect(sdi->cert_pass, "changed", G_CALLBACK(smtp_server_changed), sdi);

    /* split large messages */
    sdi->split_button = gtk_check_button_new_with_mnemonic(_("Sp_lit message larger than"));
    gtk_grid_attach(GTK_GRID(grid), sdi->split_button, 0, ++row, 1, 1);
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    sdi->big_message = gtk_spin_button_new_with_range(0.1, 100, 0.1);
    gtk_widget_set_hexpand(sdi->big_message, TRUE);
    gtk_box_pack_start(GTK_BOX(hbox), sdi->big_message);
    label = gtk_label_new(_("MB"));
    gtk_box_pack_start(GTK_BOX(hbox), label);
    if (smtp_server->big_message > 0) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button), TRUE);
        /* The widget is in MB, but big_message is stored in kB. */
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message),
                                  ((gdouble) smtp_server->big_message) / 1024.0);
    } else {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(sdi->split_button), FALSE);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(sdi->big_message), 1);
    }
    g_signal_connect(sdi->split_button, "toggled", G_CALLBACK(smtp_server_changed), sdi);
    g_signal_connect(sdi->big_message, "changed", G_CALLBACK(smtp_server_changed), sdi);
    gtk_grid_attach(GTK_GRID(grid), hbox, 1, row, 1, 1);

    smtp_server_changed(NULL, sdi);

    gtk_widget_show(dialog);
}
