/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "config.h"

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
#include "pop3.h"
#include "mailbox.h"
#include "mailbox_pop3.h"

#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

int PopDebug = 0;

enum {
    CONFIG_CHANGED,
    LAST_SIGNAL
};
static LibBalsaMailboxClass *parent_class = NULL;
static guint libbalsa_mailbox_pop3_signals[LAST_SIGNAL];

static void libbalsa_mailbox_pop3_finalize(GObject * object);
static void libbalsa_mailbox_pop3_class_init(LibBalsaMailboxPop3Class *
					     klass);
static void libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 * mailbox);

static gboolean libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox);

static void libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_pop3_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

#ifdef FIXME
static void progress_cb(LibBalsaMailbox *m, char *msg, int prog, int tot);
#endif

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
    libbalsa_mailbox_pop3_signals[CONFIG_CHANGED] =
	g_signal_new("config-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST | G_SIGNAL_NO_HOOKS,
                     G_STRUCT_OFFSET(LibBalsaMailboxPop3Class,
                                     config_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

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

    mailbox->filter = FALSE;
    mailbox->filter_cmd = NULL;
    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server =
	LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_POP3));
}

static void
libbalsa_mailbox_pop3_finalize(GObject * object)
{
    LibBalsaMailboxPop3 *mailbox = LIBBALSA_MAILBOX_POP3(object);
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(object);

    if (!mailbox)
	return;

    g_free(mailbox->last_popped_uid);

    g_object_unref(G_OBJECT(remote->server));

    if (G_OBJECT_CLASS(parent_class)->finalize)
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

GObject *
libbalsa_mailbox_pop3_new(void)
{
    LibBalsaMailbox *mailbox;

    mailbox = g_object_new(LIBBALSA_TYPE_MAILBOX_POP3, NULL);

    return G_OBJECT(mailbox);
}


static void
libbalsa_mailbox_pop3_config_changed(LibBalsaMailboxPop3* mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    g_signal_emit(G_OBJECT(mailbox), 
                  libbalsa_mailbox_pop3_signals[CONFIG_CHANGED], 0);
}

static gboolean
libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox)
{
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox), FALSE);

    /* FIXME: it should never be called. */

    g_print("Opened a POP3 mailbox!\n");

    return TRUE;
}

static GHashTable*
mp_load_uids(void)
{
    char line[1024]; /* arbitrary limit of uid len */
    GHashTable *res = g_hash_table_new(g_str_hash, g_str_equal);
    gchar *fname = g_strconcat(g_get_home_dir(), "/.balsa/pop-uids", NULL);
    FILE *f = fopen(fname, "r");
    g_free(fname);
    if(f) {
        struct flock lck;
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
    } /* else COUDL NOT SAVE UIDS! SHOUT! */
    return;
}

static int
dump_cb(unsigned len, char *buf, void *arg)
{
    /* FIXME: Bad things happen when messages are empty, . */
    return fwrite(buf, 1, len, (FILE*)arg) == len;
}

static gchar*
pop_direct_get_path(const char *pattern, unsigned msgno)
{
    return g_strdup_printf("%s/%u", pattern, msgno);
}

static FILE*
pop_direct_open(const char *path)
{
    return fopen(path, "w");
}

static gchar*
pop_filter_get_path(const char *pattern, unsigned msgno)
{
    return g_strdup(pattern);
}
static FILE*
pop_filter_open(const char *command)
{
    return popen(command, "w");
}

struct PopDownloadMode {
    gchar* (*get_path)(const char *pattern, unsigned msgno);
    FILE* (*open)(const char *path);
    int (*close)(FILE *arg);
} pop_direct = {
    pop_direct_get_path,
    pop_direct_open,
    fclose
}, pop_filter = {
    pop_filter_get_path,
    pop_filter_open,
    pclose
};

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
    LibBalsaMailbox *tmp_mailbox;
    LibBalsaMailboxPop3 *m = LIBBALSA_MAILBOX_POP3(mailbox);
    LibBalsaServer *server;
    gboolean remove_tmp = TRUE;
    gchar *msgbuf, *mhs;
    GError *err = NULL;
    unsigned msgcnt, i;
    GHashTable *uids = NULL, *current_uids = NULL;
    const struct PopDownloadMode *mode;
    
    if (!m->check) return;

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(m);

    msgbuf = g_strdup_printf("POP3: %s", mailbox->name);
    libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_SOURCE,0,0,msgbuf);
    g_free(msgbuf);

    do {
	tmp_path = g_strdup("/tmp/pop-XXXXXX");
	tmp_file = g_mkstemp(tmp_path);
    } while ((tmp_file < 0) && (errno == EEXIST));

    if(tmp_file < 0) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp file error:\n%s"), 
			     mailbox->name,
			     g_strerror(errno));
	g_free(tmp_path);
	return;
    }
    close(tmp_file);
    unlink(tmp_path);

    if( mkdir(tmp_path, 0700) < 0 ) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp file error:\n%s"), 
			     mailbox->name,
			     g_strerror(errno));
	g_free(tmp_path);
	return;	
    }
    
    mhs = g_strdup_printf ( "%s/.mh_sequences", tmp_path );
    if( (tmp_file=creat( mhs, 0600)) != -1) close(tmp_file);
    /* we fake a real mh box - it's good enough */
    
    PopHandle * pop = pop_new();
    pop_set_option(pop, IMAP_POP_OPT_FILTER_CR, TRUE);
    pop_set_usercb(pop, libbalsa_server_user_cb, server);
    pop_set_monitorcb(pop, monitor_cb, NULL);
