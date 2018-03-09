/* NetClient - simple line-based network client library
 *
 * Copyright (C) Albrecht Dreß <mailto:albrecht.dress@arcor.de> 2017
 *
 * This library is free software; you can redistribute it and/or modify it under the terms of
 * the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License along with this
 * library. If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <stdio.h>
#include <glib/gi18n.h>
#include "net-client-utils.h"
#include "net-client-pop.h"


struct _NetClientPopPrivate {
    NetClientCryptMode crypt_mode;
    gchar *apop_banner;
    guint auth_allowed[2];                      /** 0: encrypted, 1: unencrypted */
    gboolean can_pipelining;
    gboolean can_uidl;
    gboolean use_pipelining;
};


/* Note: the maximum line length of a message body downloaded from the POP3 server may be up to
   998 chars, excluding the terminating
 * CRLF, see RFC 5322, Sect. 2.1.1.  However, it also states that "Receiving implementations
 * would do well to handle an arbitrarily
 * large number of characters in a line for robustness sake", so we actually accept lines from
 * POP3 of unlimited length. */
#define MAX_POP_LINE_LEN                        0U
#define POP_DATA_BUF_SIZE                       4096U


/*lint -save -e9026		allow function-like macros, see MISRA C:2012, Directive 4.9 */
#define IS_ML_TERM(str)                         ((str[0] == '.') && (str[1] == '\0'))
/*lint -emacro(9079,POP_MSG_INFO) -emacro(9087,POP_MSG_INFO)
 * allow conversion of GList data pointer, MISRA C:2012, Rules 11.3, 11.5 */
#define POP_MSG_INFO(list)                              ((NetClientPopMessageInfo *) ((list)-> \
                                                                                      data))
/*lint -restore */


G_DEFINE_TYPE(NetClientPop, net_client_pop, NET_CLIENT_TYPE)


static void net_client_pop_dispose(GObject *object);
static void net_client_pop_finalise(GObject *object);
static void net_client_pop_get_capa(NetClientPop *client,
                                    guint        *auth_supported);
static gboolean net_client_pop_read_reply(NetClientPop *client,
                                          gchar       **reply,
                                          GError      **error);
static gboolean net_client_pop_uidl(NetClientPop *client,
                                    GList *const *msg_list,
                                    GError      **error);
static gboolean net_client_pop_starttls(NetClientPop *client,
                                        GError      **error);
static gboolean net_client_pop_execute(NetClientPop *client,
                                       const gchar  *request_fmt,
                                       gchar       **last_reply,
                                       GError      **error,
                                       ...)
G_GNUC_PRINTF(2, 5);
static gboolean net_client_pop_execute_sasl(NetClientPop *client,
                                            const gchar  *request_fmt,
                                            gchar       **challenge,
                                            GError      **error,
                                            ...);
static gboolean net_client_pop_auth(NetClientPop *client,
                                    const gchar  *user,
                                    const gchar  *passwd,
                                    guint         auth_supported,
                                    GError      **error);
static gboolean net_client_pop_auth_plain(NetClientPop *client,
                                          const gchar  *user,
                                          const gchar  *passwd,
                                          GError      **error);
static gboolean net_client_pop_auth_login(NetClientPop *client,
                                          const gchar  *user,
                                          const gchar  *passwd,
                                          GError      **error);
static gboolean net_client_pop_auth_user_pass(NetClientPop *client,
                                              const gchar  *user,
                                              const gchar  *passwd,
                                              GError      **error);
static gboolean net_client_pop_auth_apop(NetClientPop *client,
                                         const gchar  *user,
                                         const gchar  *passwd,
                                         GError      **error);
static gboolean net_client_pop_auth_cram(NetClientPop *client,
                                         GChecksumType chksum_type,
                                         const gchar  *user,
                                         const gchar  *passwd,
                                         GError      **error);
static gboolean net_client_pop_auth_gssapi(NetClientPop *client,
                                           const gchar  *user,
                                           GError      **error);
