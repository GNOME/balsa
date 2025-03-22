/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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


#include <glib.h>
#include <string.h>
#include <math.h>

#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "net-client-pop.h"
#include "server.h"
#include "misc.h"
#include "mailbox.h"
#include "mailbox_pop3.h"
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "mbox-pop3"

enum {
    LAST_SIGNAL
};

struct _LibBalsaMailboxPOP3 {
    LibBalsaMailboxRemote mailbox;

    gboolean check;
    gboolean delete_from_server;
    gchar *filter_cmd;
    LibBalsaMailbox *inbox;
    gint msg_size_limit;
    gboolean filter; /* filter through procmail/filter_cmd? */
    gboolean disable_apop; /* Some servers claim to support it but
                              * they do not. */
    gboolean enable_pipe;  /* ditto */
};

static void libbalsa_mailbox_pop3_finalize(GObject * object);

static gboolean libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox,
					   GError **err);
static void libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_pop3_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);


#define MBOX_POP3_ERROR 	(g_quark_from_static_string("mailbox-pop3"))

G_DEFINE_TYPE(LibBalsaMailboxPOP3, libbalsa_mailbox_pop3, LIBBALSA_TYPE_MAILBOX_REMOTE)

static void
libbalsa_mailbox_pop3_class_init(LibBalsaMailboxPOP3Class * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    object_class->finalize = libbalsa_mailbox_pop3_finalize;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_pop3_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_pop3_check;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_pop3_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_pop3_load_config;

}

static void
libbalsa_mailbox_pop3_init(LibBalsaMailboxPOP3 * mailbox_pop3)
{
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(mailbox_pop3);
    LibBalsaServer *server;

    mailbox_pop3->check = FALSE;
    mailbox_pop3->delete_from_server = FALSE;
    mailbox_pop3->inbox = NULL;
    mailbox_pop3->msg_size_limit = -1;

    mailbox_pop3->filter = FALSE;
    mailbox_pop3->filter_cmd = g_strdup("procmail -f -");

    server = libbalsa_server_new();
    libbalsa_server_set_protocol(server, "pop3");
    libbalsa_mailbox_remote_set_server(remote, server);
}

static void
libbalsa_mailbox_pop3_finalize(GObject * object)
{
    LibBalsaMailboxPOP3 *mailbox_pop3 = LIBBALSA_MAILBOX_POP3(object);

    g_free(mailbox_pop3->filter_cmd);

    G_OBJECT_CLASS(libbalsa_mailbox_pop3_parent_class)->finalize(object);
}

LibBalsaMailboxPOP3*
libbalsa_mailbox_pop3_new(void)
{
    LibBalsaMailboxPOP3 *mailbox_pop3;

    mailbox_pop3 = g_object_new(LIBBALSA_TYPE_MAILBOX_POP3, NULL);

    return mailbox_pop3;
}


static gboolean
libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox, GError **err)
{
    /* FIXME: it should never be called. */

    g_debug("Opened a POP3 mailbox!");

    return TRUE;
}


#define POP_UID_FILE	"pop-uids"
static GMutex uid_mutex;


static GHashTable *
mp_load_uids(const gchar *prefix)
{
	GHashTable *res;
	gboolean read_res;
	gchar *fname;
	gchar *contents;

	res = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	fname = g_build_filename(g_get_user_state_dir(), "balsa", POP_UID_FILE, NULL);
	g_mutex_lock(&uid_mutex);
	read_res = g_file_get_contents(fname, &contents, NULL, NULL);
	g_mutex_unlock(&uid_mutex);
	g_free(fname);

	if (read_res) {
		gchar **lines;
		size_t prefix_len;
		guint n;

		lines = g_strsplit(contents, "\n", -1);
		g_free(contents);
		prefix_len = strlen(prefix);
		for (n = 0; lines[n] != NULL; n++) {
			if (strncmp(lines[n], prefix, prefix_len) == 0) {
				g_hash_table_insert(res, g_strdup(lines[n]), GINT_TO_POINTER(1));
			}
		}

		g_strfreev(lines);
	}

	return res;
}


static void
mp_save_uid(gpointer key, gpointer G_GNUC_UNUSED value, gpointer user_data)
{
	FILE *out = (FILE *) user_data;

	fputs((const gchar *) key, out);
	fputc('\n', out);
}


