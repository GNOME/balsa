/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017 - 2020
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "net-client-smtp.h"


/*lint -esym(754,_NetClientSmtp::parent)	required field, not referenced directly */
struct _NetClientSmtp {
    NetClient parent;

	NetClientCryptMode crypt_mode;
	guint auth_enabled;					/* Note: 0 = anonymous connection w/o authentication */
	gboolean can_dsn;
	gboolean data_state;
};


struct _NetClientSmtpMessage {
	gchar *sender;
	GList *recipients;
	gchar *dsn_envid;
	gboolean dsn_ret_full;
	gboolean have_dsn_rcpt;
	NetClientSmtpSendCb data_callback;
	gpointer user_data;
};


typedef struct {
	gchar *rfc5321_addr;
	NetClientSmtpDsnMode dsn_mode;
} smtp_rcpt_t;


/** @name SMTP authentication methods
 * @{
 */
/** Anonymous access (no authentication) */
#define NET_CLIENT_SMTP_AUTH_NONE			0x01U
/** RFC 4616 "PLAIN" authentication method. */
#define NET_CLIENT_SMTP_AUTH_PLAIN			0x02U
/** "LOGIN" authentication method. */
#define NET_CLIENT_SMTP_AUTH_LOGIN			0x04U
/** RFC 2195 "CRAM-MD5" authentication method. */
#define NET_CLIENT_SMTP_AUTH_CRAM_MD5		0x08U
/** RFC xxxx "CRAM-SHA1" authentication method. */
#define NET_CLIENT_SMTP_AUTH_CRAM_SHA1		0x10U
/** RFC 4752 "GSSAPI" authentication method. */
#define NET_CLIENT_SMTP_AUTH_GSSAPI			0x20U


/** Mask of all authentication methods requiring user name and password. */
#define NET_CLIENT_SMTP_AUTH_PASSWORD		\
	(NET_CLIENT_SMTP_AUTH_PLAIN | NET_CLIENT_SMTP_AUTH_LOGIN | NET_CLIENT_SMTP_AUTH_CRAM_MD5 | NET_CLIENT_SMTP_AUTH_CRAM_SHA1)

/** Mask of all authentication methods. */
#define NET_CLIENT_SMTP_AUTH_ALL			\
	(NET_CLIENT_SMTP_AUTH_NONE | NET_CLIENT_SMTP_AUTH_PASSWORD | NET_CLIENT_SMTP_AUTH_GSSAPI)
/** @} */


/* Note: RFC 5321 defines a maximum line length of 512 octets, including the terminating CRLF.  However, RFC 4954, Sect. 4. defines
 * 12288 octets as safe maximum length for SASL authentication. */
#define MAX_SMTP_LINE_LEN			12288U
#define SMTP_DATA_BUF_SIZE			8192U


/*lint -esym(528,net_client_smtp_get_instance_private)		auto-generated function, not referenced */
G_DEFINE_TYPE(NetClientSmtp, net_client_smtp, NET_CLIENT_TYPE)


static void net_client_smtp_finalise(GObject *object);
static gboolean net_client_smtp_ehlo(NetClientSmtp *client, guint *auth_supported, gboolean *can_starttls, GError **error);
static gboolean net_client_smtp_starttls(NetClientSmtp *client, GError **error);
static gboolean net_client_smtp_execute(NetClientSmtp *client, const gchar *request_fmt, gint expect_code, gchar **last_reply,
	GError **error, ...)
	G_GNUC_PRINTF(2, 6);
static gboolean net_client_smtp_auth(NetClientSmtp *client, guint auth_supported, GError **error);
static gboolean net_client_smtp_auth_plain(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error);
static gboolean net_client_smtp_auth_login(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error);
static gboolean net_client_smtp_auth_cram(NetClientSmtp *client, GChecksumType chksum_type, const gchar *user, const gchar *passwd,
										  GError **error);
static gboolean net_client_smtp_auth_gssapi(NetClientSmtp *client, const gchar *user, GError **error);
static gboolean net_client_smtp_read_reply(NetClientSmtp *client, gint expect_code, gchar **last_reply, GError **error);
static gboolean net_client_smtp_eval_rescode(gint res_code, gint expect_code, const gchar *reply, GError **error);
static gchar *net_client_smtp_dsn_to_string(const NetClientSmtp *client, NetClientSmtpDsnMode dsn_mode);
static void smtp_rcpt_free(smtp_rcpt_t *rcpt);


