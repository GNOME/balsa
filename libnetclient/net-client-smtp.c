/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "net-client-smtp.h"


struct _NetClientSmtpPrivate {
	NetClientCryptMode crypt_mode;
	guint auth_allowed[2];			/** 0: encrypted, 1: unencrypted */
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


/* Note: RFC 5321 defines a maximum line length of 512 octets, including the terminating CRLF.  However, RFC 4954, Sect. 4. defines
 * 12288 octets as safe maximum length for SASL authentication. */
#define MAX_SMTP_LINE_LEN			12288U
#define SMTP_DATA_BUF_SIZE			8192U


G_DEFINE_TYPE(NetClientSmtp, net_client_smtp, NET_CLIENT_TYPE)


static void net_client_smtp_finalise(GObject *object);
static gboolean net_client_smtp_ehlo(NetClientSmtp *client, guint *auth_supported, gboolean *can_starttls, GError **error);
static gboolean net_client_smtp_starttls(NetClientSmtp *client, GError **error);
static gboolean net_client_smtp_execute(NetClientSmtp *client, const gchar *request_fmt, gchar **last_reply, GError **error, ...)
	G_GNUC_PRINTF(2, 5);
static gboolean net_client_smtp_auth(NetClientSmtp *client, const gchar *user, const gchar *passwd, guint auth_supported,
									 GError **error);
static gboolean net_client_smtp_auth_plain(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error);
static gboolean net_client_smtp_auth_login(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error);
static gboolean net_client_smtp_auth_cram(NetClientSmtp *client, GChecksumType chksum_type, const gchar *user, const gchar *passwd,
										  GError **error);
static gboolean net_client_smtp_auth_gssapi(NetClientSmtp *client, const gchar *user, GError **error);
static gboolean net_client_smtp_read_reply(NetClientSmtp *client, gint expect_code, gchar **last_reply, GError **error);
static gboolean net_client_smtp_eval_rescode(gint res_code, const gchar *reply, GError **error);
static gchar *net_client_smtp_dsn_to_string(const NetClientSmtp *client, NetClientSmtpDsnMode dsn_mode);
static void smtp_rcpt_free(smtp_rcpt_t *rcpt);


NetClientSmtp *
net_client_smtp_new(const gchar *host, guint16 port, NetClientCryptMode crypt_mode)
{
	NetClientSmtp *client;

	g_return_val_if_fail((host != NULL) && (crypt_mode >= NET_CLIENT_CRYPT_ENCRYPTED) && (crypt_mode <= NET_CLIENT_CRYPT_NONE),
		NULL);

	client = NET_CLIENT_SMTP(g_object_new(NET_CLIENT_SMTP_TYPE, NULL));
	if (client != NULL) {
		if (!net_client_configure(NET_CLIENT(client), host, port, MAX_SMTP_LINE_LEN, NULL)) {
			g_object_unref(G_OBJECT(client));
			client = NULL;
		} else {
			client->priv->crypt_mode = crypt_mode;
		}
	}

	return client;
}


gboolean
net_client_smtp_allow_auth(NetClientSmtp *client, gboolean encrypted, guint allow_auth)
{
	/* paranoia check */
	g_return_val_if_fail(NET_IS_CLIENT_SMTP(client), FALSE);
	if (encrypted) {
		client->priv->auth_allowed[0] = allow_auth;
	} else {
		client->priv->auth_allowed[1] = allow_auth;
	}
	return TRUE;
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
	if (result && (client->priv->crypt_mode == NET_CLIENT_CRYPT_ENCRYPTED)) {
		result = net_client_start_tls(NET_CLIENT(client), error);
	}

	/* get the greeting */
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
		((client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS) || (client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS_OPT))) {
		if (!can_starttls) {
			if (client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS) {
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

	/* authenticate if we were successful so far */
	if (result) {
		gchar **auth_data;
		gboolean need_pwd;

		auth_data = NULL;
		need_pwd = (auth_supported & NET_CLIENT_SMTP_AUTH_NO_PWD) == 0U;
		g_debug("emit 'auth' signal for client %p", client);
		g_signal_emit_by_name(client, "auth", need_pwd, &auth_data);
		if ((auth_data != NULL) && (auth_data[0] != NULL)) {
			result = net_client_smtp_auth(client, auth_data[0], auth_data[1], auth_supported, error);
			net_client_free_authstr(auth_data[0]);
			net_client_free_authstr(auth_data[1]);
		}
		g_free(auth_data);
	}

	return result;
}


gboolean
net_client_smtp_can_dsn(NetClientSmtp *client)
{
	return NET_IS_CLIENT_SMTP(client) ? client->priv->can_dsn : FALSE;
}


gboolean
net_client_smtp_send_msg(NetClientSmtp *client, const NetClientSmtpMessage *message, GError **error)
{
	NetClient *netclient;
	gboolean result;
	const GList *rcpt;

	/* paranoia checks */
	g_return_val_if_fail(NET_IS_CLIENT_SMTP(client) && (message != NULL) && (message->sender != NULL) &&
		(message->recipients != NULL) && (message->data_callback != NULL), FALSE);

	/* set the RFC 5321 sender and recipient(s) */
	netclient = NET_CLIENT(client);		/* convenience pointer */
	(void) net_client_set_timeout(netclient, 5U * 60U);	/* RFC 5321, Sect. 4.5.3.2.2., 4.5.3.2.3.: 5 minutes timeout */
	if (client->priv->can_dsn && message->have_dsn_rcpt) {
		if (message->dsn_envid != NULL) {
			result = net_client_smtp_execute(client, "MAIL FROM:<%s> RET=%s ENVID=%s", NULL, error, message->sender,
											 (message->dsn_ret_full) ? "FULL" : "HDRS", message->dsn_envid);
		} else {
			result = net_client_smtp_execute(client, "MAIL FROM:<%s> RET=%s", NULL, error, message->sender,
											 (message->dsn_ret_full) ? "FULL" : "HDRS");
		}
	} else {
		result = net_client_smtp_execute(client, "MAIL FROM:<%s>", NULL, error, message->sender);
	}
	rcpt = message->recipients;
	while (result && (rcpt != NULL)) {
		const smtp_rcpt_t *this_rcpt = (const smtp_rcpt_t *) rcpt->data;	/*lint !e9079 !e9087 (MISRA C:2012 Rules 11.3, 11.5) */
		gchar *dsn_opts;

		/* create the RFC 3461 DSN string */
		dsn_opts = net_client_smtp_dsn_to_string(client, this_rcpt->dsn_mode);
		result = net_client_smtp_execute(client, "RCPT TO:<%s>%s", NULL, error, this_rcpt->rfc5321_addr, dsn_opts);
		g_free(dsn_opts);
		rcpt = rcpt->next;
	}

	/* initialise sending the message data */
	if (result) {
		(void) net_client_set_timeout(netclient, 2U * 60U);	/* RFC 5321, Sect. 4.5.3.2.4.: 2 minutes timeout */
		result = net_client_smtp_execute(client, "DATA", NULL, error);
	}

	/* call the data callback until all data has been transmitted or an error occurs */
	if (result) {
		gchar buffer[SMTP_DATA_BUF_SIZE];
		gssize count;
		gchar last_char = '\0';

		(void) net_client_set_timeout(netclient, 3U * 60U);	/* RFC 5321, Sect. 4.5.3.2.5.: 3 minutes timeout */
		client->priv->data_state = TRUE;
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
		result = net_client_smtp_read_reply(client, -1, NULL, error);
		client->priv->data_state = FALSE;
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
		/*lint -e{9074} -e{9087}	accept safe (and required) pointer conversion */
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
	self->priv = g_new0(NetClientSmtpPrivate, 1U);
	self->priv->auth_allowed[0] = NET_CLIENT_SMTP_AUTH_ALL;
	self->priv->auth_allowed[1] = NET_CLIENT_SMTP_AUTH_SAFE;
}


static void
net_client_smtp_finalise(GObject *object)
{
	const NetClientSmtp *client = NET_CLIENT_SMTP(object);
	const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_smtp_parent_class);

	/* send the 'QUIT' command unless we are in 'DATA' state where the server will probably fail to reply - no need to evaluate the
	 * reply or check for errors */
	if (net_client_is_connected(NET_CLIENT(client)) && !client->priv->data_state) {
		(void) net_client_execute(NET_CLIENT(client), NULL, "QUIT", NULL);
	}

	g_free(client->priv);
	(*parent_class->finalize)(object);
}


static gboolean
net_client_smtp_starttls(NetClientSmtp *client, GError **error)
{
	gboolean result;

	result = net_client_smtp_execute(client, "STARTTLS", NULL, error);
	if (result) {
		result = net_client_start_tls(NET_CLIENT(client), error);
	}

	return result;
}


static gboolean
net_client_smtp_auth(NetClientSmtp *client, const gchar *user, const gchar *passwd, guint auth_supported, GError **error)
{
	gboolean result = FALSE;
	guint auth_mask;

	/* calculate the possible authentication methods */
	if (net_client_is_encrypted(NET_CLIENT(client))) {
		auth_mask = client->priv->auth_allowed[0] & auth_supported;
	} else {
		auth_mask = client->priv->auth_allowed[1] & auth_supported;
	}

	if (((auth_mask & NET_CLIENT_SMTP_AUTH_NO_PWD) == 0U) && (passwd == NULL)) {
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_AUTH, _("password required"));
	} else {
		/* first try authentication methods w/o password, then safe ones, and finally the plain-text methods */
		if ((auth_mask & NET_CLIENT_SMTP_AUTH_GSSAPI) != 0U) {
			result = net_client_smtp_auth_gssapi(client, user, error);
		} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_CRAM_SHA1) != 0U) {
			result = net_client_smtp_auth_cram(client, G_CHECKSUM_SHA1, user, passwd, error);
		} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_CRAM_MD5) != 0U) {
			result = net_client_smtp_auth_cram(client, G_CHECKSUM_MD5, user, passwd, error);
		} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_PLAIN) != 0U) {
			result = net_client_smtp_auth_plain(client, user, passwd, error);
		} else if ((auth_mask & NET_CLIENT_SMTP_AUTH_LOGIN) != 0U) {
			result = net_client_smtp_auth_login(client, user, passwd, error);
		} else {
			g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_NO_AUTH,
				_("no suitable authentication mechanism"));
		}
	}

	return result;
}


