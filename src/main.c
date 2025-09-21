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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include "autocrypt.h"
#include "xdg-folders.h"

#include "libinit_balsa/assistant_init.h"

#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-cb.h"

#ifdef HAVE_HTML_WIDGET
#include <gtk/gtk.h>
#if defined(GTK_DISABLE_DEPRECATED)
#define GtkAction GAction
#include <webkit2/webkit2.h>
#undef GtkAction
#else  /* defined(GTK_DISABLE_DEPRECATED) */
#include <webkit2/webkit2.h>
#endif /* defined(GTK_DISABLE_DEPRECATED) */
#endif /* HAVE_HTML_WIDGET */

#ifdef HAVE_WEBDAV
#include <libxml/parser.h>
#endif

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
static const gchar *opt_compose_email = NULL;

static void
accel_map_load(void)
{
    gchar *accel_map_filename =
        g_build_filename(g_get_user_config_dir(), "balsa", "accelmap", NULL);
    gtk_accel_map_load(accel_map_filename);
    g_free(accel_map_filename);
}

static void
accel_map_save(void)
{
    gchar *accel_map_filename =
        g_build_filename(g_get_user_config_dir(), "balsa", "accelmap", NULL);
    gtk_accel_map_save(accel_map_filename);
    g_free(accel_map_filename);
}

