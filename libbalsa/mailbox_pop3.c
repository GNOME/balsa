/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */


#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>
#include <string.h>

#include "libbalsa.h"
#include "libbalsa-conf.h"
#include "pop3.h"
#include "server.h"
#include "misc.h"
#include "mailbox.h"
#include "mailbox_pop3.h"
#include <glib/gi18n.h>

int PopDebug = 0;

enum {
    LAST_SIGNAL
};
static LibBalsaMailboxClass *parent_class = NULL;

struct _LibBalsaMailboxPop3Class {
    LibBalsaMailboxRemoteClass klass;

    void (*config_changed) (LibBalsaMailboxPop3* mailbox);
};

static void libbalsa_mailbox_pop3_finalize(GObject * object);
static void libbalsa_mailbox_pop3_class_init(LibBalsaMailboxPop3Class *
					     klass);
static void libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 * mailbox);

static gboolean libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox,
					   GError **err);
static void libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox);

static void libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_pop3_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

GType
libbalsa_mailbox_pop3_get_type(void)
{
    static GType mailbox_type = 0;

    if (!mailbox_type) {
	static const GTypeInfo mailbox_info = {
	    sizeof(LibBalsaMailboxPop3Class),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
	    (GClassInitFunc) libbalsa_mailbox_pop3_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
	    sizeof(LibBalsaMailboxPop3),
            0,                  /* n_preallocs */
	    (GInstanceInitFunc) libbalsa_mailbox_pop3_init
	};

	mailbox_type =
	    g_type_register_static(LIBBALSA_TYPE_MAILBOX_REMOTE,
                                   "LibBalsaMailboxPOP3",
			           &mailbox_info, 0);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_pop3_class_init(LibBalsaMailboxPop3Class * klass)
{
    GObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = G_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = g_type_class_peek_parent(klass);

    object_class->finalize = libbalsa_mailbox_pop3_finalize;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_pop3_open;
    libbalsa_mailbox_class->check = libbalsa_mailbox_pop3_check;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_pop3_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_pop3_load_config;

}

static void
libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 * mailbox)
{
    LibBalsaMailboxRemote *remote;
    mailbox->check = FALSE;
    mailbox->delete_from_server = FALSE;
    mailbox->inbox = NULL;
    mailbox->msg_size_limit = -1;

    mailbox->filter = FALSE;
    mailbox->filter_cmd = NULL;
    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server = libbalsa_server_new();
}

static void
libbalsa_mailbox_pop3_finalize(GObject * object)
{
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(object);

    g_object_unref(G_OBJECT(remote->server));

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

LibBalsaMailboxPop3*
libbalsa_mailbox_pop3_new(void)
{
    LibBalsaMailboxPop3 *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_POP3, NULL);

    return mailbox;
}


static gboolean
libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox, GError **err)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox), FALSE);

    /* FIXME: it should never be called. */

    g_print("Opened a POP3 mailbox!\n");

    return TRUE;
}

static gboolean
move_from_msgdrop(const gchar *tmp_path, LibBalsaMailbox *dest,
                  gchar *pop_name, unsigned msgno)
{
    gint fd;
    GMimeStream *stream;
    gboolean success;
    GError *err = NULL;
    struct stat buf;

    if(stat(tmp_path, &buf) != 0)
        return FALSE;
    if(buf.st_size == 0) /* filtered out? Nothing to move in any case */
        return TRUE;
    fd = open(tmp_path, O_RDWR);
    stream = g_mime_stream_fs_new(fd);
    success =
        libbalsa_mailbox_add_message(dest, stream,
                                     LIBBALSA_MESSAGE_FLAG_NEW |
                                     LIBBALSA_MESSAGE_FLAG_RECENT, &err)
        && ftruncate(fd, 0) == 0;
    g_object_unref(stream);

    if(!success)
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error appending message %d from %s to %s: %s"),
                             msgno, pop_name, dest->name,
                             err ? err->message : "?");
    g_clear_error(&err);

    return success;
}


