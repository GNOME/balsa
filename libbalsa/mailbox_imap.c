/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
#endif

#include "imap/imap.h"
#include "mutt_socket.h"

static LibBalsaMailboxClass *parent_class = NULL;

static void libbalsa_mailbox_imap_destroy(GtkObject * object);
static void libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass *
					     klass);
static void libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox);
static void libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox);
static LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_imap_append(LibBalsaMailbox * mailbox);
static void libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox);
static FILE *libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox *
						      mailbox,
						      LibBalsaMessage *
						      message);
static void libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox);

static void libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);
static void libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
					      const gchar * prefix);

static void server_settings_changed(LibBalsaServer * server,
				    LibBalsaMailbox * mailbox);
static void server_user_settings_changed_cb(LibBalsaServer * server,
					    gchar * string,
					    LibBalsaMailbox * mailbox);
static void server_host_settings_changed_cb(LibBalsaServer * server,
					    gchar * host, gint port,
#ifdef USE_SSL
					    gboolean use_ssl,
#endif
					    LibBalsaMailbox * mailbox);


GtkType libbalsa_mailbox_imap_get_type(void)
{
    static GtkType mailbox_type = 0;

    if (!mailbox_type) {
	static const GtkTypeInfo mailbox_info = {
	    "LibBalsaMailboxImap",
	    sizeof(LibBalsaMailboxImap),
	    sizeof(LibBalsaMailboxImapClass),
	    (GtkClassInitFunc) libbalsa_mailbox_imap_class_init,
	    (GtkObjectInitFunc) libbalsa_mailbox_imap_init,
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
libbalsa_mailbox_imap_class_init(LibBalsaMailboxImapClass * klass)
{
    GtkObjectClass *object_class;
    LibBalsaMailboxClass *libbalsa_mailbox_class;

    object_class = GTK_OBJECT_CLASS(klass);
    libbalsa_mailbox_class = LIBBALSA_MAILBOX_CLASS(klass);

    parent_class = gtk_type_class(libbalsa_mailbox_remote_get_type());

    object_class->destroy = libbalsa_mailbox_imap_destroy;

    libbalsa_mailbox_class->open_mailbox = libbalsa_mailbox_imap_open;
    libbalsa_mailbox_class->open_mailbox_append = libbalsa_mailbox_imap_append;
    libbalsa_mailbox_class->close_mailbox = libbalsa_mailbox_imap_close;
    libbalsa_mailbox_class->get_message_stream =
	libbalsa_mailbox_imap_get_message_stream;

    libbalsa_mailbox_class->check = libbalsa_mailbox_imap_check;

    libbalsa_mailbox_class->save_config =
	libbalsa_mailbox_imap_save_config;
    libbalsa_mailbox_class->load_config =
	libbalsa_mailbox_imap_load_config;

    ImapCheckTimeout = 10;
}

static void
libbalsa_mailbox_imap_init(LibBalsaMailboxImap * mailbox)
{
    LibBalsaMailboxRemote *remote;
    mailbox->path = NULL;
    mailbox->auth_type = AuthCram;	/* reasonable default */

    remote = LIBBALSA_MAILBOX_REMOTE(mailbox);
    remote->server =
	LIBBALSA_SERVER(libbalsa_server_new(LIBBALSA_SERVER_IMAP));
    gtk_object_ref(GTK_OBJECT(remote->server));
    gtk_object_sink(GTK_OBJECT(remote->server));

    remote->server->port = 143;

    gtk_signal_connect(GTK_OBJECT(remote->server), "set-username",
		       GTK_SIGNAL_FUNC(server_user_settings_changed_cb),
		       (gpointer) mailbox);
    gtk_signal_connect(GTK_OBJECT(remote->server), "set-password",
		       GTK_SIGNAL_FUNC(server_user_settings_changed_cb),
		       (gpointer) mailbox);
    gtk_signal_connect(GTK_OBJECT(remote->server), "set-host",
		       GTK_SIGNAL_FUNC(server_host_settings_changed_cb),
		       (gpointer) mailbox);
}

/* libbalsa_mailbox_imap_destroy:
   NOTE: we have to close mailbox ourselves without waiting for
   LibBalsaMailbox::destroy because we want to destroy server as well,
   and close requires server for proper operation.  
*/
static void
libbalsa_mailbox_imap_destroy(GtkObject * object)
{
    LibBalsaMailboxImap *mailbox;
    LibBalsaMailboxRemote *remote;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(object));

    mailbox = LIBBALSA_MAILBOX_IMAP(object);

    while (LIBBALSA_MAILBOX(mailbox)->open_ref > 0)
	libbalsa_mailbox_close(LIBBALSA_MAILBOX(object));

    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

    remote = LIBBALSA_MAILBOX_REMOTE(object);
    g_free(mailbox->path); mailbox->path = NULL;

    if(remote->server) {
	gtk_object_unref(GTK_OBJECT(remote->server));
	remote->server = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

GtkObject *
libbalsa_mailbox_imap_new(void)
{
    LibBalsaMailbox *mailbox;
    mailbox = gtk_type_new(LIBBALSA_TYPE_MAILBOX_IMAP);

    return GTK_OBJECT(mailbox);
}

/* libbalsa_mailbox_imap_update_url:
   this is to be used only by mailboxImap functions, with exception
   for the folder scanner, which has to go around libmutt limitations.
*/
void
libbalsa_mailbox_imap_update_url(LibBalsaMailboxImap* mailbox)
{
    LibBalsaServer* s = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    g_free(LIBBALSA_MAILBOX(mailbox)->url);
    LIBBALSA_MAILBOX(mailbox)->url =  g_strdup_printf("imap%s://%s:%i/%s", 
#ifdef USE_SSL
						      s->use_ssl ? "s" : "",
#else
						      "",
#endif
						      s->host, s->port, 
						      mailbox->path);
}

/* Unregister an old notification and add a current one */
static void
server_settings_changed(LibBalsaServer * server, LibBalsaMailbox * mailbox)
{
    fprintf(stderr, "changing server settings for '%s' (%p)\n",
	    mailbox->url ? mailbox->url : "", mailbox->url);
    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));

    if (server->user && server->passwd && server->host)
	libbalsa_notify_register_mailbox(mailbox);
}

void
libbalsa_mailbox_imap_set_path(LibBalsaMailboxImap* mailbox, const gchar* path)
{
    g_return_if_fail(mailbox);
    g_free(mailbox->path);
    mailbox->path = g_strdup(path);
    libbalsa_mailbox_imap_update_url(mailbox);

    g_return_if_fail(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));
    server_settings_changed(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			    LIBBALSA_MAILBOX(mailbox));
}

static void
server_user_settings_changed_cb(LibBalsaServer * server, gchar * string,
				LibBalsaMailbox * mailbox)
{
    server_settings_changed(server, mailbox);
}

static void
server_host_settings_changed_cb(LibBalsaServer * server, gchar * host,
				gint port, 
#ifdef USE_SSL
				gboolean use_ssl,
#endif
				LibBalsaMailbox * mailbox)
{
    libbalsa_mailbox_imap_update_url(LIBBALSA_MAILBOX_IMAP(mailbox));
    server_settings_changed(server, mailbox);
}

void
reset_mutt_passwords(LibBalsaServer* server)
{
    if (ImapUser)
	safe_free((void **) &ImapUser);	/* because mutt does so */
    ImapUser = strdup(server->user);

    if (ImapPass)
	safe_free((void **) &ImapPass);	/* because mutt does so */
    ImapPass = strdup(server->passwd);
}

/* libbalsa_mailbox_imap_open:
   opens IMAP mailbox. On failure leaves the object in sane state.
   FIXME:
   should intelligently use auth_type field 
*/
static void
libbalsa_mailbox_imap_open(LibBalsaMailbox * mailbox)
{
    LibBalsaMailboxImap *imap;
    LibBalsaServer *server;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    LOCK_MAILBOX(mailbox);

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	/* increment the reference count */
	mailbox->open_ref++;
	UNLOCK_MAILBOX(mailbox);
	return;
    }

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    /* try getting password, quit on cancel */
    if (!(server->passwd && *server->passwd) &&
	!(server->passwd = libbalsa_server_get_password(server, mailbox))) {
	UNLOCK_MAILBOX(mailbox);
	return;
    }
    gdk_threads_leave();
    libbalsa_lock_mutt();
    reset_mutt_passwords(server);
    CLIENT_CONTEXT(mailbox) = mx_open_mailbox(mailbox->url, 0, NULL);
    libbalsa_unlock_mutt();

    if (CLIENT_CONTEXT_OPEN(mailbox)) {
	mailbox->readonly = CLIENT_CONTEXT(mailbox)->readonly;
	mailbox->messages = 0;
	mailbox->total_messages = 0;
	mailbox->unread_messages = 0;
	mailbox->new_messages = CLIENT_CONTEXT(mailbox)->msgcount;
	if(mailbox->open_ref == 0)
	    libbalsa_notify_unregister_mailbox(LIBBALSA_MAILBOX(mailbox));
	/* increment the reference count */
	mailbox->open_ref++;

	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
	libbalsa_mailbox_load_messages(mailbox);
#ifdef DEBUG
	g_print(_("LibBalsaMailboxImap: Opening %s Refcount: %d\n"),
		mailbox->name, mailbox->open_ref);
#endif
    } else {
	UNLOCK_MAILBOX(mailbox);
	gdk_threads_enter();
    }
}

