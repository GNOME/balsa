/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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

#include "mutt.h"

/*
 * public macros
 */
#define MAILBOX(mailbox)        ((Mailbox *)(mailbox))
#define MAILBOX_LOCAL(mailbox)  ((MailboxLocal *)(mailbox))
#define MAILBOX_POP3(mailbox)   ((MailboxPOP3 *)(mailbox))
#define MAILBOX_IMAP(mailbox)   ((MailboxIMAP *)(mailbox))


typedef *(my_variadic_function)(gchar* fmt, ...);

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
  }
MailboxType;


typedef enum
  {
    MESSAGE_MARK_CLEAR,		/* clear all flags */
    MESSAGE_MARK_ANSWER,	/* message has been answered */
    MESSAGE_MARK_READ,		/* message has changed from new to read */
    MESSAGE_MARK_UNREAD,	/* message has changed from read to new */
    MESSAGE_MARK_DELETE,	/* message has been marked deleted */
    MESSAGE_MARK_UNDELETE,	/* message has been marked undeleted */
    MESSAGE_DELETE,		/* message has been deleted */
    MESSAGE_NEW,		/* message is new to the mailbox */
    MESSAGE_FLAGGED,		/* the message was flagged */
    MESSAGE_REPLIED		/* the message was answered */
  }
MailboxWatcherMessageType;


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
    MESSAGE_REPLIED_MASK = 1 << 9
  }
MailboxWatcherMessageMask;


typedef enum
  {
    MESSAGE_FLAG_NEW = 1 << 1,
    MESSAGE_FLAG_DELETED = 1 << 2,
    MESSAGE_FLAG_REPLIED = 1 << 3,
    MESSAGE_FLAG_FLAGGED = 1 << 4
  }
MessageFlags;


/*
 * strucutres
 */
typedef struct _Mailbox Mailbox;
typedef struct _MailboxLocal MailboxLocal;
typedef struct _MailboxPOP3 MailboxPOP3;
typedef struct _MailboxIMAP MailboxIMAP;
typedef struct _MailboxNNTP MailboxNNTP;

typedef struct _MailboxWatcherMessage MailboxWatcherMessage;
typedef struct _MailboxWatcherMessageNew MailboxWatcherMessageNew;

typedef struct _Message Message;
typedef struct _Address Address;
typedef struct _Body Body;

struct _Mailbox
  {
    MailboxType type;
    gchar *name;
    void *private;
    guint open_ref;

    glong messages;
    glong new_messages;
    GList *message_list;
  };


struct _MailboxLocal
  {
    Mailbox mailbox;
    gchar *path;
  };


struct _MailboxPOP3
  {
    Mailbox mailbox;
    gchar *user;
    gchar *passwd;
    gchar *server;
  };


struct _MailboxIMAP
  {
    Mailbox mailbox;
    gchar *user;
    gchar *passwd;
    gchar *server;
    gchar *path;
    gint port;
    gchar *tmp_file_path;
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


struct _Message
  {
    /* the mailbox this message belongs to */
    Mailbox *mailbox;

    /* flags */
    MessageFlags flags;

    /* the ordered numberic index of this message in 
     * the mailbox beginning from 1, not 0 */
    glong msgno;

    /* remail header if any */
    gchar *remail;

    /* message composition date string */
    gchar *date;

    /* from, sender, and reply addresses */
    Address *from;
    Address *sender;
    Address *reply_to;

    /* subject line */
    gchar *subject;

    /* primary, secondary, and blind recipent lists */
    GList *to_list;
    GList *cc_list;
    GList *bcc_list;

    /* replied message ID */
    gchar *in_reply_to;

    /* message ID */
    gchar *message_id;

    /* message body */
    guint body_ref;
    GList *body_list;
  };


struct _Address
  {
    gchar *personal;		/* full text name */
    gchar *mailbox;		/* user name and host (mailbox name) on remote system */
  };


struct _Body
  {
    gchar *buffer;		/* holds raw data of the MIME part, or NULL */
    gchar *htmlized;		/* holds htmlrep of buffer, or NULL */
    BODY *mutt_body;		/* pointer to BODY struct of mutt message */
  };


/*
 * function typedefs
 */
typedef void (*MailboxWatcherFunc) (MailboxWatcherMessage * arg1);


/*
 * call before using any mailbox functions
 */
void mailbox_init (gchar * inbox, my_variadic_function);

gint set_imap_username (Mailbox * mb);
void check_all_pop3_hosts (Mailbox *);
void add_mailboxes_for_checking (Mailbox *);
gint mailbox_have_new_messages (gchar * path);
GList *make_list_from_string (gchar *);

/* 
 * open and close a mailbox 
 */
int mailbox_open_ref (Mailbox * mailbox);
void mailbox_open_unref (Mailbox * mailbox);


/*
 * create and destroy a mailbox structure
 */
Mailbox *mailbox_new (MailboxType type);
void mailbox_free (Mailbox * mailbox);
gint mailbox_check_new_messages (Mailbox * mailbox);


/*
 * watchers
 */
guint mailbox_watcher_set (Mailbox * mailbox, MailboxWatcherFunc func, guint16 mask, gpointer data);
void mailbox_watcher_remove (Mailbox * mailbox, guint id);
void mailbox_watcher_remove_by_data (Mailbox * mailbox, gpointer data);


/*
 * messages
 */
Message *message_new ();
void message_free (Message * message);

void message_move (Message * message, Mailbox * mailbox);

void message_read (Message * message);
void message_unread (Message * message);
void message_delete (Message * message);
void message_undelete (Message * message);

void message_answer (Message * message);

void message_body_ref (Message * message);
void message_body_unref (Message * message);


/*
 * addresses
 */
Address *address_new ();
void address_free (Address * address);


/*
 * body
 */
Body *body_new ();
void body_free (Body * body);


/*
 * misc mailbox releated functions
 */
MailboxType mailbox_type_from_description (gchar * description);
gchar *mailbox_type_description (MailboxType type);
MailboxType mailbox_valid (gchar * filename);
gchar *message_pathname(Message *message);

#endif /* __MAILBOX_H__ */