NetClientSmtp *
net_client_smtp_new(const gchar *host, guint16 port, NetClientCryptMode crypt_mode)
{
	NetClientSmtp *client;

	g_return_val_if_fail((host != NULL) && (crypt_mode >= NET_CLIENT_CRYPT_ENCRYPTED) && (crypt_mode <= NET_CLIENT_CRYPT_NONE),
		NULL);

	client = NET_CLIENT_SMTP(g_object_new(NET_CLIENT_SMTP_TYPE, NULL));
	if (!net_client_configure(NET_CLIENT(client), host, port, MAX_SMTP_LINE_LEN, NULL)) {
		g_assert_not_reached();
	}
	client->crypt_mode = crypt_mode;

	return client;
}


gboolean
net_client_smtp_set_auth_mode(NetClientSmtp *client, NetClientAuthMode auth_mode)
{
	g_return_val_if_fail(NET_IS_CLIENT_SMTP(client), FALSE);

	client->auth_enabled = 0U;
	if ((auth_mode & NET_CLIENT_AUTH_NONE_ANON) != 0U) {
		client->auth_enabled |= NET_CLIENT_SMTP_AUTH_NONE;
	}
	if ((auth_mode & NET_CLIENT_AUTH_USER_PASS) != 0U) {
		client->auth_enabled |= NET_CLIENT_SMTP_AUTH_PASSWORD;
	}
#if defined(HAVE_GSSAPI)
	if ((auth_mode & NET_CLIENT_AUTH_KERBEROS) != 0U) {
		client->auth_enabled |= NET_CLIENT_SMTP_AUTH_GSSAPI;
	}
#endif
	return (client->auth_enabled != 0U);
}


gboolean
net_client_smtp_probe(const gchar *host, guint timeout_secs, NetClientProbeResult *result, GCallback cert_cb, GError **error)
{
	guint16 probe_ports[] = {465U, 587U, 25U, 0U};		/* submissions, submission, smtp */
	gchar *host_only;
	gboolean retval = FALSE;
	gint check_id;

	/* paranoia check */
	g_return_val_if_fail((host != NULL) && (result != NULL), FALSE);

	host_only = net_client_host_only(host);

	if (!net_client_host_reachable(host_only, error)) {
		g_free(host_only);
		return FALSE;
	}

	for (check_id = 0; !retval && (probe_ports[check_id] > 0U); check_id++) {
		NetClientSmtp *client;

		g_debug("%s: probing %s:%u…", __func__, host_only, probe_ports[check_id]);
		client = net_client_smtp_new(host_only, probe_ports[check_id], NET_CLIENT_CRYPT_NONE);
		net_client_set_timeout(NET_CLIENT(client), timeout_secs);
		if (net_client_connect(NET_CLIENT(client), NULL)) {
			gboolean this_success;
			guint auth_supported;
			gboolean can_starttls;

			if (cert_cb != NULL) {
				g_signal_connect(client, "cert-check", cert_cb, client);
			}
			if (check_id == 0) {	/* submissions */
				this_success = net_client_start_tls(NET_CLIENT(client), NULL);
			} else {
				this_success = TRUE;
			}

			/* get the greeting */
			if (this_success) {
				this_success = net_client_smtp_read_reply(client, 220, NULL, NULL);
			}

			/* send EHLO and read the capabilities of the server */
			if (this_success) {
				this_success = net_client_smtp_ehlo(client, &auth_supported, &can_starttls, NULL);
			}

			/* try to perform STARTTLS if supported, and send EHLO again */
			if (this_success && can_starttls) {
				can_starttls = net_client_smtp_starttls(client, NULL);
				if (can_starttls) {
					gboolean dummy;

					can_starttls = net_client_smtp_ehlo(client, &auth_supported, &dummy, NULL);
				}
			}

			/* evaluate on success */
			if (this_success) {
				result->port = probe_ports[check_id];

				if (check_id == 0) {
					result->crypt_mode = NET_CLIENT_CRYPT_ENCRYPTED;
				} else if (can_starttls) {
					result->crypt_mode = NET_CLIENT_CRYPT_STARTTLS;
				} else {
					result->crypt_mode = NET_CLIENT_CRYPT_NONE;
				}

				result->auth_mode = 0U;
				if ((auth_supported & NET_CLIENT_SMTP_AUTH_PASSWORD) != 0U) {
					result->auth_mode |= NET_CLIENT_AUTH_USER_PASS;
				}
				if ((auth_supported & NET_CLIENT_SMTP_AUTH_GSSAPI) != 0U) {
					result->auth_mode |= NET_CLIENT_AUTH_KERBEROS;
				}
				retval = TRUE;
			}
		}
		g_object_unref(client);
	}

	if (!retval) {
		g_set_error(error, NET_CLIENT_ERROR_QUARK, NET_CLIENT_PROBE_FAILED,
			_("the server %s does not offer the SMTP service at port 465, 587 or 25"), host_only);
	}

	g_free(host_only);

	return retval;
}


