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
#include "net-client-smtp.h"
#include "net-client-pop.h"
#include "imap-handle.h"
#include "libbalsa-conf.h"
#include "server-config.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "libbalsa-server"


struct _LibBalsaServerCfg {
        GtkNotebook parent;

	/* "Basic" notebook page */
	GtkWidget *basic_grid;			/* grid */
	guint basic_rows;				/* count of rows */
	GtkWidget *name;				/* descriptive name */
	GtkWidget *host_port;			/* host and optionally port */
	GtkWidget *probe_host;			/* button for probing the host */
	GtkWidget *security;			/* security (SSL/TLS/...) */
	GtkWidget *auth_mode;			/* authentication mode */
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

	/* function for probing a server's capabilities */
	gboolean (*probe_fn)(const gchar *, guint, NetClientProbeResult *, GCallback, GError **);

	gboolean cfg_valid;				/* whether the config options are valid (parent may enable OK) */
};


G_DEFINE_TYPE(LibBalsaServerCfg, libbalsa_server_cfg, GTK_TYPE_NOTEBOOK)


static GtkWidget *server_cfg_add_entry(GtkWidget *grid, guint row, const gchar *label, const gchar *value, GCallback callback,
		GtkWidget *button, gpointer cb_data)
	G_GNUC_WARN_UNUSED_RESULT;
static GtkWidget *server_cfg_add_check(GtkWidget *grid, guint row, const gchar *label, gboolean value, GCallback callback,
									   gpointer cb_data)
	G_GNUC_WARN_UNUSED_RESULT;
static void server_cfg_add_widget(GtkWidget *grid, guint row, const gchar *text, GtkWidget *widget);
static GtkWidget *server_cfg_security_widget(LibBalsaServer *server);
static GtkWidget *server_cfg_auth_widget(LibBalsaServer *server);
static void on_server_cfg_changed(GtkWidget *widget, LibBalsaServerCfg *server_cfg);
static void on_server_probe(GtkWidget *widget, LibBalsaServerCfg *server_cfg);


static guint changed_sig;


static const gchar *remember_password_message[4] = {
	N_("_Remember user password in Secret Service"),
	N_("_Remember certificate pass phrase in Secret Service"),
	N_("_Remember user password"),
	N_("_Remember certificate pass phrase")
};


