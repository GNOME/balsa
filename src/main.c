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

#include "config.h"

#include <gnome.h>
#include <libgnorba/gnorba.h>
#include <orb/orbit.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-init.h"
#include "main-window.h"
#include "libbalsa.h"
#include "misc.h"
#include "save-restore.h"
#include "main.h"
#include "balsa-impl.c"

#ifdef BALSA_USE_THREADS
#include "threads.h"


/* Globals for Thread creation, messaging, pipe I/O */
pthread_t			get_mail_thread;
pthread_mutex_t			mailbox_lock;
int				checking_mail;
int				mail_thread_pipes[2];
GIOChannel 		*mail_thread_msg_send;
GIOChannel 		*mail_thread_msg_receive;

static void threads_init( gboolean init );
#endif

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
  static struct poptOption options[] = {
         {"checkmail", 'c', POPT_ARG_NONE, &(balsa_app.check_mail_upon_startup), 0, N_("Get new mail on startup"), NULL},
         {NULL, '\0', 0, NULL, 0} /* end the list */
  };

  CORBA_exception_init (&ev);

  orb = gnome_CORBA_init_with_popt_table ("balsa", VERSION,
			  &argc, argv, options, 0, NULL,
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

  goad_server_register (CORBA_OBJECT_NIL,
			balsa_servant, "balsa_mail_send", "server", &ev);
}

static void
config_init (void)
{
  if (config_load (BALSA_CONFIG_FILE) == FALSE)
    {
      fprintf (stderr, "*** Could not load config file %s!\n",
	       BALSA_CONFIG_FILE);
      initialize_balsa ();
      //return;
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
      balsa_app.sentbox == NULL || balsa_app.trash == NULL ||
      balsa_app.draftbox == NULL)
    {
      fprintf (stderr, 
               "*** One of inbox/outbox/sentbox/draftbox/trash is NULL\n");
      initialize_balsa ();
      return;
    }
}


#ifdef BALSA_USE_THREADS
void
threads_init( gboolean init )
{
  if( init )
  {
    g_thread_init( NULL );
    pthread_mutex_init( &mailbox_lock, NULL );
    checking_mail = 0;
	if( pipe( mail_thread_pipes) < 0 )
	{
	   g_log ("BALSA Init", G_LOG_LEVEL_DEBUG, "Error opening pipes.\n" );
	}
	mail_thread_msg_send = g_io_channel_unix_new ( mail_thread_pipes[1] );
	mail_thread_msg_receive = g_io_channel_unix_new ( mail_thread_pipes[0] );
	g_io_add_watch ( mail_thread_msg_receive, G_IO_IN,
					mail_progress_notify_cb,
					NULL );
					
  }
  else
  {
    pthread_mutex_destroy( &mailbox_lock );
  }
}
#endif



int
main (int argc, char *argv[])
{
  GtkWidget *window;

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE);
 
  balsa_init (argc, argv);

  balsa_app_init ();

  /* checking for valid config files */
  config_init ();

  /* load mailboxes */
  config_mailboxes_init ();
  mailboxes_init ();

#ifdef BALSA_USE_THREADS
  /* initiate thread mutexs, variables */
  threads_init( TRUE );
#endif  

  /* create all the pretty icons that balsa uses that
   * arn't part of gnome-libs */
  balsa_icons_init ();

  gnome_triggers_do ("", "program", "balsa", "startup", NULL);

  window = balsa_window_new();
  gtk_widget_show(window);

  gtk_main ();

#ifdef BALSA_USE_THREADS
  threads_init( FALSE );
#endif

  return 0;
}

static gboolean
close_all_mailboxes (GNode * node, gpointer data)
{
  Mailbox *mailbox;
  MailboxNode *mbnode;

  if (node->data)
    {
      mbnode = (MailboxNode *) node->data;

      if (mbnode)
	{
	  gchar *tmpfile;

	  if (mbnode->IsDir)
	    {
	      if (mbnode->expanded)
		{
		  tmpfile = g_strdup_printf ("%s/.expanded", mbnode->name);
		  if (access (tmpfile, F_OK) == -1)
		    creat (tmpfile, S_IRUSR | S_IWUSR);
		  g_free (tmpfile);
		}
	      else
		{
		  tmpfile = g_strdup_printf ("%s/.expanded", mbnode->name);
		  if (access (tmpfile, F_OK) != -1)
		    unlink (tmpfile);
		  g_free (tmpfile);
		}
	    }

	  mailbox = mbnode->mailbox;

	  if (!mailbox)
	    return FALSE;

	  if (balsa_app.debug)
	    g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);

	  while (mailbox->open_ref > 0)
	    mailbox_open_unref (mailbox);
	}
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

  mailbox = balsa_app.sentbox;
  if (mailbox)
    {
      if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
      while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
    }

  mailbox = balsa_app.draftbox;
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

  gtk_main_quit();
}