gboolean
net_client_smtp_connect(NetClientSmtp *client, gchar **greeting, GError **error)
{
	gboolean result;
	gboolean can_starttls = FALSE;
	guint auth_supported = 0U;

	/* paranoia checks */
	g_return_val_if_fail(NET_IS_CLIENT_SMTP(client), FALSE);

	/* establish connection, and immediately switch to TLS if required */
	result = net_client_connect(NET_CLIENT(client), error);
	if (result && (client->crypt_mode == NET_CLIENT_CRYPT_ENCRYPTED)) {
		result = net_client_start_tls(NET_CLIENT(client), error);
	}

	/* get the greeting, RFC 5321 requires status 220 */
	if (result) {
		(void) net_client_set_timeout(NET_CLIENT(client), 5U * 60U);	/* RFC 5321, Sect. 4.5.3.2.1.: 5 minutes timeout */
		result = net_client_smtp_read_reply(client, 220, greeting, error);
	}

	/* send EHLO and read the capabilities of the server */
	if (result) {
		result = net_client_smtp_ehlo(client, &auth_supported, &can_starttls, error);
	}

	/* perform STARTTLS if required, and send EHLO again */
	if (result &&
		((client->crypt_mode == NET_CLIENT_CRYPT_STARTTLS) || (client->crypt_mode == NET_CLIENT_CRYPT_STARTTLS_OPT))) {
		if (!can_starttls) {
			if (client->crypt_mode == NET_CLIENT_CRYPT_STARTTLS) {
				g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_STARTTLS,
					_("remote server does not support STARTTLS"));
				result = FALSE;
			}
		} else {
			result = net_client_smtp_starttls(client, error);
			if (result) {
				result = net_client_smtp_ehlo(client, &auth_supported, &can_starttls, error);
			}
		}
	}

	/* authenticate if we were successful so far, unless anonymous access is configured */
	if (result && ((client->auth_enabled & NET_CLIENT_SMTP_AUTH_NONE) == 0U)) {
		result = net_client_smtp_auth(client, auth_supported, error);
	}

	return result;
}


gboolean
net_client_smtp_can_dsn(NetClientSmtp *client)
{
	return NET_IS_CLIENT_SMTP(client) ? client->can_dsn : FALSE;
}