static gboolean
mp_save_uids(GHashTable *uids, const gchar *prefix, GError **error)
{
	gchar *fname;
	gboolean read_res;
	gchar *contents = NULL;
	FILE *out;
	gboolean result;

	fname = g_build_filename(g_get_user_state_dir(), "balsa", POP_UID_FILE, NULL);

	g_mutex_lock(&uid_mutex);
	read_res = g_file_get_contents(fname, &contents, NULL, NULL);
	out = fopen(fname, "w");
	g_free(fname);
	if (out != NULL) {
		if (read_res) {
			gchar **lines;
			size_t prefix_len;
			guint n;

			lines = g_strsplit(contents, "\n", -1);
			g_free(contents);
			prefix_len = strlen(prefix);
			for (n = 0; lines[n] != NULL; n++) {
				if ((lines[n][0] != '\0') && (strncmp(lines[n], prefix, prefix_len) != 0)) {
					fputs(lines[n], out);
					fputc('\n', out);
				}
			}

			g_strfreev(lines);
		}

		if (uids != NULL) {
			g_hash_table_foreach(uids, mp_save_uid, out);
		}
		fclose(out);
		result = TRUE;
	} else {
		g_set_error(error, MBOX_POP3_ERROR, errno, _("Saving the POP3 message UID list failed: %s"), g_strerror(errno));
		result = FALSE;
	}

	g_mutex_unlock(&uid_mutex);
	return result;
}


#ifdef POP_SYNC
static int
dump_cb(unsigned len, char *buf, void *arg)
{
    /* FIXME: Bad things happen in the mh driver when messages are empty. */
    return fwrite(buf, 1, len, (FILE*)arg) == len;
}
#endif /* POP_SYNC */


typedef struct {
	gboolean filter;
	GMimeStream *mbx_stream;	/* used if we store directly to a mailbox only */
	FILE *filter_pipe;			/* used of we write to a filter pipe only */
	gchar *path;				/* needed for error reporting only */
} pop_handler_t;


static pop_handler_t *
pop_handler_new(const gchar *filter_path,
				GError     **error)
{
	pop_handler_t *res;

	res = g_new0(pop_handler_t, 1U);
	if (filter_path != NULL) {
		res->filter = TRUE;
		res->filter_pipe = popen(filter_path, "w");
		if (res->filter_pipe == NULL) {
			g_set_error(error, MBOX_POP3_ERROR, errno, _("Passing POP message to %s failed: %s"), filter_path, g_strerror(errno));
			g_free(res);
			res = NULL;
		} else {
			res->path = g_strdup(filter_path);
		}
	} else {
		res->mbx_stream = g_mime_stream_mem_new();
	}

	return res;
}


static gboolean
pop_handler_write(pop_handler_t *handler,
				  const gchar   *buffer,
				  gsize          count,
				  GError       **error)
{
	gboolean result = TRUE;

	if (handler->filter) {
		if (fwrite(buffer, 1U, count, handler->filter_pipe) != count) {
			g_set_error(error, MBOX_POP3_ERROR, errno, _("Passing POP message to %s failed: %s"), handler->path, g_strerror(errno));
			result = FALSE;
		}
	} else {
		if (g_mime_stream_write(handler->mbx_stream, buffer, count) != (ssize_t) count) {
			g_set_error(error, MBOX_POP3_ERROR, -1, _("Saving POP message failed"));
			result = FALSE;
		}
	}
	return result;
}


static gboolean
pop_handler_close(pop_handler_t *handler,
				  GError       **error)
{
	gboolean result = TRUE;

	if (handler->filter) {
		int res;

		res = pclose(handler->filter_pipe);
		if (res != 0) {
			g_set_error(error, MBOX_POP3_ERROR, errno, _("Transferring POP message to %s failed: %s"), handler->path,
				g_strerror(errno));
			result = FALSE;
		}
	} else {
		g_object_unref(handler->mbx_stream);
	}
	g_free(handler->path);
	g_free(handler);
	return result;
}


/* ===================================================================
   Functions supporting asynchronous retrieval of messages.
*/
struct fetch_data {
    LibBalsaMailbox *mailbox;
    const gchar *filter_path;				/* filter path, NULL for storing the message without filtering */
    gsize total_messages;
    gsize total_size;
    gchar *total_size_msg;
    gsize msgno;
    gsize received;
    pop_handler_t *handler;
    gint64 next_notify;
};