static gboolean net_client_pop_retr_msg(NetClientPop                  *client,
                                        const NetClientPopMessageInfo *info,
                                        NetClientPopMsgCb              callback,
                                        gpointer                       user_data,
                                        GError                       **error);


NetClientPop *
net_client_pop_new(const gchar       *host,
                   guint16            port,
                   NetClientCryptMode crypt_mode,
                   gboolean           use_pipelining)
{
    NetClientPop *client;

    g_return_val_if_fail((host != NULL) && (crypt_mode >= NET_CLIENT_CRYPT_ENCRYPTED) &&
                         (crypt_mode <= NET_CLIENT_CRYPT_NONE),
                         NULL);

    client = NET_CLIENT_POP(g_object_new(NET_CLIENT_POP_TYPE, NULL));
    if (client != NULL) {
        if (!net_client_configure(NET_CLIENT(client), host, port, MAX_POP_LINE_LEN, NULL)) {
            g_clear_object(&client);
        } else {
            client->priv->crypt_mode = crypt_mode;
            client->priv->use_pipelining = use_pipelining;
        }
    }

    return client;
}


gboolean
net_client_pop_allow_auth(NetClientPop *client,
                          gboolean      encrypted,
                          guint         allow_auth)
{
    /* paranoia check */
    g_return_val_if_fail(NET_IS_CLIENT_POP(client), FALSE);
    if (encrypted) {
        client->priv->auth_allowed[0] = allow_auth;
    } else {
        client->priv->auth_allowed[1] = allow_auth;
    }
    return TRUE;
}


gboolean
net_client_pop_connect(NetClientPop *client,
                       gchar       **greeting,
                       GError      **error)
{
    gchar *server_msg = NULL;
    guint auth_supported = 0U;
    gboolean result;

    /* paranoia checks */
    g_return_val_if_fail(NET_IS_CLIENT_POP(client), FALSE);

    /* establish connection, and immediately switch to TLS if required */
    result = net_client_connect(NET_CLIENT(client), error);
    if (result && (client->priv->crypt_mode == NET_CLIENT_CRYPT_ENCRYPTED)) {
        result = net_client_start_tls(NET_CLIENT(client), error);
    }

    /* get the greeting */
    if (result) {
        result = net_client_pop_read_reply(client, &server_msg, error);
    }

    /* extract the APOP banner */
    if (result) {
        const gchar *ang_open;

        ang_open = strchr(server_msg, '<');                     /*lint !e668 !e9034		server_msg
                                                                   cannot be NULL; accept char
                                                                   literal as int */
        if (ang_open != NULL) {
            const gchar *ang_close;

            ang_close = strchr(ang_open, '>');                  /*lint !e9034	accept char
                                                                   literal as int */
            if (ang_close != NULL) {
                /*lint -e{737,946,947,9029}	allowed exception according to MISRA Rules 18.2
                   and 18.3 */
                client->priv->apop_banner = g_strndup(ang_open, (ang_close - ang_open) + 1U);
                auth_supported = NET_CLIENT_POP_AUTH_APOP;
            }
        }
        if (greeting != NULL) {
            *greeting = g_strdup(server_msg);
        }
        g_free(server_msg);
    }

    /* perform STLS if required- note that some servers support STLS, but do not announce it.
        So just try... */
    if (result &&
        ((client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS) ||
         (client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS_OPT))) {
        result = net_client_pop_starttls(client, error);
        if (!result) {
            if (client->priv->crypt_mode == NET_CLIENT_CRYPT_STARTTLS_OPT) {
                result = TRUE;
                g_clear_error(error);
            }
        }
    }

    /* read the capabilities (which may be unsupported, so ignore any negative result) */
    if (result) {
        net_client_pop_get_capa(client, &auth_supported);
    }

    /* authenticate if we were successful so far */
    if (result) {
        gchar **auth_data;
        gboolean need_pwd;

        auth_data = NULL;
        need_pwd = (auth_supported & NET_CLIENT_POP_AUTH_NO_PWD) == 0U;
        g_debug("emit 'auth' signal for client %p", client);
        g_signal_emit_by_name(client, "auth", need_pwd, &auth_data);
        if ((auth_data != NULL) && (auth_data[0] != NULL)) {
            result = net_client_pop_auth(client,
                                         auth_data[0],
                                         auth_data[1],
                                         auth_supported,
                                         error);
            memset(auth_data[0], 0, strlen(auth_data[0]));
            if (auth_data[1] != NULL) {
                memset(auth_data[1], 0, strlen(auth_data[1]));
            }
        }
        g_strfreev(auth_data);
    }

    return result;
}