static gboolean
balsa_main_check_new_messages(gpointer data)
{
    check_new_messages_real(data, FALSE);
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
config_init(void)
{
    while (!config_load()) {
	balsa_init_begin();
        config_defclient_save();
    }
}

static void
mailboxes_init(gboolean check_only)
{
    check_special_mailboxes();
    if (!balsa_app.inbox && !check_only) {
	g_warning("*** error loading mailboxes");
	balsa_init_begin();
        config_defclient_save();
	return;
    }
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

            g_debug("opening %s..", libbalsa_mailbox_get_name(mailbox));
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

    g_debug("opening %s..", libbalsa_mailbox_get_name(balsa_app.inbox));
    balsa_mblist_open_mailbox_hidden(balsa_app.inbox);

    return FALSE;
}

static void
balsa_get_stats(long *unread, long *unsent)
{
    if (balsa_app.inbox && libbalsa_mailbox_open(balsa_app.inbox, NULL)) {
        /* set threading type to load messages */
        libbalsa_mailbox_set_threading(balsa_app.inbox);
        *unread = libbalsa_mailbox_get_unread_messages(balsa_app.inbox);
        libbalsa_mailbox_close(balsa_app.inbox, FALSE);
    } else {
        *unread = -1;
    }

    if (balsa_app.outbox && libbalsa_mailbox_open(balsa_app.outbox, NULL)){
        *unsent = libbalsa_mailbox_total_messages(balsa_app.outbox);
        libbalsa_mailbox_close(balsa_app.outbox, FALSE);
    } else {
        *unsent = -1;
    }
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
    g_free(cmd_line_open_mailboxes); /* It was a shallow copy. */
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
            g_ptr_array_add(url_array, g_strdup(libbalsa_mailbox_get_url(mailbox)));
        }
        g_list_free(gl);
    }

    if (cmd_line_open_mailboxes != NULL) {
        gchar *join;
        gchar **urls;
        gchar **p;

        join = g_strjoinv(";", cmd_line_open_mailboxes);
        g_free(cmd_line_open_mailboxes); /* It was a shallow copy. */
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
        g_ptr_array_add(url_array, g_strdup(libbalsa_mailbox_get_url(balsa_app.inbox)));
    }

    if (url_array->len) {
        g_ptr_array_add(url_array, NULL);
        balsa_open_mailbox_list((gchar **) g_ptr_array_free(url_array,
                                                            FALSE));
    }

    if (cmd_get_stats) {
        long unread, unsent;
        balsa_get_stats(&unread, &unsent);
        g_debug("Unread: %ld Unsent: %ld", unread, unsent);
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
    if (!balsa_app.expunge_auto)
        return TRUE;

    balsa_information(LIBBALSA_INFORMATION_DEBUG,
    	_("Compressing mail folders…"));
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc)mbnode_expunge_func,
			   &list);

    for (l = list; l != NULL; l = l->next) {
        BalsaMailboxNode *mbnode = l->data;
        LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(mbnode);

        if (mailbox != NULL && libbalsa_mailbox_is_open(mailbox) &&
            !libbalsa_mailbox_get_readonly(mailbox)) {
            time_t tm = time(NULL);
            if (tm - balsa_mailbox_node_get_last_use_time(mbnode) > balsa_app.expunge_timeout)
                libbalsa_mailbox_sync_storage(mailbox, TRUE);
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
static gint64   prev_time_val;
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
    prev_time_val = g_get_monotonic_time();
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
    gint64 time_val;
    gint elapsed;

    if (*progress == LIBBALSA_PROGRESS_NO)
        return;

    if (fraction > 0.0 && fraction < min_fraction)
        return;

    time_val = g_get_monotonic_time();
    elapsed = time_val - prev_time_val;
    if (elapsed < LIBBALSA_PROGRESS_MIN_UPDATE_USECS)
        return;

    prev_time_val += LIBBALSA_PROGRESS_MIN_UPDATE_USECS;
    min_fraction  += LIBBALSA_PROGRESS_MIN_UPDATE_STEP;

    if (balsa_app.main_window)
        balsa_window_progress_bar_set_fraction(balsa_app.main_window, fraction);
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
    if (opt_compose_email != NULL || opt_attach_list != NULL) {
        BalsaSendmsg *snd;
        gchar **attach;

        snd = sendmsg_window_compose();

        if (opt_compose_email != NULL) {
            if (g_ascii_strncasecmp(opt_compose_email, "mailto:", 7) == 0)
                sendmsg_window_process_url(snd, opt_compose_email + 7, TRUE);
            else
                sendmsg_window_set_field(snd, "to", opt_compose_email, TRUE);
            opt_compose_email = NULL;
        }

        if (opt_attach_list != NULL) {
            for (attach = opt_attach_list; *attach; ++attach)
                add_attachment(snd, *attach, FALSE, NULL);
            g_free(opt_attach_list); /* It was a shallow copy. */
            opt_attach_list = NULL;
        }

        return TRUE;
    }

    return FALSE;
}


#define BALSA_NOTIFICATION "balsa-notification"

/* -------------------------- main --------------------------------- */
static void
balsa_startup_cb(GApplication *application,
           gpointer      user_data)
{
    gchar *default_icon;
#ifdef ENABLE_AUTOCRYPT
    GError *error = NULL;
#endif

#ifdef HAVE_HTML_WIDGET
    /* https://gitlab.gnome.org/GNOME/Initiatives/-/wikis/Sandbox-all-the-WebKit! */
    webkit_web_context_set_sandbox_enabled(webkit_web_context_get_default(), TRUE);
#endif

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);
    setlocale(LC_ALL, "");
#endif

#ifdef HAVE_WEBDAV
    LIBXML_TEST_VERSION
    xmlInitParser();
#endif

    libbalsa_assure_balsa_dirs();
    balsa_app_init();

    g_set_prgname("org.desktop.Balsa");

    default_icon = libbalsa_pixmap_finder("balsa_icon.png");
    if (default_icon) { /* may be NULL for developer installations */
        gtk_window_set_default_icon_from_file(default_icon, NULL);
        g_free(default_icon);
    }

    /* migrate to XDG folders if necessary, terminate on serious error */
    if (!xdg_config_check()) {
        g_error("migration to XDG standard folders FAILED");
    }

    /* Initialize libbalsa */
    libbalsa_information_init(application, "Balsa", BALSA_NOTIFICATION);
    libbalsa_init();
    libbalsa_filters_set_url_mapper(balsa_find_mailbox_by_url);
    libbalsa_filters_set_filter_list(&balsa_app.filters);

    libbalsa_progress_set_text     = balsa_progress_set_text;
    libbalsa_progress_set_fraction = balsa_progress_set_fraction;
    libbalsa_progress_set_activity = balsa_progress_set_activity;

    libbalsa_mailbox_date_format = &balsa_app.date_string;

    /* initialise the gpgme library and set the callback funcs */
    libbalsa_gpgme_init(lb_gpgme_select_key, lb_gpgme_accept_low_trust_key);

#ifdef ENABLE_AUTOCRYPT
    if (!autocrypt_init(&error)) {
    	libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Autocrypt error: %s"), error->message);
    	g_error_free(error);
    }
#endif

    balsa_app.has_openpgp =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_OpenPGP);
    balsa_app.has_smime =
        libbalsa_gpgme_check_crypto_engine(GPGME_PROTOCOL_CMS);

    accel_map_load();
}

