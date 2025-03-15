/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2022 Stuart Parmenter and others,
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
#include "assistant_page_server.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include <glib/gi18n.h>
#include "imap/imap-handle.h"
#include "imap-server.h"
#include "smtp-server.h"
#include "server.h"
#include "balsa-app.h"
#include "save-restore.h"
#include "net-client-utils.h"
#include "net-client-pop.h"
#include "net-client-smtp.h"


static void balsa_druid_page_server_init(BalsaDruidPageServer *server,
										 GtkWidget            *page,
										 GtkAssistant         *druid);
static void balsa_druid_page_server_prepare(GtkAssistant         *druid,
											GtkWidget            *page,
											BalsaDruidPageServer *server);
static void balsa_druid_page_server_next(GtkAssistant         *druid,
										 GtkWidget            *page,
										 BalsaDruidPageServer *server);
static void on_server_type_changed(GtkComboBox *widget,
								   gpointer     user_data);
static gchar **guess_servers(const gchar *mail_address)
	G_GNUC_WARN_UNUSED_RESULT;
static gchar *server_from_srv(GResolver           *resolver,
							  const gchar * const *service,
							  const gchar         *domain)
	G_GNUC_WARN_UNUSED_RESULT;
static gchar *server_by_name(GResolver           *resolver,
							 const gchar * const *prefix,
							 const gchar         *domain)
	G_GNUC_WARN_UNUSED_RESULT;


static void
balsa_druid_page_server_init(BalsaDruidPageServer *server, GtkWidget *page, GtkAssistant *druid)
{
	static const char *header =
		N_("The pre-set server configuration is a best guess based "
			"upon your email address. Balsa will automatically choose "
			"optimum settings for encryption and user name, and select "
			"password based authentication. Please note that probing the "
			"server capabilities when you click “Next” will take a few seconds.\n"
			"Check the “Edit \342\217\265 Preferences \342\217\265 Settings "
			"\342\217\265 Mail options” menu item if you need to fine-tune them.");
	static const char* server_types[] = { "IMAP", "POP3", NULL };
	static const char* remember_passwd[] = {
		N_("Yes, remember it"), N_("No, type it in every time"), NULL };
	GtkGrid *grid;
	GtkLabel *label;
	gchar *preset;
	int row = 0;

	label = GTK_LABEL(gtk_label_new(_(header)));
	gtk_label_set_line_wrap(label, TRUE);
	gtk_widget_set_hexpand(GTK_WIDGET(label), TRUE);
	gtk_widget_set_margin_bottom(GTK_WIDGET(label), 12);
	gtk_container_add(GTK_CONTAINER(page), GTK_WIDGET(label));

	grid = GTK_GRID(gtk_grid_new());
	gtk_grid_set_row_spacing(grid, 2);
	gtk_grid_set_column_spacing(grid, 5);

	balsa_init_add_grid_option(grid, row++, _("_Type of mail server:"),
		server_types, druid, &(server->incoming_type));
	g_signal_connect(server->incoming_type, "changed", G_CALLBACK(on_server_type_changed), server);

	balsa_init_add_grid_entry(grid, row++,
		_("Name of mail server for incoming _mail:"),
		"", /* no guessing here */
		NULL, druid, page, &(server->incoming_srv));

	balsa_init_add_grid_entry(grid, row++, _("Your email _login name:"),
		g_get_user_name(),
		NULL, druid, page, &(server->login));
	balsa_init_add_grid_entry(grid, row++, _("Your _password:"),
		"",
		NULL, druid, page, &(server->passwd));
	libbalsa_entry_config_passwd(GTK_ENTRY(server->passwd));
	/* separator line here */

	preset = "localhost:25";
	balsa_init_add_grid_entry(grid, row++, _("_SMTP Server:"), preset,
		NULL, druid, page, &(server->smtp));

	balsa_init_add_grid_option(grid, row++,
		_("_Remember your password:"),
		remember_passwd, druid,
		&(server->remember_passwd));

        libbalsa_set_vmargins(GTK_WIDGET(grid), 3);
	gtk_container_add(GTK_CONTAINER(page), GTK_WIDGET(grid));

	server->need_set = FALSE;
}


