/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

#define _XOPEN_SOURCE 500
#include "config.h"

#include <gnome.h>
#include <libgnomeui/gnome-window-icon.h>

#ifdef GTKHTML_HAVE_GCONF
# include <gconf/gconf.h>
#endif

#include <signal.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#ifdef HAVE_RUBRICA
#include <libxml/xmlversion.h>
#endif

#include <glib/gi18n.h>

#include "address-entry.h"

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "filter.h"
#include "main-window.h"
#include "libbalsa.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "sendmsg-window.h"
#include "information.h"
#include "imap-server.h"

#include "libinit_balsa/init_balsa.h"

#include "Balsa.h"
#include "balsa-bonobo.h"
#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-exception.h>

#ifdef HAVE_GPGME
#include <string.h>
#include <gpgme.h>
#endif

#ifdef BALSA_USE_THREADS
#include "threads.h"

/* Globals for Thread creation, messaging, pipe I/O */
pthread_t get_mail_thread;
pthread_t send_mail;
pthread_mutex_t send_messages_lock;
int checking_mail;
int mail_thread_pipes[2];
int send_thread_pipes[2];
GIOChannel *mail_thread_msg_send;
GIOChannel *mail_thread_msg_receive;
GIOChannel *send_thread_msg_send;
GIOChannel *send_thread_msg_receive;

static void threads_init(void);
static void threads_destroy(void);
#endif				/* BALSA_USE_THREADS */

static void balsa_init(int argc, char **argv);
static void config_init(gboolean check_only);
static void mailboxes_init(gboolean check_only);
static void balsa_cleanup(void);
static gint balsa_kill_session(GnomeClient * client, gpointer client_data);
static gint balsa_save_session(GnomeClient * client, gint phase,
			       GnomeSaveStyle save_style, gint is_shutdown,
			       GnomeInteractStyle interact_style,
			       gint is_fast, gpointer client_data);
gboolean initial_open_unread_mailboxes(void); 
/* yes void is there cause gcc is tha suck */
gboolean initial_open_inbox(void);

/* We need separate variable for storing command line requests to check the 
   mail because such selection cannot be stored in balsa_app and later 
   saved to the configuration file.
*/
static gchar *cmd_line_open_mailboxes;
static gboolean cmd_check_mail_on_startup,
    cmd_open_unread_mailbox, cmd_open_inbox, cmd_get_stats;

/* opt_attach_list: list of attachments */
static GSList* opt_attach_list = NULL;
/* opt_compose_email: To: field for the compose window */
static gchar *opt_compose_email = NULL;

static void
accel_map_load(void)
{
    gchar *accel_map_filename =
        g_build_filename(g_get_home_dir(), ".balsa", "accelmap", NULL);
    gtk_accel_map_load(accel_map_filename);
    g_free(accel_map_filename);
}

static void
accel_map_save(void)
{
    gchar *accel_map_filename =
        g_build_filename(g_get_home_dir(), ".balsa", "accelmap", NULL);
    gtk_accel_map_save(accel_map_filename);
    g_free(accel_map_filename);
}

