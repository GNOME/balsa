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


/*
 * supported mailbox types
 */
typedef enum
{
  MAILBOX_MBX,		/* fastest preformance */
  MAILBOX_MTX,		/* good  (pine default mailbox format) */
  MAILBOX_TENEX,	/* good */
  MAILBOX_MBOX,		/* fair */
  MAILBOX_MMDF,		/* fair */
  MAILBOX_UNIX,		/* fair */
  MAILBOX_MH,		/* very poor */
  MAILBOX_POP3,
  MAILBOX_IMAP,
  MAILBOX_NNTP,
  MAILBOX_UNKNOWN
} MailboxType;



/*
 * macros for casting mailbox structures
 */
#define MAILBOX(mailbox)        ((Mailbox *)(mailbox))
#define MAILBOX_LOCAL(mailbox)  ((MailboxLocal *)(mailbox))
#define MAILBOX_POP3(mailbox)   ((MailboxPOP3 *)(mailbox))
#define MAILBOX_IMAP(mailbox)   ((MailboxIMAP *)(mailbox))
#define MAILBOX_NNTP(mailbox)   ((MailboxNNTP *)(mailbox))



typedef struct _Mailbox Mailbox;
typedef struct _MailboxLocal MailboxLocal;
typedef struct _MailboxPOP3 MailboxPOP3;
typedef struct _MailboxIMAP MailboxIMAP;
typedef struct _MailboxNNTP MailboxNNTP;

typedef struct _MessageHeader MessageHeader;


struct _Mailbox
{
  MailboxType type;
  gchar *name;
  void *private;
  guint open_ref;

  glong messages;
  glong new_messages;
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
};

struct _MailboxNNTP
{
  Mailbox mailbox;
  gchar *user;
  gchar *passwd;
  gchar *server;
  gchar *newsgroup;
};


/*
 * message structures
 */
struct _MessageHeader
{
  gchar *from;
  gchar *subject;
  gchar *date;
};



/*
 * call before using any mailbox functions
 */
void mailbox_init ();


/* 
 * open and close a mailbox -- but don't use mailbox_close
 * directly unless you know what you're doing, the mailbox
 * is normally closed when it has a open_ref == 0
 */
int mailbox_open_ref (Mailbox * mailbox);
void mailbox_open_unref (Mailbox * mailbox);
void mailbox_close (Mailbox * mailbox);


/*
 * create and destroy a mailbox
 */
Mailbox *mailbox_new (MailboxType type);
void mailbox_free (Mailbox * mailbox);
gint mailbox_check_new_messages (Mailbox * mailbox);


/* 
 * mailboxes & messages
 */
void mailbox_message_delete (Mailbox * mailbox, glong msgno);
void mailbox_message_undelete (Mailbox * mailbox, glong msgno);
MessageHeader * mailbox_message_header (Mailbox * mailbox, glong msgno, gint allocate);



/*
 * misc mailbox releated functions
 */
MailboxType mailbox_type_from_description (gchar * description);
gchar * mailbox_type_description (MailboxType type);
MailboxType mailbox_valid (gchar * filename);


#endif /* __MAILBOX_H__ */