static void
on_server_type_changed(GtkComboBox *widget, gpointer user_data)
{
	BalsaDruidPageServer *server = (BalsaDruidPageServer *) user_data;
	const gchar **servers;
	int idx;

	servers = g_object_get_data(G_OBJECT(widget), "SERVERS");
	idx = 1 + gtk_combo_box_get_active(widget);
	gtk_entry_set_text(GTK_ENTRY(server->incoming_srv),
		((servers != NULL) && (servers[idx] != NULL)) ? servers[idx] : "");
}


void
balsa_druid_page_server(GtkAssistant * druid)
{
	BalsaDruidPageServer *server;

	server = g_new0(BalsaDruidPageServer, 1);
	server->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_assistant_append_page(druid, server->page);
	gtk_assistant_set_page_title(druid, server->page, _("Server Settings"));
	balsa_druid_page_server_init(server, server->page, druid);
	g_signal_connect(druid, "prepare", G_CALLBACK(balsa_druid_page_server_prepare), server);
	g_object_weak_ref(G_OBJECT(druid), (GWeakNotify) g_free, server);
}


static void
balsa_druid_page_server_prepare(GtkAssistant *druid, GtkWidget *page, BalsaDruidPageServer *server)
{
	if (page != server->page) {
		if (server->need_set) {
			balsa_druid_page_server_next(druid, page, server);
			server->need_set = FALSE;
		}
		return;
	}

	g_object_set_data(G_OBJECT(server->incoming_type), "SERVERS", NULL);
	if (balsa_app.current_ident != NULL) {
		InternetAddress *ia;
		const gchar *address;

		ia = libbalsa_identity_get_address(balsa_app.current_ident);	/* should never be NULL */
		address = internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(ia));
		if (address != NULL) {
			const gchar *at_sign;

			at_sign = strchr(address, '@');
			if (at_sign != NULL) {
				gchar *login;
				gchar **servers;

				/* wild guess: login is the part up to the '@' in the mail address */
				login = g_strndup(address, at_sign - address);
				gtk_entry_set_text(GTK_ENTRY(server->login), login);
				g_free(login);

				/* try to guess proper servers */
				servers = guess_servers(address);
				if (servers != NULL) {
					g_object_set_data_full(G_OBJECT(server->incoming_type), "SERVERS", servers, (GDestroyNotify) g_strfreev);
					if (servers[0] != NULL) {
						gtk_entry_set_text(GTK_ENTRY(server->smtp), servers[0]);
					}
					gtk_combo_box_set_active(GTK_COMBO_BOX(server->incoming_type), -1);		/* force update */
					gtk_combo_box_set_active(GTK_COMBO_BOX(server->incoming_type), (servers[1] != NULL) ? 0 : 1);
				}
			}
		}
	}

	gtk_assistant_set_page_complete(druid, page, TRUE);
	gtk_widget_grab_focus(server->incoming_srv);
	server->need_set = TRUE;
}


