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

#include <gtk/gtk.h>
#include <unistd.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"
#include "pop3.h"
#include "mailbox.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

#include "mailbox-filter.h"
#include "filter-file.h"
#include <libgnome/gnome-defs.h> 
#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "threads.h"
#endif

/* GTK_CLASS_TYPE for 1.2<->1.3/2.0 GTK+ compatibility */
#ifndef GTK_CLASS_TYPE
#define GTK_CLASS_TYPE(x) (GTK_OBJECT_CLASS(x)->type)
#endif /* GTK_CLASS_TYPE */

enum {
    CONFIG_CHANGED,
    LAST_SIGNAL
};

static LibBalsaMailboxClass *parent_class = NULL;
static guint libbalsa_mailbox_pop3_signals[LAST_SIGNAL];

static void libbalsa_mailbox_pop3_destroy(GtkObject * object);
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

GtkType libbalsa_mailbox_pop3_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailboxPOP3",
	    sizeof(LibBalsaMailboxPop3),
	    sizeof(LibBalsaMailboxPop3Class),
	    (GtkClassInitFunc) libbalsa_mailbox_pop3_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_pop3_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	mailbox_type =
	    gtk_type_unique(libbalsa_mailbox_remote_get_type(),
			    &mailbox_info);
    }

    return mailbox_type;
}

static void
libbalsa_mailbox_pop3_class_init(LibBalsaMailboxPop3Class * klass)
{
    GtkObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = GTK_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = gtk_type_class(libbalsa_mailbox_remote_get_type());
    libbalsa_mailbox_pop3_signals[CONFIG_CHANGED] =
	gtk_signal_new("config-changed",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaMailboxPop3Class,
					 config_changed),
		       gtk_marshal_NONE__NONE, GTK_TYPE_NONE, 0);

    object_class->destroy = libbalsa_mailbox_pop3_destroy;

    klass->config_changed = NULL;

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

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server =
	LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_POP3));
}

static void
libbalsa_mailbox_pop3_destroy(GtkObject * object)
{
    LibBalsaMailboxPop3 *mailbox = LIBBALSA_MAILBOX_POP3(object);
    LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(object);

    if (!mailbox)
	return;

    g_free(mailbox->last_popped_uid);

    gtk_object_destroy(GTK_OBJECT(remote->server));

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

GtkObject *
libbalsa_mailbox_pop3_new(void)
{
    LibBalsaMailbox *mailbox;

    mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_POP3);

    return GTK_OBJECT(mailbox);
}


static void
libbalsa_mailbox_pop3_config_changed(LibBalsaMailboxPop3* mailbox)
{
    g_return_if_fail(mailbox != NULL);
    g_return_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox));

    gtk_signal_emit(GTK_OBJECT(mailbox), 
                    libbalsa_mailbox_pop3_signals[CONFIG_CHANGED]);
}

static gboolean
libbalsa_mailbox_pop3_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxPop3 *pop;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_POP3(mailbox), FALSE);

    /* FIXME: I wonder whether this function is ever called... */

    g_print("Opened a POP3 mailbox!\n");

    LOCK_MAILBOX_RETURN_VAL(mailbox, FALSE);

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	/* increment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return TRUE;
    }

    pop = LIBBALSA_MAILBOX_POP3(mailbox);

    libbalsa_lock_mutt();
    CLIENT_CONTEXT(mailbox) =
	mx_open_mailbox(LIBBALSA_MAILBOX_REMOTE_SERVER(pop)->host, 0,
			NULL);
    libbalsa_unlock_mutt();

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	mailbox->messages = 0;
	mailbox->total_messages = 0;
	mailbox->unread_messages = 0;
	mailbox->new_messages = CLIENT_CONTEXT(mailbox)->msgcount;
	libbalsa_mailbox_load_messages(mailbox);

	/* increment the reference count */
	mailbox->open_ref++;

#ifdef DEBUG
	g_print(_("LibBalsaMailboxPop3: Opening %s Refcount: %d\n"),
		mailbox->name, mailbox->open_ref);
#endif

    }

    UNLOCK_MAILBOX(mailbox);
    return TRUE;
}