static void
balsa_shutdown_cb(void)
{
    if (cmd_get_stats)
        return;

    balsa_app_destroy();

    libbalsa_conf_drop_all();
    accel_map_save();
    libbalsa_imap_server_close_all_connections();
    libbalsa_information_shutdown();
}

static void
balsa_activate_cb(GApplication *application,
            gpointer      user_data)
{
    GtkWidget *window;

    if (balsa_app.main_window != NULL) {
        gtk_window_present_with_time(GTK_WINDOW(balsa_app.main_window),
                                     gtk_get_current_event_time());
        return;
    }

    /* checking for valid config files */
    config_init();

    window = balsa_window_new(GTK_APPLICATION(application));
    balsa_app.main_window = BALSA_WINDOW(window);
    g_object_add_weak_pointer(G_OBJECT(window),
			      (gpointer *) &balsa_app.main_window);

    balsa_check_open_compose_window();

    g_idle_add((GSourceFunc) scan_mailboxes_idle_cb, NULL);
    g_timeout_add_seconds(1801, (GSourceFunc) periodic_expunge_cb, NULL);

    if (cmd_check_mail_on_startup || balsa_app.check_mail_upon_startup)
        g_idle_add((GSourceFunc) balsa_main_check_new_messages,
                   balsa_app.main_window);

    /* load mailboxes */
    config_load_sections();
    mailboxes_init(cmd_get_stats);
}

/*
 * Parse command line options
 */
static GOptionEntry option_entries[] = {
    {"check-mail", 'c', 0, G_OPTION_ARG_NONE, NULL,
        N_("Get new mail on start-up"), NULL},
    {"compose", 'm', 0, G_OPTION_ARG_STRING, NULL,
        N_("Compose a new email to EMAIL@ADDRESS"), "EMAIL@ADDRESS"},
    {"attach", 'a', 0, G_OPTION_ARG_FILENAME_ARRAY, NULL,
        N_("Attach file at URI"), "URI"},
    {"open-mailbox", 'o', 0, G_OPTION_ARG_STRING_ARRAY, NULL,
        N_("Opens MAILBOXNAME"), N_("MAILBOXNAME")},
    {"open-unread-mailbox", 'u', 0, G_OPTION_ARG_NONE, NULL,
        N_("Opens first unread mailbox"), NULL},
    {"open-inbox", 'i', 0, G_OPTION_ARG_NONE, NULL,
        N_("Opens default Inbox on start-up"), NULL},
    {"get-stats", 's', 0, G_OPTION_ARG_NONE, NULL,
        N_("Prints number unread and unsent messages"), NULL},
    {"version", 'v', 0, G_OPTION_ARG_NONE, NULL,
        N_("Show version"), NULL},
    /* last but not least a special option that collects filenames */
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_FILENAME_ARRAY, NULL,
        "Special option that collects any remaining arguments for us"},
    {NULL}
};

