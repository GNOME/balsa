/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

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
#include "libbalsa-conf.h"

#include "libinit_balsa/assistant_init.h"

#ifdef HAVE_GPGME
#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-cb.h"
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

static gboolean balsa_init(int argc, char **argv);
static void config_init(gboolean check_only);
static void mailboxes_init(gboolean check_only);
static void balsa_cleanup(void);

/* We need separate variable for storing command line requests to check the
   mail because such selection cannot be stored in balsa_app and later
   saved to the configuration file.
*/
static gchar **cmd_line_open_mailboxes;
static gboolean cmd_check_mail_on_startup,
    cmd_open_unread_mailbox, cmd_open_inbox, cmd_get_stats;

/* opt_attach_list: list of attachments */
static gchar **opt_attach_list = NULL;
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

static gboolean
balsa_main_check_new_messages(gpointer data)
{
    check_new_messages_real(data, TYPE_CALLBACK);
    return FALSE;
}

/* balsa_init:
   FIXME - check for memory leaks.
*/
static gchar **remaining_args = NULL;
static GOptionEntry option_entries[] = {
    {"checkmail", 'c', 0, G_OPTION_ARG_NONE,
     &(cmd_check_mail_on_startup),
     N_("Get new mail on startup"), NULL},
    {"compose", 'm', 0, G_OPTION_ARG_STRING, &(opt_compose_email),
     N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
    {"attach", 'a', 0, G_OPTION_ARG_FILENAME_ARRAY, &(opt_attach_list),
     N_("Attach file at URI"), "URI"},
    {"open-mailbox", 'o', 0, G_OPTION_ARG_STRING_ARRAY,
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

static gboolean
balsa_init(int argc, char **argv)
{
    GError *err = NULL;

    if (!gtk_init_with_args(&argc, &argv, PACKAGE, option_entries, NULL,
                            &err)) {
        g_print("%s\n", err->message);
        g_print(_("Run '%s --help' to see a full list"
                  " of available command line options.\n"), argv[0]);
        g_error_free(err);
        return FALSE;
    }

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

    return TRUE;
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

pthread_mutex_t checking_mail_lock = PTHREAD_MUTEX_INITIALIZER;

static void
threads_init(void)
{
    g_type_init();

    libbalsa_threads_init();

    pthread_mutex_init(&send_messages_lock, NULL);
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
    pthread_mutex_destroy(&checking_mail_lock);
    pthread_mutex_destroy(&send_messages_lock);
    libbalsa_threads_destroy();
}

#endif				/* BALSA_USE_THREADS */

/* initial_open_mailboxes:
   open mailboxes on startup if requested so.
   This is an idle handler. Be sure to use gdk_threads_{enter/leave}
 */
static gboolean
initial_open_unread_mailboxes()
{
    GList *l, *gl;
    gdk_threads_enter();
    gl = balsa_mblist_find_all_unread_mboxes(NULL);

    if (gl) {
        for (l = gl; l; l = l->next) {
            LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(l->data);

            printf("opening %s..\n", mailbox->name);
            balsa_mblist_open_mailbox(mailbox);
        }
        g_list_free(gl);
    }
    gdk_threads_leave();
    return FALSE;
}


static gboolean
initial_open_inbox()
{
    if (!balsa_app.inbox)
	return FALSE;

    printf("opening %s..\n", balsa_app.inbox->name);
    gdk_threads_enter();
    balsa_mblist_open_mailbox_hidden(balsa_app.inbox);
    gdk_threads_leave();

    return FALSE;
}

static void
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

static void
balsa_check_open_mailboxes(void)
{
    gchar *join;
    gchar **urls;

    join = g_strjoinv(";", cmd_line_open_mailboxes);
    g_strfreev(cmd_line_open_mailboxes);
    cmd_line_open_mailboxes = NULL;

    urls = g_strsplit(join, ";", 20);
    g_free(join);
    g_idle_add((GSourceFunc) open_mailboxes_idle_cb, urls);
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
    GPtrArray *url_array;

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

    url_array = g_ptr_array_new();
    if (cmd_open_unread_mailbox || balsa_app.open_unread_mailbox){
        GList *l, *gl;

        gl = balsa_mblist_find_all_unread_mboxes(NULL);
        for (l = gl; l; l = l->next) {
            LibBalsaMailbox *mailbox = l->data;
            g_ptr_array_add(url_array, g_strdup(mailbox->url));
        }
        g_list_free(gl);
    }

    if (cmd_line_open_mailboxes) {
        gchar *join;
        gchar **urls;
        gchar **p;

        join = g_strjoinv(";", cmd_line_open_mailboxes);
        g_strfreev(cmd_line_open_mailboxes);
        cmd_line_open_mailboxes = NULL;

        urls = g_strsplit(join, ";", 20);
        g_free(join);

        for (p = urls; *p; p++)
            g_ptr_array_add(url_array, *p);
        g_free(urls); /* not g_strfreev */
    }

    if (balsa_app.remember_open_mboxes) {
        if (balsa_app.current_mailbox_url)
            g_ptr_array_add(url_array,
                            g_strdup(balsa_app.current_mailbox_url));
        balsa_add_open_mailbox_urls(url_array);
    }

    if (cmd_open_inbox || balsa_app.open_inbox_upon_startup) {
        g_ptr_array_add(url_array, g_strdup(balsa_app.inbox->url));
    }

    if (url_array->len) {
        g_ptr_array_add(url_array, NULL);
        open_mailboxes_idle_cb((gchar **) g_ptr_array_free(url_array,
                                                           FALSE));
    }

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

    if (!balsa_app.main_window) {
        return;
    }

    /* balsa_window_setup_progress is thread-safe, so we do not check
     * for a subthread */
    if (!text || total >= LIBBALSA_PROGRESS_MIN_COUNT)
        rc = balsa_window_setup_progress(balsa_app.main_window, text);
    g_get_current_time(&prev_time_val);
    min_fraction = LIBBALSA_PROGRESS_MIN_UPDATE_STEP;

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

static gboolean
balsa_check_open_compose_window(void)
{
    if (opt_compose_email || opt_attach_list) {
        BalsaSendmsg *snd;
        gchar **attach;

        snd = sendmsg_window_compose();
        snd->quit_on_close = FALSE;

        if (opt_compose_email) {
            if (g_ascii_strncasecmp(opt_compose_email, "mailto:", 7) == 0)
                sendmsg_window_process_url(opt_compose_email + 7,
                                           sendmsg_window_set_field, snd);
            else
                sendmsg_window_set_field(snd, "to", opt_compose_email);
            g_free(opt_compose_email);
            opt_compose_email = NULL;
        }

        if (opt_attach_list) {
            for (attach = opt_attach_list; *attach; ++attach)
                add_attachment(snd, *attach, FALSE, NULL);
            g_strfreev(opt_attach_list);
            opt_attach_list = NULL;
        }

        return TRUE;
    }

    return FALSE;
}

/* -------------------------- main --------------------------------- */
static int
real_main(int argc, char *argv[])
{
    GtkWidget *window;
    gchar *default_icon;

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

    /* FIXME: do we need to allow a non-GUI mode? */
    if (!balsa_init(argc, argv))
        return 0;

#ifdef HAVE_GPGME
    /* initialise the gpgme library and set the callback funcs */
    libbalsa_gpgme_init(lb_gpgme_passphrase, lb_gpgme_select_key,
			lb_gpgme_accept_low_trust_key);
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

    window = balsa_window_new();
    balsa_app.main_window = BALSA_WINDOW(window);
    g_object_add_weak_pointer(G_OBJECT(window),
			      (gpointer) &balsa_app.main_window);

    /* load mailboxes */
    config_load_sections();
    mailboxes_init(cmd_get_stats);

    if(cmd_get_stats) {
        long unread, unsent;
        balsa_get_stats(&unread, &unsent);
        printf("Unread: %ld Unsent: %ld\n", unread, unsent);
        return(0);
    }

#ifdef HAVE_GPGME
    balsa_app.has_openpgp =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_OpenPGP);
    balsa_app.has_smime =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_CMS);
#endif /* HAVE_GPGME */

    balsa_check_open_compose_window();

    gtk_widget_show(window);

    g_idle_add((GSourceFunc) scan_mailboxes_idle_cb, NULL);
    g_timeout_add_seconds(1801, (GSourceFunc) periodic_expunge_cb, NULL);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
        g_idle_add((GSourceFunc) balsa_main_check_new_messages,
                   balsa_app.main_window);

    accel_map_load();
    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();

    balsa_cleanup();
    accel_map_save();

#ifdef BALSA_USE_THREADS
    threads_destroy();
#endif
    libbalsa_imap_server_close_all_connections();
    return 0;
}

static void
balsa_cleanup(void)
{
#ifdef BALSA_USE_THREADS
    /* move threads shutdown to separate routine?
       There are actually many things to do, e.g. threads should not
       be started after this point.
    */
    pthread_mutex_lock(&checking_mail_lock);
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
    pthread_mutex_unlock(&checking_mail_lock);
#endif
    balsa_app_destroy();
    g_hash_table_destroy(libbalsa_mailbox_view_table);
    libbalsa_mailbox_view_table = NULL;

    libbalsa_conf_drop_all();
}

static int
handle_remote(int argc, char **argv,
              GApplicationCommandLine * command_line)
{
    gboolean help;
    GOptionContext *context;
    GOptionEntry help_entries[] = {
        {"help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL},
        {NULL}
    };
    gboolean rc;
    GError *error;
    gint status = 0;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, option_entries, NULL);

    /* Disable the built-in help-handling of GOptionContext, since it
     * calls exit() after printing help, which is not what we want to
     * happen in the primary instance. */
    g_option_context_set_help_enabled(context, FALSE);
    g_option_context_add_main_entries(context, help_entries, NULL);

    error = NULL;
    help = FALSE;
    cmd_check_mail_on_startup = FALSE;
    cmd_get_stats = FALSE;
    cmd_open_unread_mailbox = FALSE;
    cmd_open_inbox = FALSE;

    rc = g_option_context_parse(context, &argc, &argv, &error);
    /* We offer --help and -? as help options;
     * GOptionContext promises --help and -h, but -h causes an error.
     * If we got an error, we check to see if it was caused by -h,
     * and if so, honor it: */
    if (!rc && strcmp(*++argv, "-h") == 0) {
        rc = help = TRUE;
        g_error_free(error);
    }

    if (!rc) {
        /* Some other bad option */
        g_application_command_line_printerr(command_line, "%s\n",
                                            error->message);
        g_error_free(error);
        status = 1;
    } else if (help) {
        gchar *text;

        text = g_option_context_get_help(context, FALSE, NULL);
        g_application_command_line_print(command_line, "%s", text);
        g_free(text);
    } else if (cmd_get_stats) {
        glong unread, unsent;

        balsa_get_stats(&unread, &unsent);
        g_application_command_line_print(command_line,
                                         "Unread: %ld Unsent: %ld\n",
                                         unread, unsent);
    } else {
        gdk_threads_enter();

        if (cmd_check_mail_on_startup)
            balsa_main_check_new_messages(balsa_app.main_window);

        if (cmd_open_unread_mailbox)
            initial_open_unread_mailboxes();

        if (cmd_open_inbox)
            initial_open_inbox();

        if (cmd_line_open_mailboxes)
            balsa_check_open_mailboxes();

        if (!balsa_check_open_compose_window()) {
            /* Move the main window to the request's screen */
            gtk_window_present(GTK_WINDOW(balsa_app.main_window));
        }

        gdk_threads_leave();
    }

    g_option_context_free(context);

    return status;
}

static int
command_line_cb(GApplication * application,
                GApplicationCommandLine * command_line)
{
    gchar **args, **argv;
    gint argc;
    int status;

    args = g_application_command_line_get_arguments(command_line, &argc);
    /* We have to make an extra copy of the array, since
     * g_option_context_parse() assumes that it can remove strings from
     * the array without freeing them. */
    argv = g_memdup(args, (argc + 1) * sizeof(gchar *));

    /* The signal is emitted when the GApplication is run, but is always
     * handled by the primary instance of Balsa. */
    if (g_application_command_line_get_is_remote(command_line)) {
        /* A remote instance caused the emission; skip start-up, just
         * handle the command line. */
        status = handle_remote(argc, argv, command_line);
    } else {
        /* This is the primary instance; start up as usual. */
        status = real_main(argc, argv);
    }

    g_free(argv);
    g_strfreev(args);

    return status;
}

int
main(int argc, char **argv)
{
    GApplication *application;
    int status;

    application = g_application_new("org.desktop.Balsa",
                                    G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(application, "command-line",
                     G_CALLBACK(command_line_cb), NULL);

    status = g_application_run(application, argc, argv);

    g_object_unref(application);

    return status;
}