static void
notify_progress(const struct fetch_data *fd)
{
    gdouble fraction;
	gchar *recvbuf;

	fraction = (gdouble) fd->received / (gdouble) fd->total_size;
	recvbuf = libbalsa_size_to_gchar(fd->received);
	libbalsa_mailbox_progress_notify(fd->mailbox, LIBBALSA_NTFY_UPDATE, fraction, _("Message %lu of %lu (%s of %s)"),
		(unsigned long) fd->msgno, (unsigned long) fd->total_messages, recvbuf, fd->total_size_msg);
	g_free(recvbuf);
}

static gboolean
message_cb(const gchar                    *buffer,
		   gssize                          count,
		   gsize                           lines,
		   const NetClientPopMessageInfo  *info,
		   gpointer                        user_data,
		   GError                        **error)
{
	struct fetch_data *fd = (struct fetch_data *) user_data;
	gboolean result = TRUE;

	if (count > 0) {
		/* message data chunk - initialise for a new message if the output does not exist */
		if (fd->handler == NULL) {
			fd->handler = pop_handler_new(fd->filter_path, error);

			if (fd->handler == NULL) {
				result = FALSE;
			} else {
				fd->msgno++;
				fd->next_notify = g_get_monotonic_time();		/* force immediate update of the progress dialogue */
			}
		}

		/* add data if we were successful so far */
		if (result) {
			result = pop_handler_write(fd->handler, buffer, count, error);
			if (result) {
				gint64 current_time;

				current_time = g_get_monotonic_time();
				/* as the cr is removed from line endings, we have to compensate for it - see RFC 1939, sect. 11 */
				fd->received += (gsize) count + lines;
				if (current_time >= fd->next_notify) {
					notify_progress(fd);
					fd->next_notify = current_time + 500000;
				}
			}
		}
	} else if (count == 0) {
		gboolean close_res;

		notify_progress(fd);
		if (fd->filter_path == NULL) {
                    GError *add_err = NULL;
                    LibBalsaMailbox *mailbox;
                    LibBalsaMailbox *inbox;

                    mailbox = fd->mailbox;
                    inbox = LIBBALSA_MAILBOX_POP3(mailbox)->inbox;

		    g_mime_stream_reset(fd->handler->mbx_stream);
		    result =
                        libbalsa_mailbox_add_message(inbox, fd->handler->mbx_stream,
                                                     LIBBALSA_MESSAGE_FLAG_NEW |
                                                     LIBBALSA_MESSAGE_FLAG_RECENT,
                                                     &add_err);
		    if (!result) {
		        libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("Error appending message %d from %s to %s: %s"),
		        	info->id,
                                libbalsa_mailbox_get_name(mailbox),
                                libbalsa_mailbox_get_name(inbox),
                                add_err != NULL ? add_err->message : "?");
		        g_clear_error(&add_err);
		    }
		}

		/* current message done */
		close_res = pop_handler_close(fd->handler, error);
		fd->handler = NULL;
		result = close_res & result;
	} else {
		/* count < 0: error; note that the handler may already be NULL if the error occurred for count == 0 */
		if (fd->handler != NULL) {
			(void) pop_handler_close(fd->handler, NULL);
			fd->handler = NULL;
		}
		result = FALSE;
	}

	return result;
}