static gboolean
net_client_smtp_auth_plain(NetClientSmtp *client, const gchar *user, const gchar *passwd, GError **error)
{
	gboolean result ;
	gchar *base64_buf;

	base64_buf = net_client_auth_plain_calc(user, passwd);
	if (base64_buf != NULL) {
		result = net_client_smtp_execute(client, "AUTH PLAIN %s", NULL, error, base64_buf);
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

	base64_buf = g_base64_encode((const guchar *) user, strlen(user));
	result = net_client_smtp_execute(client, "AUTH LOGIN %s", NULL, error, base64_buf);
	net_client_free_authstr(base64_buf);
	if (result) {
		base64_buf = g_base64_encode((const guchar *) passwd, strlen(passwd));
		result = net_client_smtp_execute(client, "%s", NULL, error, base64_buf);
		net_client_free_authstr(base64_buf);
	}

	return result;
}


static gboolean
net_client_smtp_auth_cram(NetClientSmtp *client, GChecksumType chksum_type, const gchar *user, const gchar *passwd, GError **error)
{
	gboolean result;
	gchar *challenge = NULL;

	result = net_client_smtp_execute(client, "AUTH CRAM-%s", &challenge, error, net_client_chksum_to_str(chksum_type));
	if (result) {
		gchar *auth_buf;

		auth_buf = net_client_cram_calc(challenge, chksum_type, user, passwd);
		if (auth_buf != NULL) {
			result = net_client_smtp_execute(client, "%s", NULL, error, auth_buf);
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
					result = net_client_smtp_execute(client, "AUTH GSSAPI %s", &input_token, error, output_token);
					initial = FALSE;
				} else {
					result = net_client_smtp_execute(client, "%s", &input_token, error, output_token);
				}
			}
			g_free(output_token);
		} while (result && (state == 0));

		if (state == 1) {
			output_token = net_client_gss_auth_finish(gss_ctx, input_token, error);
			if (output_token != NULL) {
			    result = net_client_smtp_execute(client, "%s", NULL, error, output_token);
			    g_free(output_token);
			}
		}
		g_free(input_token);
		net_client_gss_ctx_free(gss_ctx);
	}

	return result;
}