/* libbalsa_mailbox_pop3_check:
   checks=downloads POP3 mail. 
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
    
    if (!m->check) return;

    server = LIBBALSA_MAILBOX_REMOTE_SERVER(m);
    if(!server->passwd &&
       !(server->passwd = libbalsa_server_get_password(server, mailbox)))
       return;

    /* Unlock GDK - this is safe since libbalsa_error is threadsafe. */
    gdk_threads_leave();
        
    msgbuf = g_strdup_printf("POP3: %s", mailbox->name);
    libbalsa_mailbox_progress_notify(mailbox, MSGMAILTHREAD_SOURCE,0,0,msgbuf);
    g_free(msgbuf);

    if(m->last_popped_uid) 
	strncpy(uid, m->last_popped_uid, sizeof(uid));
    else uid[0] = '\0';

    do {
	tmp_path = g_strdup("/tmp/pop-XXXXXX");
	tmp_file = mkstemp(tmp_path);
    } while ((tmp_file < 0) && (errno == EEXIST));

    /* newer glibc does this for us */
    fchmod(tmp_file,  S_IRUSR | S_IWUSR );

    if(tmp_file < 0) {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp file error:\n%s"), 
			     mailbox->name,
			     g_strerror(errno));
	g_free(tmp_path);
        gdk_threads_enter();
	return;
    }
    close(tmp_file);

    status =  LIBBALSA_MAILBOX_POP3(mailbox)->filter 
	? libbalsa_fetch_pop_mail_filter (m, uid, progress_cb, mailbox)
	: libbalsa_fetch_pop_mail_direct (m, tmp_path, uid, 
                                          progress_cb, mailbox);

    if(status != POP_OK)
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s accessing error:\n%s"), 
			     mailbox->name,
			     pop_get_errstr(status));
    
    if (LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid == NULL ||
	strcmp(LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid,
	       uid) != 0) {
	
	g_free(LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid);
	LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid =
	    g_strdup(uid);
	
        libbalsa_mailbox_pop3_config_changed(LIBBALSA_MAILBOX_POP3(mailbox));
    } 

    /* Regrab the gdk lock before leaving */
    gdk_threads_enter();

    tmp_mailbox = (LibBalsaMailbox*)
        libbalsa_mailbox_local_new(tmp_path, FALSE);
    if(!tmp_mailbox)  {
	libbalsa_information(LIBBALSA_INFORMATION_WARNING,
			     _("POP3 mailbox %s temp mailbox error:\n"), 
			     mailbox->name);
	g_free(tmp_path);
	return;
    }	
    libbalsa_mailbox_open(tmp_mailbox);
    if((m->inbox) && (tmp_mailbox->messages)) {
	GSList * filters; 

	 /* Load associated filters if needed */
        if (!mailbox->filters)                                
            config_mailbox_filters_load(mailbox);
        filters = libbalsa_mailbox_filters_when(mailbox->filters,
						FILTER_WHEN_INCOMING);
	if (filters) {
	    if (filters_prepare_to_run(filters)) {
		libbalsa_filter_match(filters, tmp_mailbox->message_list, FALSE);
		libbalsa_filter_apply(filters);
	    }
	    g_slist_free(filters);
	}

	if (!libbalsa_messages_move(tmp_mailbox->message_list, m->inbox)) {    
	    libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				 _("Error placing messages from %s on %s\n"
				   "Messages are left in %s\n"),
				 mailbox->name, 
				 LIBBALSA_MAILBOX(m->inbox)->name,
				 tmp_path);
            remove_tmp = FALSE;
	}
    } 
    libbalsa_mailbox_close(LIBBALSA_MAILBOX(tmp_mailbox));
    gtk_object_destroy(GTK_OBJECT(tmp_mailbox));	
    if(remove_tmp) 
        unlink(tmp_path);
    g_free(tmp_path);
}


static void
progress_cb(void* mailbox, char *msg, int prog, int tot)
{
    int msg_type;

    switch(tot) {
    case -1: msg_type = MSGMAILTHREAD_FINISHED; break;
    case 0:
    default: msg_type = MSGMAILTHREAD_MSGINFO;  break;
    }

    libbalsa_mailbox_progress_notify(LIBBALSA_MAILBOX(mailbox), 
                                     msg_type, prog, tot, msg);
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