static NetClientPop *
libbalsa_mailbox_pop3_startup(LibBalsaServer      *server,
                              LibBalsaMailboxPOP3 *mailbox_pop3,
                              const gchar         *name,
                              GList              **msg_list)
{
        LibBalsaMailbox *mailbox = (LibBalsaMailbox *) mailbox_pop3;
        NetClientCryptMode security;
        const gchar *host;
	NetClientPop *pop;
	GError *error = NULL;

	/* create the mailbox connection */
        security = libbalsa_server_get_security(server);
        host = libbalsa_server_get_host(server);
	if (security == NET_CLIENT_CRYPT_ENCRYPTED) {
		pop = net_client_pop_new(host, 995U, security, mailbox_pop3->enable_pipe);
	} else {
		pop = net_client_pop_new(host, 110U, security, mailbox_pop3->enable_pipe);
	}
	if (pop == NULL) {
		return NULL;
	}

	/* configure the mailbox connection; allow configured auth methods */
	net_client_pop_set_auth_mode(pop, libbalsa_server_get_auth_mode(server), mailbox_pop3->disable_apop);
	net_client_set_timeout(NET_CLIENT(pop), 60U);

	/* load client certificate if configured */
	if (libbalsa_server_get_client_cert(server)) {
                const gchar *cert_file = libbalsa_server_get_cert_file(server);

		g_signal_connect(pop, "cert-pass", G_CALLBACK(libbalsa_server_get_cert_pass), server);
		if (!net_client_set_cert_from_file(NET_CLIENT(pop), cert_file, &error)) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Cannot load certificate file %s: %s"), cert_file,
				error->message);
			/* bad certificate private key password: clear it */
			if (error->code == NET_CLIENT_ERROR_CERT_KEY_PASS) {
				libbalsa_server_set_password(server, NULL, TRUE);
			}
			g_error_free(error);
			g_object_unref(pop);
			return NULL;
		}
	}

	/* connect signals */
	g_signal_connect(pop, "cert-check", G_CALLBACK(libbalsa_server_check_cert), pop);
	g_signal_connect(pop, "auth", G_CALLBACK(libbalsa_server_get_auth), server);

	/* connect server */
	libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_INIT, INFINITY, _("Connecting %s…"), host);
	if (!net_client_pop_connect(pop, NULL, &error)) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("POP3 mailbox %s: cannot connect %s: %s"), name, host,
			error->message);
		/* authentication failed: clear password */
		if (error->code == NET_CLIENT_ERROR_POP_AUTHFAIL) {
			libbalsa_server_set_password(server, NULL, FALSE);
		}
		g_error_free(error);
		net_client_shutdown(NET_CLIENT(pop));
		g_object_unref(pop);
		return NULL;
	}

	/* load message list */
	libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_UPDATE, INFINITY, _("List messages…"));
	if (!net_client_pop_list(pop, msg_list, !mailbox_pop3->delete_from_server, &error)) {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("POP3 mailbox %s error: %s"), name, error->message);
		g_error_free(error);
		net_client_shutdown(NET_CLIENT(pop));
		g_object_unref(pop);
		pop = NULL;
	}

	return pop;
}


static GList *
update_msg_list(struct fetch_data   *fd,
                LibBalsaMailboxPOP3 *mailbox_pop3,
                GHashTable         **current_uids,
                LibBalsaServer      *server,
                GList               *msg_list)
{
	GHashTable *uids = NULL;
	gchar *uid_prefix = NULL;
	size_t prefix_len = 0U;
	GList *p;

	/* load uid's if messages shall be left on the server */
	if (!mailbox_pop3->delete_from_server) {
		uid_prefix = g_strconcat(libbalsa_server_get_user(server), "@",
                                         libbalsa_server_get_host(server), NULL);
		prefix_len = strlen(uid_prefix);
		uids = mp_load_uids(uid_prefix);
		*current_uids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	}

	/* calculate totals, remove oversized messages, remember in the hash if required */
	fd->total_messages = 0U;
	fd->total_size = 0U;
	p = msg_list;
	while (p != NULL) {
		NetClientPopMessageInfo *msg_info = (NetClientPopMessageInfo*) p->data;
		gboolean skip = FALSE;
		GList* next = p->next;

		/* check for oversized message */
		if ((mailbox_pop3->msg_size_limit > 0) &&
                        (msg_info->size >= (gsize) mailbox_pop3->msg_size_limit)) {
			gchar *size_str;

			size_str = libbalsa_size_to_gchar(msg_info->size);
			libbalsa_information(LIBBALSA_INFORMATION_WARNING, _("POP3 mailbox %s: message %d oversized: %s — skipped."),
				libbalsa_mailbox_get_name(LIBBALSA_MAILBOX(mailbox_pop3)), msg_info->id, size_str);
			g_free(size_str);
			skip = TRUE;
		}

		/* check if we already know this message */
		if (!skip && !mailbox_pop3->delete_from_server) {
			gchar *full_uid = g_strconcat(uid_prefix, " ", msg_info->uid, NULL);

			g_hash_table_insert(*current_uids, full_uid, GINT_TO_POINTER(1));
			if (g_hash_table_lookup(uids, full_uid) != NULL) {
				skip = TRUE;
			}
		}

		/* delete from list if we want to skip the message, update totals otherwise */
		if (skip) {
			net_client_pop_msg_info_free(msg_info);
			msg_list = g_list_delete_link(msg_list, p);
		} else {
			fd->total_messages++;
			fd->total_size += msg_info->size;
		}
		p = next;
	}

	/* copy all keys /not/ starting with the prefix from the old to the current hash table, and drop the old table */
	if (!mailbox_pop3->delete_from_server && (msg_list != NULL)) {
		GHashTableIter iter;
		gpointer key;

		g_hash_table_iter_init(&iter, uids);
                g_assert(uid_prefix != NULL);  /* Avoid a false positive in scan-build */
		while (g_hash_table_iter_next(&iter, &key, NULL)) {
			if (strncmp((const char *) key, uid_prefix, prefix_len) != 0) {
				g_hash_table_insert(*current_uids, key, GINT_TO_POINTER(1));
			}
		}
	}

	g_free(uid_prefix);
	if (uids != NULL) {
		g_hash_table_destroy(uids);
	}

	return msg_list;
}


