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
#include <libgnorba/gnorba.h>
#include <orb/orbit.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-init.h"
#include "main-window.h"
#include "mailbox.h"
#include "misc.h"
#include "save-restore.h"

#include "main.h"

#include "balsa-impl.c"


static void balsa_init (int argc, char **argv);
static void config_init (void);
static void mailboxes_init (void);

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


static void
balsa_init (int argc, char **argv)
{
  CORBA_ORB orb;
  CORBA_Environment ev;
  balsa_mail_send balsa_servant;
  PortableServer_POA root_poa;
  PortableServer_POAManager pm;
  CORBA_char *objref;

  CORBA_exception_init (&ev);

  orb = gnome_CORBA_init ("balsa", VERSION,
			  &argc, argv,
			  GNORBA_INIT_SERVER_FUNC,
			  &ev);

  Exception (&ev);

  root_poa = (PortableServer_POA) CORBA_ORB_resolve_initial_references (orb,
							    "RootPOA", &ev);
  Exception (&ev);

  balsa_servant = impl_balsa_mail_send__create (root_poa, &ev);
  Exception (&ev);

  pm = PortableServer_POA__get_the_POAManager (root_poa, &ev);
  Exception (&ev);

  PortableServer_POAManager_activate (pm, &ev);
  Exception (&ev);

  objref = CORBA_ORB_object_to_string (orb, balsa_servant, &ev);

  g_print ("%s\n", objref);
  fflush (stdout);
}

static void
config_init (void)
{
  if (config_load (BALSA_CONFIG_FILE) == FALSE)
    {
      fprintf (stderr, "*** Could not load config file %s!\n",
	       BALSA_CONFIG_FILE);
      initialize_balsa ();
      return;
    }

  /* Load all the global settings.  If there's an error, then some crucial
     piece of the global settings was not available, and we need to run
     balsa-init. */
  if (config_global_load () == FALSE)
    {
      fprintf (stderr, "*** config_global_load failed\n");
      initialize_balsa ();
      return;
    }
}

static void
mailboxes_init (void)
{
  /* initalize our mailbox access crap */
  if (do_load_mailboxes () == FALSE)
    {
      fprintf (stderr, "*** error loading mailboxes\n");
      initialize_balsa ();
      return;
    }

  /* At this point, if inbox/outbox/trash are still null, then we
     were not able to locate the settings for them anywhere in our
     configuartion and should run balsa-init. */
  if (balsa_app.inbox == NULL || balsa_app.outbox == NULL ||
      balsa_app.trash == NULL)
    {
      fprintf (stderr, "*** One of inbox/outbox/trash is NULL\n");
      initialize_balsa ();
      return;
    }
}

int
main (int argc, char *argv[])
{
  balsa_init (argc, argv);

  balsa_app_init ();

  /* checking for valid config files */
  config_init ();

  /* load mailboxes */
  config_mailboxes_init ();
  mailboxes_init ();

  /* create all the pretty icons that balsa uses that
   * arn't part of gnome-libs */
  balsa_icons_init();
  
  gnome_triggers_do ("", "program", "balsa", "startup", NULL);

  main_window_init ();

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

  if (balsa_app.proplist)
    config_global_save ();

  gnome_sound_shutdown ();

  gtk_exit (0);
}
