/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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

#include <glib/gi18n.h>
#include "misc.h"
#include "server-config.h"


struct _LibBalsaServerCfg {
        GObject parent;

        GtkWidget *notebook;

	/* "Basic" notebook page */
	GtkWidget *basic_grid;			/* grid */
	guint basic_rows;				/* count of rows */
	GtkWidget *name;				/* descriptive name */
	GtkWidget *host_port;			/* host and optionally port */
	GtkWidget *security;			/* security (SSL/TLS/...) */
	GtkWidget *require_auth;		/* require authentication */
	GtkWidget *username;			/* user name for authentication */
	GtkWidget *password;			/* password for authentication */
	GtkWidget *remember_pass;		/* remember password */

	/* "Advanced" notebook page */
	GtkWidget *advanced_grid;		/* grid */
	guint advanced_rows;			/* count of rows */
	GtkWidget *require_cert;		/* require a client certificate */
	GtkWidget *cert_file;			/* client certificate file name */
	GtkWidget *cert_pass;			/* client certificate pass phrase */
	GtkWidget *remember_cert_pass;	/* remember certificate pass phrase */

	gboolean cfg_valid;				/* whether the config options are valid (parent may enable OK) */
};


G_DEFINE_TYPE(LibBalsaServerCfg, libbalsa_server_cfg, G_TYPE_OBJECT);


static GtkWidget *server_cfg_add_entry(GtkWidget *grid, guint row, const gchar *label, const gchar *value, GCallback callback,
								gpointer cb_data)
	G_GNUC_WARN_UNUSED_RESULT;
static GtkWidget *server_cfg_add_check(GtkWidget *grid, guint row, const gchar *label, gboolean value, GCallback callback,
									   gpointer cb_data)
	G_GNUC_WARN_UNUSED_RESULT;
static void server_cfg_add_widget(GtkWidget *grid, guint row, const gchar *text, GtkWidget *widget);
static GtkWidget *server_cfg_security_widget(LibBalsaServer *server);
static void on_server_cfg_changed(GtkWidget *widget, LibBalsaServerCfg *server_cfg);


static guint changed_sig;


#if defined(HAVE_LIBSECRET)
static const gchar *remember_password_message[2] = {
	N_("_Remember user password in Secret Service"),
	N_("_Remember certificate pass phrase in Secret Service")
};
#else
static const gchar *remember_password_message[2] = {
	N_("_Remember user password"),
	N_("_Remember certificate pass phrase")
};
#endif                          /* defined(HAVE_LIBSECRET) */


