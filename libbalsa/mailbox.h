/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#ifndef __MAILBOX_H__
#define __MAILBOX_H__

#include <glib.h>
#include <gtk/gtk.h>

#include "libmutt/mutt.h"


#define BALSA_TYPE_MAILBOX			(balsa_mailbox_get_type())
#define BALSA_MAILBOX(obj)			(GTK_CHECK_CAST (obj, BALSA_TYPE_MAILBOX, Mailbox))
#define BALSA_MAILBOX_CLASS(klass)		(GTK_CHECK_CLASS_CAST (klass, BALSA_TYPE_MAILBOX, MailboxClass))
#define BALSA_IS_MAILBOX(obj)			(GTK_CHECK_TYPE (obj, BALSA_TYPE_MAILBOX))
#define BALSA_IS_MAILBOX_CLASS(klass)		(GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_MAILBOX))

/*
 * enumes
 */
typedef enum
{
  MAILBOX_MBOX,
  MAILBOX_MH,
  MAILBOX_MAILDIR,
  MAILBOX_POP3,
  MAILBOX_IMAP,
  MAILBOX_UNKNOWN
} MailboxType;

typedef enum
{
  SERVER_POP3,
  SERVER_IMAP,
  SERVER_UNKNOWN
}
ServerType;


typedef enum
{
  MAILBOX_SORT_DATE = 1,
  MAILBOX_SORT_SIZE = 2,
  MAILBOX_SORT_SUBJECT = 3,
  MAILBOX_SORT_FROM = 4,
  MAILBOX_SORT_ORDER = 5,
  MAILBOX_SORT_THREADS = 6,
  MAILBOX_SORT_RECEIVED = 7,
  MAILBOX_SORT_TO = 8,
  MAILBOX_SORT_SCORE = 9,
  MAILBOX_SORT_ALIAS = 10,
  MAILBOX_SORT_ADDRESS = 11,
  MAILBOX_SORT_MASK = 0xf,
  MAILBOX_SORT_REVERSE = (1 << 4),
  MAILBOX_SORT_LAST = (1 << 5)
} MailboxSort;

typedef enum
{
  MESSAGE_MARK_CLEAR,		/* clear all flags */
  MESSAGE_MARK_ANSWER,		/* message has been answered */
  MESSAGE_MARK_READ,		/* message has changed from new to read */
  MESSAGE_MARK_UNREAD,		/* message has changed from read to new */
  MESSAGE_MARK_DELETE,		/* message has been marked deleted */
  MESSAGE_MARK_UNDELETE,	/* message has been marked undeleted */
  MESSAGE_DELETE,		/* message has been deleted */
  MESSAGE_NEW,			/* message is new to the mailbox */
  MESSAGE_FLAGGED,		/* the message was flagged */
  MESSAGE_REPLIED,		/* the message was answered */
  MESSAGE_APPEND		/* message has been appended */
} MailboxWatcherMessageType;


typedef enum
{
  MESSAGE_MARK_CLEAR_MASK = 1,
  MESSAGE_MARK_ANSWER_MASK = 1 << 1,
  MESSAGE_MARK_READ_MASK = 1 << 2,
  MESSAGE_MARK_UNREAD_MASK = 1 << 3,
  MESSAGE_MARK_DELETE_MASK = 1 << 4,
  MESSAGE_MARK_UNDELETE_MASK = 1 << 5,
  MESSAGE_DELETE_MASK = 1 << 6,
  MESSAGE_NEW_MASK = 1 << 7,
  MESSAGE_FLAGGED_MASK = 1 << 8,
  MESSAGE_REPLIED_MASK = 1 << 9,
  MESSAGE_APPEND_MASK = 1 << 10
} MailboxWatcherMessageMask;


/*
 * strucutres
 */
typedef struct _MailboxClass MailboxClass;
typedef struct _ServerClass ServerClass;
typedef struct _MailboxRemote MailboxRemote;



typedef struct _MailboxWatcherMessage MailboxWatcherMessage;
typedef struct _MailboxWatcherMessageNew MailboxWatcherMessageNew;

GtkType balsa_mailbox_get_type (void);

struct _Mailbox
{
  GtkObject object;

  MailboxType type;
  gchar *name;
  void *private;
  guint open_ref;

  gboolean lock;

  glong messages;
  glong new_messages;
  GList *message_list;

  /* info fields */
  glong unread_messages; /* number of unread messages in the mailbox */
  glong total_messages;  /* total number of messages in the mailbox  */
};


struct _MailboxClass
{
  GtkObjectClass parent_class;

  void (* open_mailbox)    (Mailbox *mailbox);
  void (* close_mailbox)   (Mailbox *mailbox);