static void
balsa_handle_automation_options() {
   CORBA_Object factory;
   CORBA_Environment ev;
   BonoboObject *balsacomposer;
   BonoboObject *balsaapp;
 
   CORBA_exception_init (&ev);

   factory = bonobo_activation_activate_from_id 
       ("OAFIID:GNOME_Balsa_Application_Factory",
	Bonobo_ACTIVATION_FLAG_EXISTING_ONLY,
	NULL, &ev);
   
   if ( !(BONOBO_EX (&ev) || factory == CORBA_OBJECT_NIL) ) {
       /* there already is a server. good */
       CORBA_Object app;
       printf("Another Balsa found. Talking to it...\n");
       app =  
	   bonobo_activation_activate_from_id ("OAFIID:GNOME_Balsa_Application",
						   0, NULL, &ev);

       if (cmd_check_mail_on_startup) 
	   GNOME_Balsa_Application_checkmail (app, &ev);

       if (cmd_open_unread_mailbox)
	   GNOME_Balsa_Application_openUnread (app, &ev);

       if (cmd_open_inbox)
	   GNOME_Balsa_Application_openInbox (app, &ev);

       if (cmd_get_stats) {
           CORBA_long unread = 0, unsent = 0;
	   GNOME_Balsa_Application_getStats (app, &unread, &unsent, &ev);
           printf("Unread: %ld Unsent: %ld\n", (long)unread, (long)unsent);
       }

       if (cmd_line_open_mailboxes)
	   GNOME_Balsa_Application_openMailbox (app,
					       cmd_line_open_mailboxes,
					       &ev);

       if (opt_compose_email || opt_attach_list) {
	   GNOME_Balsa_Composer_attachs *attachs;
	   CORBA_Object server;
	   
	   attachs = CORBA_sequence_CORBA_string__alloc();
	   server =  
	       bonobo_activation_activate_from_id ("OAFIID:GNOME_Balsa_Composer",
						   0, NULL, &ev);
	   if(opt_attach_list) {
	       gint i,l;
	       l = g_slist_length(opt_attach_list);
	       attachs->_buffer = 
		   CORBA_sequence_CORBA_string_allocbuf(l);
	       attachs->_length = l;
	       
	       
	       for( i = 0 ; i < l; i++) {
		   attachs->_buffer[i] = 
		       g_slist_nth_data( opt_attach_list, i );
	       }
	   } else 
	       attachs->_length = 0;
	   CORBA_sequence_set_release( attachs, TRUE);

	   GNOME_Balsa_Composer_sendMessage(server,
					    "",
					    opt_compose_email ? opt_compose_email : "",
					    "",
					    "",
					    attachs,
					    0, 
					    &ev );
           CORBA_exception_free( &ev );
       }

       exit(0);
   } else {
       balsacomposer = balsa_composer_new ();
       balsaapp = balsa_application_new ();
   }
   
}