#else

/*lint -e{715} -e{818} */
static gboolean
net_client_smtp_auth_gssapi(NetClientSmtp G_GNUC_UNUSED *client, const gchar G_GNUC_UNUSED *user, GError G_GNUC_UNUSED **error)
{
	g_assert_not_reached();			/* this should never happen! */
	return FALSE;					/* never reached, make gcc happy */
}

#endif  /* HAVE_GSSAPI */


/* note: if supplied, last_reply is never NULL on success */
static gboolean
net_client_smtp_execute(NetClientSmtp *client, const gchar *request_fmt, gchar **last_reply, GError **error, ...)
{
	va_list args;
	gboolean result;

	va_start(args, error);		/*lint !e413	a NULL error argument is irrelevant here */
	result = net_client_vwrite_line(NET_CLIENT(client), request_fmt, args, error);
	va_end(args);

	if (result) {
		result = net_client_smtp_read_reply(client, -1, last_reply, error);
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
	client->priv->can_dsn = FALSE;
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
					client->priv->can_dsn = TRUE;
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
	gint rescode;
	gboolean done;
	gboolean result;

	done = FALSE;
	rescode = expect_code;
	do {
		gchar *reply;

		result = net_client_read_line(NET_CLIENT(client), &reply, error);
		if (result) {
			gint this_rescode;
			gchar *endptr;

			this_rescode = strtol(reply, &endptr, 10);
			if (rescode == -1) {
				rescode = this_rescode;
				result = net_client_smtp_eval_rescode(rescode, reply, error);
			} else if (rescode != this_rescode) {
				g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_PROTOCOL,
					_("bad server reply: %s"), reply);
				result = FALSE;
			} else {
				/* nothing to do (see MISRA C:2012, Rule 15.7) */
			}
			if (reply[3] == ' ') {
				done = TRUE;
				if (last_reply != NULL) {
					*last_reply = g_strdup(&reply[4]);
				}
			}

			g_free(reply);
		}
	} while (result && !done);

	return result;
}


static gboolean
net_client_smtp_eval_rescode(gint res_code, const gchar *reply, GError **error)
{
	gboolean result;

	switch (res_code / 100) {
	case 2:
	case 3:
		result = TRUE;
		break;
	case 4:
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_TRANSIENT,
			_("transient error %d: %s"), res_code, reply);
		result = FALSE;
		break;
	case 5:
		g_set_error(error, NET_CLIENT_SMTP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_SMTP_PERMANENT,
			_("permanent error %d: %s"), res_code, reply);
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
	if (client->priv->can_dsn && (dsn_mode != NET_CLIENT_SMTP_DSN_NEVER)) {
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