static LibBalsaMailboxAppendHandle* 
libbalsa_mailbox_imap_append(LibBalsaMailbox * mailbox)
{
    LibBalsaServer *server;
    LibBalsaMailboxAppendHandle* res = g_new0(LibBalsaMailboxAppendHandle,1);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    libbalsa_lock_mutt();
    reset_mutt_passwords(server);
    res->context = mx_open_mailbox(mailbox->url, M_APPEND, NULL);

    if(res->context == NULL) {
	g_free(res);
	res = NULL;
    } else if (res->context->readonly) {
	g_warning("Cannot open dest local mailbox '%s' for writing.", 
		  mailbox->name);
	mx_close_mailbox(res->context, NULL);
	g_free(res);
	res = NULL;
    }
    libbalsa_unlock_mutt();
    g_print("libbalsa_mailbox_imap_append: returning %p\n", res);
    return res;
}

static void
libbalsa_mailbox_imap_close(LibBalsaMailbox * mailbox)
{
    if (LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox)
	(*LIBBALSA_MAILBOX_CLASS(parent_class)->close_mailbox) 
	    (LIBBALSA_MAILBOX(mailbox));
    if(mailbox->open_ref == 0)
	libbalsa_notify_register_mailbox(LIBBALSA_MAILBOX(mailbox));
}


