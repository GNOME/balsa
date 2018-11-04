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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <glib/gi18n.h>
#include "misc.h"
#include "server-config.h"


struct _LibBalsaServerCfgPrivate {
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


G_DEFINE_TYPE_WITH_PRIVATE(LibBalsaServerCfg, libbalsa_server_cfg, GTK_TYPE_NOTEBOOK)


static void libbalsa_server_cfg_class_init(LibBalsaServerCfgClass *klass);
static void libbalsa_server_cfg_init(LibBalsaServerCfg *self);
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
	LibBalsaServerCfgPrivate *priv;

	g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    server_cfg = LIBBALSA_SERVER_CFG(g_object_new(libbalsa_server_cfg_get_type(), NULL));
    priv = server_cfg->priv;

    /* notebook page with basic options */
#define HIG_PADDING 12
    priv->basic_grid = libbalsa_create_grid();
    priv->basic_rows = 0U;

    gtk_container_set_border_width(GTK_CONTAINER(priv->basic_grid), HIG_PADDING);
    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg), priv->basic_grid, gtk_label_new_with_mnemonic(_("_Basic")));

    /* server descriptive name */
    priv->name = server_cfg_add_entry(priv->basic_grid, priv->basic_rows++, _("_Descriptive Name:"), name,
    	G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* host and port */
    priv->host_port = server_cfg_add_entry(priv->basic_grid, priv->basic_rows++, _("_Server:"), server->host,
    	G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* security settings */
    priv->security = server_cfg_security_widget(server);
    server_cfg_add_widget(priv->basic_grid, priv->basic_rows++, _("Se_curity:"), priv->security);
    g_signal_connect(priv->security, "changed", G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* check box for authentication or anonymous access - smtp and imap only */
    if ((strcmp(server->protocol, "smtp") == 0) || (strcmp(server->protocol, "imap") == 0)) {
    	priv->require_auth = server_cfg_add_check(priv->basic_grid, priv->basic_rows++, _("Server requires _authentication"),
    		!server->try_anonymous, G_CALLBACK(on_server_cfg_changed), server_cfg);
    }

    /* user name and password */
    priv->username = server_cfg_add_entry(priv->basic_grid, priv->basic_rows++, _("_User Name:"), server->user,
    	G_CALLBACK(on_server_cfg_changed), server_cfg);

    priv->password = server_cfg_add_entry(priv->basic_grid, priv->basic_rows++, _("_Pass Phrase:"), server->passwd,
    	G_CALLBACK(on_server_cfg_changed), server_cfg);
    g_object_set(G_OBJECT(priv->password), "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(priv->password), FALSE);

    priv->remember_pass = server_cfg_add_check(priv->basic_grid, priv->basic_rows++, remember_password_message[0],
    	server->remember_passwd, G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* notebook page with advanced options */
    priv->advanced_grid = libbalsa_create_grid();
    priv->advanced_rows = 0U;
    gtk_container_set_border_width(GTK_CONTAINER(priv->advanced_grid), HIG_PADDING);
    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg), priv->advanced_grid, gtk_label_new_with_mnemonic(_("_Advanced")));

    /* client certificate and passphrase */
    priv->require_cert = server_cfg_add_check(priv->advanced_grid, priv->advanced_rows++, _("Server _requires client certificate"),
    	server->client_cert, G_CALLBACK(on_server_cfg_changed), server_cfg);

    priv->cert_file = gtk_file_chooser_button_new(_("Choose Client Certificate"), GTK_FILE_CHOOSER_ACTION_OPEN);
    server_cfg_add_widget(priv->advanced_grid, priv->advanced_rows++, _("Certificate _File:"), priv->cert_file);
    if (server->cert_file != NULL) {
    	gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(priv->cert_file), server->cert_file);
    }
    g_signal_connect(priv->cert_file, "file-set", G_CALLBACK(on_server_cfg_changed), server_cfg);

	priv->cert_pass = server_cfg_add_entry(priv->advanced_grid, priv->advanced_rows++, _("Certificate _Pass Phrase:"),
		server->cert_passphrase, G_CALLBACK(on_server_cfg_changed), server_cfg);
    g_object_set(G_OBJECT(priv->cert_pass), "input-purpose", GTK_INPUT_PURPOSE_PASSWORD, NULL);
    gtk_entry_set_visibility(GTK_ENTRY(priv->cert_pass), FALSE);

    priv->remember_cert_pass = server_cfg_add_check(priv->advanced_grid, priv->advanced_rows++, remember_password_message[1],
    	server->remember_cert_passphrase, G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* initially run the validity check */
    on_server_cfg_changed(NULL, server_cfg);

    return server_cfg;
}


gboolean
libbalsa_server_cfg_valid(LibBalsaServerCfg *server_cfg)
{
	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg), FALSE);
	return server_cfg->priv->cfg_valid;
}


GtkWidget *
libbalsa_server_cfg_add_check(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, gboolean initval,
	GCallback callback, gpointer cb_data)
{
	GtkWidget *new_check;
	LibBalsaServerCfgPrivate *priv;

	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL), NULL);

	priv = server_cfg->priv;
	if (basic) {
		new_check = server_cfg_add_check(priv->basic_grid, priv->basic_rows++, label, initval, callback, cb_data);
	} else {
		new_check = server_cfg_add_check(priv->advanced_grid, priv->advanced_rows++, label, initval, callback, cb_data);
	}
	return new_check;
}