/* balsa_init:
   FIXME - check for memory leaks.
*/
static void
balsa_init(int argc, char **argv)
{
#ifndef GNOME_PARAM_GOPTION_CONTEXT
    static char *attachment = NULL;
    int opt;
    poptContext context;
    static struct poptOption options[] = {

	{"checkmail", 'c', POPT_ARG_NONE,
	 &(cmd_check_mail_on_startup), 0,
	 N_("Get new mail on startup"), NULL},
	{"compose", 'm', POPT_ARG_STRING, &(opt_compose_email),
	 0, N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
	{"attach", 'a', POPT_ARG_STRING, &(attachment),
	 'a', N_("Attach file at PATH"), "PATH"},
	{"open-mailbox", 'o', POPT_ARG_STRING, &(cmd_line_open_mailboxes),
	 0, N_("Opens MAILBOXNAME"), N_("MAILBOXNAME")},
	{"open-unread-mailbox", 'u', POPT_ARG_NONE,
	 &(cmd_open_unread_mailbox), 0,
	 N_("Opens first unread mailbox"), NULL},
	{"open-inbox", 'i', POPT_ARG_NONE,
	 &(cmd_open_inbox), 0,
	 N_("Opens default Inbox on startup"), NULL},
	{"get-stats", 's', POPT_ARG_NONE,
	 &(cmd_get_stats), 0,
	 N_("Prints number unread and unsent messages"), NULL},
	{"debug-pop", 'd', POPT_ARG_NONE, &PopDebug, 0, 
	 N_("Debug POP3 connection"), NULL},
	{"debug-imap", 'D', POPT_ARG_NONE, &ImapDebug, 0, 
	 N_("Debug IMAP connection"), NULL},
	{NULL, '\0', 0, NULL, 0}	/* end the list */
    };


    context = poptGetContext(PACKAGE, argc, (const char **)argv, options, 0);
    while((opt = poptGetNextOpt(context)) > 0) {
        switch (opt) {
	    case 'a':
	        opt_attach_list = g_slist_append(opt_attach_list, 
						 g_strdup(attachment));
		break;
	}
    }
    poptFreeContext(context);

    /* Process remaining options,  */
    
    gnome_program_init(PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
                       GNOME_PARAM_POPT_TABLE, options,
                       GNOME_PARAM_APP_PREFIX, BALSA_STD_PREFIX,
                       GNOME_PARAM_APP_DATADIR, BALSA_STD_PREFIX "/share",
		       GNOME_PARAM_HUMAN_READABLE_NAME, _("The Balsa E-Mail Client"),
                       NULL);
#else /* USE GOption interface */
    static gchar **remaining_args = NULL;
    static gchar **attach_vect = NULL;
    static GOptionEntry option_entries[] = {
	{"checkmail", 'c', 0, G_OPTION_ARG_NONE,
	 &(cmd_check_mail_on_startup),
	 N_("Get new mail on startup"), NULL},
	{"compose", 'm', 0, G_OPTION_ARG_STRING, &(opt_compose_email),
	 N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
	{"attach", 'a', 0, G_OPTION_ARG_FILENAME_ARRAY, &(attach_vect),
	 N_("Attach file at PATH"), "PATH"},
	{"open-mailbox", 'o', 0, G_OPTION_ARG_STRING,
         &(cmd_line_open_mailboxes),
	 N_("Opens MAILBOXNAME"), N_("MAILBOXNAME")},
	{"open-unread-mailbox", 'u', 0, G_OPTION_ARG_NONE,
	 &(cmd_open_unread_mailbox),
	 N_("Opens first unread mailbox"), NULL},
	{"open-inbox", 'i', 0, G_OPTION_ARG_NONE,
	 &(cmd_open_inbox),
	 N_("Opens default Inbox on startup"), NULL},
	{"get-stats", 's', 0, G_OPTION_ARG_NONE,
	 &(cmd_get_stats),
	 N_("Prints number unread and unsent messages"), NULL},
	{"debug-pop", 'd', 0, G_OPTION_ARG_NONE, &PopDebug,
	 N_("Debug POP3 connection"), NULL},
	{"debug-imap", 'D', 0, G_OPTION_ARG_NONE, &ImapDebug,
	 N_("Debug IMAP connection"), NULL},
        /* last but not least a special option that collects filenames */
        { G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY,
          &remaining_args,
          "Special option that collects any remaining arguments for us" },
        { NULL }
    };
    GOptionContext *option_context = g_option_context_new("balsa");
    GnomeProgram *my_app;
    g_option_context_add_main_entries(option_context, option_entries, NULL);

    my_app = gnome_program_init(PACKAGE, VERSION,
                                LIBGNOMEUI_MODULE, argc, argv,
                                GNOME_PARAM_GOPTION_CONTEXT, option_context,
                                GNOME_PARAM_APP_DATADIR,
                                BALSA_STD_PREFIX "/share",
                                GNOME_PARAM_NONE);

    if (remaining_args != NULL) {
        gint i, num_args;
        
        num_args = g_strv_length (remaining_args);
        for (i = 0; i < num_args; ++i) {
            /* process remaining_args[i] here */
            /* we do nothing for now */
        }
        g_strfreev (remaining_args);
        remaining_args = NULL;
    }
    if (attach_vect != NULL) {
        gint i, num_args;
        
        num_args = g_strv_length (attach_vect);
        for (i = 0; i < num_args; ++i) {
            opt_attach_list = g_slist_append(opt_attach_list, attach_vect[i]);
        }
        g_free(attach_vect);
        attach_vect = NULL;
    }
#endif /* OPTION HANDLING */
    balsa_handle_automation_options();  
    
}

/* check_special_mailboxes: 
   check for special mailboxes. Cannot use GUI because main window is not
   initialized yet.  
*/
static gboolean
check_special_mailboxes(void)
{
    gboolean bomb = FALSE;

    if (balsa_app.inbox == NULL) {
	g_warning(_("Balsa cannot open your \"%s\" mailbox."), _("Inbox"));
	bomb = TRUE;
    }

    if (balsa_app.outbox == NULL) {
	g_warning(_("Balsa cannot open your \"%s\" mailbox."),
		  _("Outbox"));
	bomb = TRUE;
    }

    if (balsa_app.sentbox == NULL) {
	g_warning(_("Balsa cannot open your \"%s\" mailbox."),
		  _("Sentbox"));
	bomb = TRUE;
    }

    if (balsa_app.draftbox == NULL) {
	g_warning(_("Balsa cannot open your \"%s\" mailbox."),
		  _("Draftbox"));
	bomb = TRUE;
    }

    if (balsa_app.trash == NULL) {
	g_warning(_("Balsa cannot open your \"%s\" mailbox."), _("Trash"));
	bomb = TRUE;
    }

    return bomb;
}

static void
config_init(gboolean check_only)
{
    while(!config_load() && !check_only) {
	balsa_init_begin();
        config_defclient_save();
    }
}

static void
mailboxes_init(gboolean check_only)
{
    check_special_mailboxes();
    if (!balsa_app.inbox && !check_only) {
	g_warning("*** error loading mailboxes\n");
	balsa_init_begin();
        config_defclient_save();
	return;
    }
}

#ifdef BALSA_USE_THREADS

static void
threads_init(void)
{
    int status;
    pthread_mutexattr_t attr;
    g_thread_init(NULL);
    
    if( (status=pthread_mutexattr_init(&attr)) )
        g_warning("pthread_mutexattr_init failed with %d\n", status);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mailbox_lock, &attr);

    libbalsa_threads_init();
    gdk_threads_init();

    pthread_mutexattr_destroy(&attr);
    pthread_mutex_init(&send_messages_lock, NULL);
    checking_mail = 0;
    if (pipe(mail_thread_pipes) < 0) {
	g_log("BALSA Init", G_LOG_LEVEL_DEBUG,
	      "Error opening pipes.\n");
    }
    mail_thread_msg_send = g_io_channel_unix_new(mail_thread_pipes[1]);
    mail_thread_msg_receive =
	g_io_channel_unix_new(mail_thread_pipes[0]);
    g_io_add_watch(mail_thread_msg_receive, G_IO_IN,
		   (GIOFunc) mail_progress_notify_cb, 
                   &balsa_app.main_window);
    
    if (pipe(send_thread_pipes) < 0) {
	g_log("BALSA Init", G_LOG_LEVEL_DEBUG,
	      "Error opening pipes.\n");
    }
    send_thread_msg_send = g_io_channel_unix_new(send_thread_pipes[1]);
    send_thread_msg_receive =
	g_io_channel_unix_new(send_thread_pipes[0]);
    g_io_add_watch(send_thread_msg_receive, G_IO_IN,
		   (GIOFunc) send_progress_notify_cb,
                   &balsa_app.main_window);
}

static void
threads_destroy(void)
{
    pthread_mutex_destroy(&mailbox_lock);
    pthread_mutex_destroy(&send_messages_lock);
    libbalsa_threads_destroy();
}

#endif				/* BALSA_USE_THREADS */

/* initial_open_mailboxes:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 */
gboolean
initial_open_unread_mailboxes()
{
    GList *i, *gl;
    gdk_threads_enter();
    gl = balsa_mblist_find_all_unread_mboxes(NULL);

    if (gl) {
        for (i = g_list_first(gl); i; i = g_list_next(i)) {
            printf("opening %s..\n", (LIBBALSA_MAILBOX(i->data))->name);
            balsa_mblist_open_mailbox(LIBBALSA_MAILBOX(i->data));
        }
        g_list_free(gl);
    }
    gdk_threads_leave();
    return FALSE;
}


gboolean
initial_open_inbox()
{
    if (!balsa_app.inbox)
	return FALSE;

    printf("opening %s..\n", (LIBBALSA_MAILBOX(balsa_app.inbox))->name);
    gdk_threads_enter();
    balsa_mblist_open_mailbox(LIBBALSA_MAILBOX(balsa_app.inbox));
    gdk_threads_leave();
    
    return FALSE;
}

void balsa_get_stats(long *unread, long *unsent);
void
balsa_get_stats(long *unread, long *unsent)
{
    
    if(balsa_app.inbox && libbalsa_mailbox_open(balsa_app.inbox, NULL) ) {
        /* set threading type to load messages */
        gdk_threads_enter();
        libbalsa_mailbox_set_threading(balsa_app.inbox,
                                       balsa_app.inbox->view->threading_type);
        gdk_threads_leave();
        *unread = balsa_app.inbox->unread_messages;
        libbalsa_mailbox_close(balsa_app.inbox, FALSE);
    } else *unread = -1;
    if(balsa_app.draftbox && libbalsa_mailbox_open(balsa_app.outbox, NULL)){
        *unsent = libbalsa_mailbox_total_messages(balsa_app.outbox);
        libbalsa_mailbox_close(balsa_app.outbox, FALSE);
    } else *unsent = -1;
}

/* scan_mailboxes:
   this is an idle handler. Expands subtrees.
*/
static gboolean
scan_mailboxes_idle_cb()
{
    gboolean valid;
    GtkTreeModel *model;
    GtkTreeIter iter;

    gdk_threads_enter();
    model = GTK_TREE_MODEL(balsa_app.mblist_tree_store);
    /* The model contains only nodes from config. */
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
	 valid = gtk_tree_model_iter_next(model, &iter)) {
	BalsaMailboxNode *mbnode;

	gtk_tree_model_get(model, &iter, 0, &mbnode, -1);
	balsa_mailbox_node_append_subtree(mbnode);
	g_object_unref(mbnode);
    }
    /* The root-node (typically ~/mail) isn't in the model, so its
     * children will be appended to the top level. */
    balsa_mailbox_node_append_subtree(balsa_app.root_node);
    gdk_threads_leave();

    if (cmd_open_unread_mailbox || balsa_app.open_unread_mailbox)
	g_idle_add((GSourceFunc) initial_open_unread_mailboxes, NULL);

    if (cmd_line_open_mailboxes) {
	gchar **urls = g_strsplit(cmd_line_open_mailboxes, ";", 20);
	g_idle_add((GSourceFunc) open_mailboxes_idle_cb, urls);
    }

    if (balsa_app.remember_open_mboxes)
	g_idle_add((GSourceFunc) open_mailboxes_idle_cb, NULL);

    if (cmd_open_inbox || balsa_app.open_inbox_upon_startup)
	g_idle_add((GSourceFunc) initial_open_inbox, NULL);

    if(cmd_get_stats) {
        long unread, unsent;
        balsa_get_stats(&unread, &unsent);
        printf("Unread: %ld Unsent: %ld\n", unread, unsent);
    }

    return FALSE; 
}