static GHashTable*
mp_load_uids(void)
{
    GHashTable *res = g_hash_table_new_full(g_str_hash, g_str_equal,
                                            g_free, NULL);
    gchar *fname = g_strconcat(g_get_home_dir(), "/.balsa/pop-uids", NULL);
    FILE *f = fopen(fname, "r");
    g_free(fname);
    if(f) {
        struct flock lck;
        char line[1024]; /* arbitrary limit of uid len */
        memset (&lck, 0, sizeof(struct flock));
        lck.l_type = F_RDLCK; lck.l_whence = SEEK_SET;
        fcntl(fileno(f), F_SETLK, &lck);
        while(fgets(line, sizeof(line), f)) {
            int len = strlen(line);
            if(len>0&& line[len-1] == '\n') line[len-1] = '\0';
            g_hash_table_insert(res, g_strdup(line), GINT_TO_POINTER(1));
        }
        lck.l_type = F_UNLCK;
        fcntl(fileno(f), F_SETLK, &lck);
        fclose(f);
    }
    return res;
}

struct save_uid_data {
    FILE *file;
    const char *exclude_prefix;
};

static void
mp_save_uid(gpointer key, gpointer value, gpointer user_data)
{
    struct save_uid_data *d = (struct save_uid_data*)user_data;
    if(d->exclude_prefix &&
       strncmp(key, d->exclude_prefix, strlen(d->exclude_prefix)) == 0)
        return;
    fprintf(d->file, "%s\n", (char*)key);
}

static void
mp_save_uids(GHashTable *old_uids, GHashTable *new_uids, const char *prefix)
{
    gchar *fname = g_strconcat(g_get_home_dir(), "/.balsa/pop-uids", NULL);
    FILE *f;

    libbalsa_assure_balsa_dir();
    f = fopen(fname, "w");
    g_free(fname);
    if(f) {
        struct save_uid_data data;
        struct flock lck;

        memset (&lck, 0, sizeof (struct flock));
        lck.l_type = F_WRLCK; lck.l_whence = SEEK_SET;
        data.file = f; data.exclude_prefix = prefix;
        g_hash_table_foreach(old_uids, mp_save_uid, &data);
        data.exclude_prefix = NULL;
        g_hash_table_foreach(new_uids, mp_save_uid, &data);
        lck.l_type = F_UNLCK;
        fcntl(fileno(f), F_SETLK, &lck);
        fclose(f);
    } /* else COULD NOT SAVE UIDS! SHOUT! */
    return;
}

#ifdef POP_SYNC
static int
dump_cb(unsigned len, char *buf, void *arg)
{
    /* FIXME: Bad things happen in the mh driver when messages are empty. */
    return fwrite(buf, 1, len, (FILE*)arg) == len;
}
#endif /* POP_SYNC */
/* ===================================================================
   Functions for direct or filtered saving to the output mailbox.
*/
static FILE*
pop_direct_open(const char *path)
{
    return fopen(path, "w");
}

static FILE*
pop_filter_open(const char *command)
{
    return popen(command, "w");
}

struct PopDownloadMode {
    FILE* (*open)(const char *path);
    int (*close)(FILE *arg);
} pop_direct = {
    pop_direct_open,
    fclose
}, pop_filter = {
    pop_filter_open,
    pclose
};

/* ===================================================================
   Functions supporting asynchronous retrival of messages.
*/
struct fetch_data {
    LibBalsaMailbox *mailbox;
    PopHandle *pop;
    GError   **err;
    const gchar     *tmp_path;
    const gchar     *dest_path;
    const struct PopDownloadMode *dm;
    unsigned   msgno, total_messages;
    unsigned long *current_pos;
    unsigned long total_size;
    FILE      *f;
    unsigned   error_occured:1;
    unsigned   delete_on_server:1;
};

