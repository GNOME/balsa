/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
#include <signal.h>

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "main-window.h"
#include "libbalsa.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "sendmsg-window.h"
#include "information.h"
#include "pop3.h"

#include "libinit_balsa/init_balsa.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"

/* Globals for Thread creation, messaging, pipe I/O */
pthread_t get_mail_thread;
pthread_t send_mail;
pthread_mutex_t mailbox_lock;
pthread_mutex_t send_messages_lock;
int checking_mail;
int mail_thread_pipes[2];
int send_thread_pipes[2];
GIOChannel *mail_thread_msg_send;
GIOChannel *mail_thread_msg_receive;
GIOChannel *send_thread_msg_send;
GIOChannel *send_thread_msg_receive;

/* Thread for updating mblist */
pthread_t mblist_thread;
/* we use the mailbox_lock pthread_mutex */
int updating_mblist;

/* Semaphore to prevent dual use of appbar progressbar */
int updating_progressbar;

static void threads_init(void);
static void threads_destroy(void);
#endif				/* BALSA_USE_THREADS */

static void balsa_init(int argc, char **argv);
static void config_init(void);
static void mailboxes_init(void);
static void balsa_cleanup(void);
static gint balsa_kill_session(GnomeClient * client, gpointer client_data);
static gint balsa_save_session(GnomeClient * client, gint phase,
			       GnomeSaveStyle save_style, gint is_shutdown,
			       GnomeInteractStyle interact_style,
			       gint is_fast, gpointer client_data);

/* We need separate variable for storing command line requests to check the 
   mail because such selection cannot be stored in balsa_app and later 
   saved to the configuration file.
*/
static gchar *cmd_line_open_mailboxes;
static gboolean cmd_check_mail_on_startup,
    cmd_open_unread_mailbox, cmd_open_inbox;

/* opt_attach_list: list of attachments */
static GSList* opt_attach_list = NULL;
/* opt_compose_email: To: field for the compose window */
static gchar *opt_compose_email = NULL;

/* balsa_init:
   FIXME - check for memory leaks.
*/
static void
balsa_init(int argc, char **argv)
{
    poptContext context;
    int opt;
    static char *attachment = NULL;
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
	{"debug-pop", 'd', POPT_ARG_NONE, &PopDebug, 0, 
	 N_("Debug POP3 connection"), NULL},
	{NULL, '\0', 0, NULL, 0}	/* end the list */
    };

    context = poptGetContext(PACKAGE, argc, argv, options, 0);
    while((opt = poptGetNextOpt(context)) > 0) {
        switch (opt) {
	    case 'a':
	        opt_attach_list = g_slist_append(opt_attach_list, 
						 g_strdup(attachment));
		break;
	}
    }
    /* Process remaining options,  */
    gnome_init_with_popt_table(PACKAGE, VERSION, argc, argv, options, 0, NULL);
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
config_init(void)
{
    while(!config_load() || check_special_mailboxes()) {
	g_warning("*** Could not load basic configuration!\n");
	balsa_init_begin();
    }
}

static void
mailboxes_init(void)
{
    if (!do_load_mailboxes()) {
	g_warning("*** error loading mailboxes\n");
	balsa_init_begin();
	return;
    }
}


#ifdef BALSA_USE_THREADS
static void
threads_init(void)
{
    g_thread_init(NULL);
    pthread_mutex_init(&mailbox_lock, NULL);
    pthread_mutex_init(&send_messages_lock, NULL);
    checking_mail = 0;
    updating_mblist = 0;
    updating_progressbar = 0;
    if (pipe(mail_thread_pipes) < 0) {
	g_log("BALSA Init", G_LOG_LEVEL_DEBUG,
	      "Error opening pipes.\n");
    }
    mail_thread_msg_send = g_io_channel_unix_new(mail_thread_pipes[1]);
    mail_thread_msg_receive =
	g_io_channel_unix_new(mail_thread_pipes[0]);
    g_io_add_watch(mail_thread_msg_receive, G_IO_IN,
		   (GIOFunc) mail_progress_notify_cb, NULL);
    
    if (pipe(send_thread_pipes) < 0) {
	g_log("BALSA Init", G_LOG_LEVEL_DEBUG,
	      "Error opening pipes.\n");
    }
    send_thread_msg_send = g_io_channel_unix_new(send_thread_pipes[1]);
    send_thread_msg_receive =
	g_io_channel_unix_new(send_thread_pipes[0]);
    g_io_add_watch(send_thread_msg_receive, G_IO_IN,
		   (GIOFunc) send_progress_notify_cb, NULL);
}