/* periodic_expunge_func makes sure that even the open mailboxes get
 * expunged now and than, even if they are opened longer than
 * balsa_app.expunge_timeout. If we did not do it, the mailboxes would in
 * principle grow indefinetely. */
static gboolean
mbnode_expunge_func(GtkTreeModel *model, GtkTreePath *path,
                    GtkTreeIter *iter, GSList ** list)
{
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    g_return_val_if_fail(mbnode, FALSE);

    *list = g_slist_prepend(*list, mbnode);

    return FALSE;
}

static gboolean
periodic_expunge_cb(void)
{
    GSList *list = NULL, *l;

#if !defined(ENABLE_TOUCH_UI)
    /* should we enforce expunging now and then? Perhaps not... */
    if(!balsa_app.expunge_auto) return TRUE;
#endif

    gdk_threads_enter();
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                         _("Compressing mail folders..."));
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc)mbnode_expunge_func,
			   &list);
    gdk_threads_leave();

    for (l = list; l; l = l->next) {
        BalsaMailboxNode *mbnode = l->data;
        if (mbnode->mailbox && libbalsa_mailbox_is_open(mbnode->mailbox)
            && !mbnode->mailbox->readonly) {
            time_t tm = time(NULL);
            if (tm-mbnode->last_use > balsa_app.expunge_timeout)
                libbalsa_mailbox_sync_storage(mbnode->mailbox, TRUE);
        }
        g_object_unref(mbnode);
    }
    g_slist_free(list);

    /* purge imap cache? leave 15MB */
    libbalsa_imap_purge_temp_dir(15*1024*1024);

    return TRUE; /* do it later as well */
}