/* GError arguments - if any - are for one way communication from
   libimap to the callback.
   All errors are communicated to UI through fetch_data::err.
 */
static void
fetch_async_cb(PopReqCode prc, void *arg, ...)
{
    static time_t last_update = 0, current_time;
    static unsigned batch_cnt = 0;
    struct fetch_data *fd = (struct fetch_data*)arg;
    va_list alist;
    char threadbuf[160];
    
    va_start(alist, arg);
    switch(prc) {
    case POP_REQ_OK: 
	{/* GError **err = va_arg(alist, GError**); */
        fd->f  = fd->dm->open(fd->dest_path);
        if(fd->f == NULL) {
            if(!*fd->err) {
                g_set_error(fd->err, IMAP_ERROR, IMAP_POP_SAVE_ERROR,
                            _("Saving POP message to %s failed"),
                            fd->dest_path);
            }
            fd->error_occured = 1;
            printf("fopen failed for %s %p\n", fd->dest_path, fd->err);
        } else {
            snprintf(threadbuf, sizeof(threadbuf),
                     _("Retrieving Message %d of %d"),
                    fd->msgno, fd->total_messages);
            libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(fd->mailbox),
                                             LIBBALSA_NTFY_MSGINFO, 
                                             fd->msgno, fd->total_messages,
                                             threadbuf);
        }
        }
	break;
    case POP_REQ_ERR:
	{GError **err = va_arg(alist, GError**);
	if(!*fd->err) {
	    *fd->err  = *err;
	    *err = NULL;
	}
	}
	break;
    case POP_REQ_DATA:
	{unsigned len = va_arg(alist, unsigned);
	char    *buf = va_arg(alist, char*);
        if( (batch_cnt++ & 0x04) &&
            last_update != (current_time = time(NULL))) {
            snprintf(threadbuf, sizeof(threadbuf),
                     _("Received %ld kB of %ld"),
                     (long)(*fd->current_pos/1024),
                     (long)fd->total_size/1024);
            libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(fd->mailbox), 
                                             LIBBALSA_NTFY_PROGRESS, 
                                             *fd->current_pos, fd->total_size,
                                             threadbuf);
            last_update = current_time;
        }
        *fd->current_pos += len;
	if(fd->f)
	    if(fwrite(buf, 1, len, fd->f) != len) {
                if(!*fd->err)
                    g_set_error(fd->err, IMAP_ERROR, IMAP_POP_SAVE_ERROR,
                                _("Saving POP message to %s failed."),
                                fd->dest_path);
                fd->error_occured = 1;
            }
	}
	break;
    case POP_REQ_DONE:
        if(fd->dm->close(fd->f) != 0) {
            if(!*fd->err)
                g_set_error(fd->err, IMAP_ERROR, IMAP_POP_SAVE_ERROR,
                            _("Transferring POP message to %s failed."),
                            fd->dest_path);
        } else if(!fd->error_occured &&
                  move_from_msgdrop
                  (fd->tmp_path,
                   LIBBALSA_MAILBOX_POP3(fd->mailbox)->inbox,
                   fd->mailbox->name, fd->msgno) &&
                  fd->delete_on_server)
            pop_delete_message(fd->pop, fd->msgno, NULL, NULL, NULL);
	fd->f = NULL;
	break;
    }
    va_end(alist);
}

static void
async_notify(struct fetch_data *fd)
{
    /* We need to close the file here only when POP_REQ_ERR
       arrived. */
    if (fd->f) {
        fd->dm->close(fd->f);
        fd->f = NULL;
    }
    g_free(fd);
}

/* libbalsa_mailbox_pop3_check:
   checks=downloads POP3 mail.
   LOCKING : assumes gdk lock HELD and other locks (libmutt, mailbox) NOT HELD
*/
static void
monitor_cb(const char *buffer, int length, int direction, void *arg)
{
    int i;
    if(!PopDebug) return;

    if(direction) {
        if(strncmp(buffer, "Pass ", 5) == 0) {
            printf("POP C: Pass (passwd hidden)\n");
            return;
        }
    }
    printf("POP %c: ", direction ? 'C' : 'S');
    for (i = 0; i < length; i++)
      putchar(buffer[i]);
    fflush(NULL);
}