gboolean
net_client_smtp_send_msg(NetClientSmtp *client, const NetClientSmtpMessage *message, gchar **server_stat, GError **error)
{
	NetClient *netclient;
	gboolean result;
	const GList *rcpt;

	/* paranoia checks */
	g_return_val_if_fail(NET_IS_CLIENT_SMTP(client) && (message != NULL) && (message->sender != NULL) &&
		(message->recipients != NULL) && (message->data_callback != NULL), FALSE);

	/* set the RFC 5321 sender and recipient(s); Sect. 3.3 requires status 250 */
	netclient = NET_CLIENT(client);		/* convenience pointer */
	(void) net_client_set_timeout(netclient, 5U * 60U);	/* RFC 5321, Sect. 4.5.3.2.2., 4.5.3.2.3.: 5 minutes timeout */
	if (client->can_dsn && message->have_dsn_rcpt) {
		if (message->dsn_envid != NULL) {
			result = net_client_smtp_execute(client, "MAIL FROM:<%s> RET=%s ENVID=%s", 250, NULL, error, message->sender,
											 (message->dsn_ret_full) ? "FULL" : "HDRS", message->dsn_envid);
		} else {
			result = net_client_smtp_execute(client, "MAIL FROM:<%s> RET=%s", 250, NULL, error, message->sender,
											 (message->dsn_ret_full) ? "FULL" : "HDRS");
		}
	} else {
		result = net_client_smtp_execute(client, "MAIL FROM:<%s>", 250, NULL, error, message->sender);
	}
	rcpt = message->recipients;
	while (result && (rcpt != NULL)) {
		const smtp_rcpt_t *this_rcpt = (const smtp_rcpt_t *) rcpt->data;	/*lint !e9079 !e9087 (MISRA C:2012 Rules 11.3, 11.5) */
		gchar *dsn_opts;

		/* create the RFC 3461 DSN string */
		dsn_opts = net_client_smtp_dsn_to_string(client, this_rcpt->dsn_mode);
		result = net_client_smtp_execute(client, "RCPT TO:<%s>%s", 250, NULL, error, this_rcpt->rfc5321_addr, dsn_opts);
		g_free(dsn_opts);
		rcpt = rcpt->next;
	}

	/* initialise sending the message data; Sect. 3.3 requires status 354 */
	if (result) {
		(void) net_client_set_timeout(netclient, 2U * 60U);	/* RFC 5321, Sect. 4.5.3.2.4.: 2 minutes timeout */
		result = net_client_smtp_execute(client, "DATA", 354, NULL, error);
	}

	/* call the data callback until all data has been transmitted or an error occurs */
	if (result) {
		gchar buffer[SMTP_DATA_BUF_SIZE];
		gssize count;
		gchar last_char = '\0';

		(void) net_client_set_timeout(netclient, 3U * 60U);	/* RFC 5321, Sect. 4.5.3.2.5.: 3 minutes timeout */
		client->data_state = TRUE;
		do {
			count = message->data_callback(buffer, SMTP_DATA_BUF_SIZE, message->user_data, error);
			if (count < 0) {
				result = FALSE;
			} else if (count > 0) {
				result = net_client_write_buffer(netclient, buffer, (gsize) count, error);
				last_char = buffer[count - 1];
			} else {
				/* write termination */
				if (last_char == '\n') {
					result = net_client_write_buffer(netclient, ".\r\n", 3U, error);
				} else {
					result = net_client_write_buffer(netclient, "\r\n.\r\n", 5U, error);
				}
			}
		} while (result && (count > 0));
	}

	if (result) {
		(void) net_client_set_timeout(netclient, 10U * 60U);	/* RFC 5321, Sect 4.5.3.2.6.: 10 minutes timeout */
		result = net_client_smtp_read_reply(client, -1, server_stat, error);
		client->data_state = FALSE;
	}

	return result;
}


NetClientSmtpMessage *
net_client_smtp_msg_new(NetClientSmtpSendCb data_callback, gpointer user_data)
{
	NetClientSmtpMessage *smtp_msg;

	g_return_val_if_fail(data_callback != NULL, NULL);

	smtp_msg = g_new0(NetClientSmtpMessage, 1U);
	smtp_msg->data_callback = data_callback;
	smtp_msg->user_data = user_data;
	return smtp_msg;
}


gboolean
net_client_smtp_msg_set_dsn_opts(NetClientSmtpMessage *smtp_msg, const gchar *envid, gboolean ret_full)
{
	g_return_val_if_fail(smtp_msg != NULL, FALSE);

	g_free(smtp_msg->dsn_envid);
	if (envid != NULL) {
		smtp_msg->dsn_envid = g_strdup(envid);
	} else {
		smtp_msg->dsn_envid = NULL;
	}
	smtp_msg->dsn_ret_full = ret_full;
	return TRUE;
}


gboolean
net_client_smtp_msg_set_sender(NetClientSmtpMessage *smtp_msg, const gchar *rfc5321_sender)
{
	g_return_val_if_fail((smtp_msg != NULL) && (rfc5321_sender != NULL), FALSE);

	g_free(smtp_msg->sender);
	smtp_msg->sender = g_strdup(rfc5321_sender);
	return TRUE;
}