static void
libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox)
{
	LibBalsaMailboxPOP3 *mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
	LibBalsaServer *server;
	NetClientPop *pop;
	GList *msg_list;

	if (!mailbox_pop3->check || (mailbox_pop3->inbox == NULL)) {
		return;
	}

	server = LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox_pop3);

	/* open the mailbox connection and get the messages list (note: initiates the progress dialogue) */
	pop = libbalsa_mailbox_pop3_startup(server, mailbox_pop3,
                                            libbalsa_mailbox_get_name(mailbox), &msg_list);

	/* proceed on success only */
	if (pop != NULL) {
		struct fetch_data fd;
		GHashTable *current_uids = NULL;
		gboolean result = TRUE;
		GError *err = NULL;

		libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_UPDATE, INFINITY,
			_("Connected to %s"), net_client_get_host(NET_CLIENT(pop)));
		memset(&fd, 0, sizeof(fd));

		/* nothing to do if no messages are on the server */
		if (msg_list != NULL) {
			/* load uid's if messages shall be left on the server */
			msg_list = update_msg_list(&fd, mailbox_pop3, &current_uids,
                                                   server, msg_list);
		}

		/* download messages unless the list is empty */
		if (fd.total_messages > 0U) {
			fd.mailbox = mailbox;
			fd.total_size_msg = libbalsa_size_to_gchar(fd.total_size);

			libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_UPDATE, 0.0,
				ngettext("%lu new message (%s)", "%lu new messages (%s)", fd.total_messages),
				(unsigned long) fd.total_messages, fd.total_size_msg);

			if (mailbox_pop3->filter) {
				fd.filter_path = mailbox_pop3->filter_cmd;
			}

			if (result) {
				result = net_client_pop_retr(pop, msg_list, message_cb, &fd, &err);
				if (result && mailbox_pop3->delete_from_server) {
					libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_UPDATE, INFINITY,
						_("Deleting messages on server…"));
					result = net_client_pop_dele(pop, msg_list, &err);
				}
			}

			/* clean up */
			g_free(fd.total_size_msg);
			g_list_free_full(msg_list, (GDestroyNotify) net_client_pop_msg_info_free);
		}

		/* store uid list */
		if (result && !mailbox_pop3->delete_from_server) {
			gchar *uid_prefix =
                            g_strconcat(libbalsa_server_get_user(server), "@",
                                        libbalsa_server_get_host(server), NULL);

			mp_save_uids(current_uids, uid_prefix, &err);
			g_free(uid_prefix);
			if (current_uids != NULL) {
				g_hash_table_destroy(current_uids);
			}
		}

		if (!result) {
			net_client_shutdown(NET_CLIENT(pop));
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("POP3 mailbox %s error: %s"),
				libbalsa_mailbox_get_name(mailbox), err != NULL ? err->message : "?");
			if (err != NULL)
				g_error_free(err);
		}

		/* done - clean up */
		g_object_unref(pop);
	}

	libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_FINISHED, 1.0, _("Finished"));
}


static void
libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxPOP3 *mailbox_pop3;

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox));

    mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_conf_set_bool("Check", mailbox_pop3->check);
    libbalsa_conf_set_bool("Delete", mailbox_pop3->delete_from_server);
    libbalsa_conf_set_bool("DisableApop", mailbox_pop3->disable_apop);
    libbalsa_conf_set_bool("EnablePipe", mailbox_pop3->enable_pipe);
    libbalsa_conf_set_bool("Filter", mailbox_pop3->filter);
    if (mailbox_pop3->filter_cmd != NULL)
        libbalsa_conf_set_string("FilterCmd", mailbox_pop3->filter_cmd);

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_pop3_parent_class)->save_config != NULL)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_pop3_parent_class)->save_config(mailbox, prefix);

}