static void
libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox)
{
    gchar *tmp_path, *dest_path, *uid_prefix = NULL;
    gint tmp_file;
    LibBalsaMailboxPop3 *m = LIBBALSA_MAILBOX_POP3(mailbox);
    LibBalsaServer *server;
    gchar *msgbuf;
    GError *err = NULL;
    unsigned msgcnt, i;
    GHashTable *uids = NULL, *current_uids = NULL;
    const struct PopDownloadMode *mode;
    unsigned long current_pos = 0, total_size;
    ImapTlsMode tls_mode;
    PopHandle *pop;

    if (!m->check || !m->inbox) return;

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(m);
    switch(server->tls_mode) {
    case LIBBALSA_TLS_DISABLED: tls_mode = IMAP_TLS_DISABLED; break;
    default:
    case LIBBALSA_TLS_ENABLED : tls_mode = IMAP_TLS_ENABLED;  break;
    case LIBBALSA_TLS_REQUIRED: tls_mode = IMAP_TLS_REQUIRED; break;
    }

    msgbuf = g_strdup_printf("POP3: %s", mailbox->name);
    libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_SOURCE,0,0,msgbuf);
    g_free(msgbuf);

    tmp_file = g_file_open_tmp("pop-XXXXXX", &tmp_path, &err);
    if (tmp_file < 0) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp file error:\n%s"), 
			     mailbox->name, err->message);
	g_error_free(err);
	return;
    }
    close(tmp_file);

    pop = pop_new();
    pop_set_option(pop, IMAP_POP_OPT_FILTER_CR, TRUE);
    pop_set_option(pop, IMAP_POP_OPT_OVER_SSL, server->use_ssl);
    pop_set_option(pop, IMAP_POP_OPT_DISABLE_APOP, m->disable_apop);
    pop_set_tls_mode(pop, tls_mode);
    pop_set_timeout(pop, 60000); /* wait 1.5 minute for packets */
    pop_set_usercb(pop, libbalsa_server_user_cb, server);
    pop_set_monitorcb(pop, monitor_cb, NULL);

    if(m->filter) {
        mode       = &pop_filter;
        dest_path = m->filter_cmd;
    } else {
        mode  = &pop_direct;
        dest_path = tmp_path;
    }
    if(!pop_connect(pop, server->host, &err)) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s error: %s\n"), 
			     mailbox->name, err->message);
        g_error_free(err);
        unlink(tmp_path); g_free(tmp_path);
        pop_destroy(pop, NULL);
	return;
    }

    /* ===================================================================
     * Main download loop...
     * =================================================================== */
    msgcnt     = pop_get_exists(pop, NULL);
    total_size = pop_get_total_size(pop);
    if(!m->delete_from_server) {
        uids = mp_load_uids();
        current_uids = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
        uid_prefix = g_strconcat(server->user, "@", server->host, NULL);
    }

    for(i=1; i<=msgcnt; i++) {
        unsigned msg_size = pop_get_msg_size(pop, i);
#if POP_SYNC
        FILE *f;
        gboolean retval;
#endif
        if(!m->delete_from_server) {
            const char *uid = pop_get_uid(pop, i, NULL);
            char *full_uid = g_strconcat(uid_prefix, " ", uid, NULL);
            g_hash_table_insert(current_uids, full_uid, GINT_TO_POINTER(1));
            if(g_hash_table_lookup(uids, full_uid)) {
                total_size -= msg_size;
                continue;
            }
        }
        if(m->msg_size_limit>0 && msg_size >= (unsigned)m->msg_size_limit) {
            libbalsa_information
                (LIBBALSA_INFORMATION_WARNING,
                 _("POP3 message %d oversized: %d kB - skipped."),
                 i, msg_size);
            total_size -= msg_size;
            continue;
        }
#if POP_SYNC
        libbalsa_mailbox_progress_notify(mailbox,
                                         LIBBALSA_NTFY_PROGRESS, i, msgcnt,
                                         "next message");
        f = mode->open(dest_path);
        if(!f) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("POP3 error: cannot open %s for writing."), 
			     dest_path);
            break;
        }
        retval = pop_fetch_message_s(pop, i, dump_cb, f, &err);

        if(mode->close(f) != 0) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("POP3 error: cannot close %s."), 
			     dest_path);
            break;
        }
        if (!retval)
            break;
        if(move_from_msgdrop(tmp_path, m->inbox,
                             mailbox->name, i) &&
           m->delete_from_server)
            pop_delete_message_s(pop, i, NULL);
