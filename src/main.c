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
#include <orb/orbit.h>
#include "balsa-app.h"
#include "main-window.h"
#include "mailbox.h"
#include "misc.h"
#include "save-restore.h"

#include "main.h"

#include "balsa-impl.c"

void Exception (CORBA_Environment *);

void
Exception (CORBA_Environment * ev)
{
  switch (ev->_major)
    {
    case CORBA_SYSTEM_EXCEPTION:
      g_log ("BALSA Server", G_LOG_LEVEL_DEBUG, "CORBA system exception %s.\n",
	     CORBA_exception_id (ev));
      exit (1);
    case CORBA_USER_EXCEPTION:
      g_log ("BALSA Server", G_LOG_LEVEL_DEBUG, "CORBA user exception: %s.\n",
	     CORBA_exception_id (ev));
      exit (1);
    default:
      break;
    }
}


int
main (int argc, char *argv[])
{
  CORBA_ORB orb;
  CORBA_Environment ev;
  PortableServer_ObjectId *oid;
  balsa_mail_send balsa_servant;
  PortableServer_POA root_poa;
  PortableServer_POAManager pm;
  CORBA_char *objref;


  CORBA_exception_init (&ev);
  orb = gnome_CORBA_init ("balsa", NULL, &argc, argv, 0, NULL, &ev);
  Exception (&ev);

  root_poa = (PortableServer_POA) CORBA_ORB_resolve_initial_references (orb, "RootPOA", &ev);
  Exception (&ev);

  balsa_servant = impl_balsa_mail_send__create (root_poa, &ev);
  Exception (&ev);

  pm = PortableServer_POA__get_the_POAManager (root_poa, &ev);
  Exception (&ev);

  PortableServer_POAManager_activate (pm, &ev);
  Exception (&ev);

  objref = CORBA_ORB_object_to_string (orb, balsa_servant, &ev);

  printf ("%s\n", objref);
  fflush (stdout);

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

	if (!mailbox)
	  return FALSE;

	if (balsa_app.debug)
	  g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);

	while (mailbox->open_ref > 0)
	  mailbox_open_unref (mailbox);
      }
  return FALSE;
}

void
balsa_exit (void)
{
  Mailbox *mailbox;

  g_node_traverse (balsa_app.mailbox_nodes,
		   G_LEVEL_ORDER,
		   G_TRAVERSE_ALL,
		   10,
		   close_all_mailboxes,
		   NULL);

  mailbox = balsa_app.inbox;
  if (mailbox)
    {
      if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
      while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
    }

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

  if (balsa_app.proplist)
    config_global_save ();

  gnome_sound_shutdown ();

  gtk_exit (0);
}
