/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
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
#ifdef HAVE_LIBGNOMEUI_GNOME_WINDOW_ICON_H
#include <libgnomeui/gnome-window-icon.h>
#endif

#ifdef GTKHTML_HAVE_GCONF
# include <gconf/gconf.h>
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "balsa-app.h"
#include "balsa-icons.h"
#include "mblist-window.h"
#include "balsa-mblist.h"
#include "main-window.h"
#include "libbalsa.h"
#include "misc.h"
#include "save-restore.h"
#include "main.h"

#include "libinit_balsa/init_balsa.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"

/* Globals for Thread creation, messaging, pipe I/O */
pthread_t			get_mail_thread;
pthread_t                       send_mail;
pthread_mutex_t			mailbox_lock;
pthread_mutex_t                 send_messages_lock;
pthread_mutex_t                 appbar_lock;
int				checking_mail;
int                             sending_mail;
int				mail_thread_pipes[2];
int                             send_thread_pipes[2];
GIOChannel 		*mail_thread_msg_send;
GIOChannel 		*mail_thread_msg_receive;
GIOChannel              *send_thread_msg_send;
GIOChannel              *send_thread_msg_receive;

/* Thread for updating mblist */
pthread_t mblist_thread;
/* we use the mailbox_lock pthread_mutex */
int updating_mblist;

/* Semaphore to prevent dual use of appbar progressbar */
int updating_progressbar;

static void threads_init( gboolean init );
#endif /* BALSA_USE_THREADS */

static void balsa_init (int argc, char **argv);
static void config_init (void);
static void mailboxes_init (void);
static void empty_trash (void);
static gint balsa_kill_session (GnomeClient* client, gpointer client_data);
static gint balsa_save_session (GnomeClient* client, gint phase, 
                                GnomeSaveStyle save_style, gint is_shutdown, 
                                GnomeInteractStyle interact_style, 
                                gint is_fast, gpointer client_data);