#else 
	{struct fetch_data *fd = g_new0(struct fetch_data, 1);
        fd->mailbox  = mailbox;
        fd->tmp_path = tmp_path;
	fd->pop      = pop; 
	fd->err      = &err;
	fd->dest_path= dest_path;
	fd->dm       = mode;
	fd->msgno    = i;
        fd->total_messages = msgcnt;
        fd->current_pos    = &current_pos;
        fd->total_size     = total_size;
	fd->delete_on_server = m->delete_from_server;
        pop_fetch_message(pop, i, fetch_async_cb, fd, 
			  (GDestroyNotify)async_notify);
	}
#endif
    }
    pop_destroy(pop, NULL);
    if(err) {
        /* 
        libbalsa_mailbox_progress_notify(mailbox,
                                         LIBBALSA_NTFY_ERROR, 0,
                                         1, err->message);*/
        libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("POP3 error: %s."), 
			     err->message);
        g_error_free(err);
    }
    if(!m->delete_from_server) {
        mp_save_uids(uids, current_uids, uid_prefix);
        g_hash_table_destroy(uids);
        g_hash_table_destroy(current_uids);
        g_free(uid_prefix);
    }
    libbalsa_mailbox_progress_notify(mailbox,
                                     LIBBALSA_NTFY_PROGRESS, 0,
                                     1, "Finished");
    
    /* ===================================================================
     * Postprocessing...
     * =================================================================== */

    unlink(tmp_path);
    g_free(tmp_path);
}

static void
libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxPop3 *pop;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    pop = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    libbalsa_conf_set_bool("Check", pop->check);
    libbalsa_conf_set_bool("Delete", pop->delete_from_server);
    libbalsa_conf_set_bool("DisableApop", pop->disable_apop);
    libbalsa_conf_set_bool("Filter", pop->filter);
    if(pop->filter_cmd)
        libbalsa_conf_set_string("FilterCmd", pop->filter_cmd);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);

}

static void
libbalsa_mailbox_pop3_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxPop3 *pop;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    pop = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_server_load_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    pop->check = libbalsa_conf_get_bool("Check=false");
    pop->delete_from_server = libbalsa_conf_get_bool("Delete=false");
    pop->disable_apop = libbalsa_conf_get_bool("DisableApop=false");
    pop->filter = libbalsa_conf_get_bool("Filter=false");
    pop->filter_cmd = libbalsa_conf_get_string("FilterCmd");
    if(pop->filter_cmd && *pop->filter_cmd == '\0') {
	g_free(pop->filter_cmd); pop->filter_cmd = NULL;
    }

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

}
void
libbalsa_mailbox_pop3_set_inbox(LibBalsaMailbox *mailbox,
                                LibBalsaMailbox *inbox)
{
    LibBalsaMailboxPop3 *pop;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    pop = LIBBALSA_MAILBOX_POP3(mailbox);

	pop->inbox=inbox;
}

void
libbalsa_mailbox_pop3_set_msg_size_limit(LibBalsaMailboxPop3 *pop,
                                         gint sz_limit)
{
    pop->msg_size_limit = sz_limit;
}