/* libbalsa_mailbox_imap_get_message_stream:
   we make use of fact that imap_fetch_message doesn't set msg->path field. 
*/
static FILE *
libbalsa_mailbox_imap_get_message_stream(LibBalsaMailbox * mailbox,
					 LibBalsaMessage * message)
{
    FILE *stream = NULL;
    MESSAGE *msg;

    g_return_val_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox), NULL);
    g_return_val_if_fail(LIBBALSA_IS_MESSAGE(message), NULL);
    g_return_val_if_fail(CLIENT_CONTEXT(mailbox), NULL);

    msg = safe_calloc(1, sizeof(MESSAGE));
    msg->magic = CLIENT_CONTEXT(mailbox)->magic;
    if (!imap_fetch_message(msg, CLIENT_CONTEXT(mailbox), message->msgno))
	stream = msg->fp;
    FREE(&msg);
    return stream;
}

/* libbalsa_mailbox_imap_check:
   checks imap mailbox for new messages.
   Only open mailboxes are checked, although closed can be checked too
   with OPTIMAPPASIVE option set.
   NOTE: mx_check_mailbox can close mailbox(). Be cautious.
   We have to set Timeout to 0 because mutt would not allow checking several
   mailboxes in row.
*/
static void
libbalsa_mailbox_imap_check(LibBalsaMailbox * mailbox)
{
    if(mailbox->open_ref == 0) {
	if (libbalsa_notify_check_mailbox(mailbox) ) 
	    libbalsa_mailbox_set_unread_messages_flag(mailbox, TRUE); 
    } else {
	gint i = 0;
	long newmsg, timeout;
	gint index_hint;
	g_return_if_fail(CLIENT_CONTEXT(mailbox));
	
	LOCK_MAILBOX(mailbox);
	newmsg = CLIENT_CONTEXT(mailbox)->msgcount - mailbox->messages;
	index_hint = CLIENT_CONTEXT(mailbox)->vcount;

	libbalsa_lock_mutt();
	imap_allow_reopen(CLIENT_CONTEXT(mailbox));
	timeout = Timeout; Timeout = -1;
	i = mx_check_mailbox(CLIENT_CONTEXT(mailbox), &index_hint, 0);
	Timeout = timeout;
	libbalsa_unlock_mutt();

	if (i < 0) {
	    g_print("mx_check_mailbox() failed on %s\n", mailbox->name);
	    if(CLIENT_CONTEXT_CLOSED(mailbox)||
	       !CLIENT_CONTEXT(mailbox)->id_hash)
		libbalsa_mailbox_free_messages(mailbox);
	} 
	if (newmsg || i == M_NEW_MAIL || i == M_REOPENED) {
	    mailbox->new_messages =
		CLIENT_CONTEXT(mailbox)->msgcount - mailbox->messages;
	    
	    UNLOCK_MAILBOX(mailbox);
	    libbalsa_mailbox_load_messages(mailbox);
	} else {
	    UNLOCK_MAILBOX(mailbox);
	}
    }
}