static void
parse_options(GApplicationCommandLine * cmdline)
{
    GVariantDict *dict = g_application_command_line_get_options_dict(cmdline);
    gchar **remaining_args;

    cmd_check_mail_on_startup = g_variant_dict_contains(dict, "check-mail");
    cmd_open_unread_mailbox = g_variant_dict_contains(dict, "open-unread-mailbox");
    cmd_open_inbox = g_variant_dict_contains(dict, "open-inbox");
    cmd_get_stats = g_variant_dict_contains(dict, "get-stats");

    if (!g_variant_dict_lookup(dict, "compose", "&s", &opt_compose_email))
        opt_compose_email = NULL;

    if (!g_variant_dict_lookup(dict, "attach", "^a&ay", &opt_attach_list))
        opt_attach_list = NULL;

    if (!g_variant_dict_lookup(dict, "open-mailbox", "^a&s", &cmd_line_open_mailboxes))
        cmd_line_open_mailboxes = NULL;

    if (!g_variant_dict_lookup(dict, G_OPTION_REMAINING, "^a&ay", &remaining_args))
        remaining_args = NULL;

    if (remaining_args != NULL) {
        gint i, num_args;

        num_args = g_strv_length(remaining_args);
        for (i = 0; i < num_args; ++i) {
            /* process remaining_args[i] here */
            /* we do nothing for now */
        }
        g_free(remaining_args); /* It was a shallow copy. */
        remaining_args = NULL;
    }
}

static void
handle_remote(GApplicationCommandLine * command_line)
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

        if (cmd_line_open_mailboxes != NULL)
            balsa_check_open_mailboxes();

        if (!balsa_check_open_compose_window()) {
            /* Move the main window to the request's screen */
            gtk_window_present_with_time(GTK_WINDOW(balsa_app.main_window),
                                         gtk_get_current_event_time());
        }
    }
}

static gint
balsa_handle_local_options_cb(GApplication *application,
                              GVariantDict *options,
                              gpointer      user_data)
{
    if (g_variant_dict_contains(options, "version")) {
        g_print("Balsa email client %s\n", BALSA_VERSION);

        return 0;
    }

    return -1;
}

static int
balsa_command_line_cb(GApplication            * application,
                      GApplicationCommandLine * command_line,
                      gpointer                  user_data)
{
    /* The signal is emitted when the GApplication is run, but is always
     * handled by the primary instance of Balsa. */
    parse_options(command_line);

    if (g_application_command_line_get_is_remote(command_line) &&
        balsa_app.main_window != NULL) {
        /* A remote instance caused the emission; skip start-up, just
         * handle the command line. */
        handle_remote(command_line);
    } else if (cmd_get_stats) {
        long unread, unsent;

        balsa_app.inbox =
            libbalsa_mailbox_new_from_config("mailbox-Inbox", FALSE);
        balsa_app.outbox =
            libbalsa_mailbox_new_from_config("mailbox-Outbox", FALSE);
        balsa_get_stats(&unread, &unsent);
        g_application_command_line_print(command_line,
                                         "Unread: %ld Unsent: %ld\n",
                                         unread, unsent);
        g_object_unref(balsa_app.outbox);
        g_object_unref(balsa_app.inbox);
    } else {
        g_application_activate(application);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    GtkApplication *application;
    int status;

    balsa_app.application = application =
        gtk_application_new("org.desktop.Balsa",
                            G_APPLICATION_HANDLES_COMMAND_LINE);
    g_object_set(application, "register-session", TRUE, NULL);
    g_application_add_main_option_entries(G_APPLICATION(application), option_entries);

    g_signal_connect(application, "handle-local-options",
                     G_CALLBACK(balsa_handle_local_options_cb), NULL);
    g_signal_connect(application, "command-line",
                     G_CALLBACK(balsa_command_line_cb), NULL);
    g_signal_connect(application, "startup",
                     G_CALLBACK(balsa_startup_cb), NULL);
    g_signal_connect(application, "activate",
                     G_CALLBACK(balsa_activate_cb), NULL);
    g_signal_connect(application, "shutdown",
                     G_CALLBACK(balsa_shutdown_cb), NULL);

    status = g_application_run(G_APPLICATION(application), argc, argv);

    g_object_unref(application);

    return status;
}