gboolean
net_client_pop_stat(NetClientPop *client,
                    gsize        *msg_count,
                    gsize        *mbox_size,
                    GError      **error)
{
    gboolean result;
    gchar *stat_buf;

    /* paranoia checks */
    g_return_val_if_fail(NET_IS_CLIENT_POP(client), FALSE);

    /* run the STAT command */
    result = net_client_pop_execute(client, "STAT", &stat_buf, error);
    if (result) {
        unsigned long count;
        unsigned long total_size;

        if (sscanf(stat_buf, "%lu %lu", &count, &total_size) == 2) {
            if (msg_count != NULL) {
                *msg_count = count;
            }
            if (mbox_size != NULL) {
                *mbox_size = total_size;
            }
        } else {
            result = FALSE;
            g_set_error(error,
                        NET_CLIENT_POP_ERROR_QUARK,
                        (gint) NET_CLIENT_ERROR_POP_PROTOCOL,
                        _("bad server reply: %s"),
                        stat_buf);
        }

        g_free(stat_buf);
    }

    return result;
}


gboolean
net_client_pop_list(NetClientPop *client,
                    GList       **msg_list,
                    gboolean      with_uid,
                    GError      **error)
{
    gboolean result;
    gboolean done;

    /* paranoia checks */
    g_return_val_if_fail(NET_IS_CLIENT_POP(client) && (msg_list != NULL), FALSE);

    *msg_list = NULL;

    /* run the LIST command */
    result = net_client_pop_execute(client, "LIST", NULL, error);
    done = FALSE;
    while (result && !done) {
        gchar *reply;

        result = net_client_read_line(NET_CLIENT(client), &reply, error);
        if (result) {
            if (IS_ML_TERM(reply)) {
                done = TRUE;
            } else {
                NetClientPopMessageInfo *info;

                info = g_new0(NetClientPopMessageInfo, 1U);
                *msg_list = g_list_prepend(*msg_list, info);
                if (sscanf(reply, "%u %" G_GSIZE_FORMAT, &info->id, &info->size) != 2) {
                    result = FALSE;
                    g_set_error(error,
                                NET_CLIENT_POP_ERROR_QUARK,
                                (gint) NET_CLIENT_ERROR_POP_PROTOCOL,
                                _("bad server reply"));
                }
            }
            g_free(reply);
        }
    }

    /* on success, turn the list into the order reported by the remote server */
    if (result) {
        *msg_list = g_list_reverse(*msg_list);
    }

    /* get all uid's if requested */
    if (result && with_uid && client->priv->can_uidl && (*msg_list != NULL)) {
        result = net_client_pop_uidl(client, msg_list, error);
    }

    if (!result) {
        /*lint -e{9074,9087}	accept sane pointer conversion */
        g_list_free_full(*msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
    }

    return result;
}


gboolean
net_client_pop_retr(NetClientPop     *client,
                    GList            *msg_list,
                    NetClientPopMsgCb callback,
                    gpointer          user_data,
                    GError          **error)
{
    gboolean result;
    gboolean pipelining;
    const GList *p;

    /* paranoia checks */
    g_return_val_if_fail(NET_IS_CLIENT_POP(
                             client) && (msg_list != NULL) && (callback != NULL), FALSE);

    /* pipelining: send all RETR commands */
    pipelining = client->priv->can_pipelining && client->priv->use_pipelining;
    if (pipelining) {
        GString *retr_buf;

        retr_buf = g_string_sized_new(10U * g_list_length(msg_list));
        for (p = msg_list; p != NULL; p = p->next) {
            g_string_append_printf(retr_buf, "RETR %u\r\n", POP_MSG_INFO(p)->id);
        }
        result =
            net_client_write_buffer(NET_CLIENT(client), retr_buf->str, retr_buf->len, error);
        (void) g_string_free(retr_buf, TRUE);
    } else {
        result = TRUE;
    }

    for (p = msg_list; result && (p != NULL); p = p->next) {
        const NetClientPopMessageInfo *info = POP_MSG_INFO(p);

        if (pipelining) {
            result = net_client_pop_read_reply(client, NULL, error);
        } else {
            result = net_client_pop_execute(client, "RETR %u", NULL, error, info->id);
        }
        if (result) {
            result = net_client_pop_retr_msg(client, info, callback, user_data, error);
        }
    }

    return result;
}


gboolean
net_client_pop_dele(NetClientPop *client,
                    GList        *msg_list,
                    GError      **error)
{
    gboolean result;
    gboolean pipelining;
    const GList *p;

    /* paranoia checks */
    g_return_val_if_fail(NET_IS_CLIENT_POP(client) && (msg_list != NULL), FALSE);

    /* pipelining: send all DELE commands */
    pipelining = client->priv->can_pipelining && client->priv->use_pipelining;
    if (pipelining) {
        GString *dele_buf;

        dele_buf = g_string_sized_new(10U * g_list_length(msg_list));
        for (p = msg_list; p != NULL; p = p->next) {
            g_string_append_printf(dele_buf, "DELE %u\r\n", POP_MSG_INFO(p)->id);
        }
        result =
            net_client_write_buffer(NET_CLIENT(client), dele_buf->str, dele_buf->len, error);
        (void) g_string_free(dele_buf, TRUE);
    } else {
        result = TRUE;
    }

    for (p = msg_list; result && (p != NULL); p = p->next) {
        const NetClientPopMessageInfo *info = POP_MSG_INFO(p);

        if (pipelining) {
            result = net_client_pop_read_reply(client, NULL, error);
        } else {
            result = net_client_pop_execute(client, "DELE %u", NULL, error, info->id);
        }
    }

    return result;

}


void
net_client_pop_msg_info_free(NetClientPopMessageInfo *info)
{
    if (info != NULL) {
        g_free(info->uid);
        g_free(info);
    }
}


/* == local functions
   ===========================================================================================================
 */

static void
net_client_pop_class_init(NetClientPopClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose = net_client_pop_dispose;
    gobject_class->finalize = net_client_pop_finalise;
}


static void
net_client_pop_init(NetClientPop *self)
{
    self->priv = g_new0(NetClientPopPrivate, 1U);
    self->priv->auth_allowed[0] = NET_CLIENT_POP_AUTH_ALL;
    self->priv->auth_allowed[1] = NET_CLIENT_POP_AUTH_SAFE;
}


static void
net_client_pop_dispose(GObject *object)
{
    const NetClientPop *client = NET_CLIENT_POP(object);
    const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_pop_parent_class);

    /* send the 'QUIT' command - no need to evaluate the reply or check for errors */
    if (net_client_is_connected(NET_CLIENT(client))) {
        (void) net_client_execute(NET_CLIENT(client), NULL, "QUIT", NULL);
    }

    (*parent_class->dispose)(object);
}