GtkWidget *
libbalsa_server_cfg_add_entry(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, const gchar *initval,
	GCallback callback, gpointer cb_data)
{
	GtkWidget *new_entry;
	LibBalsaServerCfgPrivate *priv;

	g_return_val_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL), NULL);

	priv = server_cfg->priv;
	if (basic) {
		new_entry = server_cfg_add_entry(priv->basic_grid, priv->basic_rows++, label, initval, callback, cb_data);
	} else {
		new_entry = server_cfg_add_entry(priv->advanced_grid, priv->advanced_rows++, label, initval, callback, cb_data);
	}
	return new_entry;
}


void
libbalsa_server_cfg_add_item(LibBalsaServerCfg *server_cfg, gboolean basic, const gchar *label, GtkWidget *widget)
{
	LibBalsaServerCfgPrivate *priv;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (label != NULL) && (widget != NULL));

	priv = server_cfg->priv;
	if (basic) {
		server_cfg_add_widget(priv->basic_grid, priv->basic_rows++, label, widget);
	} else {
		server_cfg_add_widget(priv->advanced_grid, priv->advanced_rows++, label, widget);
	}
}


void
libbalsa_server_cfg_add_row(LibBalsaServerCfg *server_cfg, gboolean basic, GtkWidget *left, GtkWidget *right)
{
	LibBalsaServerCfgPrivate *priv;
	GtkGrid *dest;
	guint *dest_row;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && (left != NULL));

	priv = server_cfg->priv;
	if (basic) {
		dest = GTK_GRID(priv->basic_grid);
		dest_row = &priv->basic_rows;
	} else {
		dest = GTK_GRID(priv->advanced_grid);
		dest_row = &priv->advanced_rows;
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
	return gtk_entry_get_text(GTK_ENTRY(server_cfg->priv->name));
}


/* note: name is special, see libbalsa_server_cfg_get_name() */
void
libbalsa_server_cfg_assign_server(LibBalsaServerCfg *server_cfg, LibBalsaServer *server)
{
	LibBalsaServerCfgPrivate *priv;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && LIBBALSA_IS_SERVER(server));

	priv = server_cfg->priv;

	/* host, post and security */
    server->security = (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->security)) + 1);
    libbalsa_server_set_host(server, gtk_entry_get_text(GTK_ENTRY(priv->host_port)), server->security);

    /* authentication stuff */
    if (priv->require_auth != NULL) {
    	server->try_anonymous = !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->require_auth));
    } else {
    	server->try_anonymous = FALSE;
    }
    libbalsa_server_set_username(server, gtk_entry_get_text(GTK_ENTRY(priv->username)));
    libbalsa_server_set_password(server, gtk_entry_get_text(GTK_ENTRY(priv->password)));
    server->remember_passwd = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->remember_pass));

    /* client certificate */
    server->client_cert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->require_cert));
    g_free(server->cert_file);
    server->cert_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(priv->cert_file));
    g_free(server->cert_passphrase);
    server->cert_passphrase = g_strdup(gtk_entry_get_text(GTK_ENTRY(priv->cert_pass)));
    server->remember_cert_passphrase = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->remember_cert_pass));
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
	self->priv = libbalsa_server_cfg_get_instance_private(self);
}


static GtkWidget *
server_cfg_add_entry(GtkWidget *grid, guint row, const gchar *label, const gchar *value, GCallback callback, gpointer cb_data)
{
	GtkWidget *new_entry;

	new_entry = gtk_entry_new();
    server_cfg_add_widget(grid, row, label, new_entry);
    if (value != NULL) {
        gtk_entry_set_text(GTK_ENTRY(new_entry), value);
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

    proto_upper = g_ascii_strup(server->protocol, -1);
    ssl_label = g_strdup_printf(_("%s over SSL (%sS)"), proto_upper, proto_upper);
    g_free(proto_upper);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), ssl_label);
    g_free(ssl_label);
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS required"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("TLS if possible (not recommended)"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box), _("None (not recommended)"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), (gint) server->security - 1);

    return combo_box;
}


static void
on_server_cfg_changed(GtkWidget *widget, LibBalsaServerCfg *server_cfg)
{
	LibBalsaServerCfgPrivate *priv = server_cfg->priv;
	gboolean sensitive;

	/* valid configuration only if a name and a host have been given */
	priv->cfg_valid = (*gtk_entry_get_text(GTK_ENTRY(priv->name)) != '\0') &&
		(*gtk_entry_get_text(GTK_ENTRY(priv->host_port)) != '\0');

	/* user name/password only if authentication is required */
	if (priv->require_auth != NULL) {
		sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->require_auth));
	} else {
		sensitive = TRUE;
	}
	gtk_widget_set_sensitive(priv->username, sensitive);
	gtk_widget_set_sensitive(priv->password, sensitive);
	gtk_widget_set_sensitive(priv->remember_pass, sensitive);

	/* invalid configuration if authentication is required, but no user name given */
	if (sensitive && (*gtk_entry_get_text(GTK_ENTRY(priv->username)) == '\0')) {
		priv->cfg_valid = FALSE;
	}

	/* client certificate and passphrase stuff only if TLS/SSL is enabled */
	sensitive = (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(priv->security)) + 1) != NET_CLIENT_CRYPT_NONE;
	gtk_widget_set_sensitive(priv->require_cert, sensitive);
	if (sensitive) {
		sensitive = sensitive && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->require_cert));
	}

	gtk_widget_set_sensitive(priv->cert_file, sensitive);
	gtk_widget_set_sensitive(priv->cert_pass, sensitive);
	gtk_widget_set_sensitive(priv->remember_cert_pass, sensitive);

	/* invalid configuration if a certificate is required, but no file name given */
	if (sensitive) {
		gchar *cert_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(priv->cert_file));

		if ((cert_file == NULL) || (cert_file[0] == '\0')) {
			priv->cfg_valid = FALSE;
		}
		g_free(cert_file);
	}

	g_signal_emit(server_cfg, changed_sig, 0);
}