static void
threads_destroy(void)
{
    pthread_mutex_destroy(&mailbox_lock);
    pthread_mutex_destroy(&send_messages_lock);
}

#endif				/* BALSA_USE_THREADS */

/* initial_open_mailboxes:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 */
static gboolean
initial_open_unread_mailboxes()
{
    GList *i, *gl = mblist_find_all_unread_mboxes();

    if (!gl)
	return FALSE;

    for (i = g_list_first(gl); i; i = g_list_next(i)) {
	printf("opening %s..\n", (LIBBALSA_MAILBOX(i->data))->name);
	mblist_open_mailbox(LIBBALSA_MAILBOX(i->data));
    }
    g_list_free(gl);

    return FALSE;
}


static gboolean
initial_open_inbox()
{
    GList *i;
    
    if (!balsa_app.inbox)
	return FALSE;

    printf("opening %s..\n", (LIBBALSA_MAILBOX(balsa_app.inbox))->name);
    mblist_open_mailbox(LIBBALSA_MAILBOX(balsa_app.inbox));
    
    return FALSE;
}

static gint
append_subtree_f(GNode* gn, gpointer data)
{
    g_return_val_if_fail(gn->data, FALSE);
    balsa_mailbox_node_append_subtree(BALSA_MAILBOX_NODE(gn->data),
				      gn);
    return FALSE;
}

/* scan_mailboxes:
   this is an idle handler. Expands subtrees
*/
static gboolean
scan_mailboxes_idle_cb()
{
    gdk_threads_enter();
    g_node_traverse(balsa_app.mailbox_nodes, G_POST_ORDER, G_TRAVERSE_ALL, -1,
                    append_subtree_f, NULL);
    balsa_mblist_repopulate(balsa_app.mblist);
    gdk_threads_leave();
    return FALSE; 
}
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
#ifdef BALSA_USE_THREADS
    pthread_t scan_thread;
#endif

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    textdomain(PACKAGE);
    setlocale(LC_CTYPE, gnome_i18n_get_language());
#endif

#ifdef BALSA_USE_THREADS
    /* initiate thread mutexs, variables */
    threads_init();
#endif

    balsa_init(argc, argv);

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	g_error_free(gconf_error);
    gconf_error = NULL;
#endif

    balsa_app_init();

    /* Initialize libbalsa */
    libbalsa_init((LibBalsaInformationFunc) balsa_information);

    gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
    gtk_widget_set_default_visual(gdk_rgb_get_visual());

    /* Allocate the best colormap we can get */
    balsa_app.visual = gdk_visual_get_best();
    balsa_app.colormap = gdk_colormap_new(balsa_app.visual, TRUE);

    /* checking for valid config files */
    config_init();
    /* load mailboxes */
    mailboxes_init();

    /* create all the pretty icons that balsa uses that
     * arn't part of gnome-libs */
    balsa_icons_init();
    default_icon = balsa_pixmap_finder("balsa/balsa_icon.png");
#ifdef HAVE_LIBGNOMEUI_GNOME_WINDOW_ICON_H
    gnome_window_icon_set_default_from_file(default_icon);