static void
net_client_pop_finalise(GObject *object)
{
    const NetClientPop *client = NET_CLIENT_POP(object);
    const GObjectClass *parent_class = G_OBJECT_CLASS(net_client_pop_parent_class);

    g_free(client->priv->apop_banner);
    g_free(client->priv);

    (*parent_class->finalize)(object);
}


/* Note: if supplied, reply is never NULL on success */
static gboolean
net_client_pop_read_reply(NetClientPop *client,
                          gchar       **reply,
                          GError      **error)
{
    gboolean result;
    gchar *reply_buf;

    result = net_client_read_line(NET_CLIENT(client), &reply_buf, error);
    if (result) {
        if (strncmp(reply_buf, "+OK", 3U) == 0) {
            if ((strlen(reply_buf) > 3U) && (reply != NULL)) {
                *reply = g_strdup(&reply_buf[4]);
            }
        } else if (strncmp(reply_buf, "-ERR", 4U) == 0) {
            if (strlen(reply_buf) > 4U) {
                g_set_error(error,
                            NET_CLIENT_POP_ERROR_QUARK,
                            (gint) NET_CLIENT_ERROR_POP_SERVER_ERR,
                            _("error: %s"),
                            &reply_buf[5]);
            } else {
                g_set_error(error,
                            NET_CLIENT_POP_ERROR_QUARK,
                            (gint) NET_CLIENT_ERROR_POP_SERVER_ERR,
                            _("error"));
            }
            result = FALSE;
        } else {
            /* unexpected server reply */
            g_set_error(error,
                        NET_CLIENT_POP_ERROR_QUARK,
                        (gint) NET_CLIENT_ERROR_POP_PROTOCOL,
                        _("bad server reply: %s"),
                        reply_buf);
            result = FALSE;
        }

        g_free(reply_buf);
    }

    return result;
}


