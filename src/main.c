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

#include "config.h"

#include <gnome.h>

#include "balsa-app.h"
#include "main-window.h"
#include "mailbox.h"
#include "misc.h"
#include "save-restore.h"

int
main (int argc, char *argv[])
{
  gtk_init (&argc, &argv);
  gnome_init ("balsa", NULL, argc, argv, 0, NULL);
  init_balsa_app (argc, argv);

  gtk_main ();
  return 0;
}

static gboolean
close_all_mailboxes (GNode * node, gpointer data)
{
  Mailbox *mailbox;

  if (node->data)
    if (((MailboxNode *) node->data)->mailbox)
      {
	mailbox = ((MailboxNode *) node->data)->mailbox;

	if (balsa_app.debug)
	  g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);

	while (mailbox->open_ref > 0)
	  mailbox_open_unref (mailbox);
      }
  return FALSE;
}

void
balsa_exit ()
{
  Mailbox *mailbox;

  g_node_traverse (balsa_app.mailbox_nodes,
		   G_LEVEL_ORDER,
		   G_TRAVERSE_ALL,
		   10,
		   close_all_mailboxes,
		   NULL);

  mailbox = balsa_app.inbox;
  if (balsa_app.debug)
    g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
  while (mailbox->open_ref > 0)
    mailbox_open_unref (mailbox);

  mailbox = balsa_app.outbox;
  if (mailbox)
    {
      if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
      while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
    }

  mailbox = balsa_app.trash;
  if (mailbox)
    {
      if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
      while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
    }

#if 0
  gtk_timeout_remove (balsa_app.check_mail_timer);
  gtk_timeout_remove (balsa_app.new_messages_timer);
#endif

  save_global_settings ();
  gtk_exit (0);
}