LibBalsaServerCfg *
libbalsa_server_cfg_new(LibBalsaServer *server, const gchar *name)
{
	LibBalsaServerCfg *server_cfg;
        const gchar *protocol;
        const gchar *cert_file;

	g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    server_cfg = LIBBALSA_SERVER_CFG(g_object_new(libbalsa_server_cfg_get_type(), NULL));

    /* notebook page with basic options */
#define HIG_PADDING 12
    server_cfg->basic_grid = libbalsa_create_grid();
    server_cfg->basic_rows = 0U;

    gtk_widget_set_margin_top(server_cfg->basic_grid, HIG_PADDING);
    gtk_widget_set_margin_bottom(server_cfg->basic_grid, HIG_PADDING);
    gtk_widget_set_margin_start(server_cfg->basic_grid, HIG_PADDING);
    gtk_widget_set_margin_end(server_cfg->basic_grid, HIG_PADDING);

    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg->notebook), server_cfg->basic_grid, gtk_label_new_with_mnemonic(_("_Basic")));

    /* server descriptive name */
    server_cfg->name = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Descriptive Name:"), name,
        G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* host and port */
    server_cfg->host_port = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Server:"),
                                           libbalsa_server_get_host(server),
                                           G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* security settings */
    server_cfg->security = server_cfg_security_widget(server);
    server_cfg_add_widget(server_cfg->basic_grid, server_cfg->basic_rows++, _("Se_curity:"), server_cfg->security);
    g_signal_connect(server_cfg->security, "changed", G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* check box for authentication or anonymous access - smtp and imap only */
    protocol = libbalsa_server_get_protocol(server);
    if ((strcmp(protocol, "smtp") == 0) || (strcmp(protocol, "imap") == 0)) {
        server_cfg->require_auth = server_cfg_add_check(server_cfg->basic_grid, server_cfg->basic_rows++,
                                                  _("Server requires _authentication"),
                                                  !libbalsa_server_get_try_anonymous(server),
                                                  G_CALLBACK(on_server_cfg_changed), server_cfg);
    }

    /* user name and password */
    server_cfg->username = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_User Name:"),
                                          libbalsa_server_get_user(server),
                                          G_CALLBACK(on_server_cfg_changed), server_cfg);

    server_cfg->password = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Pass Phrase:"),
                                          libbalsa_server_get_password(server),
        G_CALLBACK(on_server_cfg_changed), server_cfg);
    g_object_set(server_cfg->password, "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(server_cfg->password), FALSE);

    server_cfg->remember_pass = server_cfg_add_check(server_cfg->basic_grid, server_cfg->basic_rows++, remember_password_message[0],
        libbalsa_server_get_remember_password(server), G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* notebook page with advanced options */
    server_cfg->advanced_grid = libbalsa_create_grid();
    server_cfg->advanced_rows = 0U;

    gtk_widget_set_margin_top(server_cfg->advanced_grid, HIG_PADDING);
    gtk_widget_set_margin_bottom(server_cfg->advanced_grid, HIG_PADDING);
    gtk_widget_set_margin_start(server_cfg->advanced_grid, HIG_PADDING);
    gtk_widget_set_margin_end(server_cfg->advanced_grid, HIG_PADDING);

    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg->notebook), server_cfg->advanced_grid, gtk_label_new_with_mnemonic(_("_Advanced")));

    /* client certificate and passphrase */
    server_cfg->require_cert = server_cfg_add_check(server_cfg->advanced_grid, server_cfg->advanced_rows++, _("Server _requires client certificate"),
        libbalsa_server_get_client_cert(server), G_CALLBACK(on_server_cfg_changed), server_cfg);

    server_cfg->cert_file = gtk_file_chooser_button_new(_("Choose Client Certificate"), GTK_FILE_CHOOSER_ACTION_OPEN);
    server_cfg_add_widget(server_cfg->advanced_grid, server_cfg->advanced_rows++, _("Certificate _File:"), server_cfg->cert_file);

    cert_file = libbalsa_server_get_cert_file(server);
    if (cert_file != NULL) {
        GFile *file = g_file_new_for_path(cert_file);
        gtk_file_chooser_set_file(GTK_FILE_CHOOSER(server_cfg->cert_file), file, NULL);
        g_object_unref(file);
    }
    g_signal_connect(server_cfg->cert_file, "file-set", G_CALLBACK(on_server_cfg_changed), server_cfg);

	server_cfg->cert_pass = server_cfg_add_entry(server_cfg->advanced_grid, server_cfg->advanced_rows++, _("Certificate _Pass Phrase:"),
		libbalsa_server_get_cert_passphrase(server), G_CALLBACK(on_server_cfg_changed), server_cfg);
    g_object_set(server_cfg->cert_pass, "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(server_cfg->cert_pass), FALSE);

    server_cfg->remember_cert_pass = server_cfg_add_check(server_cfg->advanced_grid, server_cfg->advanced_rows++, remember_password_message[1],
        libbalsa_server_get_remember_cert_passphrase(server), G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* initially run the validity check */
    on_server_cfg_changed(NULL, server_cfg);

    return server_cfg;
}


gboolean
libbalsa_server_cfg_valid(LibBalsaServerCfg *server_cfg)
{
	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg), FALSE);
	return server_cfg->cfg_valid;
}


GtkWidget *
libbalsa_server_cfg_add_check(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, gboolean initval,
	GCallback callback, gpointer cb_data)
{
	GtkWidget *new_check;

	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL), NULL);

	if (basic) {
		new_check = server_cfg_add_check(server_cfg->basic_grid, server_cfg->basic_rows++, label, initval, callback, cb_data);
	} else {
		new_check = server_cfg_add_check(server_cfg->advanced_grid, server_cfg->advanced_rows++, label, initval, callback, cb_data);
	}
	return new_check;
}