static void
balsa_init (int argc, char **argv)
{
  static struct poptOption options[] = {
         {"checkmail", 'c', POPT_ARG_NONE, &(balsa_app.check_mail_upon_startup), 0, N_("Get new mail on startup"), NULL},
         {"compose", 'm', POPT_ARG_STRING, &(balsa_app.compose_email), 
	  0, N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
         {"open-mailbox", 'o', POPT_ARG_STRING, &(balsa_app.open_mailbox), 
	  0, N_("Opens MAILBOXNAME"),N_("MAILBOXNAME")},
         {"open-unread-mailbox", 'u', POPT_ARG_NONE, &(balsa_app.open_unread_mailbox), 0, N_("Opens first unread mailbox"), NULL},
         {NULL, '\0', 0, NULL, 0} /* end the list */
  };

  gnome_init_with_popt_table (PACKAGE, VERSION, argc, argv, options, 0, NULL);
}

static void
config_init (void)
{
  if (config_load (BALSA_CONFIG_FILE) == FALSE)
    {
      fprintf (stderr, "*** Could not load config file %s!\n",
	       BALSA_CONFIG_FILE);
      balsa_init_begin ();
      /*return;*/
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
    pthread_mutex_init (&appbar_lock, NULL);
    checking_mail = 0;
    updating_mblist = 0;
    sending_mail = 0;
    updating_progressbar = 0;
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
    pthread_mutex_destroy (&appbar_lock);
  }
}
#endif /* BALSA_USE_THREADS */

int
main (int argc, char *argv[])
{
  GtkWidget *window;
  GnomeClient* client;
  gchar *default_icon;
#ifdef GTKHTML_HAVE_GCONF
  GConfError *gconf_error;
#endif

  /* Initialize the i18n stuff */
  bindtextdomain (PACKAGE, GNOMELOCALEDIR);
  textdomain (PACKAGE); 

#ifdef BALSA_USE_THREADS
  /* initiate thread mutexs, variables */
  threads_init( TRUE );
#endif  

  balsa_init (argc, argv);

#ifdef GTKHTML_HAVE_GCONF
  if (!gconf_init(argc, argv, &gconf_error))
    gconf_error_destroy(gconf_error);
  gconf_error = NULL;
#endif

  balsa_app_init ();

  /* Initialize libbalsa */
  libbalsa_init (balsa_error);

#ifdef USE_PIXBUF
  gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
  gtk_widget_set_default_visual(gdk_rgb_get_visual());
#else
  gtk_widget_set_default_colormap(gdk_imlib_get_colormap());
  gtk_widget_set_default_visual(gdk_imlib_get_visual());
#endif
  
  /* Allocate the best colormap we can get */
  balsa_app.visual = gdk_visual_get_best ();
  balsa_app.colormap = gdk_colormap_new (balsa_app.visual, TRUE);

  /* checking for valid config files */
  config_init ();

  /* load mailboxes */
  config_mailboxes_init ();
  mailboxes_init ();

  /* create all the pretty icons that balsa uses that
   * arn't part of gnome-libs */
  balsa_icons_init ();

  default_icon = balsa_pixmap_finder( "balsa/balsa_icon.png" );
#ifdef HAVE_LIBGNOMEUI_GNOME_WINDOW_ICON_H
  gnome_window_icon_set_default_from_file ( default_icon );
#endif
  g_free( default_icon );

  gnome_triggers_do ("", "program", "balsa", "startup", NULL);

  window = balsa_window_new();
  balsa_app.main_window = BALSA_WINDOW (window);

  /* session management */
  client = gnome_master_client ();
  gtk_signal_connect (GTK_OBJECT (client), "save_yourself", GTK_SIGNAL_FUNC (balsa_save_session), argv[0]);
  gtk_signal_connect (GTK_OBJECT (client), "die", GTK_SIGNAL_FUNC (balsa_kill_session), NULL);

  gdk_rgb_init();

  if(balsa_app.compose_email)  {
    BalsaSendmsg *snd;
    snd=sendmsg_window_new(window,NULL,SEND_NORMAL);
    gtk_entry_set_text(GTK_ENTRY(snd->to[1]),balsa_app.compose_email);
    gtk_widget_grab_focus(balsa_app.compose_email[0] 
			  ? snd->subject[1] : snd->to[1]);
  } else gtk_widget_show(window);




  /* open mailboxes if requested so */
  if (balsa_app.open_unread_mailbox) {
     GList * i, *gl = mblist_find_all_unread_mboxes();
     for( i=g_list_first(gl); i; i=g_list_next(i) ) {
	printf("opening %s..\n", (LIBBALSA_MAILBOX(i->data))->name);
	mblist_open_mailbox( LIBBALSA_MAILBOX (i->data) );
     }
     g_list_free(gl);
  }
  if (balsa_app.open_mailbox) {
     gint i =0;
     gchar** names= g_strsplit(balsa_app.open_mailbox,";",20);
     while(names[i]) {
	LibBalsaMailbox *mbox = balsa_find_mbox_by_name(names[i]);
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

  gdk_threads_enter();
  gtk_main();
  gdk_threads_leave();
  
  gdk_colormap_unref (balsa_app.colormap);
  
#ifdef BALSA_USE_THREADS
  threads_init( FALSE );
#endif

  return 0;
}

static void
force_close_mailbox(LibBalsaMailbox *mailbox) {
    if (!mailbox) return;
    if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
    while (mailbox->open_ref > 0)
      libbalsa_mailbox_close(mailbox);
}

/* Word of comment: previous definition of this function used access()
function before attempting creat/unlink operation. In PS opinion, the
speed gain is negligible or negative: the number of called system
functions in present case is constant and equal to 1; the previous
version called system function either once or twice per directory. */
static gboolean
close_all_mailboxes (GNode * node, gpointer data)
{
    MailboxNode *mbnode = (MailboxNode *) node->data;
    
    if (mbnode)
    {
	if (mbnode->IsDir)
	{
	    gchar *tmpfile = g_strdup_printf ("%s/.expanded", mbnode->name);
	    if (mbnode->expanded)
		creat (tmpfile, S_IRUSR | S_IWUSR);
	    else
		unlink (tmpfile);
	    g_free (tmpfile);
	}
	
	force_close_mailbox(mbnode->mailbox);
    }
    return FALSE;
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
  
  libbalsa_mailbox_open (balsa_app.trash, FALSE);
  
  message = balsa_app.trash->message_list;
  
  while(message) {
    libbalsa_message_delete(message->data);
		message = message->next;
  }
  libbalsa_mailbox_commit_changes(balsa_app.trash);
  
  libbalsa_mailbox_close(balsa_app.trash);
  
  if ( balsa_app.notebook && 
       (page=balsa_find_notebook_page(balsa_app.trash)))
    balsa_index_page_reset( page );
}


static gint
balsa_kill_session (GnomeClient* client, gpointer client_data)
{
        balsa_exit ();
        return TRUE;
}
        

static gint
balsa_save_session (GnomeClient* client, gint phase, GnomeSaveStyle save_style,
                    gint is_shutdown, GnomeInteractStyle interact_style, 
                    gint is_fast, gpointer client_data)
{
        gchar** argv;
        guint argc;
        
        /* allocate 0-filled so it will be NULL terminated */
        argv = g_malloc0 (sizeof (gchar*) * 7);
        
        argc = 1;
        argv[0] = client_data;
        
        if (balsa_app.open_unread_mailbox) {
                argv[argc] = g_strdup ("--open-unread-mailbox");
                argc++;
        }
        
        if (balsa_app.check_mail_upon_startup) {
                argv[argc] = g_strdup ("--checkmail");
                argc++;
        }

        if (balsa_app.open_mailbox) {
                argv[argc] = g_strdup ("--open-mailbox");
                argc++;
                
                argv[argc] = g_strconcat ("'", balsa_app.open_mailbox, "'", NULL);
                argc++;
        }

        if (balsa_app.compose_email) {
                argv[argc] = g_strdup ("--compose");
                argc++;

                argv[argc] = g_strdup (balsa_app.compose_email);
                argc++;
        }
                
        gnome_client_set_clone_command (client, argc, argv);
        gnome_client_set_restart_command (client, argc, argv);

        return TRUE;
}