/* note: if supplied, last_reply is never NULL on success */
static gboolean
net_client_pop_execute(NetClientPop *client,
                       const gchar  *request_fmt,
                       gchar       **last_reply,
                       GError      **error,
                       ...)
{
    va_list args;
    gboolean result;

    va_start(args, error);              /*lint !e413	a NULL error argument is irrelevant here
                                         */
    result = net_client_vwrite_line(NET_CLIENT(client), request_fmt, args, error);
    va_end(args);

    if (result) {
        result = net_client_pop_read_reply(client, last_reply, error);
    }

    return result;
}


static gboolean
net_client_pop_starttls(NetClientPop *client,
                        GError      **error)
{
    gboolean result;

    result = net_client_pop_execute(client, "STLS", NULL, error);
    if (result) {
        result = net_client_start_tls(NET_CLIENT(client), error);
    }

    return result;
}


static gboolean
net_client_pop_auth(NetClientPop *client,
                    const gchar  *user,
                    const gchar  *passwd,
                    guint         auth_supported,
                    GError      **error)
{
    gboolean result = FALSE;
    guint auth_mask;

    if (net_client_is_encrypted(NET_CLIENT(client))) {
        auth_mask = client->priv->auth_allowed[0] & auth_supported;
    } else {
        auth_mask = client->priv->auth_allowed[1] & auth_supported;
    }

    if (((auth_mask & NET_CLIENT_POP_AUTH_NO_PWD) == 0U) && (passwd == NULL)) {
        g_set_error(error, NET_CLIENT_POP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_POP_NO_AUTH,
                    _("password required"));
    } else {
        /* first try authentication methods w/o password, then safe ones, and finally the
           plain-text methods */
        if ((auth_mask & NET_CLIENT_POP_AUTH_GSSAPI) != 0U) {
            result = net_client_pop_auth_gssapi(client, user, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_CRAM_SHA1) != 0U) {
            result = net_client_pop_auth_cram(client, G_CHECKSUM_SHA1, user, passwd, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_CRAM_MD5) != 0U) {
            result = net_client_pop_auth_cram(client, G_CHECKSUM_MD5, user, passwd, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_APOP) != 0U) {
            result = net_client_pop_auth_apop(client, user, passwd, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_PLAIN) != 0U) {
            result = net_client_pop_auth_plain(client, user, passwd, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_USER_PASS) != 0U) {
            result = net_client_pop_auth_user_pass(client, user, passwd, error);
        } else if ((auth_mask & NET_CLIENT_POP_AUTH_LOGIN) != 0U) {
            result = net_client_pop_auth_login(client, user, passwd, error);
        } else {
            g_set_error(error, NET_CLIENT_POP_ERROR_QUARK, (gint) NET_CLIENT_ERROR_POP_NO_AUTH,
                        _("no suitable authentication mechanism"));
        }
    }

    return result;
}