static void
create_pop3_mbx(const gchar *host, const gchar *login, const gchar *passwd, gboolean save_pwd)
{
	gchar *host_only;
	gchar *host_port;
	NetClientProbeResult probe_res;
	LibBalsaMailboxPOP3 *mailbox_pop3;
	LibBalsaMailbox *mailbox;
	LibBalsaServer *server;
	GError *error = NULL;

	host_only = net_client_host_only(host);
	if (net_client_pop_probe(host_only, 5, &probe_res, G_CALLBACK(libbalsa_server_check_cert), &error)) {
		host_port = g_strdup_printf("%s:%hu", host_only, probe_res.port);
		g_debug("probe result for server %s: port %hu, crypt mode %d", host, probe_res.port, probe_res.crypt_mode);
	} else {
		g_debug("failed to probe server %s, fall back to POP3S: %s", host, error->message);
		host_port = g_strdup_printf("%s:995", host_only);
		probe_res.crypt_mode = NET_CLIENT_CRYPT_ENCRYPTED;
	}
	g_clear_error(&error);

	mailbox_pop3 = libbalsa_mailbox_pop3_new();
	mailbox = LIBBALSA_MAILBOX(mailbox_pop3);
	server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox);
	libbalsa_server_set_host(server, host_port, probe_res.crypt_mode);
	libbalsa_server_set_username(server, login);
	libbalsa_server_set_password(server, passwd, FALSE);
	libbalsa_server_set_auth_mode(server, NET_CLIENT_AUTH_USER_PASS);
	libbalsa_server_set_remember_password(server, save_pwd);
	libbalsa_mailbox_set_name(mailbox, host_only);

	libbalsa_mailbox_pop3_set_check(mailbox_pop3, TRUE);
	libbalsa_mailbox_pop3_set_disable_apop(mailbox_pop3, FALSE);
	libbalsa_mailbox_pop3_set_delete_from_server(mailbox_pop3, TRUE);
	libbalsa_mailbox_pop3_set_filter(mailbox_pop3, FALSE);
	libbalsa_mailbox_pop3_set_filter_cmd(mailbox_pop3, "procmail -f -");
	config_mailbox_add(mailbox, NULL);

	g_free(host_only);
	g_free(host_port);
}


static void
create_imap_mbx(const gchar *host, const gchar *login, const gchar *passwd, gboolean save_pwd)
{
	gchar *host_only;
	gchar *host_port;
	NetClientProbeResult probe_res;
	BalsaMailboxNode *mbnode;
	LibBalsaServer *server;
	GError *error = NULL;

	host_only = net_client_host_only(host);
	server = LIBBALSA_SERVER(libbalsa_imap_server_new(login, host));
	if (imap_server_probe(host_only, 5, &probe_res, G_CALLBACK(libbalsa_server_check_cert), &error)) {
		host_port = g_strdup_printf("%s:%hu", host_only, probe_res.port);
		g_debug("probe result for server %s: port %hu, crypt mode %d", host, probe_res.port, probe_res.crypt_mode);
	} else {
		g_debug("failed to probe server %s, fall back to IMAPS: %s", host, error->message);
		host_port = g_strdup_printf("%s:993", host_only);
		probe_res.crypt_mode = NET_CLIENT_CRYPT_ENCRYPTED;
	}
	g_clear_error(&error);
	libbalsa_server_set_host(server, host_port, probe_res.crypt_mode);
	libbalsa_server_set_username(server, login);
	libbalsa_server_set_password(server, passwd, FALSE);
	libbalsa_server_set_auth_mode(server, NET_CLIENT_AUTH_USER_PASS);
	libbalsa_server_set_remember_password(server, save_pwd);
	mbnode = balsa_mailbox_node_new_imap_folder(server, NULL);
	balsa_mailbox_node_set_name(mbnode, host_only);
	config_folder_add(mbnode, NULL);
	g_object_unref(mbnode);
	g_free(host_only);
	g_free(host_port);
}


static void
config_smtp(const gchar *host, LibBalsaServer *smtpserver)
{
	gchar *host_only;
	gchar *host_port;
	NetClientProbeResult probe_res;
	GError *error = NULL;

	host_only = net_client_host_only(host);
	if (net_client_smtp_probe(host_only, 5, &probe_res, G_CALLBACK(libbalsa_server_check_cert), &error)) {
		host_port = g_strdup_printf("%s:%hu", host_only, probe_res.port);
		g_debug("probe result for server %s: port %hu, crypt mode %d", host, probe_res.port, probe_res.crypt_mode);
	} else {
		/* Note: the better choice in this case would be SUBMISSIONS (RFC 8314), but in practice some ISP's do not support it. */
		g_debug("failed to probe server %s, fall back to SUBMISSION w/ STARTTLS: %s", host, error->message);
		host_port = g_strdup_printf("%s:587", host_only);
		probe_res.crypt_mode = NET_CLIENT_CRYPT_STARTTLS;
	}
	libbalsa_server_set_host(smtpserver, host_port, probe_res.crypt_mode);
	g_clear_error(&error);
	g_free(host_only);
	g_free(host_port);
}