static void
libbalsa_mailbox_imap_save_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *imap;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);

    gnome_config_set_string("Path", imap->path);

    libbalsa_server_save_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox));

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->save_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->save_config(mailbox, prefix);
}

static void
libbalsa_mailbox_imap_load_config(LibBalsaMailbox * mailbox,
				  const gchar * prefix)
{
    LibBalsaMailboxImap *imap;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);

    g_free(imap->path);
    imap->path = gnome_config_get_string("Path");

    libbalsa_server_load_config(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				143);

    if (LIBBALSA_MAILBOX_CLASS(parent_class)->load_config)
	LIBBALSA_MAILBOX_CLASS(parent_class)->load_config(mailbox, prefix);

    server_settings_changed(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
			    mailbox);
    libbalsa_mailbox_imap_update_url(LIBBALSA_MAILBOX_IMAP(mailbox));
}

void libbalsa_mailbox_imap_subscribe(LibBalsaMailboxImap * mailbox, 
				     gboolean subscribe)
{
    g_return_if_fail(LIBBALSA_IS_MAILBOX_IMAP(mailbox));
    imap_subscribe(LIBBALSA_MAILBOX(mailbox)->url, subscribe);
}

/* imap_close_all_connections:
   close all connections to leave the place cleanly.
*/
void
libbalsa_imap_close_all_connections(void)
{
    imap_logout_all();
}

/* libbalsa_imap_rename_subfolder:
   dir+parent determine current name. 
   folder - new name.
 */
void
libbalsa_imap_rename_subfolder(const gchar *dir, const gchar *parent,
			       const gchar *folder, gboolean subscribe, 
			       LibBalsaServer *server)
{
    gchar *imap_prefix =  g_strdup_printf("imap%s://%s:%i/  ", 
#ifdef USE_SSL
					  server->use_ssl ? "s" : "",
#else
					  "",
#endif
					  server->host, server->port);

    imap_mailbox_rename(imap_prefix, dir, parent, folder, subscribe);
    g_free(imap_prefix);
}

void
libbalsa_imap_new_subfolder(const gchar *parent, const gchar *folder,
			    gboolean subscribe, LibBalsaServer *server)
{
    gchar *imap_path = g_strdup_printf("imap%s://%s:%i/%s",
#ifdef USE_SSL
				       server->use_ssl ? "s" : "",
#else
				       "",
#endif
				       server->host,
				       server->port, parent);

    imap_mailbox_create(imap_path, folder, subscribe);
    g_free(imap_path);
}

void
libbalsa_imap_delete_folder(LibBalsaMailboxImap *mailbox)
{

    /* Some IMAP servers (UW2000) do not like removing subscribed mailboxes:
     * they do not remove the mailbox from the subscription list. */
    imap_subscribe(LIBBALSA_MAILBOX(mailbox)->url, FALSE);
    /*
	should be able to do this using the existing public method
	from libmutt/imap/imap.h:
    imap_delete_mailbox(CLIENT_CONTEXT(LIBBALSA_MAILBOX(mailbox)),
			mailbox->path);
	but it segfaults because ctx->data is NULL
	instead of being a pointer to an IMAP_DATA structure!

	instead we'll use our own new method:
	FIXME: this is one, big, ugly HACK.
    */
    libbalsa_mailbox_imap_open(LIBBALSA_MAILBOX(mailbox));
    imap_mailbox_delete(LIBBALSA_MAILBOX(mailbox)->url);
    libbalsa_mailbox_imap_close(LIBBALSA_MAILBOX(mailbox));
}