static gboolean
net_client_pop_auth_plain(NetClientPop *client,
                          const gchar  *user,
                          const gchar  *passwd,
                          GError      **error)
{
    gboolean result;
    gchar *base64_buf;

    base64_buf = net_client_auth_plain_calc(user, passwd);
    if (base64_buf != NULL) {
        result = net_client_pop_execute_sasl(client, "AUTH PLAIN", NULL, error);
        if (result) {
            result = net_client_pop_execute(client, "%s", NULL, error, base64_buf);
        }
        memset(base64_buf, 0, strlen(base64_buf));
        g_free(base64_buf);
    } else {
        result = FALSE;
    }

    return result;
}


static gboolean
net_client_pop_auth_login(NetClientPop *client,
                          const gchar  *user,
                          const gchar  *passwd,
                          GError      **error)
{
    gboolean result;

    result = net_client_pop_execute_sasl(client, "AUTH LOGIN", NULL, error);
    if (result) {
        gchar *base64_buf;

        base64_buf = g_base64_encode((const guchar *) user, strlen(user));
        result = net_client_pop_execute_sasl(client, "%s", NULL, error, base64_buf);
        memset(base64_buf, 0, strlen(base64_buf));
        g_free(base64_buf);
        if (result) {
            base64_buf = g_base64_encode((const guchar *) passwd, strlen(passwd));
            result = net_client_pop_execute(client, "%s", NULL, error, base64_buf);
            memset(base64_buf, 0, strlen(base64_buf));
            g_free(base64_buf);
        }
    }

    return result;
}


static gboolean
net_client_pop_auth_user_pass(NetClientPop *client,
                              const gchar  *user,
                              const gchar  *passwd,
                              GError      **error)
{
    gboolean result;

    result = net_client_pop_execute(client, "USER %s", NULL, error, user);
    if (result) {
        result = net_client_pop_execute(client, "PASS %s", NULL, error, passwd);
    }

    return result;
}


static gboolean
net_client_pop_auth_apop(NetClientPop *client,
                         const gchar  *user,
                         const gchar  *passwd,
                         GError      **error)
{
    gboolean result;
    gchar *auth_buf;
    gchar *md5_buf;

    auth_buf = g_strconcat(client->priv->apop_banner, passwd, NULL);
    md5_buf = g_compute_checksum_for_string(G_CHECKSUM_MD5, auth_buf, -1);
    memset(auth_buf, 0, strlen(auth_buf));
    g_free(auth_buf);
    result = net_client_pop_execute(client, "APOP %s %s", NULL, error, user, md5_buf);
    memset(md5_buf, 0, strlen(md5_buf));
    g_free(md5_buf);

    return result;
}


static gboolean
net_client_pop_auth_cram(NetClientPop *client,
                         GChecksumType chksum_type,
                         const gchar  *user,
                         const gchar  *passwd,
                         GError      **error)
{
    gboolean result;
    gchar *challenge = NULL;

    result = net_client_pop_execute_sasl(client,
                                         "AUTH CRAM-%s",
                                         &challenge,
                                         error,
                                         net_client_chksum_to_str(chksum_type));
    if (result) {
        gchar *auth_buf;
        auth_buf = net_client_cram_calc(challenge, chksum_type, user, passwd);
        if (auth_buf != NULL) {
            result = net_client_pop_execute(client, "%s", NULL, error, auth_buf);
            memset(auth_buf, 0, strlen(auth_buf));
            g_free(auth_buf);
        } else {
            result = FALSE;
        }
    }
    g_free(challenge);

    return result;
}


#if defined(HAVE_GSSAPI)