gboolean
net_client_smtp_msg_add_recipient(NetClientSmtpMessage *smtp_msg, const gchar *rfc5321_rcpt, NetClientSmtpDsnMode dsn_mode)
{
	smtp_rcpt_t *new_rcpt;

	g_return_val_if_fail((smtp_msg != NULL) && (rfc5321_rcpt != NULL), FALSE);
	new_rcpt = g_new0(smtp_rcpt_t, 1U);
	new_rcpt->rfc5321_addr = g_strdup(rfc5321_rcpt);
	new_rcpt->dsn_mode = dsn_mode;
	smtp_msg->recipients = g_list_append(smtp_msg->recipients, new_rcpt);
	if (dsn_mode != NET_CLIENT_SMTP_DSN_NEVER) {
		smtp_msg->have_dsn_rcpt = TRUE;
	}
	return TRUE;
}


void
net_client_smtp_msg_free(NetClientSmtpMessage *smtp_msg)
{
	if (smtp_msg != NULL) {
		g_free(smtp_msg->sender);
		g_free(smtp_msg->dsn_envid);
		/*lint -e{9074} -e{9087}	accept safe (and required) pointer conversion (MISRA C:2012 Rules 11.1, 11.3) */
		g_list_free_full(smtp_msg->recipients, (GDestroyNotify) smtp_rcpt_free);
		g_free(smtp_msg);
	}
}


/* == local functions =========================================================================================================== */

static void
net_client_smtp_class_init(NetClientSmtpClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

	gobject_class->finalize = net_client_smtp_finalise;
}


static void
net_client_smtp_init(NetClientSmtp *self)
{
	self->auth_enabled = NET_CLIENT_SMTP_AUTH_ALL;
}


static void
net_client_smtp_finalise(GObject *object)
{
	NetClientSmtp *client = NET_CLIENT_SMTP(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_smtp_parent_class);

	/* send the 'QUIT' command unless we are in 'DATA' state where the server will probably fail to reply - no need to evaluate the
	 * reply or check for errors */
	if (net_client_is_connected(NET_CLIENT(client)) && !client->data_state) {
		(void) net_client_execute(NET_CLIENT(client), NULL, "QUIT", NULL);
	}

	(*parent_class->finalize)(object);
}


static gboolean
net_client_smtp_starttls(NetClientSmtp *client, GError **error)
{
	gboolean result;

	/* RFC 3207, Sect. 4 requires status 220 */
	result = net_client_smtp_execute(client, "STARTTLS", 220, NULL, error);
	if (result) {
		result = net_client_start_tls(NET_CLIENT(client), error);
	}

	return result;
}


static gboolean
net_client_smtp_auth(NetClientSmtp *client, guint auth_supported, GError **error)
{
	gboolean result = FALSE;
	guint auth_mask;
	gchar **auth_data = NULL;

	/* calculate the possible authentication methods */
	auth_mask = client->auth_enabled & auth_supported;

	/* try, in this order, enabled modes: GSSAPI/Kerberos; user name and password */
	if (auth_mask == 0U) {
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_AUTH,
			_("no suitable authentication mechanism"));
	} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_GSSAPI) != 0U) {
		/* GSSAPI aka Kerberos authentication - user name required */
		g_signal_emit_by_name(client, "auth", NET_CLIENT_AUTH_KERBEROS, &auth_data);
		if ((auth_data == NULL) || (auth_data[0] == NULL)) {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_AUTH, _("user name required"));
		} else {
			result = net_client_smtp_auth_gssapi(client, auth_data[0], error);
		}
	} else {
		/* user name and password authentication methods */
		g_signal_emit_by_name(client, "auth", NET_CLIENT_AUTH_USER_PASS, &auth_data);
		if ((auth_data == NULL) || (auth_data[0] == NULL) || (auth_data[1] == NULL)) {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_AUTH,
				_("user name and password required"));
		} else {
			/* first check for safe (hashed) authentication methods, used plain-text ones if they are not supported */
			if ((auth_mask & NET_CLIENT_SMTP_AUTH_CRAM_SHA1) != 0U) {
				result = net_client_smtp_auth_cram(client, G_CHECKSUM_SHA1, auth_data[0], auth_data[1], error);
			} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_CRAM_MD5) != 0U) {
				result = net_client_smtp_auth_cram(client, G_CHECKSUM_MD5, auth_data[0], auth_data[1], error);
			} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_PLAIN) != 0U) {
				result = net_client_smtp_auth_plain(client, auth_data[0], auth_data[1], error);
			} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_LOGIN) != 0U) {
				result = net_client_smtp_auth_login(client, auth_data[0], auth_data[1], error);
			} else {
				g_assert_not_reached();
			}
		}
	}

	/* clean up any auth data */
	if (auth_data != NULL) {
		if (auth_data[0] != NULL) {
			net_client_free_authstr(auth_data[0]);
			net_client_free_authstr(auth_data[1]);
		}
		free(auth_data);
	}

	return result;
}


