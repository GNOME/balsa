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
#include "mblist-window.h"
#include "main-window.h"
#include "libbalsa.h"
#include "misc.h"
#include "save-restore.h"
#include "main.h"
#include "balsa-impl.c"

#include "libinit_balsa/init_balsa.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"

/* Globals for Thread creation, messaging, pipe I/O */
pthread_t			get_mail_thread;
pthread_t                       send_mail;
pthread_mutex_t			mailbox_lock;
pthread_mutex_t                 send_messages_lock;
int				checking_mail;
int                             sending_mail;
int				mail_thread_pipes[2];
int                             send_thread_pipes[2];
GIOChannel 		*mail_thread_msg_send;
GIOChannel 		*mail_thread_msg_receive;
GIOChannel              *send_thread_msg_send;
GIOChannel              *send_thread_msg_receive;


static void threads_init( gboolean init );
#endif /* BALSA_USE_THREADS */

static void balsa_init (int argc, char **argv);
static void config_init (void);
static void mailboxes_init (void);
static void empty_trash (void);

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
         {"compose", 'm', POPT_ARG_STRING, &(balsa_app.compose_email), 
	  0, N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
         {"open-mailbox", 'o', POPT_ARG_STRING, &(balsa_app.open_mailbox), 
	  0, N_("Opens MAILBOXNAME"),N_("MAILBOXNAME")},
         {"open-unread-mailbox", 'u', POPT_ARG_NONE, &(balsa_app.open_unread_mailbox), 0, N_("Opens first unread mailbox"), NULL},
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
      balsa_init_begin ();
      //return;
    }

  /* Load all the global settings.  If there's an error, then some crucial
     piece of the global settings was not available, and we need to run
     balsa-init. */
  if (config_global_load () == FALSE)
    {
      fprintf (stderr, "*** config_global_load failed\n");
      balsa_init_begin ();
      return;
    }
}

static void
mailboxes_init (void)
{
  /* initalize our mailbox access crap */
  if ( !do_load_mailboxes () )
    {
      fprintf (stderr, "*** error loading mailboxes\n");
      balsa_init_begin ();
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
    pthread_mutex_init( &send_messages_lock, NULL );
    checking_mail = 0;
    sending_mail = 0;
	if( pipe( mail_thread_pipes) < 0 )
	{
	   g_log ("BALSA Init", G_LOG_LEVEL_DEBUG, "Error opening pipes.\n" );
	}
	mail_thread_msg_send = g_io_channel_unix_new ( mail_thread_pipes[1] );
	mail_thread_msg_receive = g_io_channel_unix_new ( mail_thread_pipes[0] );
	g_io_add_watch ( mail_thread_msg_receive, G_IO_IN,
					(GIOFunc) mail_progress_notify_cb,
					NULL );

	if( pipe( send_thread_pipes) < 0 )
	{
	   g_log ("BALSA Init", G_LOG_LEVEL_DEBUG, "Error opening pipes.\n" );
	}
	send_thread_msg_send = g_io_channel_unix_new ( send_thread_pipes[1] );
	send_thread_msg_receive = g_io_channel_unix_new ( send_thread_pipes[0] );
	g_io_add_watch ( send_thread_msg_receive, G_IO_IN,
					(GIOFunc) send_progress_notify_cb,
					NULL );
					
  }
  else
  {
    pthread_mutex_destroy( &mailbox_lock );
    pthread_mutex_destroy( &send_messages_lock );
  }
}
#endif /* BALSA_USE_THREADS */



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
  balsa_app.main_window = BALSA_WINDOW (window);


  gdk_rgb_init();

  if(balsa_app.compose_email)  {
    BalsaSendmsg *snd;
    snd=sendmsg_window_new(window,NULL,SEND_NORMAL);
    gtk_entry_set_text(GTK_ENTRY(snd->to[1]),balsa_app.compose_email);
    gtk_widget_grab_focus(balsa_app.compose_email[0] 
			  ? snd->subject[1] : snd->to[1]);
  } else gtk_widget_show(window);


#ifdef USE_PIXBUF
  gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
  gtk_widget_set_default_visual(gdk_rgb_get_visual());
#else
  gtk_widget_set_default_colormap(gdk_imlib_get_colormap());
  gtk_widget_set_default_visual(gdk_imlib_get_visual());
#endif

  /* open mailboxes if requested so */
  if (balsa_app.open_unread_mailbox) {
     GList * i, *gl = mblist_find_all_unread_mboxes();
     for( i=g_list_first(gl); i; i=g_list_next(i) ) {
	printf("opening %s..\n", ((Mailbox*)(i->data))->name);
	mblist_open_mailbox( (Mailbox*) (i->data) );
     }
     g_list_free(gl);
  }
  if (balsa_app.open_mailbox) {
     gint i =0;
     gchar** names= g_strsplit(balsa_app.open_mailbox,";",20);
     while(names[i]) {
	Mailbox *mbox = mblist_find_mbox_by_name(names[i]);
	if(balsa_app.debug)
	    fprintf(stderr,"opening %s => %p..\n", names[i], mbox);
	if(mbox) {
	   mblist_open_mailbox(mbox);
	}
	i++;
     }
     g_strfreev(names);
  }

  /* TODO: select the first one, if any is open */ 
  if(gtk_notebook_get_current_page( GTK_NOTEBOOK(balsa_app.notebook) ) >=0 ) 
     gtk_notebook_set_page( GTK_NOTEBOOK(balsa_app.notebook), 0);

  gtk_main();

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

static void
force_close_mailbox(Mailbox *mailbox) {
    if (!mailbox) return;
    if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
    while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
}


void
balsa_exit (void)
{
  g_node_traverse (balsa_app.mailbox_nodes,
		   G_LEVEL_ORDER,
		   G_TRAVERSE_ALL,
		   10,
		   close_all_mailboxes,
		   NULL);

  if (balsa_app.empty_trash_on_exit)
	  empty_trash( );

  force_close_mailbox(balsa_app.inbox);
  force_close_mailbox(balsa_app.outbox);
  force_close_mailbox(balsa_app.sentbox);
  force_close_mailbox(balsa_app.draftbox);
  force_close_mailbox(balsa_app.trash);

  if (balsa_app.proplist)
    config_global_save ();

  gnome_sound_shutdown ();

  gtk_main_quit();
}

/* balsa_window_destroy
   It may be called from balsa_window_destroy or balsa_exit; this is why
   it should not make assumptions about the presence of the like
   the notebook and so on.
*/
static void
empty_trash( void )
{
	BalsaIndexPage *page;
	GList *message;

	balsa_mailbox_open(balsa_app.trash);

	message = balsa_app.trash->message_list;

	while(message) {
		message_delete(message->data);
		message = message->next;
	}
	mailbox_commit_flagged_changes(balsa_app.trash);

	balsa_mailbox_close(balsa_app.trash);

	if ( balsa_app.notebook && 
	     (page=balsa_find_notebook_page(balsa_app.trash)))
		balsa_index_page_reset( page );
}


/* Eeew. But I'm tired of ifdefs. -- PGKW */
/* Don't EVER EVER EVER call this even for a joke -- it recurses. */
static void __lame_hack_to_avoid_unused_warnings( void );
typedef void (*__lame_funcptr)( void );
static void __lame_hack_to_avoid_unused_warnings( void ) 
{
	__lame_funcptr i_b_m_i__c = (__lame_funcptr) impl_balsa_mailbox_info__create;
	__lame_funcptr self = (__lame_funcptr) __lame_hack_to_avoid_unused_warnings;

	i_b_m_i__c();
	self();
}