LibBalsaServerCfg *
libbalsa_server_cfg_new(LibBalsaServer *server, const gchar *name)
{
	LibBalsaServerCfg *server_cfg;
	const gchar *protocol;
	const gchar *cert_file;
	int pwd_msg_offs;

	g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

#if defined(HAVE_LIBSECRET)
	pwd_msg_offs = libbalsa_conf_use_libsecret() ? 0 : 2;
#else
	pwd_msg_offs = 2;
#endif

    server_cfg = LIBBALSA_SERVER_CFG(g_object_new(libbalsa_server_cfg_get_type(), NULL));

    /* notebook page with basic options */
    server_cfg->basic_grid = libbalsa_create_grid();
    server_cfg->basic_rows = 0U;

    gtk_container_set_border_width(GTK_CONTAINER(server_cfg->basic_grid), 2 * HIG_PADDING);
    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg), server_cfg->basic_grid, gtk_label_new_with_mnemonic(_("_Basic")));

    /* server descriptive name */
    server_cfg->name = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Descriptive Name:"), name,
        G_CALLBACK(on_server_cfg_changed), NULL, server_cfg);

    /* probe button */
    server_cfg->probe_host = gtk_button_new_from_icon_name("system-run", GTK_ICON_SIZE_MENU);
    gtk_button_set_label(GTK_BUTTON(server_cfg->probe_host), _("probe…"));
    gtk_button_set_always_show_image(GTK_BUTTON(server_cfg->probe_host), TRUE);
    g_signal_connect(server_cfg->probe_host, "clicked", G_CALLBACK(on_server_probe), server_cfg);
    protocol = libbalsa_server_get_protocol(server);
    if (strcmp(protocol, "smtp") == 0) {
    	server_cfg->probe_fn = net_client_smtp_probe;
    } else if (strcmp(protocol, "pop3") == 0) {
    	server_cfg->probe_fn = net_client_pop_probe;
    } else if (strcmp(protocol, "imap") == 0) {
    	server_cfg->probe_fn = imap_server_probe;
    } else {
    	g_assert_not_reached();
    }

    /* host and port */
    server_cfg->host_port = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Server:"),
    	libbalsa_server_get_host(server), G_CALLBACK(on_server_cfg_changed), server_cfg->probe_host, server_cfg);

    /* security settings */
    server_cfg->security = server_cfg_security_widget(server);
    server_cfg_add_widget(server_cfg->basic_grid, server_cfg->basic_rows++, _("Se_curity:"), server_cfg->security);
    g_signal_connect(server_cfg->security, "changed", G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* authentication mode */
    server_cfg->auth_mode = server_cfg_auth_widget(server);
    server_cfg_add_widget(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Authentication:"), server_cfg->auth_mode);
    g_signal_connect(server_cfg->auth_mode, "changed", G_CALLBACK(on_server_cfg_changed), server_cfg);

    /* user name and password */
    server_cfg->username = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_User Name:"),
                                          libbalsa_server_get_user(server),
                                          G_CALLBACK(on_server_cfg_changed), NULL, server_cfg);

    server_cfg->password = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, _("_Pass Phrase:"),
                                          libbalsa_server_get_password(server),
										  G_CALLBACK(on_server_cfg_changed), NULL, server_cfg);
    libbalsa_entry_config_passwd(GTK_ENTRY(server_cfg->password));

    server_cfg->remember_pass = server_cfg_add_check(server_cfg->basic_grid, server_cfg->basic_rows++,
        remember_password_message[pwd_msg_offs], libbalsa_server_get_remember_password(server), G_CALLBACK(on_server_cfg_changed),
        server_cfg);

    /* notebook page with advanced options */
    server_cfg->advanced_grid = libbalsa_create_grid();
    server_cfg->advanced_rows = 0U;
    gtk_container_set_border_width(GTK_CONTAINER(server_cfg->advanced_grid), 2 * HIG_PADDING);
    gtk_notebook_append_page(GTK_NOTEBOOK(server_cfg), server_cfg->advanced_grid, gtk_label_new_with_mnemonic(_("_Advanced")));

    /* client certificate and passphrase */
    server_cfg->require_cert = server_cfg_add_check(server_cfg->advanced_grid, server_cfg->advanced_rows++, _("Server _requires client certificate"),
        libbalsa_server_get_client_cert(server), G_CALLBACK(on_server_cfg_changed), server_cfg);

    server_cfg->cert_file = gtk_file_chooser_button_new(_("Choose Client Certificate"), GTK_FILE_CHOOSER_ACTION_OPEN);
    server_cfg_add_widget(server_cfg->advanced_grid, server_cfg->advanced_rows++, _("Certificate _File:"), server_cfg->cert_file);

    cert_file = libbalsa_server_get_cert_file(server);
    if (cert_file != NULL) {
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(server_cfg->cert_file), cert_file);
    }
    g_signal_connect(server_cfg->cert_file, "file-set", G_CALLBACK(on_server_cfg_changed), server_cfg);

	server_cfg->cert_pass = server_cfg_add_entry(server_cfg->advanced_grid, server_cfg->advanced_rows++,
		_("Certificate _Pass Phrase:"),	libbalsa_server_get_cert_passphrase(server), G_CALLBACK(on_server_cfg_changed), NULL,
		server_cfg);
	libbalsa_entry_config_passwd(GTK_ENTRY(server_cfg->cert_pass));

    server_cfg->remember_cert_pass = server_cfg_add_check(server_cfg->advanced_grid, server_cfg->advanced_rows++,
        remember_password_message[pwd_msg_offs + 1], libbalsa_server_get_remember_cert_passphrase(server),
        G_CALLBACK(on_server_cfg_changed), server_cfg);

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
		new_entry = server_cfg_add_entry(server_cfg->basic_grid, server_cfg->basic_rows++, label, initval, callback, NULL, cb_data);
	} else {
		new_entry = server_cfg_add_entry(server_cfg->advanced_grid, server_cfg->advanced_rows++, label, initval, callback, NULL,
			cb_data);
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
	return gtk_entry_get_text(GTK_ENTRY(server_cfg->name));
}