static gboolean
net_client_smtp_auth_plain(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error)
{
	gboolean result;
	gchar *base64_buf;

	base64_buf = net_client_auth_plain_calc(user, passwd);
	if (base64_buf != NULL) {
		/* RFC 4954, Sect. 6 requires status 235 */
		result = net_client_smtp_execute(client, "AUTH PLAIN %s", 235, NULL, error, base64_buf);
		net_client_free_authstr(base64_buf);
	} else {
		result = FALSE;
	}

	return result;
}


static gboolean
net_client_smtp_auth_login(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error)
{
	gboolean result;
	gchar *base64_buf;

	/* RFC 4954, Sect. 4 requires status 334 for the challenge; Sect. 6 requires status 235 */
	base64_buf = g_base64_encode((const guchar *) user, strlen(user));
	result = net_client_smtp_execute(client, "AUTH LOGIN %s", 334, NULL, error, base64_buf);
	net_client_free_authstr(base64_buf);
	if (result) {
		base64_buf = g_base64_encode((const guchar *) passwd, strlen(passwd));
		result = net_client_smtp_execute(client, "%s", 235, NULL, error, base64_buf);
		net_client_free_authstr(base64_buf);
	}

	return result;
}


static gboolean
net_client_smtp_auth_cram(NetClientSmtp *client, GChecksumType chksum_type, const gchar *user, const gchar *passwd, GError **error)
{
	gboolean result;
	gchar *challenge = NULL;

	/* RFC 4954, Sect. 4 requires status 334 for the challenge; Sect. 6 requires status 235 */
	result = net_client_smtp_execute(client, "AUTH CRAM-%s", 334, &challenge, error, net_client_chksum_to_str(chksum_type));
	if (result) {
		gchar *auth_buf;

		auth_buf = net_client_cram_calc(challenge, chksum_type, user, passwd);
		if (auth_buf != NULL) {
			result = net_client_smtp_execute(client, "%s", 235, NULL, error, auth_buf);
			net_client_free_authstr(auth_buf);
		} else {
			result = FALSE;
		}
	}
	g_free(challenge);

	return result;
}


#if defined(HAVE_GSSAPI)

static gboolean
net_client_smtp_auth_gssapi(NetClientSmtp *client, const gchar *user, GError **error)
{
	NetClientGssCtx *gss_ctx;
	gboolean result = FALSE;

	/* RFC 4954, Sect. 4 requires status 334 for the challenges; Sect. 6 requires status 235 */
	gss_ctx = net_client_gss_ctx_new("smtp", net_client_get_host(NET_CLIENT(client)), user, error);
	if (gss_ctx != NULL) {
		gint state;
		gboolean initial = TRUE;
		gchar *input_token = NULL;
		gchar *output_token = NULL;

		do {
			state = net_client_gss_auth_step(gss_ctx, input_token, &output_token, error);
			g_free(input_token);
			input_token = NULL;
			if (state >= 0) {
				if (initial) {
					result = net_client_smtp_execute(client, "AUTH GSSAPI %s", 334, &input_token, error, output_token);
					initial = FALSE;
				} else {
					result = net_client_smtp_execute(client, "%s", 334, &input_token, error, output_token);
				}
			}
			g_free(output_token);
		} while (result && (state == 0));

		if (state == 1) {
			output_token = net_client_gss_auth_finish(gss_ctx, input_token, error);
			if (output_token != NULL) {
			    result = net_client_smtp_execute(client, "%s", 235, NULL, error, output_token);
			    g_free(output_token);
			}
		}
		g_free(input_token);
		net_client_gss_ctx_free(gss_ctx);
	}

	return result;
}