static void
balsa_druid_page_server_next(GtkAssistant *druid, GtkWidget *page, BalsaDruidPageServer *server)
{
	const gchar *login;
	const gchar *passwd;
	gboolean save_pwd;
	const gchar *host;
	LibBalsaServer *lbserver;
	LibBalsaSmtpServer *smtp_server;

	/* common stuff: user name, password, remember password */
	login = gtk_entry_get_text(GTK_ENTRY(server->login));
	passwd = gtk_entry_get_text(GTK_ENTRY(server->passwd));
	save_pwd = balsa_option_get_active(server->remember_passwd) == 0;

	/* incoming mail - skip if nothing has been entered */
	host = gtk_entry_get_text(GTK_ENTRY(server->incoming_srv));
	if ((host != NULL) && (host[0] != '\0')) {
		switch (balsa_option_get_active(server->incoming_type)) {
		case 0: /* IMAP */
			create_imap_mbx(host, login, passwd, save_pwd);
			break;
		case 1: /* POP */
			create_pop3_mbx(host, login, passwd, save_pwd);
			break;
		default:
			g_assert_not_reached();		/* internal error */
		}
	}

	/* outgoing mail */
	if (balsa_app.smtp_servers == NULL) {
		smtp_server = libbalsa_smtp_server_new();
		libbalsa_smtp_server_set_name(smtp_server, libbalsa_smtp_server_get_name(NULL));
		balsa_app.smtp_servers = g_slist_prepend(NULL, smtp_server);
	} else {
		smtp_server = balsa_app.smtp_servers->data;
	}
	lbserver = LIBBALSA_SERVER(smtp_server);
	host = gtk_entry_get_text(GTK_ENTRY(server->smtp));
	if ((host != NULL) && (host[0] != '\0') && (strncmp(host, "localhost", 9U) != 0)) {
		config_smtp(host, lbserver);
		libbalsa_server_set_username(lbserver, login);
		libbalsa_server_set_password(lbserver, passwd, FALSE);
		libbalsa_server_set_auth_mode(lbserver, NET_CLIENT_AUTH_USER_PASS);
		libbalsa_server_set_remember_password(lbserver, save_pwd);
	} else {
		/* localhost at port 25, without encryption and authentication */
		libbalsa_server_set_host(lbserver, "localhost:25", NET_CLIENT_CRYPT_NONE);
		libbalsa_server_set_auth_mode(lbserver, NET_CLIENT_AUTH_NONE_ANON);
	}
}


/** @brief Guess the email servers for a given email address
 *
 * @param[in] mail_address email address
 * @return an NULL-terminated array containing the submission, imap and pop3 servers on success, @em must be freed by the caller
 *
 * Strategy for guessing the server names:
 * - some ISP's have "strange" server names (inter alia Microsoft for outlook.com, Yahoo for yahoo.*): return them from a
 *   hard-wired lookup table;
 * - do a DNS lookup for SRV records (see https://www.rfc-editor.org/rfc/rfc6186.html, e.g. _imaps._tcp.<domain>);
 * - try to guess servers by name (e.g. imap.<domain>).
 */