/* note: name is special, see libbalsa_server_cfg_get_name() */
void
libbalsa_server_cfg_assign_server(LibBalsaServerCfg *server_cfg, LibBalsaServer *server)
{
	gchar *cert_file;
	const gchar *auth_id;

	g_return_if_fail(LIBBALSA_IS_SERVER_CFG(server_cfg) && LIBBALSA_IS_SERVER(server));

	/* host, post and security */
    libbalsa_server_set_security(server, (NetClientCryptMode) (gtk_combo_box_get_active(GTK_COMBO_BOX(server_cfg->security)) + 1));
    libbalsa_server_set_host(server, gtk_entry_get_text(GTK_ENTRY(server_cfg->host_port)), libbalsa_server_get_security(server));

    /* authentication stuff - use no or anonymous authentication if noting is selected */
    auth_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(server_cfg->auth_mode));
    libbalsa_server_set_auth_mode(server, (auth_id != NULL) ? atoi(auth_id) : NET_CLIENT_AUTH_NONE_ANON);
    libbalsa_server_set_username(server, gtk_entry_get_text(GTK_ENTRY(server_cfg->username)));
    libbalsa_server_set_password(server, gtk_entry_get_text(GTK_ENTRY(server_cfg->password)), FALSE);
    libbalsa_server_set_remember_password(server, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->remember_pass)));

    /* client certificate */
    libbalsa_server_set_client_cert(server, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(server_cfg->require_cert)));

    cert_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(server_cfg->cert_file));
    libbalsa_server_set_cert_file(server, cert_file);
    g_free(cert_file);

    libbalsa_server_set_password(server, gtk_entry_get_text(GTK_ENTRY(server_cfg->cert_pass)), TRUE);
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
    /* Nothing to do */
}


static GtkWidget *
server_cfg_add_entry(GtkWidget *grid, guint row, const gchar *label, const gchar *value, GCallback callback, GtkWidget *button,
	gpointer cb_data)
{
	GtkWidget *new_entry;

	new_entry = gtk_entry_new();
	if (button != NULL) {
		GtkWidget *hbox;

		hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
		server_cfg_add_widget(grid, row, label, hbox);
                gtk_widget_set_hexpand(new_entry, TRUE);
                gtk_widget_set_halign(new_entry, GTK_ALIGN_FILL);
		gtk_container_add(GTK_CONTAINER(hbox), new_entry);
		gtk_container_add(GTK_CONTAINER(hbox), button);
	} else {
		server_cfg_add_widget(grid, row, label, new_entry);
	}
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


static GtkWidget *
server_cfg_auth_widget(LibBalsaServer *server)
{
	const gchar *protocol;
    GtkWidget *combo_box = gtk_combo_box_text_new();
    gchar id_buf[8];

    protocol = libbalsa_server_get_protocol(server);
    if ((strcmp(protocol, "pop3") == 0) || (strcmp(protocol, "imap") == 0)) {
    	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box), "1", _("anonymous access"));		/* RFC 4505 */
    } else {
    	gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box), "1", _("none required"));
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box), "2", _("user name and pass phrase"));
#if defined(HAVE_GSSAPI)
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box), "4", _("Kerberos (GSSAPI)"));
#endif

    snprintf(id_buf, sizeof(id_buf), "%d", (gint) libbalsa_server_get_auth_mode(server));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo_box), id_buf);

    return combo_box;
}


static void
on_server_cfg_changed(GtkWidget *widget, LibBalsaServerCfg *server_cfg)
{
	const gchar *active_auth;
	gboolean sensitive;
	NetClientAuthMode auth_mode;

	/* valid configuration only if a name and a host have been given */
	server_cfg->cfg_valid = (*gtk_entry_get_text(GTK_ENTRY(server_cfg->name)) != '\0') &&
		(*gtk_entry_get_text(GTK_ENTRY(server_cfg->host_port)) != '\0');

	/* can probe only if name and host/port are given */
	gtk_widget_set_sensitive(server_cfg->probe_host, server_cfg->cfg_valid);

	/* user name/password depend upon auth mode */
	active_auth = gtk_combo_box_get_active_id(GTK_COMBO_BOX(server_cfg->auth_mode));
	if (active_auth != 0) {
		auth_mode = atoi(active_auth);
	} else {
		auth_mode = 0;
	}
	gtk_widget_set_sensitive(server_cfg->username, auth_mode != NET_CLIENT_AUTH_NONE_ANON);
	gtk_widget_set_sensitive(server_cfg->password, auth_mode == NET_CLIENT_AUTH_USER_PASS);
	gtk_widget_set_sensitive(server_cfg->remember_pass, auth_mode == NET_CLIENT_AUTH_USER_PASS);

	/* invalid configuration if authentication is required, but no user name given */
	if ((auth_mode != NET_CLIENT_AUTH_NONE_ANON) && (*gtk_entry_get_text(GTK_ENTRY(server_cfg->username)) == '\0')) {
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
		gchar *cert_file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(server_cfg->cert_file));

		if ((cert_file == NULL) || (cert_file[0] == '\0')) {
			server_cfg->cfg_valid = FALSE;
		}
		g_free(cert_file);
	}

	g_signal_emit(server_cfg, changed_sig, 0);
}