GtkWidget *
libbalsa_server_cfg_add_entry(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, const gchar *initval,
	GCallback callback, gpointer cb_data)
{
	GtkWidget *new_entry;

	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL), NULL);

	if (basic) {
		new_entry = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, label, initval, callback, cb_data);
	} else {
		new_entry = server_cfg_add_entry(server_cfg->advanced_grid, server_cfg->advanced_rows++, label, initval, callback, cb_data);
	}
	return new_entry;
}


void
libbalsa_server_cfg_add_item(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, GtkWidget *widget)
{

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL) && (widget != NULL));

	if (basic) {
		server_cfg_add_widget(server_cfg->basic_grid, server_cfg->basic_rows++, label, widget);
	} else {
		server_cfg_add_widget(server_cfg->advanced_grid, server_cfg->advanced_rows++, label, widget);
	}
}


void
libbalsa_server_cfg_add_row(LibBalsaServerCfg *server_cfg, gboolean basic, GtkWidget *left, GtkWidget *right)
{
	GtkGrid *dest;
	guint *dest_row;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (left != NULL));

	if (basic) {
		dest = GTK_GRID(server_cfg->basic_grid);
		dest_row = &server_cfg->basic_rows;
	} else {
		dest = GTK_GRID(server_cfg->advanced_grid);
		dest_row = &server_cfg->advanced_rows;
	}

	if (right != NULL) {
		gtk_grid_attach(GTK_GRID(dest), left, 0, *dest_row, 1, 1);
		gtk_grid_attach(GTK_GRID(dest), right, 1, *dest_row, 2, 1);
	} else {
		gtk_grid_attach(GTK_GRID(dest), left, 0, *dest_row, 2, 1);
	}
	*dest_row += 1;
}


const gchar *
libbalsa_server_cfg_get_name(LibBalsaServerCfg *server_cfg)
{
	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg), FALSE);
	return gtk_editable_get_text(GTK_EDITABLE(server_cfg->name));
}


/* note: name is special, see libbalsa_server_cfg_get_name() */
void
libbalsa_server_cfg_assign_server(LibBalsaServerCfg *server_cfg, LibBalsaServer *server)
{
    GFile *file;
        gchar *cert_file;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && LIBBALSA_IS_SERVER(server));


	/* host, post and security */
    libbalsa_server_set_security(server, (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(server_cfg->security)) + 1));
    libbalsa_server_set_host(server, gtk_editable_get_text(GTK_EDITABLE(server_cfg->host_port)), libbalsa_server_get_security(server));

    /* authentication stuff */
    if (server_cfg->require_auth != NULL) {
        libbalsa_server_set_try_anonymous(server, !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->require_auth)));
    } else {
        libbalsa_server_set_try_anonymous(server, FALSE);
    }
    libbalsa_server_set_username(server, gtk_editable_get_text(GTK_EDITABLE(server_cfg->username)));
    libbalsa_server_set_password(server, gtk_editable_get_text(GTK_EDITABLE(server_cfg->password)), FALSE);
    libbalsa_server_set_remember_password(server, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->remember_pass)));

    /* client certificate */
    libbalsa_server_set_client_cert(server, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->require_cert)));

    file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(server_cfg->cert_file));
    cert_file = g_file_get_path(file);
    g_object_unref(file);

    libbalsa_server_set_cert_file(server, cert_file);
    g_free(cert_file);

    libbalsa_server_set_password(server, gtk_editable_get_text(GTK_EDITABLE(server_cfg->cert_pass)), TRUE);
    libbalsa_server_set_remember_cert_passphrase(server, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->remember_cert_pass)));
}


/* -- local stuff --------------------------------------------------------------------------------------------------------------- */
static void
libbalsa_server_cfg_class_init(LibBalsaServerCfgClass *klass)
{
	changed_sig = g_signal_new("changed", LIBBALSA_TYPE_SERVER_CFG, G_SIGNAL_RUN_LAST, 0U, NULL, NULL, NULL, G_TYPE_NONE, 0U);
}


static void
libbalsa_server_cfg_init(LibBalsaServerCfg *self)
{
    self->notebook = gtk_notebook_new();
}