#ifdef FIXME
    pop_set_infocb(pop, progress_cb,      server);
#endif

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
	g_free(tmp_path); g_free(mhs);
	return;
    }

    /* ===================================================================
     * Main download loop...
     * =================================================================== */
    msgcnt = pop_get_exists(pop, NULL);
    if(!m->delete_from_server) {
        uids = mp_load_uids();
        current_uids = g_hash_table_new(g_str_hash, g_str_equal);
        uid_prefix = g_strconcat(server->user, "@", server->host, NULL);
    }
    for(i=1; i<=msgcnt; i++) {
        char *msg_path = mode->get_path(dest_path, i);
        FILE *f;

        if(!m->delete_from_server) {
            const char *uid = pop_get_uid(pop, i, NULL);
            char *full_uid = g_strconcat(uid_prefix, " ", uid, NULL);
            g_hash_table_insert(current_uids, full_uid, GINT_TO_POINTER(1));
            if(g_hash_table_lookup(uids, full_uid))
                continue;
        }
        libbalsa_mailbox_progress_notify(mailbox,
                                         LIBBALSA_NTFY_PROGRESS, i, msgcnt,
                                         "next message");
        f = mode->open(msg_path);
        if(!f) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("POP3 error: cannot open %s for writing."), 
			     msg_path);
            break;
        }
        if(!pop_fetch_message(pop, i, dump_cb, f, &err)) 
            break;
        if(mode->close(f) != 0) {
            libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _("POP3 error: cannot close %s."), 
			     msg_path);
            break;
        }
        if(m->delete_from_server)
            pop_delete_message(pop, i, NULL);
        g_free(msg_path);
    }
    if(err) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 error: %s."), 
			     err->message);
        g_clear_error(&err);
    }

    pop_destroy(pop, NULL);
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
    tmp_mailbox = (LibBalsaMailbox*)
        libbalsa_mailbox_mh_new(tmp_path, FALSE);
    if(!tmp_mailbox)  {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp mailbox error:\n"), 
			     mailbox->name);
	g_free(tmp_path); g_free(mhs);
	return;
    }
    libbalsa_mailbox_open(tmp_mailbox);
    if ((m->inbox) && (libbalsa_mailbox_total_messages(tmp_mailbox))) {
	guint msgno = libbalsa_mailbox_total_messages(tmp_mailbox);
	GList *msg_list = NULL;

	do {
	    LibBalsaMessage *message =
		libbalsa_mailbox_get_message(tmp_mailbox, msgno);
	    message->flags |= (LIBBALSA_MESSAGE_FLAG_NEW |
			       LIBBALSA_MESSAGE_FLAG_RECENT);
	    msg_list = g_list_prepend(msg_list, message);
	} while (--msgno > 0);

	if (!libbalsa_messages_move(msg_list, m->inbox)) {    
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("Error placing messages from %s on %s\n"
				   "Messages are left in %s\n"),
				 mailbox->name, 
				 LIBBALSA_MAILBOX(m->inbox)->name,
				 tmp_path);
	    remove_tmp = FALSE;
	}
	g_list_free(msg_list);
    }
    libbalsa_mailbox_close(tmp_mailbox);
    libbalsa_mailbox_pop3_config_changed(m);
    
    g_object_unref(G_OBJECT(tmp_mailbox));
    if(remove_tmp) { 
	unlink(mhs);
	if (rmdir(tmp_path)) {
	    /* Probably some file was left behind... */
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("POP3 temp mailbox %s was not removed "
				   "(system error message: %s)"),
				   tmp_path, g_strerror(errno));
	}
    }
    g_free(tmp_path);
    g_free(mhs);
}


#ifdef FIXME
static void
progress_cb(LibBalsaMailbox* mailbox, char *msg, int prog, int tot)
{
    /* tot=-1 means finished */
    if (tot==-1)
	libbalsa_mailbox_progress_notify(mailbox,
					 LIBBALSA_NTFY_PROGRESS, 0,
					 1, "Finished");
    else {
	if (tot>0)
	    libbalsa_mailbox_progress_notify(mailbox,
					     LIBBALSA_NTFY_PROGRESS, prog,
					     tot, msg);
	libbalsa_mailbox_progress_notify(mailbox,
					 LIBBALSA_NTFY_MSGINFO, prog,
                                         tot, msg);
    }
}
#endif

static void
libbalsa_mailbox_pop3_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxPop3 *pop;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    pop = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    gnome_config_set_bool("Check", pop->check);
    gnome_config_set_bool("Delete", pop->delete_from_server);
    gnome_config_set_bool("Apop", pop->use_apop);
    gnome_config_set_bool("Filter", pop->filter);
    if(pop->filter_cmd)
        gnome_config_set_string("FilterCmd", pop->filter_cmd);
    if(pop->last_popped_uid)
        gnome_config_set_string("Lastuid", pop->last_popped_uid);

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

    pop->check = gnome_config_get_bool("Check=false");
    pop->delete_from_server = gnome_config_get_bool("Delete=false");
    pop->use_apop = gnome_config_get_bool("Apop=false");
    pop->filter = gnome_config_get_bool("Filter=false");
    pop->filter_cmd = gnome_config_get_string("FilterCmd");
    if(pop->filter_cmd && *pop->filter_cmd == '\0') {
	g_free(pop->filter_cmd); pop->filter_cmd = NULL;
    }

    g_free(pop->last_popped_uid);
    pop->last_popped_uid = gnome_config_get_string("Lastuid");

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
