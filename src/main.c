/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include "balsa-app.h"
#include "balsa-icons.h"
#include "main-window.h"
#include "libbalsa.h"
#include "mailbox-node.h"
#include "save-restore.h"
#include "main.h"
#include "information.h"

#include "libinit_balsa/init_balsa.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"

/* Globals for Thread creation, messaging, pipe I/O */
pthread_t get_mail_thread;
pthread_t send_mail;
pthread_mutex_t mailbox_lock;
pthread_mutex_t send_messages_lock;
pthread_mutex_t appbar_lock;
int checking_mail;
int sending_mail;
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

static void threads_init(gboolean init);
#endif				/* BALSA_USE_THREADS */

static void balsa_init(int argc, char **argv);
static void config_init(void);
static void mailboxes_init(void);
static void empty_trash(void);
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
     cmd_open_unread_mailbox;

static void
balsa_init(int argc, char **argv)
{
    static struct poptOption options[] = {

	{"checkmail", 'c', POPT_ARG_NONE,
	 &(cmd_check_mail_on_startup), 0,
	 N_("Get new mail on startup"), NULL},
	{"compose", 'm', POPT_ARG_STRING, &(balsa_app.compose_email),
	 0, N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
	{"open-mailbox", 'o', POPT_ARG_STRING, &(cmd_line_open_mailboxes),
	 0, N_("Opens MAILBOXNAME"), N_("MAILBOXNAME")},
	{"open-unread-mailbox", 'u', POPT_ARG_NONE,
	 &(cmd_open_unread_mailbox), 0,
	 N_("Opens first unread mailbox"), NULL},
	{NULL, '\0', 0, NULL, 0}	/* end the list */
    };

    gnome_init_with_popt_table(PACKAGE, VERSION, argc, argv, options, 0,
			       NULL);
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
    do {
	config_load();
	if (check_special_mailboxes()) {
	    g_warning("*** Could not load basic mailboxes!\n");
	    balsa_init_begin();
	    /*return; */
	} else break;
    } while(1);
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
void
threads_init(gboolean init)
{
    if (init) {
	g_thread_init(NULL);
	pthread_mutex_init(&mailbox_lock, NULL);
	pthread_mutex_init(&send_messages_lock, NULL);
	pthread_mutex_init(&appbar_lock, NULL);
	checking_mail = 0;
	updating_mblist = 0;
	sending_mail = 0;
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
    } else {
	pthread_mutex_destroy(&mailbox_lock);
	pthread_mutex_destroy(&send_messages_lock);
	pthread_mutex_destroy(&appbar_lock);
    }
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

/* decode_and_strdup:
   decodes given URL string up to the delimiter and places the
   eos pointer in newstr if supplied (eos==NULL if end of string was reached)
*/
typedef void (*field_setter)(BalsaSendmsg *d, const gchar*, const gchar*);
static gchar* decode_and_strdup(const gchar*str, int delim, gchar** newstr)
{
    gchar num[3];
    GString *s = g_string_new(NULL);
    /* eos points to the character after the last to parse */
    gchar *eos = strchr(str, delim); 

    if(!eos) eos = (gchar*)str + strlen(str);
    while(str<eos) {
	switch(*str) {
	case '+':
	    s = g_string_append_c(s, ' ');
	    str++;
	    break;
	case '%':
	    if(str+2<eos) {
		strncpy(num, str+1, 2); num[2] = 0;
		s = g_string_append_c(s, strtol(num,NULL,16));
	    }
	    str+=3;
	    break;
	default:
	    s = g_string_append_c(s, *str++);
	}
    }
    if(newstr) *newstr = *eos ? eos+1 : NULL;
    eos = s->str;
    g_string_free(s,FALSE);
    return eos;
}
    
/* process_url:
   extracts all characters until NUL or question mark; parse later fields
   of format 'key'='value' with ampersands as separators.
*/ 
static void process_url(const char *url, field_setter func, void *data)
{
    gchar * ptr, *to, *key, *val;

    to = decode_and_strdup(url,'?', &ptr);
    func(data, "to", to);
    g_free(to);
    while(ptr) {
	key = decode_and_strdup(ptr,'=', &ptr);
	if(ptr) {
	    val = decode_and_strdup(ptr,'&', &ptr);
	    func(data, key, val);
	    g_free(val);
	}
	g_free(key);
    }
}

/* -------------------------- main --------------------------------- */
int
main(int argc, char *argv[])
{
    GtkWidget *window;
    GnomeClient *client;
    gchar *default_icon;
#ifdef GTKHTML_HAVE_GCONF
    GConfError *gconf_error;
#endif

    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    textdomain(PACKAGE);

#ifdef BALSA_USE_THREADS
    /* initiate thread mutexs, variables */
    threads_init(TRUE);
#endif

    balsa_init(argc, argv);

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	gconf_error_destroy(gconf_error);
    gconf_error = NULL;
#endif

    balsa_app_init();

    /* Initialize libbalsa */
    libbalsa_init((LibBalsaInformationFunc) balsa_information);

#ifdef USE_PIXBUF
    gtk_widget_set_default_colormap(gdk_rgb_get_cmap());
    gtk_widget_set_default_visual(gdk_rgb_get_visual());
#else
    gtk_widget_set_default_colormap(gdk_imlib_get_colormap());
    gtk_widget_set_default_visual(gdk_imlib_get_visual());
#endif

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

    /* session management */
    client = gnome_master_client();
    gtk_signal_connect(GTK_OBJECT(client), "save_yourself",
		       GTK_SIGNAL_FUNC(balsa_save_session), argv[0]);
    gtk_signal_connect(GTK_OBJECT(client), "die",
		       GTK_SIGNAL_FUNC(balsa_kill_session), NULL);

    gdk_rgb_init();

    if (balsa_app.compose_email) {
	BalsaSendmsg *snd;
	snd = sendmsg_window_new(window, NULL, SEND_NORMAL);
	if(strncmp(balsa_app.compose_email, "mailto:", 7) == 0)
	    process_url(balsa_app.compose_email+7, 
			sendmsg_window_set_field, snd);
	else sendmsg_window_set_field(snd,"to", balsa_app.compose_email);
    } else
	gtk_widget_show(window);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
	check_new_messages_cb(NULL, NULL);

    if (cmd_open_unread_mailbox || balsa_app.open_unread_mailbox)
	gtk_idle_add((GtkFunction) initial_open_unread_mailboxes, NULL);

    if (cmd_line_open_mailboxes) {
	gchar **names = g_strsplit(cmd_line_open_mailboxes, ";", 20);
	gtk_idle_add((GtkFunction) open_mailboxes_idle_cb, names);
    }

    signal( SIGPIPE, SIG_IGN );

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();

    gdk_colormap_unref(balsa_app.colormap);

#ifdef BALSA_USE_THREADS
    threads_init(FALSE);
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
close_all_mailboxes(GNode * node, gpointer data)
{
    BalsaMailboxNode *mbnode = (BalsaMailboxNode *) node->data;

    if(mbnode == NULL) /* true for root node only */
	return FALSE;
    
    if (mbnode->mailbox) 
	force_close_mailbox(mbnode->mailbox);
    else {
	gchar *tmpfile = g_strdup_printf("%s/.expanded", mbnode->name);
	if (mbnode->expanded)
	    close(creat(tmpfile, S_IRUSR | S_IWUSR));
	else
	    unlink(tmpfile);
	g_free(tmpfile);
    }

    return FALSE;
}

void
balsa_exit(void)
{
    g_node_traverse(balsa_app.mailbox_nodes,
		    G_LEVEL_ORDER,
		    G_TRAVERSE_ALL, 10, close_all_mailboxes, NULL);

    if (balsa_app.empty_trash_on_exit)
	empty_trash();

    force_close_mailbox(balsa_app.inbox);
    force_close_mailbox(balsa_app.outbox);
    force_close_mailbox(balsa_app.sentbox);
    force_close_mailbox(balsa_app.draftbox);
    force_close_mailbox(balsa_app.trash);

    config_save();

    gnome_sound_shutdown();
    gtk_main_quit();
}

/* balsa_window_destroy
   It may be called from balsa_window_destroy or balsa_exit; this is why
   it should not make assumptions about the presence of the like
   the notebook and so on.
*/
static void
empty_trash(void)
{
    BalsaIndexPage *page;
    GList *message;

    libbalsa_mailbox_open(balsa_app.trash, FALSE);

    message = balsa_app.trash->message_list;

    while (message) {
	libbalsa_message_delete(message->data);
	message = message->next;
    }
    libbalsa_mailbox_commit_changes(balsa_app.trash);

    libbalsa_mailbox_close(balsa_app.trash);

    if (balsa_app.notebook &&
	(page = balsa_find_notebook_page(balsa_app.trash)))
	    balsa_index_page_reset(page);
}


static gint
balsa_kill_session(GnomeClient * client, gpointer client_data)
{
    balsa_exit();
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

    if (balsa_app.compose_email) {
	argv[argc] = g_strdup("--compose");
	argc++;

	argv[argc] = g_strdup(balsa_app.compose_email);
	argc++;
    }

    gnome_client_set_clone_command(client, argc, argv);
    gnome_client_set_restart_command(client, argc, argv);

    return TRUE;
}
