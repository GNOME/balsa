/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Stuart Parmenter and Jay Painter
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

#include "libbalsa.h"
#include "libmutt/mutt.h"
#include "libmutt/mailbox.h"
#include "libmutt/imap.h"

static MailboxClass *parent_class = NULL;
//static guint mailbox_signals[LAST_SIGNAL] = { 0 };

static void balsa_mailbox_imap_destroy (GtkObject *object);
static void balsa_mailbox_imap_class_init (MailboxIMAPClass *klass);
static void balsa_mailbox_imap_init(MailboxIMAP *mailbox);

GtkType
balsa_mailbox_imap_get_type (void)
{
  static GtkType mailbox_type = 0;

  if (!mailbox_type)
    {
      static const GtkTypeInfo mailbox_info =
      {
	"MailboxIMAP",
	sizeof (MailboxIMAP),
	sizeof (MailboxIMAPClass),
	(GtkClassInitFunc) balsa_mailbox_imap_class_init,
	(GtkObjectInitFunc) balsa_mailbox_imap_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      mailbox_type = gtk_type_unique(balsa_mailbox_get_type(), &mailbox_info);
    }

  return mailbox_type;
}

static void
balsa_mailbox_imap_class_init (MailboxIMAPClass *klass)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass*) klass;

  parent_class = gtk_type_class(balsa_mailbox_get_type());

  object_class->destroy = balsa_mailbox_imap_destroy;
}

static void
balsa_mailbox_imap_init(MailboxIMAP *mailbox)
{
  mailbox->server = server_new(SERVER_IMAP);
  mailbox->path = NULL;
  mailbox->tmp_file_path = NULL;
}

static void
balsa_mailbox_imap_destroy (GtkObject *object)
{
  MailboxIMAP *mailbox = BALSA_MAILBOX_IMAP(object);

  if (!mailbox)
    return;

  server_free(mailbox->server);
  g_free(mailbox->path);

  if (GTK_OBJECT_CLASS(parent_class)->destroy)
    (*GTK_OBJECT_CLASS(parent_class)->destroy)(GTK_OBJECT(object));
}

/* mailbox_imap_has_new_messages:
   returns non-zero when the IMAP mbox in question has new messages.
   should it load new messages, too?
*/
gint
mailbox_imap_has_new_messages(MailboxIMAP *mailbox)
{
    gint res;
    gchar * tmp;

    g_assert(mailbox!=NULL);

    if(MAILBOX(mailbox)->has_unread_messages)
	return MAILBOX(mailbox)->has_unread_messages;

    tmp = g_strdup_printf("{%s:%i}%s", 
			  mailbox->server->host,
			  mailbox->server->port,
			  mailbox->path);
    set_imap_username ( MAILBOX(mailbox) );
    res = imap_buffy_check (tmp);
    g_free(tmp);
    /* if(res) MAILBOX(mailbox)->has_unread_messages = res; */
    return res;
}

#if 0
/* mailbox_imap_has_new_messages:
   returns non-zero when the IMAP mbox in question has new messages.
   should it load new messages, too?

   REMARK: imap is now checked as ordinary file mailboxes, via Buffy system.
*/
gint
mailbox_imap_has_new_messages(MailboxIMAP *mailbox)
{
    gint res;
    gchar * tmp;

    g_assert(mailbox!=NULL);

    if(MAILBOX(mailbox)->has_unread_messages)
	return MAILBOX(mailbox)->has_unread_messages;

    tmp = g_strdup_printf("{%s:%i}%s", 
			  mailbox->server->host,
			  mailbox->server->port,
			  mailbox->path);
    set_imap_username ( MAILBOX(mailbox) );
    res = imap_buffy_check (tmp);
    g_free(tmp);
    /* if(res) MAILBOX(mailbox)->has_unread_messages = res; */
    return res;
}
#endif
