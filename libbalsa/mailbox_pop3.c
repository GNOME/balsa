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
#include <gtk/gtk.h>
#include <string.h>
#include "libbalsa.h"
#include "libbalsa_private.h"
#include "pop3.h"
#include "mailbox.h"

#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

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

static void progress_cb(void* m, char *msg, int prog, int tot);

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

/* libbalsa_mailbox_pop3_check:
   checks=downloads POP3 mail.
   LOCKING : assumes gdk lock HELD and other locks (libmutt, mailbox) NOT HELD
*/
static void
libbalsa_mailbox_pop3_check(LibBalsaMailbox * mailbox)
{
    gchar uid[80];
    gchar *tmp_path;
    gint tmp_file;
    LibBalsaMailbox *tmp_mailbox;
    PopStatus status;
    LibBalsaMailboxPop3 *m = LIBBALSA_MAILBOX_POP3(mailbox);
    LibBalsaServer *server;
    gboolean remove_tmp = TRUE;
    gchar *msgbuf;
    gchar *mhs;
    
    if (!m->check) return;

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(m);
    if(!server->passwd &&
       !(server->passwd = libbalsa_server_get_password(server, mailbox)))
       return;

    msgbuf = g_strdup_printf("POP3: %s", mailbox->name);
    libbalsa_mailbox_progress_notify(mailbox, LIBBALSA_NTFY_SOURCE,0,0,msgbuf);
    g_free(msgbuf);

    if(m->last_popped_uid) 
	strncpy(uid, m->last_popped_uid, sizeof(uid));
    else uid[0] = '\0';

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
    creat( mhs, 0600);
    /* we fake a real mh box - it's good enough */
    

    status =  m->filter 
	? libbalsa_fetch_pop_mail_filter (m, uid, m->filter_cmd,
                                          progress_cb, mailbox)
	: libbalsa_fetch_pop_mail_direct (m, tmp_path, uid, 
                                          progress_cb, mailbox);

    if(status != POP_OK)
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s accessing error:\n%s"), 
			     mailbox->name,
			     pop_get_errstr(status));
    
    if (m->last_popped_uid == NULL || strcmp(m->last_popped_uid, uid) != 0) {
	g_free(m->last_popped_uid);
	m->last_popped_uid = g_strdup(uid);
        libbalsa_mailbox_pop3_config_changed(m);
    } 

    tmp_mailbox = (LibBalsaMailbox*)
        libbalsa_mailbox_mh_new(tmp_path, FALSE);
    if(!tmp_mailbox)  {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp mailbox error:\n"), 
			     mailbox->name);
	g_free(tmp_path);
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


static void
progress_cb(void* mailbox, char *msg, int prog, int tot)
{
    /* tot=-1 means finished */
    if (tot==-1)
	libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(mailbox), 
					 LIBBALSA_NTFY_PROGRESS, 0,
					 1, "Finished");
    else {
	if (tot>0)
	    libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(mailbox), 
					     LIBBALSA_NTFY_PROGRESS, prog,
					     tot, msg);
	libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(mailbox),
					 LIBBALSA_NTFY_MSGINFO, prog, tot, msg);
    }
}


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
