/* -*-mode:c; c-style:k&r; c-basic-offset:8; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "mailbackend.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#else
/* FIXME: Balsa dependency */
#include "src/save-restore.h" /*config_mailbox_update*/
#endif

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_pop3_destroy (GtkObject *object);
static void libbalsa_mailbox_pop3_class_init (LibBalsaMailboxPop3Class *klass);
static void libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 *mailbox);

static void libbalsa_mailbox_pop3_open (LibBalsaMailbox *mailbox, gboolean append);
static void libbalsa_mailbox_pop3_check (LibBalsaMailbox *mailbox);

GtkType
libbalsa_mailbox_pop3_get_type (void)
{
	static GtkType mailbox_type = 0;

	if (!mailbox_type) {
		static const GtkTypeInfo mailbox_info =	{
			"LibBalsaMailboxPOP3",
			sizeof (LibBalsaMailboxPop3),
			sizeof (LibBalsaMailboxPop3Class),
			(GtkClassInitFunc) libbalsa_mailbox_pop3_class_init,
			(GtkObjectInitFunc) libbalsa_mailbox_pop3_init,
			/* reserved_1 */ NULL,
			/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		mailbox_type = gtk_type_unique(libbalsa_mailbox_remote_get_type(), &mailbox_info);
	}

	return mailbox_type;
}

static void
libbalsa_mailbox_pop3_class_init (LibBalsaMailboxPop3Class *klass)
{
	GtkObjectClass *object_class;
	LibBalsaMailboxClass *libbalsa_mailbox_class;

	object_class = GTK_OBJECT_CLASS(klass);
	libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

	parent_class = gtk_type_class(libbalsa_mailbox_remote_get_type());

	object_class->destroy = libbalsa_mailbox_pop3_destroy;

	libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_pop3_open;

	libbalsa_mailbox_class->check = libbalsa_mailbox_pop3_check;

}

static void
libbalsa_mailbox_pop3_init(LibBalsaMailboxPop3 *mailbox)
{
	LibBalsaMailboxRemote *remote;
	mailbox->check = FALSE;
	mailbox->delete_from_server = FALSE;

	remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
	remote->server = LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_POP3));
	remote->server->port = 110;

}

static void
libbalsa_mailbox_pop3_destroy (GtkObject *object)
{
	LibBalsaMailboxPop3 *mailbox = LIBBALSA_MAILBOX_POP3(object);
	LibBalsaMailboxRemote *remote = LIBBALSA_MAILBOX_REMOTE(object);

	if (!mailbox)
		return;

	g_free (mailbox->last_popped_uid);

	gtk_object_destroy(GTK_OBJECT(remote->server));

	if (GTK_OBJECT_CLASS(parent_class)->destroy)
		(*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

GtkObject *libbalsa_mailbox_pop3_new(void)
{
	LibBalsaMailbox *mailbox;

	mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_POP3);

	return GTK_OBJECT(mailbox);
}

static void libbalsa_mailbox_pop3_open (LibBalsaMailbox *mailbox, gboolean append)
{
	LibBalsaMailboxPop3 *pop;
  
	g_return_if_fail ( LIBBALSA_IS_MAILBOX_POP3(mailbox) );

	/* FIXME: I was curious if this function was ever called... */

	g_print ("Opened a POP3 mailbox!\n");

	LOCK_MAILBOX (mailbox);

	if (CLIENT_CONTEXT_OPEN (mailbox)) {
		if ( append ) {
			/* we need the mailbox to be opened fresh i think */
			libbalsa_lock_mutt();
			mx_close_mailbox( CLIENT_CONTEXT(mailbox), NULL);
			libbalsa_unlock_mutt();
		} else {
			/* incriment the reference count */
			mailbox->open_ref++;
      
			UNLOCK_MAILBOX (mailbox);
			return;
		}
	}

	pop = LIBBALSA_MAILBOX_POP3(mailbox);
	
	libbalsa_lock_mutt();
	if ( append )
		CLIENT_CONTEXT (mailbox) = mx_open_mailbox (LIBBALSA_MAILBOX_REMOTE_SERVER(pop)->host, M_APPEND, NULL);
	else
		CLIENT_CONTEXT (mailbox) = mx_open_mailbox (LIBBALSA_MAILBOX_REMOTE_SERVER(pop)->host, 0, NULL);
	libbalsa_unlock_mutt();

	if (CLIENT_CONTEXT_OPEN (mailbox)) {
		mailbox->messages = 0;
		mailbox->total_messages = 0;
		mailbox->unread_messages = 0;
		mailbox->new_messages = CLIENT_CONTEXT (mailbox)->msgcount;
		libbalsa_mailbox_load_messages (mailbox);
    
		/* increment the reference count */
		mailbox->open_ref++;
    
#ifdef DEBUG
		g_print (_("LibBalsaMailboxPop3: Opening %s Refcount: %d\n"), mailbox->name, mailbox->open_ref);
#endif
    
	}
  
	UNLOCK_MAILBOX (mailbox);

}

static void libbalsa_mailbox_pop3_check (LibBalsaMailbox *mailbox)
{
	gchar uid[80];

#ifdef BALSA_USE_THREADS
	gchar *msgbuf;
	MailThreadMessage *threadmsg;
#endif /* BALSA_USE_THREADS */

	if (LIBBALSA_MAILBOX_POP3 (mailbox)->check) {
		LibBalsaServer *server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

		/* Unlock GDK - this is safe since libbalsa_error is threadsafe. */
		gdk_threads_leave();
		libbalsa_lock_mutt();

		PopHost = g_strdup (server->host);
		PopPort = (server->port);
		PopPass = g_strdup (server->passwd);
		PopUser = g_strdup (server->user);

#ifdef BALSA_USE_THREADS
		msgbuf = g_strdup_printf( "POP3: %s", mailbox->name );
		MSGMAILTHREAD( threadmsg, MSGMAILTHREAD_SOURCE, msgbuf );
		g_free(msgbuf);
#endif
    
		if( LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid == NULL)
			uid[0] = 0;
		else
			strcpy ( uid, LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid );
    
		PopUID = uid;

		/* Delete it if necessary */
		if (LIBBALSA_MAILBOX_POP3 (mailbox)->delete_from_server) {
			set_option(OPTPOPDELETE);
		} else {
			unset_option(OPTPOPDELETE);
		}

		/* Use Apop ? */
		if (LIBBALSA_MAILBOX_POP3 (mailbox)->use_apop) {
			set_option(OPTPOPAPOP);
		} else {
			unset_option(OPTPOPAPOP);
		}
    
		mutt_fetchPopMail ();
		g_free (PopHost);
		g_free (PopPass);
		g_free (PopUser);

		if( LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid == NULL ||
		    strcmp(LIBBALSA_MAILBOX_POP3(mailbox)->last_popped_uid, uid) != 0) {

			g_free ( LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid );

			LIBBALSA_MAILBOX_POP3 (mailbox)->last_popped_uid = g_strdup ( uid );
      
#ifdef BALSA_USE_THREADS
			threadmsg = g_new (MailThreadMessage, 1);
			threadmsg->message_type = MSGMAILTHREAD_UPDATECONFIG;
			threadmsg->mailbox = (void *) mailbox;
			write( mail_thread_pipes[1], (void *) &threadmsg, sizeof(void *) );
#else
			config_mailbox_update( 
				mailbox, mailbox_get_pkey(mailbox) );
#endif
		}

		/* Regrab the gdk lock before leaving */
		libbalsa_unlock_mutt();
		gdk_threads_enter();
	}

}
