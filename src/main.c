/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

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
#include "threads.h"

#include "libinit_balsa/assistant_init.h"

#ifdef HAVE_GPGME
#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-cb.h"
#endif

/* Globals for Thread creation, messaging, pipe I/O */
gboolean checking_mail;
int mail_thread_pipes[2];
GIOChannel *mail_thread_msg_send;
GIOChannel *mail_thread_msg_receive;

static void threads_init(void);
static void threads_destroy(void);

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

/* check_special_mailboxes:
   check for special mailboxes. Cannot use GUI because main window is not
   initialized yet.
*/
static gboolean
check_special_mailboxes(void)
{
    gboolean bomb = FALSE;

    if (balsa_app.inbox == NULL) {
	g_warning(_("Balsa cannot open your “%s” mailbox."), _("Inbox"));
	bomb = TRUE;
    }

    if (balsa_app.outbox == NULL) {
	g_warning(_("Balsa cannot open your “%s” mailbox."),
		  _("Outbox"));
	bomb = TRUE;
    }

    if (balsa_app.sentbox == NULL) {
	g_warning(_("Balsa cannot open your “%s” mailbox."),
		  _("Sentbox"));
	bomb = TRUE;
    }

    if (balsa_app.draftbox == NULL) {
	g_warning(_("Balsa cannot open your “%s” mailbox."),
		  _("Draftbox"));
	bomb = TRUE;
    }

    if (balsa_app.trash == NULL) {
	g_warning(_("Balsa cannot open your “%s” mailbox."), _("Trash"));
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

GMutex checking_mail_lock;

static void
threads_init(void)
{
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
}

static void
threads_destroy(void)
{
    g_mutex_clear(&checking_mail_lock);
}

/* initial_open_mailboxes:
   open mailboxes on startup if requested so.
   This is an idle handler.
 */
static gboolean
initial_open_unread_mailboxes()
{
    GList *l, *gl;
    gl = balsa_mblist_find_all_unread_mboxes(NULL);

    if (gl) {
        for (l = gl; l; l = l->next) {
            LibBalsaMailbox *mailbox = LIBBALSA_MAILBOX(l->data);

            printf("opening %s..\n", mailbox->name);
            balsa_mblist_open_mailbox(mailbox);
        }
        g_list_free(gl);
    }
    return FALSE;
}


static gboolean
initial_open_inbox()
{
    if (!balsa_app.inbox)
	return FALSE;

    printf("opening %s..\n", balsa_app.inbox->name);
    balsa_mblist_open_mailbox_hidden(balsa_app.inbox);

    return FALSE;
}

static void
balsa_get_stats(long *unread, long *unsent)
{

    if(balsa_app.inbox && libbalsa_mailbox_open(balsa_app.inbox, NULL) ) {
        /* set threading type to load messages */
        libbalsa_mailbox_set_threading(balsa_app.inbox,
                                       balsa_app.inbox->view->threading_type);
        *unread = balsa_app.inbox->unread_messages;
        libbalsa_mailbox_close(balsa_app.inbox, FALSE);
    } else *unread = -1;
    if(balsa_app.draftbox && libbalsa_mailbox_open(balsa_app.outbox, NULL)){
        *unsent = libbalsa_mailbox_total_messages(balsa_app.outbox);
        libbalsa_mailbox_close(balsa_app.outbox, FALSE);
    } else *unsent = -1;
}

static gboolean
open_mailboxes_idle_cb(gchar ** urls)
{
    balsa_open_mailbox_list(urls);
    return FALSE;
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
        balsa_open_mailbox_list((gchar **) g_ptr_array_free(url_array,
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

    /* should we enforce expunging now and then? Perhaps not... */
    if(!balsa_app.expunge_auto) return TRUE;

    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
                         _("Compressing mail folders…"));
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc)mbnode_expunge_func,
			   &list);

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

    if (balsa_app.main_window)
        balsa_window_increment_progress(balsa_app.main_window, fraction,
                                        !libbalsa_am_i_subthread());
}

static void
balsa_progress_set_activity(gboolean set, const gchar * text)
{
    if (balsa_app.main_window) {
        if (set)
            balsa_window_increase_activity(balsa_app.main_window, text);
        else
            balsa_window_decrease_activity(balsa_app.main_window, text);
    }
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

    /* initiate thread mutexs, variables */
    threads_init();

#ifdef HAVE_RUBRICA
    /* initialise libxml */
    LIBXML_TEST_VERSION
#endif

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

    if (cmd_get_stats) {
        long unread, unsent;
        balsa_get_stats(&unread, &unsent);
        printf("Unread: %ld Unsent: %ld\n", unread, unsent);
        g_application_quit(G_APPLICATION(balsa_app.application));
        return 0;
    }

#ifdef HAVE_GPGME
    balsa_app.has_openpgp =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_OpenPGP);
    balsa_app.has_smime =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_CMS);