  void (* message_new)     (Mailbox *mailbox,
			    Message *message);

  void (* message_delete)  (Mailbox *mailbox,
			    Message *message);

  void (* message_append)  (Mailbox *mailbox,
			    Message *message);

  /* message's flags changed */
  void (* message_flagged) (Mailbox *mailbox,
			    Message *message);

};

struct _Server
{
  GtkObject object;

  ServerType type;
  gchar *name;

  gchar *host;
  gint port;

  gchar *user;
  gchar *passwd;
};

struct _ServerClass
{
  GtkObjectClass parent_class;

  void (* message_new)     (Mailbox *mailbox,
			    Message *message);

  void (* message_delete)  (Mailbox *mailbox,
			    Message *message);

  void (* message_append)  (Mailbox *mailbox,
			    Message *message);

  /* message's flags changed */
  void (* message_flagged) (Mailbox *mailbox,
			    Message *message);

};

struct _MailboxWatcherMessage
{
  MailboxWatcherMessageType type;
  Mailbox *mailbox;
  Message *message;
  gpointer data;
};


struct _MailboxWatcherMessageNew
{
  /* common */
  MailboxWatcherMessageType type;
  Mailbox *mailbox;
  Message *message;
  gpointer data;
  /* end common */
  
  gint remaining;
};


/*
 * function typedefs
 */
typedef void (*MailboxWatcherFunc) (MailboxWatcherMessage * arg1);


/*
 * call before using any mailbox functions
 */
void mailbox_init (gchar * inbox_path,
		   void (*error_func) (const char *fmt,...),
		   void (*gui_func) (void));

gint set_imap_username (Mailbox * mb);
void check_all_pop3_hosts (Mailbox *, GList *);
void check_all_imap_hosts (Mailbox *, GList *);
void add_mailboxes_for_checking (Mailbox *);
gint mailbox_have_new_messages (gchar * path);
GList *make_list_from_string (gchar *);
gint mailbox_check_new_sent (Mailbox * mailbox);

/* 
 * open and close a mailbox 
 */
/* XXX these need to return a value if they failed */
void balsa_mailbox_open(Mailbox *mailbox);
void balsa_mailbox_close(Mailbox *mailbox);

int mailbox_open_ref (Mailbox * mailbox);
int mailbox_open_append (Mailbox * mailbox);
void mailbox_open_unref (Mailbox * mailbox);

/*
 * sorting mailbox
 */
void mailbox_sort (Mailbox * mailbox, MailboxSort sort);

/*
 * create and destroy a mailbox structure
 */
GtkObject *mailbox_new (MailboxType type);
void mailbox_free (Mailbox * mailbox);
gint mailbox_check_new_messages (Mailbox * mailbox);


/*
 * watchers
 */
extern guint mailbox_watcher_set (Mailbox * mailbox, MailboxWatcherFunc func, guint16 mask, gpointer data);
extern void  mailbox_watcher_remove (Mailbox * mailbox, guint id);
extern void  mailbox_watcher_remove_by_data (Mailbox * mailbox, gpointer data);
extern void  send_watcher_mark_clear_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_mark_answer_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_mark_read_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_mark_unread_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_mark_delete_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_mark_undelete_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_new_message (Mailbox * mailbox, Message * message, gint remaining);
extern void  send_watcher_delete_message (Mailbox * mailbox, Message * message);
extern void  send_watcher_append_message (Mailbox * mailbox, Message * message);



/*
 * misc mailbox releated functions
 */
MailboxType mailbox_type_from_description (gchar * description);
gchar *mailbox_type_description (MailboxType type);
MailboxType mailbox_valid (gchar * filename);
gboolean mailbox_gather_content_info( Mailbox *mailbox );
void mailbox_commit_flagged_changes( Mailbox *mailbox );





#include "mailbox_local.h"
#include "mailbox_pop3.h"
#include "mailbox_imap.h"

/*
 * public macros
 */
#define MAILBOX(mailbox)        BALSA_MAILBOX(mailbox)
#define MAILBOX_LOCAL(mailbox)  BALSA_MAILBOX_LOCAL(mailbox)
#define MAILBOX_POP3(mailbox)   BALSA_MAILBOX_POP3(mailbox)
#define MAILBOX_IMAP(mailbox)   BALSA_MAILBOX_IMAP(mailbox)

#define MAILBOX_IS_LOCAL(mailbox) BALSA_IS_MAILBOX_LOCAL(mailbox)
#define MAILBOX_IS_POP3(mailbox)  BALSA_IS_MAILBOX_POP3(mailbox)
#define MAILBOX_IS_IMAP(mailbox)  BALSA_IS_MAILBOX_IMAP(mailbox)


#endif /* __MAILBOX_H__ */