/*
 * Wrappers for libbalsa access to the progress bar.
 */

/*
 * Initialize the progress bar and set text.
 */
static GTimeVal prev_time_val;
static gdouble  min_fraction;
static void
balsa_progress_set_text(LibBalsaProgress * progress, const gchar * text,
                        guint total)
{
    gboolean rc = FALSE;

    gdk_threads_enter();

    if (!balsa_app.main_window) {
        gdk_threads_leave();
        return;
    }

    if (!text || total >= LIBBALSA_PROGRESS_MIN_COUNT)
        rc = balsa_window_setup_progress(balsa_app.main_window, text);
    g_get_current_time(&prev_time_val);
    min_fraction = LIBBALSA_PROGRESS_MIN_UPDATE_STEP;
    gdk_threads_leave();

    *progress = (text && rc) ?
        LIBBALSA_PROGRESS_YES : LIBBALSA_PROGRESS_NO;
}

/* 
 * Set the fraction in the progress bar.
 */

static void
balsa_progress_set_fraction(LibBalsaProgress * progress, gdouble fraction)
{
    GTimeVal time_val;
    guint elapsed;

    if (*progress == LIBBALSA_PROGRESS_NO)
        return;

    if (fraction > 0.0 && fraction < min_fraction)
        return;

    g_get_current_time(&time_val);
    elapsed = time_val.tv_sec - prev_time_val.tv_sec;
    elapsed *= G_USEC_PER_SEC;
    elapsed += time_val.tv_usec - prev_time_val.tv_usec;
    if (elapsed < LIBBALSA_PROGRESS_MIN_UPDATE_USECS)
        return;

    g_time_val_add(&time_val, LIBBALSA_PROGRESS_MIN_UPDATE_USECS);
    min_fraction += LIBBALSA_PROGRESS_MIN_UPDATE_STEP;

    gdk_threads_enter();
    if (balsa_app.main_window)
        balsa_window_increment_progress(balsa_app.main_window, fraction,
                                        !libbalsa_am_i_subthread());
    gdk_threads_leave();
}