static gchar **
guess_servers(const gchar *mail_address)
{
	static const gchar * const srv_names[3][3] = {
		{"submissions", "submission", NULL},
		{"imaps", "imap", NULL},
		{"pop3s", "pop3", NULL}
	};
	static const gchar * const dns_prefix[3][4] = {
		{"smtp", "mail", "submission", NULL},
		{"imap", "imap4", "mail", NULL},
		{"pop", "pop3", "mail", NULL}
	};
	/* FIXME - this list of uncommon server names probably needs to be extended
	 * format: mail domain (lower-case glob string); smtp; imap; pop3 servers */
	static const gchar * const specials[][4] = {
		/* Microsoft: for outlook.com and apparently also for old hotmail.com and live.com addresses */
		{"outlook.com", "outlook.office365.com", "outlook.office365.com", "outlook.office365.com"},
		{"hotmail.com", "outlook.office365.com", "outlook.office365.com", "outlook.office365.com"},
		{"live.com", "outlook.office365.com", "outlook.office365.com", "outlook.office365.com"},
		/* Yahoo: same servers for yahoo.com, yahoo.de, yahoo.co.uk... */
		{"yahoo.??*", "smtp.mail.yahoo.com", "imap.mail.yahoo.com", "pop.mail.yahoo.com"},
		/* T-Online: magenta is their corporate colour... */
		{"t-online.de", "securesmtp.t-online.de", "secureimap.t-online.de", "securepop.t-online.de"},
		{"magenta.de", "securesmtp.t-online.de", "secureimap.t-online.de", "securepop.t-online.de"}
	};
	GResolver *resolver;
	const gchar *at_sign;
	gchar *domain;
	gchar **result;
	size_t n;

	at_sign = strchr(mail_address, '@');
	g_return_val_if_fail(at_sign != NULL, NULL);

	result = g_new0(gchar *, 4UL);

	/* a few (?) mail providers are known to be special... */
	domain = g_ascii_strdown(&at_sign[1], -1);
	for (n = 0U; n < G_N_ELEMENTS(specials); n++) {
		if (g_pattern_match_simple(specials[n][0], domain)) {
			result[0] = g_strdup(specials[n][1]);
			result[1] = g_strdup(specials[n][2]);
			result[2] = g_strdup(specials[n][3]);
			g_free(domain);
			return result;
		}
	}

	/* try to resolve the proper servers */
	resolver = g_resolver_get_default();
	for (n = 0; n < 3; n++) {
		result[n] = server_from_srv(resolver, srv_names[n], domain);
		if (result[n] == NULL) {
			result[n] = server_by_name(resolver, dns_prefix[n], domain);
		}
	}
	g_object_unref(resolver);

	g_free(domain);

	return result;
}


/** @brief Try to look up a server by DNS SRV record
 *
 * @param[in] resolver resolver object
 * @param[in] service NULL-terminated array of services to look up
 * @param[in] domain email domain
 * @return the name of a suitable server on success, NULL if none could be found
 */
static gchar *
server_from_srv(GResolver *resolver, const gchar * const *service, const gchar *domain)
{
	int n;
	gchar *result = NULL;

	for (n = 0; (result == NULL) && (service[n] != NULL); n++) {
		GList *res;
		GError *error = NULL;

		res = g_resolver_lookup_service(resolver, service[n], "tcp", domain, NULL, &error);
		if (res != NULL) {
			GList *p;

			for (p = res; (result == NULL) && (p != NULL); p = p->next) {
				GSrvTarget *target = (GSrvTarget *) p->data;
				const gchar *host;
				guint16 port;

				host = g_srv_target_get_hostname(target);
				port = g_srv_target_get_port(target);
				if ((host != NULL) && (host[0] != '\0') && (port > 0U)) {
					result = g_strdup(host);
					g_debug("lookup for TCP service %s of domain %s: %s:%hu", service[n], domain, result, port);
				}
			}
			g_resolver_free_targets(res);
		} else if (error != NULL) {
			g_debug("lookup for TCP service %s of domain %s failed: %s", service[n], domain, error->message);
		}
		g_clear_error(&error);
	}

	return result;
}


/** @brief Try to look up a server by DNS name
 *
 * @param[in] resolver resolver object
 * @param[in] service NULL-terminated array of server names to look up
 * @param[in] domain email domain
 * @return the name of a suitable server on success, NULL if none could be found
 */
static gchar *
server_by_name(GResolver *resolver, const gchar * const *prefix, const gchar *domain)
{
	int n;
	gchar *fullname = NULL;

	for (n = 0; (fullname == NULL) && (prefix[n] != NULL); n++) {
		GList *res;
		GError *error = NULL;

		fullname = g_strconcat(prefix[n], ".", domain, NULL);
		res = g_resolver_lookup_by_name(resolver, fullname, NULL, &error);
		if (res == NULL) {
			g_debug("lookup of %s failed: %s", fullname, error->message);
			g_free(fullname);
			fullname = NULL;
		} else {
			g_debug("lookup of %s: success", fullname);
			g_resolver_free_addresses(res);
		}
		g_clear_error(&error);
	}

	return fullname;
}