#endif /* HAVE_GPGME */

    balsa_check_open_compose_window();

    g_idle_add((GSourceFunc) scan_mailboxes_idle_cb, NULL);
    if (balsa_app.mw_maximized) {
        /*
         * When maximized at startup, the window changes from maximized
         * to not maximized a couple of times, so we wait until it has
         * stabilized (100 msec is not enough!).
         */
        g_timeout_add(800, (GSourceFunc) balsa_window_fix_paned, balsa_app.main_window);
    } else {
        /* No need to wait. */
        g_idle_add((GSourceFunc) balsa_window_fix_paned, balsa_app.main_window);
    }
    g_timeout_add_seconds(1801, (GSourceFunc) periodic_expunge_cb, NULL);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
        g_idle_add((GSourceFunc) balsa_main_check_new_messages,
                   balsa_app.main_window);

    accel_map_load();
    gtk_main();

    balsa_cleanup();
    accel_map_save();

    threads_destroy();

    libbalsa_imap_server_close_all_connections();
    return 0;
}

static void
balsa_cleanup(void)
{
    balsa_app_destroy();

    libbalsa_conf_drop_all();
}

/*
 * Parse command line options
 */
static gint
parse_options(int                       argc,
              char                   ** argv,
              GApplicationCommandLine * command_line)
{
    static gboolean help;
    static gboolean version;
    static gchar **remaining_args;
    static GOptionEntry option_entries[] = {
        {"checkmail", 'c', 0, G_OPTION_ARG_NONE,
         &(cmd_check_mail_on_startup),
         N_("Get new mail on start-up"), NULL},
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
         N_("Opens default Inbox on start-up"), NULL},
        {"get-stats", 's', 0, G_OPTION_ARG_NONE,
         &(cmd_get_stats),
         N_("Prints number unread and unsent messages"), NULL},
        {"debug-imap", 'D', 0, G_OPTION_ARG_NONE, &ImapDebug,
         N_("Debug IMAP connection"), NULL},
        {"help", 'h', 0, G_OPTION_ARG_NONE, &help, N_("Show help options"), NULL},
        {"version", 'v', 0, G_OPTION_ARG_NONE, &version, N_("Show version"), NULL},
        /* last but not least a special option that collects filenames */
        {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY,
         &remaining_args,
         "Special option that collects any remaining arguments for us"},
        {NULL}
    };
    GOptionContext *context;
    gboolean rc;
    GError *error;
    gint status = 0;

    context = g_option_context_new(NULL);
    g_option_context_add_main_entries(context, option_entries, NULL);

    /* Disable the built-in help-handling of GOptionContext, since it
     * calls exit() after printing help, which is not what we want to
     * happen in the primary instance. */
    g_option_context_set_help_enabled(context, FALSE);

    cmd_check_mail_on_startup = FALSE;
    cmd_get_stats = FALSE;
    cmd_open_unread_mailbox = FALSE;
    cmd_open_inbox = FALSE;
    help = FALSE;
    version = FALSE;
    remaining_args = NULL;
    error = NULL;

    rc = g_option_context_parse(context, &argc, &argv, &error);
    /* We offer the usual --help and -h as help options, but we should
     * also support the legacy "-?".
     * If we got an error, we check to see if it was caused by -?,
     * and if so, honor it: */
    if (!rc && strcmp(*++argv, "-?") == 0) {
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
        status = 2;
    } else if (version) {
        g_application_command_line_print(command_line,
                                         "Balsa email client %s\n",
                                         BALSA_VERSION);
        status = 2;
    }

    if (remaining_args != NULL) {
        gint i, num_args;

        num_args = g_strv_length(remaining_args);
        for (i = 0; i < num_args; ++i) {
            /* process remaining_args[i] here */
            /* we do nothing for now */
        }
        g_strfreev(remaining_args);
        remaining_args = NULL;
    }

    g_option_context_free(context);

    return status;
}

static void
handle_remote(int argc, char **argv,
              GApplicationCommandLine * command_line)
{
    if (cmd_get_stats) {
        glong unread, unsent;

        balsa_get_stats(&unread, &unsent);
        g_application_command_line_print(command_line,
                                         "Unread: %ld Unsent: %ld\n",
                                         unread, unsent);
    } else {
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
    }
}

static int
command_line_cb(GApplication            * application,
                GApplicationCommandLine * command_line,
                gpointer                  user_data)
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
    status = parse_options(argc, argv, command_line);
    if (status == 0) {
        if (g_application_command_line_get_is_remote(command_line)) {
            /* A remote instance caused the emission; skip start-up, just
             * handle the command line. */
            handle_remote(argc, argv, command_line);
        } else {
            /* This is the primary instance; start up as usual. */
            status = real_main(argc, argv);
        }
    } else if (status == 2) /* handled a "help" or "version" request */
        status = 0;

    g_free(argv);
    g_strfreev(args);

    return status;
}

int
main(int argc, char **argv)
{
    GtkApplication *application;
    int status;

# if defined(USE_WEBKIT2)
    /* temporary fix to avoid some webkit warnings, if the user has not
     * set it: */
    g_setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", FALSE);
#endif                          /* defined(USE_WEBKIT2) */

    balsa_app.application = application =
        gtk_application_new("org.desktop.Balsa",
                            G_APPLICATION_HANDLES_COMMAND_LINE);
    g_signal_connect(application, "command-line",
                     G_CALLBACK(command_line_cb), NULL);
    g_object_set(application, "register-session", TRUE, NULL);

    status = g_application_run(G_APPLICATION(application), argc, argv);

    g_object_unref(application);

    return status;
}