static void
balsa_progress_set_activity(gboolean set, const gchar * text)
{
    gdk_threads_enter();
    if (balsa_app.main_window) {
        if (set)
            balsa_window_increase_activity(balsa_app.main_window, text);
        else
            balsa_window_decrease_activity(balsa_app.main_window, text);
    }
    gdk_threads_leave();
}

#if defined(ENABLE_NLS) && defined(HAVE_GPGME)
static const gchar *
get_utf8_locale(int category)
{
    gchar * locale;
    static gchar localebuf[64];  /* should be large enough */
    gchar * dot;

    if (!(locale = setlocale(category, NULL)))
	return NULL;
    strncpy(localebuf, locale, 57);
    localebuf[57] = '\0';
    dot = strchr(localebuf, '.');
    if (!dot)
	dot = localebuf + strlen(localebuf);
    strcpy(dot, ".UTF-8");
    return localebuf;
}
#endif

/* -------------------------- main --------------------------------- */
int
main(int argc, char *argv[])
{
    GtkWidget *window;
    GnomeClient *client;
    gchar *default_icon;
#ifdef GTKHTML_HAVE_GCONF
    GError *gconf_error;
#endif

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);
    setlocale(LC_ALL, "");
#endif

#ifdef BALSA_USE_THREADS
    /* initiate thread mutexs, variables */
    threads_init();
#endif

#ifdef HAVE_RUBRICA
    /* initialise libxml */
    LIBXML_TEST_VERSION
#endif