static gboolean
net_client_pop_auth_gssapi(NetClientPop *client,
                           const gchar  *user,
                           GError      **error)
{
    NetClientGssCtx *gss_ctx;
    gboolean result = FALSE;

    gss_ctx =
        net_client_gss_ctx_new("pop", net_client_get_host(NET_CLIENT(client)), user, error);
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
                    /* split the initial auth command as the initial-response argument will
                       typically exceed the 255-octet limit on
                     * the length of a single command, see RFC 5034, Sect. 4 */
                    initial = FALSE;
                    result = net_client_pop_execute_sasl(client, "AUTH GSSAPI =", NULL, error);
                }
                if (result) {
                    result = net_client_pop_execute_sasl(client,
                                                         "%s",
                                                         &input_token,
                                                         error,
                                                         output_token);
                }
            }
            g_free(output_token);
        } while (result && (state == 0));

        if (state == 1) {
            output_token = net_client_gss_auth_finish(gss_ctx, input_token, error);
            if (output_token != NULL) {
                result = net_client_pop_execute(client, "%s", NULL, error, output_token);
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
net_client_pop_auth_gssapi(NetClientPop G_GNUC_UNUSED *client,
                           const gchar G_GNUC_UNUSED  *user,
                           GError G_GNUC_UNUSED      **error)
{
    g_assert_not_reached();                     /* this should never happen! */
    return FALSE;                                       /* never reached, make gcc happy */
}


#endif  /* HAVE_GSSAPI */


/* Note: if supplied, challenge is never NULL on success */
static gboolean
net_client_pop_execute_sasl(NetClientPop *client,
                            const gchar  *request_fmt,
                            gchar       **challenge,
                            GError      **error,
                            ...)
{
    va_list args;
    gboolean result;

    va_start(args, error);              /*lint !e413	a NULL error argument is irrelevant here
                                         */
    result = net_client_vwrite_line(NET_CLIENT(client), request_fmt, args, error);
    va_end(args);

    if (result) {
        gchar *reply_buf;

        result = net_client_read_line(NET_CLIENT(client), &reply_buf, error);
        if (result) {
            if (strncmp(reply_buf, "+ ", 2U) == 0) {
                if (challenge != NULL) {
                    *challenge = g_strdup(&reply_buf[2]);
                }
            } else {
                result = FALSE;
                g_set_error(error,
                            NET_CLIENT_POP_ERROR_QUARK,
                            (gint) NET_CLIENT_ERROR_POP_SERVER_ERR,
                            _("error: %s"),
                            reply_buf);
            }
            g_free(reply_buf);
        }
    }

    return result;
}


static void
net_client_pop_get_capa(NetClientPop *client,
                        guint        *auth_supported)
{
    gboolean result;
    gboolean done;

    /* clear all capability flags except APOP and send the CAPA command */
    *auth_supported = *auth_supported & NET_CLIENT_POP_AUTH_APOP;
    client->priv->can_pipelining = FALSE;
    result = net_client_pop_execute(client, "CAPA", NULL, NULL);

    /* evaluate the response */
    done = FALSE;
    while (result && !done) {
        gchar *reply;

        result = net_client_read_line(NET_CLIENT(client), &reply, NULL);
        if (result) {
            if (IS_ML_TERM(reply)) {
                done = TRUE;
            } else if (strcmp(reply, "USER") == 0) {
                *auth_supported |= NET_CLIENT_POP_AUTH_USER_PASS;
            } else if (strncmp(reply, "SASL ", 5U) == 0) {
                gchar **auth;
                guint n;

                auth = g_strsplit(&reply[5], " ", -1);
                for (n = 0U; auth[n] != NULL; n++) {
                    if (strcmp(auth[n], "PLAIN") == 0) {
                        *auth_supported |= NET_CLIENT_POP_AUTH_PLAIN;
                    } else if (strcmp(auth[n], "LOGIN") == 0) {
                        *auth_supported |= NET_CLIENT_POP_AUTH_LOGIN;
                    } else if (strcmp(auth[n], "CRAM-MD5") == 0) {
                        *auth_supported |= NET_CLIENT_POP_AUTH_CRAM_MD5;
                    } else if (strcmp(auth[n], "CRAM-SHA1") == 0) {
                        *auth_supported |= NET_CLIENT_POP_AUTH_CRAM_SHA1;
#if defined(HAVE_GSSAPI)
                    } else if (strcmp(auth[n], "GSSAPI") == 0) {
                        *auth_supported |= NET_CLIENT_POP_AUTH_GSSAPI;
#endif
                    } else {
                        /* other auth methods are ignored for the time being */
                    }
                }
                g_strfreev(auth);
            } else if (strcmp(reply, "PIPELINING") == 0) {
                client->priv->can_pipelining = TRUE;
            } else if (strcmp(reply, "UIDL") == 0) {
                client->priv->can_uidl = TRUE;
            } else {
                /* ignore this capability (see MISRA C:2012, Rule 15.7) */
            }

            g_free(reply);
        }
    }

    /* see RFC 1939, Sect. 4: if no other authentication method is supported explicitly (in
       particular no APOP), the server *must*
     * at least support USER/PASS... */
    if (*auth_supported == 0U) {
        *auth_supported = NET_CLIENT_POP_AUTH_USER_PASS;
    }
    client->priv->can_pipelining = TRUE;
}


static gboolean
net_client_pop_uidl(NetClientPop *client,
                    GList *const *msg_list,
                    GError      **error)
{
    gboolean result;
    gboolean done;
    const GList *p;

    result = net_client_pop_execute(client, "UIDL", NULL, error);
    done = FALSE;
    p = *msg_list;
    while (result && !done) {
        gchar *reply;

        result = net_client_read_line(NET_CLIENT(client), &reply, error);
        if (result) {
            if (IS_ML_TERM(reply)) {
                done = TRUE;
            } else {
                guint msg_id;
                gchar *endptr;

                msg_id = strtoul(reply, &endptr, 10);
                if (endptr[0] != ' ') {
                    result = FALSE;
                } else {
                    /* we assume the passed list is already in the proper order, re-scan it if
                       not */
                    if ((p == NULL) || (POP_MSG_INFO(p)->id != msg_id)) {
                        for (p = *msg_list;
                             (p != NULL) && (POP_MSG_INFO(p)->id != msg_id);
                             p = p->next) {
                            /* nothing to do (see MISRA C:2012, Rule 15.7) */
                        }
                    }
                    /* FIXME - error if we get a UID for a message which is not in the list? */
                    if (p != NULL) {
                        NetClientPopMessageInfo *info = POP_MSG_INFO(p);

                        g_free(info->uid);
                        info->uid = g_strdup(&endptr[1]);
                        p = p->next;
                    }
                }
            }
            g_free(reply);
        }
    }

    return result;
}


static gboolean
net_client_pop_retr_msg(NetClientPop                  *client,
                        const NetClientPopMessageInfo *info,
                        NetClientPopMsgCb              callback,
                        gpointer                       user_data,
                        GError                       **error)
{
    gboolean result;
    gboolean done;
    GString *msg_buf;
    gsize lines;

    result = TRUE;
    done = FALSE;
    msg_buf = g_string_sized_new(POP_DATA_BUF_SIZE);
    lines = 0U;
    while (!done && result) {
        gchar *linebuf;

        result = net_client_read_line(NET_CLIENT(client), &linebuf, error);
        if (result) {
            if (IS_ML_TERM(linebuf)) {
                done = TRUE;
            } else {
                if (linebuf[0] == '.') {
                    msg_buf = g_string_append(msg_buf, &linebuf[1]);
                } else {
                    msg_buf = g_string_append(msg_buf, linebuf);
                }
                msg_buf = g_string_append_c(msg_buf, '\n');
                lines++;

                /* pass an almost full buffer to the callback */
                if (msg_buf->len > (POP_DATA_BUF_SIZE - 100U)) {
                    result = callback(msg_buf->str,
                                      (gssize) msg_buf->len,
                                      lines,
                                      info,
                                      user_data,
                                      error);
                    msg_buf = g_string_truncate(msg_buf, 0U);
                    lines = 0U;
                }
            }
            g_free(linebuf);
        }
    }

    if (result) {
        if (msg_buf->len > 0U) {
            result =
                callback(msg_buf->str, (gssize) msg_buf->len, lines, info, user_data, error);
        }
        if (result) {
            result = callback(NULL, 0, 0U, info, user_data, error);
        }
    }

    if (!result) {
        (void) callback(NULL, -1, 0U, info, user_data, NULL);
    }
    (void) g_string_free(msg_buf, TRUE);

    return result;

}