static GtkWidget *
server_cfg_add_entry(GtkWidget *grid, guint row, const gchar *label, const gchar *value, GCallback callback, gpointer cb_data)
{
	GtkWidget *new_entry;

	new_entry = gtk_entry_new();
    server_cfg_add_widget(grid, row, label, new_entry);
    if (value != NULL) {
        gtk_editable_set_text(GTK_EDITABLE(new_entry), value);
    }
    if (callback != NULL) {
        g_signal_connect(new_entry, "changed", callback, cb_data);
    }
    return new_entry;
}


static GtkWidget *
server_cfg_add_check(GtkWidget *grid, guint row, const gchar *label, gboolean value, GCallback callback, gpointer cb_data)
{
	GtkWidget *new_check;

	new_check = libbalsa_create_grid_check(label, grid, row, value);
    if (callback != NULL) {
        g_signal_connect(new_check, "toggled", callback, cb_data);
    }
    return new_check;
}


static void
server_cfg_add_widget(GtkWidget *grid, guint row, const gchar *text, GtkWidget *widget)
{
    GtkWidget *label;

    label = libbalsa_create_grid_label(text, grid, row);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), widget);
}


/**
 * \note Make sure that the order of entries in the combo matches the order of enum _NetClientCryptMode items.  If calling
 *       gtk_combo_box_get_active() on the returned widget, remember it starts with 0, so compensate for the offset of
 *       NET_CLIENT_CRYPT_ENCRYPTED.
 */
static GtkWidget *
server_cfg_security_widget(LibBalsaServer *server)
{
    GtkWidget *combo_box = gtk_combo_box_text_new();
    gchar *proto_upper;
    gchar *ssl_label;

    proto_upper = g_ascii_strup(libbalsa_server_get_protocol(server), -1);
    ssl_label = g_strdup_printf(_("%s over SSL (%sS)"), proto_upper, proto_upper);
    g_free(proto_upper);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), ssl_label);
    g_free(ssl_label);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS required"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS if possible (not recommended)"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("None (not recommended)"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), (gint) libbalsa_server_get_security(server) - 1);

    return combo_box;
}


static void
on_server_cfg_changed(GtkWidget *widget, LibBalsaServerCfg *server_cfg)
{
	gboolean sensitive;

	/* valid configuration only if a name and a host have been given */
	server_cfg->cfg_valid = (*gtk_editable_get_text(GTK_EDITABLE(server_cfg->name)) != '\0') &&
		(*gtk_editable_get_text(GTK_EDITABLE(server_cfg->host_port)) != '\0');

	/* user name/password only if authentication is required */
	if (server_cfg->require_auth != NULL) {
		sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->require_auth));
	} else {
		sensitive = TRUE;
	}
	gtk_widget_set_sensitive(server_cfg->username, sensitive);
	gtk_widget_set_sensitive(server_cfg->password, sensitive);
	gtk_widget_set_sensitive(server_cfg->remember_pass, sensitive);

	/* invalid configuration if authentication is required, but no user name given */
	if (sensitive && (*gtk_editable_get_text(GTK_EDITABLE(server_cfg->username)) == '\0')) {
		server_cfg->cfg_valid = FALSE;
	}

	/* client certificate and passphrase stuff only if TLS/SSL is enabled */
	sensitive = (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(server_cfg->security)) + 1) != NET_CLIENT_CRYPT_NONE;
	gtk_widget_set_sensitive(server_cfg->require_cert, sensitive);
	if (sensitive) {
		sensitive = sensitive && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->require_cert));
	}

	gtk_widget_set_sensitive(server_cfg->cert_file, sensitive);
	gtk_widget_set_sensitive(server_cfg->cert_pass, sensitive);
	gtk_widget_set_sensitive(server_cfg->remember_cert_pass, sensitive);

	/* invalid configuration if a certificate is required, but no file name given */
	if (sensitive) {
            GFile *file;
		gchar *cert_file;

                file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(server_cfg->cert_file));
                cert_file = g_file_get_path(file);
                g_object_unref(file);

		if ((cert_file == NULL) || (cert_file[0] == '\0')) {
			server_cfg->cfg_valid = FALSE;
		}
		g_free(cert_file);
	}

	g_signal_emit(server_cfg, changed_sig, 0);
}

GtkWidget *
libbalsa_server_cfg_get_notebook(LibBalsaServerCfg *server_cfg)
{
    g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg), NULL);

    return server_cfg->notebook;
}