static void
libbalsa_mailbox_pop3_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxPOP3 *mailbox_pop3;

    mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_server_load_config(LIBBALSA_MAILBOX_REMOTE_GET_SERVER(mailbox));

    mailbox_pop3->check = libbalsa_conf_get_bool("Check=false");
    mailbox_pop3->delete_from_server = libbalsa_conf_get_bool("Delete=false");
    mailbox_pop3->disable_apop = libbalsa_conf_get_bool("DisableApop=false");
    mailbox_pop3->enable_pipe = libbalsa_conf_get_bool("EnablePipe=false");
    mailbox_pop3->filter = libbalsa_conf_get_bool("Filter=false");

    g_free(mailbox_pop3->filter_cmd);
    mailbox_pop3->filter_cmd = libbalsa_conf_get_string("FilterCmd");
    if (mailbox_pop3->filter_cmd != NULL && mailbox_pop3->filter_cmd[0] == '\0') {
	g_free(mailbox_pop3->filter_cmd);
        mailbox_pop3->filter_cmd = NULL;
    }

    if (LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_pop3_parent_class)->load_config != NULL)
	LIBBALSA_MAILBOX_CLASS(libbalsa_mailbox_pop3_parent_class)->load_config(mailbox, prefix);

}
void
libbalsa_mailbox_pop3_set_inbox(LibBalsaMailbox *mailbox,
                                LibBalsaMailbox *inbox)
{
    LibBalsaMailboxPOP3 *mailbox_pop3;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));
    g_return_if_fail(LIBBALSA_IS_MAILBOX(inbox));

    mailbox_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);

    mailbox_pop3->inbox=inbox;
}

/*
 * Getters
 */

gboolean
libbalsa_mailbox_pop3_get_delete_from_server(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), FALSE);

    return mailbox_pop3->delete_from_server;
}

gboolean
libbalsa_mailbox_pop3_get_check(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), FALSE);

    return mailbox_pop3->check;
}

gboolean
libbalsa_mailbox_pop3_get_filter(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), FALSE);

    return mailbox_pop3->filter;
}

const gchar *
libbalsa_mailbox_pop3_get_filter_cmd(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), NULL);

    return mailbox_pop3->filter_cmd;
}

gboolean
libbalsa_mailbox_pop3_get_disable_apop(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), FALSE);

    return mailbox_pop3->disable_apop;
}

gboolean
libbalsa_mailbox_pop3_get_enable_pipe(LibBalsaMailboxPOP3 *mailbox_pop3)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3), FALSE);

    return mailbox_pop3->enable_pipe;
}

/*
 * Setters
 */

void
libbalsa_mailbox_pop3_set_msg_size_limit(LibBalsaMailboxPOP3 *mailbox_pop3,
                                         gint sz_limit)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->msg_size_limit = sz_limit;
}

void
libbalsa_mailbox_pop3_set_check(LibBalsaMailboxPOP3 *mailbox_pop3,
                                gboolean check)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->check = check;
}

void
libbalsa_mailbox_pop3_set_disable_apop(LibBalsaMailboxPOP3 *mailbox_pop3,
                                       gboolean disable_apop)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->disable_apop = disable_apop;
}

void
libbalsa_mailbox_pop3_set_delete_from_server(LibBalsaMailboxPOP3 *mailbox_pop3,
                                             gboolean delete_from_server)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->delete_from_server = delete_from_server;
}

void
libbalsa_mailbox_pop3_set_filter(LibBalsaMailboxPOP3 *mailbox_pop3,
                                 gboolean filter)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->filter = filter;
}

void
libbalsa_mailbox_pop3_set_filter_cmd(LibBalsaMailboxPOP3 *mailbox_pop3,
                                     const gchar * filter_cmd)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    g_free(mailbox_pop3->filter_cmd);
    mailbox_pop3->filter_cmd = g_strdup(filter_cmd);
}

void
libbalsa_mailbox_pop3_set_enable_pipe(LibBalsaMailboxPOP3 *mailbox_pop3,
                                      gboolean enable_pipe)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox_pop3));

    mailbox_pop3->enable_pipe = enable_pipe;
}