#else

/*lint -e{715,818} */
static gboolean
net_client_smtp_auth_gssapi(NetClientSmtp G_GNUC_UNUSED *client, const gchar G_GNUC_UNUSED *user, GError G_GNUC_UNUSED **error)
{
	g_assert_not_reached();			/* this should never happen! */
	return FALSE;					/* never reached, make gcc happy */
}

#endif  /* HAVE_GSSAPI */


/* note: if supplied, last_reply is never NULL on success */
static gboolean
net_client_smtp_execute(NetClientSmtp *client, const gchar *request_fmt, gint expect_code, gchar **last_reply, GError **error, ...)
{
	va_list args;
	gboolean result;

	va_start(args, error);
	result = net_client_vwrite_line(NET_CLIENT(client), request_fmt, args, error);
	va_end(args);

	if (result) {
		result = net_client_smtp_read_reply(client, expect_code, last_reply, error);
	}

	return result;
}


static gboolean
net_client_smtp_ehlo(NetClientSmtp *client, guint *auth_supported, gboolean *can_starttls, GError **error)
{
	gboolean result;
	gboolean done;

	result = net_client_write_line(NET_CLIENT(client), "EHLO %s", error, g_get_host_name());

	/* clear all capability flags */
	*auth_supported = 0U;
	client->can_dsn = FALSE;
	*can_starttls = FALSE;

	/* evaluate the response */
	done = FALSE;
	while (result && !done) {
		gchar *reply;

		result = net_client_read_line(NET_CLIENT(client), &reply, error);
		if (result) {
			gint reply_code;
			gchar *endptr;

			reply_code = strtol(reply, &endptr, 10);
			if (reply_code != 250) {
				g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_PROTOCOL,
					_("bad server reply: %s"), reply);
				result = FALSE;
			} else {
				if (strcmp(&endptr[1], "DSN") == 0) {
					client->can_dsn = TRUE;
				} else if (strcmp(&endptr[1], "STARTTLS") == 0) {
					*can_starttls = TRUE;
				} else if ((strncmp(&endptr[1], "AUTH ", 5U) == 0) || (strncmp(&endptr[1], "AUTH=", 5U) == 0)) {
					gchar **auth;
					guint n;

					auth = g_strsplit(&endptr[6], " ", -1);
					for (n = 0U; auth[n] != NULL; n++) {
						if (strcmp(auth[n], "PLAIN") == 0) {
							*auth_supported |= NET_CLIENT_SMTP_AUTH_PLAIN;
						} else if (strcmp(auth[n], "LOGIN") == 0) {
							*auth_supported |= NET_CLIENT_SMTP_AUTH_LOGIN;
						} else if (strcmp(auth[n], "CRAM-MD5") == 0) {
							*auth_supported |= NET_CLIENT_SMTP_AUTH_CRAM_MD5;
						} else if (strcmp(auth[n], "CRAM-SHA1") == 0) {
							*auth_supported |= NET_CLIENT_SMTP_AUTH_CRAM_SHA1;
#if defined(HAVE_GSSAPI)
						} else if (strcmp(auth[n], "GSSAPI") == 0) {
							*auth_supported |= NET_CLIENT_SMTP_AUTH_GSSAPI;
#endif
						} else {
							/* other auth methods are ignored for the time being */
						}
					}
					g_strfreev(auth);
				} else {
					/* ignored (see MISRA C:2012, Rule 15.7) */
				}

				if (*endptr == ' ') {
					done = TRUE;
				}
			}

			g_free(reply);
		}
	}

	return result;
}