#ifdef HAVE_GPGME
    /* initialise the gpgme library */
    g_message("init gpgme version %s", gpgme_check_version(NULL));
#ifdef HAVE_GPG
    /* configure the GnuPG engine if a specific gpg path has been detected*/
    gpgme_set_engine_info(GPGME_PROTOCOL_OpenPGP, GPG_PATH, NULL);
#endif
#ifdef ENABLE_NLS
    gpgme_set_locale(NULL, LC_CTYPE, get_utf8_locale(LC_CTYPE));
    gpgme_set_locale(NULL, LC_MESSAGES, get_utf8_locale(LC_MESSAGES));
#endif /* ENABLE_NLS */
    { /* scope */
	gpgme_engine_info_t e;

	if (gpgme_get_engine_info(&e) == GPG_ERR_NO_ERROR)
	    while (e) {
		g_message("protocol %s: engine %s (home %s, version %s)",
			  gpgme_get_protocol_name(e->protocol),
			  e->file_name, e->home_dir, e->version);
		e = e->next;
	    }
    }
#endif

    /* FIXME: do we need to allow a non-GUI mode? */
    gtk_init_check(&argc, &argv);
    balsa_init(argc, argv);

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	g_error_free(gconf_error);
    gconf_error = NULL;
#endif

    balsa_app_init();

    /* Initialize libbalsa */
    libbalsa_init((LibBalsaInformationFunc) balsa_information_real);
    libbalsa_filters_set_url_mapper(balsa_find_mailbox_by_url);
    libbalsa_filters_set_filter_list(&balsa_app.filters);

    libbalsa_progress_set_text     = balsa_progress_set_text;
    libbalsa_progress_set_fraction = balsa_progress_set_fraction;
    libbalsa_progress_set_activity = balsa_progress_set_activity;
    
    /* checking for valid config files */
    config_init(cmd_get_stats);
    libbalsa_address_entry_set_address_book_list(balsa_app.
                                                 address_book_list);

    libbalsa_mailbox_view_table =
	g_hash_table_new_full(g_str_hash, g_str_equal,
			      (GDestroyNotify) g_free,
			      (GDestroyNotify) libbalsa_mailbox_view_free);
    config_views_load();

    default_icon = balsa_pixmap_finder("balsa_icon.png");
    if(default_icon) { /* may be NULL for developer installations */
        gtk_window_set_default_icon_from_file(default_icon, NULL);
        g_free(default_icon);
    }

    signal( SIGPIPE, SIG_IGN );
    gnome_triggers_do("", "program", "balsa", "startup", NULL);

    window = balsa_window_new();
    balsa_app.main_window = BALSA_WINDOW(window);
    g_object_add_weak_pointer(G_OBJECT(window),
			      (gpointer) &balsa_app.main_window);
    g_signal_connect(G_OBJECT(window), "destroy",
                     G_CALLBACK(balsa_cleanup), NULL);

    /* load mailboxes */
    config_load_sections();
    mailboxes_init(cmd_get_stats);

    if(cmd_get_stats) {
        long unread, unsent;
        balsa_get_stats(&unread, &unsent);
        printf("Unread: %ld Unsent: %ld\n", unread, unsent);
        exit(0);
    }

    /* session management */
    client = gnome_master_client();
    g_signal_connect(G_OBJECT(client), "save_yourself",
		     G_CALLBACK(balsa_save_session), argv[0]);
    g_signal_connect(G_OBJECT(client), "die",
		     G_CALLBACK(balsa_kill_session), NULL);

#ifdef HAVE_GPGME
    balsa_app.has_openpgp = 
        libbalsa_check_crypto_engine(GPGME_PROTOCOL_OpenPGP);
#ifdef HAVE_SMIME
    balsa_app.has_smime =
       libbalsa_check_crypto_engine(GPGME_PROTOCOL_CMS);
#else
    balsa_app.has_smime = FALSE;