typedef struct {
    LibBalsaServerCfg *server_cfg;
    char *server_name;
    NetClientProbeResult probe_res;
} on_server_probe_data_t;

static void on_server_probe_response(GtkDialog *self,
                                     gint       response_id,
                                     gpointer   user_data);

static void
on_server_probe(GtkWidget *widget, LibBalsaServerCfg *server_cfg)
{
	const gchar *server_name;
	gboolean success;
	NetClientProbeResult probe_res;
	GError *error = NULL;
	GtkWidget *msgdlg;
	on_server_probe_data_t *data;

	server_name = gtk_entry_get_text(GTK_ENTRY(server_cfg->host_port));
	g_assert(server_cfg->probe_fn != NULL);
	success = server_cfg->probe_fn(server_name, 5, &probe_res, G_CALLBACK(libbalsa_server_check_cert), &error);
	if (success) {
		const gchar *crypt_str;
		const gchar *auth_str;
		GtkDialogFlags flags =
		    GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags();

		switch (probe_res.crypt_mode) {
		case NET_CLIENT_CRYPT_ENCRYPTED:
			crypt_str = _("yes (SSL/TLS)");
			break;
		case NET_CLIENT_CRYPT_STARTTLS:
			crypt_str = _("yes (STARTTLS)");
			break;
		default:
			crypt_str = _("no");
		}

		if ((probe_res.auth_mode & NET_CLIENT_AUTH_KERBEROS) != 0) {
			auth_str = _("Kerberos (GSSAPI)");
			probe_res.auth_mode = NET_CLIENT_AUTH_KERBEROS;
		} else if ((probe_res.auth_mode & NET_CLIENT_AUTH_USER_PASS) != 0) {
			auth_str = _("user name and pass phrase");
			probe_res.auth_mode = NET_CLIENT_AUTH_USER_PASS;
		} else {
			auth_str = _("none");
			probe_res.auth_mode = NET_CLIENT_AUTH_NONE_ANON;
		}
		msgdlg = gtk_message_dialog_new(NULL, flags, GTK_MESSAGE_INFO, GTK_BUTTONS_NONE,
			_("Probe results for server %s\n"
			  "\342\200\242 Port: %u\n"
			  "\342\200\242 Encryption: %s\n"
			  "\342\200\242 Authentication: %s\n"),
			server_name, probe_res.port, crypt_str, auth_str);
		gtk_dialog_add_button(GTK_DIALOG(msgdlg), _("_Apply"), GTK_RESPONSE_APPLY);
		gtk_dialog_add_button(GTK_DIALOG(msgdlg), _("_Close"), GTK_RESPONSE_CLOSE);
	} else {
		msgdlg = gtk_message_dialog_new(NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
            GTK_MESSAGE_ERROR,
            GTK_BUTTONS_CLOSE,
            _("Error probing server %s: %s"), server_name,
            (error != NULL) ? error->message : _("unknown"));
	}
	g_clear_error(&error);

	data = g_new(on_server_probe_data_t, 1);
	data->server_cfg = g_object_ref(server_cfg);
	data->server_name = g_strdup(server_name);
	data->probe_res = probe_res;
	g_signal_connect(msgdlg, "response",
                         G_CALLBACK(on_server_probe_response), data);
	gtk_widget_show_all(msgdlg);
}

static void
on_server_probe_response(GtkDialog *self,
                         gint       response_id,
                         gpointer   user_data)
{
    on_server_probe_data_t *data = user_data;

	gtk_widget_destroy(GTK_WIDGET(self));

	if (response_id == GTK_RESPONSE_APPLY) {
		gchar *buffer;
		gchar id_buf[8];
		const gchar *colon;

		colon = strchr(data->server_name, ':');
		if (colon == NULL) {
			buffer = g_strdup_printf("%s:%u", data->server_name, data->probe_res.port);
		} else {
			buffer = g_strdup_printf("%.*s:%u", (int) (colon - data->server_name), data->server_name, data->probe_res.port);
		}
		gtk_entry_set_text(GTK_ENTRY(data->server_cfg->host_port), buffer);
		g_free(buffer);

		gtk_combo_box_set_active(GTK_COMBO_BOX(data->server_cfg->security), data->probe_res.crypt_mode - 1);
		snprintf(id_buf, sizeof(id_buf), "%d", data->probe_res.auth_mode);
		gtk_combo_box_set_active_id(GTK_COMBO_BOX(data->server_cfg->auth_mode), id_buf);
	}

	g_object_unref(data->server_cfg);
	g_free(data->server_name);
	g_free(data);
}
