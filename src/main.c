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
#include "cfg-balsa.h"
#include "misc.h"
#include "main.h"

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

static void args_init( int argc, char **argv );
static void close_mailbox( Mailbox *mb );
static void mailboxes_init( void );
static void empty_trash( void );

static void args_init( int argc, char **argv )
{
	static gchar *compose_data;
	static gchar *open_data;

	static struct poptOption options[] = {
		{"checkmail", 'c', POPT_ARG_NONE, &(balsa_state.checkmail), 0, N_("Get new mail on startup"), NULL},
		{"compose", 'm', POPT_ARG_STRING, &compose_data, 0, N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS" },
		{"open-mailbox", 'o', POPT_ARG_STRING, &open_data, 0, N_("Opens MAILBOX1, MAILBOX2, ... upon startup"), "MAILBOX[;MAILBOX2;...]" },
		{"open-unread-mailbox", 'u', POPT_ARG_NONE, &(balsa_state.open_unread_mailbox), 0, N_("Opens first unread mailbox"), NULL },
		{NULL, '\0', 0, NULL, 0} /* end the list */
	};

	/* Set the state to empty */
	balsa_state.open_unread_mailbox = FALSE;
	balsa_state.checkmail = FALSE;
	balsa_state.compose_mode = FALSE;
	balsa_state.compose_to = NULL;
	balsa_state.mbs_to_open = NULL;

	/* Parse our args */
	gnome_init_with_popt_table( PACKAGE, VERSION,
				    argc, argv, options, 0, NULL );

	
	/* Apply them as needed */
	if( compose_data ) {
		balsa_state.compose_mode = TRUE;
		balsa_state.compose_to = compose_data;
	} else {
		balsa_state.compose_mode = FALSE;
		balsa_state.compose_to = NULL;
	}

	if( open_data ) {
		gchar **mbs;
		int i;

		mbs = g_strsplit( open_data, ";", 16 );

		for( i = 0; mbs[i] != NULL; i++ ) 
			balsa_state.mbs_to_open = g_slist_prepend( balsa_state.mbs_to_open, mbs[i] );

		g_strfreev( mbs );
	}
}

static void mailboxes_init( void )
{
  /* initalize our mailbox access crap */
  if (do_load_mailboxes () == FALSE)
    {
      fprintf (stderr, "*** error loading mailboxes\n");
      balsa_init_begin ();
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

  /* Parse our arguments */
  args_init (argc, argv);

  /* Blank out our balsa_app */
  balsa_app_init ();

  /* Session management */
  sm_init( argc, argv );

  /* load mailboxes */
  mailboxes_init();

#ifdef BALSA_USE_THREADS
  /* initiate thread mutexs, variables */
  threads_init( TRUE );
#endif  

  /* create all the pretty icons that balsa uses that
   * aren't part of gnome-libs */
  balsa_icons_init ();

  gnome_triggers_do ("", "program", "balsa", "startup", NULL );

  window = balsa_window_new();
  balsa_app.main_window = BALSA_WINDOW (window);
  gtk_widget_show(window);

  gdk_rgb_init();

#ifdef USE_PIXBUF
  gtk_widget_set_default_colormap( gdk_rgb_get_cmap() );
  gtk_widget_set_default_visual( gdk_rgb_get_visual() );
#else
  gtk_widget_set_default_colormap( gdk_imlib_get_colormap() );
  gtk_widget_set_default_visual( gdk_imlib_get_visual() );
#endif


  if( balsa_state.compose_mode )  {
	  BalsaSendmsg *snd;
	  snd = sendmsg_window_new( window, NULL,SEND_NORMAL );

	  if( balsa_state.compose_to )
		  gtk_entry_set_text( GTK_ENTRY(snd->to[1]), balsa_state.compose_to );

	  gtk_widget_grab_focus( snd->subject[1] );
  }

  /* open mailboxes if requested so */
  if( balsa_state.open_unread_mailbox ) {
	  GList *i;
	  GList *gl = mblist_find_all_unread_mboxes();

	  for( i = g_list_first( gl ); i; i = g_list_next( i ) ) {
		  if( balsa_app.debug )
			  fprintf( stderr, "Opening %s...\n", ((Mailbox*)(i->data))->name );
		  mblist_open_mailbox( (Mailbox*) (i->data) );
	  }

	  g_list_free( gl );
  }

  if( balsa_state.mbs_to_open ) {
	  GSList *iter;

	  for( iter = balsa_state.mbs_to_open; iter; iter = iter->next ) {
		  Mailbox *mbox = mblist_find_mbox_by_name( (gchar *) iter->data );

		  if( mbox ) {
			  if( balsa_app.debug )
				  fprintf( stderr, "Opening %s..\n", (gchar *) iter->data );
			  mblist_open_mailbox( mbox );
		  }
	  }
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

static void close_mailbox( Mailbox *mb )
{
	g_return_if_fail( mb );

	if( balsa_app.debug )
		g_print( "Mailbox: %s Ref: %d\n", mb->name, mb->open_ref );

	while( mb->open_ref > 0 )
		mailbox_open_unref( mb );
}

static gboolean
close_all_mailboxes (GNode * node, gpointer data)
{
	MailboxNode *mbnode;

	if (node->data) {
		gchar *tmpfile;

		mbnode = (MailboxNode *) node->data;

		if (mbnode->IsDir) {
			if (mbnode->expanded) {
				tmpfile = g_strdup_printf ("%s/.expanded", mbnode->name);
				if (access (tmpfile, F_OK) == -1)
					creat (tmpfile, S_IRUSR | S_IWUSR);
				g_free (tmpfile);
			} else {
				tmpfile = g_strdup_printf ("%s/.expanded", mbnode->name);
				if (access (tmpfile, F_OK) != -1)
					unlink (tmpfile);
				g_free (tmpfile);
			}
		}
		
		if (!(mbnode->mailbox))
			return FALSE;
		    
		close_mailbox( mbnode->mailbox );
	}

	return FALSE;
}

void balsa_close_mailboxes( sm_exit_trigger_results_t *res, gpointer user_data )
{
  g_node_traverse (balsa_app.mailbox_nodes,
		   G_LEVEL_ORDER,
		   G_TRAVERSE_ALL,
		   10,
		   close_all_mailboxes,
		   NULL);

  if (balsa_app.empty_trash_on_exit)
	  empty_trash( );

  close_mailbox( balsa_app.inbox );
  close_mailbox( balsa_app.outbox );
  close_mailbox( balsa_app.sentbox );
  close_mailbox( balsa_app.draftbox );
  close_mailbox( balsa_app.trash );

  res->internal_error = FALSE;
  res->external_error = FALSE;
  res->need_interaction = FALSE;
}

static void answer_about_autosave( gint answer, gpointer user_data );
static void answer_about_autosave( gint answer, gpointer user_data )
{
	balsa_state.asked_about_autosave = TRUE;
	balsa_state.do_autosave = answer;

	if( answer )
		balsa_sm_save();
}

static gboolean ask_about_autosave( gboolean *exit_cancelled, gpointer user_data );
static gboolean ask_about_autosave( gboolean *exit_cancelled, gpointer user_data )
{
	GtkWidget *dlog;

	(*exit_cancelled) = FALSE;

	dlog = gnome_question_dialog( _("Do you want Balsa to automatically save your\n"
					"session when you exit? If you have your session\n"
					"manager automatically start Balsa, say yes\n"
					"and Balsa will reappear the way you left it; say\n"
					"no and it will reappear in the same state every\n"
					"time. If you don't understand this, answer no."),
				      answer_about_autosave, NULL );
	
	gtk_widget_show_all( GTK_WIDGET( dlog ) );
	gnome_win_hints_set_layer( GTK_WIDGET( dlog ), WIN_LAYER_ABOVE_DOCK );
	gnome_dialog_run_and_close( GNOME_DIALOG( dlog ) );
	return FALSE;
}

void balsa_maybe_save( sm_exit_trigger_results_t *res, gpointer user_data )
{
	if( balsa_state.asked_about_autosave == FALSE ) {
		res->internal_error = FALSE;
		res->external_error = FALSE;
		res->need_interaction = TRUE;
		res->interactor = ask_about_autosave;
		res->interactor_user_data = NULL;
	} else if( balsa_state.do_autosave ) {
		balsa_sm_save();
	}
}

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