#endif /* HAVE_SMIME */
#endif /* HAVE_GPGME */
    
    if (opt_compose_email || opt_attach_list) {
        BalsaSendmsg *snd;
        GSList *lst;
        gdk_threads_enter();
        snd = sendmsg_window_compose();
        gdk_threads_leave();
        if(opt_compose_email) {
            if(g_ascii_strncasecmp(opt_compose_email, "mailto:", 7) == 0)
                sendmsg_window_process_url(opt_compose_email+7,
                        sendmsg_window_set_field, snd);
            else sendmsg_window_set_field(snd,"to", opt_compose_email);
        }
        for(lst = opt_attach_list; lst; lst = g_slist_next(lst))
            add_attachment(snd, lst->data, FALSE, NULL);
	snd->quit_on_close = FALSE;
    };
    gtk_widget_show(window);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
	check_new_messages_cb(NULL, NULL);


    g_idle_add((GSourceFunc) scan_mailboxes_idle_cb, NULL);
    g_timeout_add(1801*1000, (GSourceFunc) periodic_expunge_cb, NULL);

    accel_map_load();
    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
    accel_map_save();

#ifdef BALSA_USE_THREADS
    threads_destroy();
#endif
    libbalsa_imap_server_close_all_connections();
    return 0;
}



#if 0
static void
force_close_mailbox(LibBalsaMailbox * mailbox)
{
    if (!mailbox)
	return;
    if (balsa_app.debug)
	g_print("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);
    while (mailbox->open_ref > 0)
	libbalsa_mailbox_close(mailbox);
}
#endif /* 0 */


static void
balsa_cleanup(void)
{
#ifdef BALSA_USE_THREADS
    /* move threads shutdown to separate routine?
       There are actually many things to do, e.g. threads should not
       be started after this point.
    */
    pthread_mutex_lock(&mailbox_lock);
    if(checking_mail) {
        /* We want to quit but there is a checking thread active.
           The alternatives are to:
           a. wait for the checking thread to finish - but it could be
           time consuming.  
           b. send cancel signal to it. 
        */
        pthread_cancel(get_mail_thread);
        printf("Mail check thread cancelled. I know it is rough.\n");
        sleep(1);
    }
    pthread_mutex_unlock(&mailbox_lock);
#endif
    balsa_app_destroy();
    g_hash_table_destroy(libbalsa_mailbox_view_table);
    libbalsa_mailbox_view_table = NULL;

    gnome_sound_shutdown();
}

static gint
balsa_kill_session(GnomeClient * client, gpointer client_data)
{
    gtk_main_quit(); /* FIXME: this won't save composed messages; 
			but it never did. */
    return TRUE;
}


static gint
balsa_save_session(GnomeClient * client, gint phase,
		   GnomeSaveStyle save_style, gint is_shutdown,
		   GnomeInteractStyle interact_style, gint is_fast,
		   gpointer client_data)
{
    gchar **argv;
    guint argc;

    /* allocate 0-filled so it will be NULL terminated */
    argv = g_malloc0(sizeof(gchar *) * 7);

    argc = 1;
    argv[0] = client_data;

    if (balsa_app.open_unread_mailbox) {
	argv[argc] = g_strdup("--open-unread-mailbox");
	argc++;
    }

    if (balsa_app.check_mail_upon_startup) {
	argv[argc] = g_strdup("--checkmail");
	argc++;
    }

    /* FIXME: I don't think this is needed?
     * We already save the open mailboes in save-restore.c 
     * so we should just open them when loading prefs...
     */
#if 0
    if (balsa_app.open_mailbox) {
	argv[argc] = g_strdup("--open-mailbox");
	argc++;

	argv[argc] = g_strconcat("'", balsa_app.open_mailbox, "'", NULL);
	argc++;
    }
#endif

    if (opt_compose_email) {
	argv[argc] = g_strdup("--compose");
	argc++;

	argv[argc] = g_strdup(opt_compose_email);
	argc++;
    }

    gnome_client_set_clone_command(client, argc, argv);
    gnome_client_set_restart_command(client, argc, argv);

    return TRUE;
}