#endif
    g_free(default_icon);

    gnome_triggers_do("", "program", "balsa", "startup", NULL);

    window = balsa_window_new();
    balsa_app.main_window = BALSA_WINDOW(window);
    gtk_signal_connect(GTK_OBJECT(window), "destroy", balsa_cleanup, NULL);

    /* session management */
    client = gnome_master_client();
    gtk_signal_connect(GTK_OBJECT(client), "save_yourself",
		       GTK_SIGNAL_FUNC(balsa_save_session), argv[0]);
    gtk_signal_connect(GTK_OBJECT(client), "die",
		       GTK_SIGNAL_FUNC(balsa_kill_session), NULL);

    gdk_rgb_init();

    if (opt_compose_email || opt_attach_list) {
	BalsaSendmsg *snd;
	GSList *lst;
	snd = sendmsg_window_new(window, NULL, SEND_NORMAL);
	if(opt_compose_email) {
	    if(strncmp(opt_compose_email, "mailto:", 7) == 0)
	        sendmsg_window_process_url(opt_compose_email+7, 
		    	sendmsg_window_set_field, snd);
	    else sendmsg_window_set_field(snd,"to", opt_compose_email);
	}
	for(lst = opt_attach_list; lst; lst = g_slist_next(lst))
	    add_attachment(snd, lst->data, FALSE, NULL);
	SENDMSG_WINDOW_QUIT_ON_CLOSE(snd);
    } else
	gtk_widget_show(window);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
	check_new_messages_cb(NULL, NULL);

    if (cmd_open_unread_mailbox || balsa_app.open_unread_mailbox)
	gtk_idle_add((GtkFunction) initial_open_unread_mailboxes, NULL);

    if (cmd_open_inbox || balsa_app.open_inbox_upon_startup)
	gtk_idle_add((GtkFunction) initial_open_inbox, NULL);

    if (cmd_line_open_mailboxes) {
	gchar **urls = g_strsplit(cmd_line_open_mailboxes, ";", 20);
	gtk_idle_add((GtkFunction) open_mailboxes_idle_cb, urls);
    }
    signal( SIGPIPE, SIG_IGN );

#ifdef BALSA_USE_THREADS
    pthread_create(&scan_thread, NULL, 
                   (void*(*)(void*))scan_mailboxes_idle_cb, NULL);
    pthread_detach(scan_thread);
#else
    gtk_idle_add((GtkFunction) scan_mailboxes_idle_cb, NULL);
#endif
    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
    
    gdk_colormap_unref(balsa_app.colormap);

#ifdef BALSA_USE_THREADS
    threads_destroy();
#endif

    return 0;
}



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

/* Word of comment: previous definition of this function used access()
function before attempting creat/unlink operation. In PS opinion, the
speed gain is negligible or negative: the number of called system
functions in present case is constant and equal to 1; the previous
version called system function either once or twice per directory. */
static gboolean
destroy_mbnode(GNode * node, gpointer data)
{
    BalsaMailboxNode *mbnode = (BalsaMailboxNode *) node->data;

    if(mbnode == NULL) /* true for root node only */
	return FALSE;
    
    if (!mbnode->mailbox) {
	gchar *tmpfile = g_strdup_printf("%s/.expanded", mbnode->name);
	if (mbnode->expanded)
	    close(creat(tmpfile, S_IRUSR | S_IWUSR));
	else
	    unlink(tmpfile);
	g_free(tmpfile);
    }
    gtk_object_destroy(GTK_OBJECT(mbnode));
    return FALSE;
}

static void
balsa_cleanup(void)
{
    if (balsa_app.empty_trash_on_exit)
	empty_trash();

    config_save();

    /* FIXME: stop switching notebook pages in a more elegant way.
       Probably, the cleanest solution is to call enable_menus_xxx
       functions from an idle function connected to balsa_message_set. */
    gtk_signal_disconnect_by_data(GTK_OBJECT(balsa_app.notebook), NULL);

    /* close all mailboxes */
    g_node_traverse(balsa_app.mailbox_nodes,
		    G_LEVEL_ORDER,
		    G_TRAVERSE_ALL, 10, destroy_mbnode, NULL);
    gtk_object_unref(GTK_OBJECT(balsa_app.inbox));
    gtk_object_unref(GTK_OBJECT(balsa_app.outbox));
    gtk_object_unref(GTK_OBJECT(balsa_app.sentbox));
    gtk_object_unref(GTK_OBJECT(balsa_app.draftbox));
    gtk_object_unref(GTK_OBJECT(balsa_app.trash));
    g_node_destroy(balsa_app.mailbox_nodes);
    balsa_app.mailbox_nodes = NULL;
    gnome_sound_shutdown();
    libbalsa_imap_close_all_connections();
    /* g_slist_free(opt_attach_list); */
    if(balsa_app.debug) g_print("Finished cleaning up.\n");
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