/* Note: according to RFC 5321, sect. 4.2, \em any reply may be multiline.  If supplied, last_reply is never NULL on success */
static gboolean
net_client_smtp_read_reply(NetClientSmtp *client, gint expect_code, gchar **last_reply, GError **error)
{
	gboolean done;
	gboolean result;

	done = FALSE;
	do {
		gchar *reply;
		GError *this_error = NULL;

		result = net_client_read_line(NET_CLIENT(client), &reply, &this_error);
		if (result) {
			gint this_rescode;

			this_rescode = strtol(reply, NULL, 10);
			result = net_client_smtp_eval_rescode(this_rescode, expect_code, reply, &this_error);

			if (!result) {
				if ((error != NULL) && (*error != NULL)) {
					g_prefix_error(&this_error, "%s ", (*error)->message);
					g_clear_error(error);
				}
				g_propagate_error(error, this_error);
			}

			if (expect_code == -1) {
				expect_code = this_rescode;
			}

			if ((strlen(reply) > 3UL) && (reply[3] == ' ')) {
				done = TRUE;
				if (last_reply != NULL) {
					*last_reply = g_strdup(&reply[4]);
				}
			}

			g_free(reply);
		} else {
			g_clear_error(error);
			g_propagate_error(error, this_error);
			done = TRUE;
		}
	} while (!done);

	return result;
}


static gboolean
net_client_smtp_eval_rescode(gint res_code, gint expect_code, const gchar *reply, GError **error)
{
	gboolean result;

	switch (res_code / 100) {
	case 2:
	case 3:
		if ((expect_code == -1) || (res_code == expect_code)) {
			result = TRUE;
		} else {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_TRANSIENT,
				_("unexpected reply: %s"), reply);
			result = FALSE;
		}
		break;
	case 4:
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_TRANSIENT,
			/* Translators: #1 SMTP (RFC 5321) error code; #2 error message */
			_("transient error %d: %s"), res_code, reply);
		result = FALSE;
		break;
	case 5:
		if ((res_code == 534) || (res_code == 535)) {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_AUTHFAIL,
				/* Translators: #1 SMTP (RFC 5321) error code; #2 error message */
				_("authentication failure %d: %s"), res_code, reply);
		} else {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_PERMANENT,
				/* Translators: #1 SMTP (RFC 5321) error code; #2 error message */
				_("permanent error %d: %s"), res_code, reply);
		}
		result = FALSE;
		break;
	default:
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_PROTOCOL,
			_("bad server reply: %s"), reply);
		result = FALSE;
		break;
	}

	return result;
}


static gchar *
net_client_smtp_dsn_to_string(const NetClientSmtp *client, NetClientSmtpDsnMode dsn_mode)
{
	gchar *result;

	/* create the RFC 3461 DSN string */
	if (client->can_dsn && (dsn_mode != NET_CLIENT_SMTP_DSN_NEVER)) {
		GString *dsn_buf;
		gsize start_len;

		dsn_buf = g_string_new(" NOTIFY=");
		start_len = dsn_buf->len;
		/*lint -save -e655 -e9027 -e9029	accept logical AND for enum, MISRA C:2012 Rules 10.1, 10.4 */
		if ((dsn_mode & NET_CLIENT_SMTP_DSN_DELAY) == NET_CLIENT_SMTP_DSN_DELAY) {
			dsn_buf = g_string_append(dsn_buf, "DELAY");
		}
		if ((dsn_mode & NET_CLIENT_SMTP_DSN_FAILURE) == NET_CLIENT_SMTP_DSN_FAILURE) {
			if (start_len != dsn_buf->len) {
				dsn_buf = g_string_append_c(dsn_buf, ',');
			}
			dsn_buf = g_string_append(dsn_buf, "FAILURE");
		}
		if ((dsn_mode & NET_CLIENT_SMTP_DSN_SUCCESS) == NET_CLIENT_SMTP_DSN_SUCCESS) {
			if (start_len != dsn_buf->len) {
				dsn_buf = g_string_append_c(dsn_buf, ',');
			}
			dsn_buf = g_string_append(dsn_buf, "SUCCESS");
		}
		/*lint -restore */
		result = g_string_free(dsn_buf, FALSE);
	} else {
		result = g_strdup("");
	}

	return result;
}


static void
smtp_rcpt_free(smtp_rcpt_t *rcpt)
{
	g_free(rcpt->rfc5321_addr);
	g_free(rcpt);
}
