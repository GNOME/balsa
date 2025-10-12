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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * BalsaWindow: subclass of GtkApplicationWindow
 *
 * The only known instance of BalsaWindow is balsa_app.main_window,
 * but the code in this module does not depend on that fact, to make it
 * more self-contained.  pb
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "main-window.h"

#include <string.h>
#include <math.h>
#include <gdk/gdkkeysyms.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "application-helpers.h"
#include "imap-server.h"
#include "libbalsa.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>

#include <gio/gio.h>

#include "ab-window.h"
#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "message-window.h"
#include "pref-manager.h"
#include "print.h"
#include "sendmsg-window.h"
#include "send.h"
#include "store-address.h"
#include "save-restore.h"
#include "toolbar-prefs.h"
#include "toolbar-factory.h"
#include "libbalsa-progress.h"
#include "geometry-manager.h"
#include "system-tray.h"

#include "filter.h"
#include "filter-funcs.h"

#include "libinit_balsa/assistant_init.h"

#define MAILBOX_DATA "mailbox_data"

enum {
    IDENTITIES_CHANGED,
    LAST_SIGNAL
};

enum {
    TARGET_MESSAGES
};

#define NUM_DROP_TYPES 1
static GtkTargetEntry notebook_drop_types[NUM_DROP_TYPES] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

/* Define thread-related globals, including dialogs */
static ProgressDialog progress_dialog;

struct check_messages_thread_info {
    BalsaWindow *window;
    gboolean with_progress_dialog;
    gboolean with_activity_bar;
    GSList *list;
};
static void bw_check_messages_thread(struct check_messages_thread_info
                                     *info);

static void bw_display_new_mail_notification(int num_new, int has_new);

static void balsa_window_real_open_mbnode(BalsaWindow *window,
                                          BalsaMailboxNode *mbnode,
                                          gboolean set_current);
static void balsa_window_real_close_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_dispose(GObject * object);

static gboolean bw_close_mailbox_on_timer(BalsaWindow * window);

static void bw_index_changed_cb(GtkWidget * widget, gpointer data);
static void bw_idle_replace(BalsaWindow * window, BalsaIndex * bindex);
static gboolean bw_idle_cb(BalsaWindow * window);


static void bw_check_mailbox_list(struct check_messages_thread_info *info, GList * list);
static gboolean bw_add_mbox_to_checklist(GtkTreeModel * model,
                                         GtkTreePath * path,
                                         GtkTreeIter * iter,
                                         GSList ** list);
static gboolean bw_imap_check_test(const gchar * path);

static void bw_enable_mailbox_menus(BalsaWindow * window, BalsaIndex * index);
static void bw_enable_message_menus(BalsaWindow * window, guint msgno);
static void bw_enable_expand_collapse(BalsaWindow * window,
                                      LibBalsaMailbox * mailbox);
#ifdef HAVE_HTML_WIDGET
static void bw_enable_view_menus(BalsaWindow * window, BalsaMessage * bm);
#endif				/* HAVE_HTML_WIDGET */
static void bw_register_open_mailbox(LibBalsaMailbox *m);
static void bw_unregister_open_mailbox(LibBalsaMailbox *m);
static gboolean bw_is_open_mailbox(LibBalsaMailbox *m);

static void bw_mailbox_tab_close_cb(GtkWidget * widget, gpointer data);

static void bw_show_mbtree(BalsaWindow * window);
static void bw_set_filter_menu(BalsaWindow * window, int gui_filter);
static LibBalsaCondition *bw_get_view_filter(BalsaWindow * window);

static void bw_select_part_cb(BalsaMessage * bm, gpointer data);

static void bw_find_real(BalsaWindow * window, BalsaIndex * bindex,
                         gboolean again);

static void bw_notebook_switch_page_cb(GtkWidget * notebook,
                                       void * page,
                                       guint page_num,
                                       gpointer data);
static void bw_send_msg_window_destroy_cb(GtkWidget * widget, gpointer data);
static BalsaIndex *bw_notebook_find_page(GtkNotebook * notebook,
                                         gint x, gint y);
static void bw_notebook_drag_received_cb(GtkWidget* widget,
                                         GdkDragContext* context,
                                         gint x, gint y,
                                         GtkSelectionData* selection_data,
                                         guint info, guint32 time,
                                         gpointer data);
static gboolean bw_notebook_drag_motion_cb(GtkWidget* widget,
                                           GdkDragContext* context,
                                           gint x, gint y, guint time,
                                           gpointer user_data);
static void bw_notebook_page_notify_cb(GtkWidget  *child,
                                       GParamSpec *child_property,
                                       gpointer    user_data);


static GtkWidget *bw_notebook_label_new (BalsaMailboxNode* mbnode);
static void bw_reset_filter(BalsaWindow * bw);

typedef struct _BalsaWindowRealOpenMbnodeInfo BalsaWindowRealOpenMbnodeInfo;
static void bw_real_open_mbnode_thread(BalsaWindowRealOpenMbnodeInfo * info);

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

typedef struct _BalsaWindowPrivate BalsaWindowPrivate;

struct _BalsaWindowPrivate {
    GtkApplicationWindow window;

    GtkWidget *toolbar;
    GtkWidget *sos_bar;
    GtkWidget *bottom_bar;
    GtkWidget *progress_bar;
    GtkWidget *statusbar;
    GtkWidget *mblist;
    GtkWidget *sos_entry;       /* SenderOrSubject filter entry */
    GtkWidget *notebook;
    GtkWidget *preview;		/* message is child */
    GtkWidget *paned_parent;
    GtkWidget *paned_child;
    GtkWidget *mblist_parent;    /* the horizontal GtkPaned parent of BalsaWindow:mblist */
    GtkWidget *notebook_parent;  /* the vertical GtkPaned parent of BalsaWindow:notebook */
    GtkWidget *current_index;
    GtkWidget *filter_choice;
    GtkWidget *vbox;
    GtkWidget *content_area;

    guint set_message_id;

    /* Progress bar stuff: */
    BalsaWindowProgress progress_type;
    guint activity_handler;
    guint activity_counter;
    GSList *activity_messages;

    gboolean new_mail_notification_sent;

    /* Support GNetworkMonitor: */
    gboolean network_available;
    time_t last_check_time;
    guint network_changed_source_id;
    gulong network_changed_handler_id;

    GThreadPool *open_mbnode_thread_pool;
    GPtrArray *open_mbnode_info_array;
};

G_DEFINE_TYPE_WITH_PRIVATE(BalsaWindow, balsa_window, GTK_TYPE_APPLICATION_WINDOW)

static guint window_signals[LAST_SIGNAL] = { 0 };

/* note: access with g_atomic_* functions, not checking mail when 1 */
static gint checking_mail = 1;

static void
balsa_window_class_init(BalsaWindowClass * klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;

    window_signals[IDENTITIES_CHANGED] =
        g_signal_new("identities-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaWindowClass, identities_changed),
                     NULL, NULL,
                     NULL, G_TYPE_NONE, 0);

    object_class->dispose = balsa_window_dispose;

    klass->open_mbnode  = balsa_window_real_open_mbnode;
    klass->close_mbnode = balsa_window_real_close_mbnode;

    /* Signals */
    klass->identities_changed = NULL;
}

static gboolean bw_change_connection_status_idle(gpointer data);
static void
print_network_status(gboolean available)
{
    GDateTime *datetime;
    gchar *datetime_string;

    datetime = g_date_time_new_now_local();
    datetime_string = g_date_time_format(datetime, "%c");
    g_date_time_unref(datetime);

    g_debug("Network is %s (%s)",
            available ? "available  " : "unavailable",
            datetime_string);
    g_free(datetime_string);
}

static void
bw_network_changed_cb(GNetworkMonitor * monitor,
                      gboolean          available,
                      gpointer          user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (priv->network_available != available) {
        priv->network_available = available;
        print_network_status(available);
    }

    if (priv->network_changed_source_id == 0) {
        /* Wait 2 seconds or so to let the network stabilize */
        priv->network_changed_source_id =
            g_timeout_add_seconds(2, bw_change_connection_status_idle, window);
    }
}

static void
balsa_window_init(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GNetworkMonitor *monitor;

    monitor = g_network_monitor_get_default();
    priv->network_available =
        g_network_monitor_get_network_available(monitor);
    print_network_status(priv->network_available);
    priv->network_changed_handler_id =
        g_signal_connect(monitor, "network-changed",
                         G_CALLBACK(bw_network_changed_cb), window);
    priv->last_check_time = 0;
    priv->open_mbnode_thread_pool =
        g_thread_pool_new((GFunc) bw_real_open_mbnode_thread, NULL,
                          1 /* g_get_num_processors()? */, FALSE, NULL);
    priv->open_mbnode_info_array =
        g_ptr_array_new_with_free_func((GDestroyNotify) g_idle_remove_by_data);
}

static gboolean
bw_delete_cb(GtkWidget* main_window, GdkEvent *event, gpointer user_data)
{
    /* we cannot leave main window disabled because compose windows
     * (for example) could refuse to get deleted and we would be left
     * with disabled main window. */
    if(libbalsa_is_sending_mail()) {
        GtkWidget* d =
            gtk_message_dialog_new(GTK_WINDOW(main_window),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_YES_NO,
                                   _("Balsa is sending a mail now.\n"
                                     "Abort sending?"));
        int retval = gtk_dialog_run(GTK_DIALOG(d));
        /* FIXME: we should terminate sending thread nicely here,
         * but we must know their ids. */
        gtk_widget_destroy(d);
        return retval != GTK_RESPONSE_YES; /* keep running unless OK */
    }
    return FALSE; /* allow delete */
}

static void
bw_mblist_parent_position_cb(GtkPaned   * mblist_parent,
                             GParamSpec * pspec,
                             gpointer     user_data)
{
    if (balsa_app.show_mblist)
        balsa_app.mblist_width = gtk_paned_get_position(mblist_parent);
}

static void
bw_notebook_parent_position_cb(GtkPaned   * notebook_parent,
                               GParamSpec * pspec,
                               gpointer     user_data)
{
    if (balsa_app.previewpane)
        balsa_app.notebook_height = gtk_paned_get_position(notebook_parent);
}

static GtkWidget *
bw_frame(GtkWidget * widget)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame), widget);
    gtk_widget_show(frame);
    return frame;
}
/*
 * Filter entry widget creation code. Block accelerator keys.
 * Otherwise, typing eg. 'c' would open the draftbox instead of
 * actually insert the 'c' character in the entry.
 */

static void
bw_check_filter(GtkWidget *widget, GParamSpec *pspec, gpointer user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    libbalsa_window_block_accels((GtkApplicationWindow *) window,
                                 gtk_widget_has_focus(widget));
}

static void
bw_set_view_filter(BalsaWindow * bw, gint filter_no, GtkWidget * entry)
{
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaCondition *view_filter;

    if (!index)
        return;

    view_filter = bw_get_view_filter(bw);
    balsa_index_set_view_filter(BALSA_INDEX(index), filter_no,
                                gtk_entry_get_text(GTK_ENTRY(entry)),
                                view_filter);
    libbalsa_condition_unref(view_filter);
}

static void
bw_filter_entry_activate(GtkWidget * entry, GtkWidget * button)
{
    BalsaWindow *window = balsa_app.main_window;
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    int filter_no =
        gtk_combo_box_get_active(GTK_COMBO_BOX(priv->filter_choice));

    bw_set_view_filter(window, filter_no, entry);
    gtk_widget_set_sensitive(button, FALSE);
}

static void
bw_filter_entry_changed(GtkWidget *entry, GtkWidget *button)
{
    gtk_widget_set_sensitive(button, TRUE);
}

/* FIXME: there should be a more compact way of creating condition
   trees than via calling special routines... */

static LibBalsaCondition *
bw_filter_sos_or_sor(const char *str, ConditionMatchType field)
{
    LibBalsaCondition *subject, *address, *retval;

    if (!(str && *str))
        return NULL;

    subject =
        libbalsa_condition_new_string(FALSE, CONDITION_MATCH_SUBJECT,
                                      g_strdup(str), NULL);
    address =
        libbalsa_condition_new_string(FALSE, field, g_strdup(str), NULL);
    retval =
        libbalsa_condition_new_bool_ptr(FALSE, CONDITION_OR, subject,
                                        address);
    libbalsa_condition_unref(subject);
    libbalsa_condition_unref(address);

    return retval;
}

static LibBalsaCondition *
bw_filter_sos(const char *str)
{
    return  bw_filter_sos_or_sor(str, CONDITION_MATCH_FROM);
}

static LibBalsaCondition *
bw_filter_sor(const char *str)
{
    return  bw_filter_sos_or_sor(str, CONDITION_MATCH_TO);
}

static LibBalsaCondition *
bw_filter_s(const char *str)
{
    return (str && *str) ?
        libbalsa_condition_new_string
        (FALSE, CONDITION_MATCH_SUBJECT, g_strdup(str), NULL)
        : NULL;
}
static LibBalsaCondition *
bw_filter_body(const char *str)
{
    return (str && *str) ?
        libbalsa_condition_new_string
        (FALSE, CONDITION_MATCH_BODY, g_strdup(str), NULL)
        : NULL;
}

static LibBalsaCondition *
bw_filter_old(const char *str)
{
    int days;
    if(str && sscanf(str, "%10d", &days) == 1) {
        time_t upperbound = time(NULL)-(days-1)*24*3600;
        return libbalsa_condition_new_date(FALSE, NULL, &upperbound);
    } else return NULL;
}

static LibBalsaCondition *
bw_filter_recent(const char *str)
{
    int days;
    if(str && sscanf(str, "%10d", &days) == 1) {
        time_t lowerbound = time(NULL)-(days-1)*24*3600;
        return libbalsa_condition_new_date(FALSE, &lowerbound, NULL);
    } else return NULL;
}

/* Subject or sender must match FILTER_SENDER, and Subject or
   Recipient must match FILTER_RECIPIENT constant. */
static struct {
    char *str;
    LibBalsaCondition *(*filter)(const char *str);
} view_filters[] = {
    { N_("Subject or Sender Contains:"),    bw_filter_sos  },
    { N_("Subject or Recipient Contains:"), bw_filter_sor  },
    { N_("Subject Contains:"),              bw_filter_s    },
    { N_("Body Contains:"),                 bw_filter_body },
    { N_("Older than (days):"),             bw_filter_old  },
    { N_("Old at most (days):"),            bw_filter_recent }
};
static gboolean view_filters_translated = FALSE;

static void
bw_sos_icon_release(GtkEntry            *entry,
                    GtkEntryIconPosition icon_pos,
                    GdkEvent            *event,
                    gpointer             user_data)
{
    /* User clicked the button for clearing the text, so we also clear the
     * search results. */
    GtkWidget *button = user_data;

    /* GtkSearchEntry will clear the text in its own icon-release
     * handler, but we need to clear it now in order to revert to no
     * filtering. */
    gtk_entry_set_text(entry, "");
    bw_filter_entry_activate(GTK_WIDGET(entry), button);
}

static GtkWidget*
bw_create_index_widget(BalsaWindow *bw)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(bw);
    GtkWidget *vbox, *button;
    unsigned i;

    if(!view_filters_translated) {
        for(i=0; i<G_N_ELEMENTS(view_filters); i++)
            view_filters[i].str = _(view_filters[i].str);
        view_filters_translated = TRUE;
    }

    priv->sos_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    priv->filter_choice = gtk_combo_box_text_new();
    gtk_container_add(GTK_CONTAINER(priv->sos_bar), priv->filter_choice);
    for(i=0; i<G_N_ELEMENTS(view_filters); i++)
        gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(priv->filter_choice),
                                       i, view_filters[i].str);
    gtk_combo_box_set_active(GTK_COMBO_BOX(priv->filter_choice), 0);
    gtk_widget_show(priv->filter_choice);

    priv->sos_entry = gtk_search_entry_new();
    /* gtk_label_set_mnemonic_widget(GTK_LABEL(priv->filter_choice),
       priv->sos_entry); */

    g_signal_connect(priv->sos_entry, "notify::has-focus",
                     G_CALLBACK(bw_check_filter), bw);

    button = gtk_button_new_from_icon_name("gtk-ok", GTK_ICON_SIZE_BUTTON);
    g_signal_connect(priv->sos_entry, "icon-release",
                     G_CALLBACK(bw_sos_icon_release), button);

    gtk_widget_set_hexpand(priv->sos_entry, TRUE);
    gtk_widget_set_halign(priv->sos_entry, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(priv->sos_bar), priv->sos_entry);
    gtk_widget_show(priv->sos_entry);

    gtk_container_add(GTK_CONTAINER(priv->sos_bar), button);
    g_signal_connect(priv->sos_entry, "activate",
                     G_CALLBACK(bw_filter_entry_activate),
                     button);
    g_signal_connect_swapped(button, "clicked",
                             G_CALLBACK(bw_filter_entry_activate),
                             priv->sos_entry);
    g_signal_connect(priv->sos_entry, "changed",
                             G_CALLBACK(bw_filter_entry_changed),
                             button);
    g_signal_connect(priv->filter_choice, "changed",
                     G_CALLBACK(bw_filter_entry_changed), button);
    gtk_widget_show_all(button);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(priv->sos_bar);
    gtk_container_add(GTK_CONTAINER(vbox), priv->sos_bar);

    gtk_widget_set_vexpand(priv->notebook, TRUE);
    gtk_widget_set_valign(priv->notebook, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), priv->notebook);

    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_show(vbox);
    return vbox;
}

/*
 * bw_fix_panes
 *
 * Called as either an idle handler or a timeout handler, after the
 * BalsaWindow has been created
 */

static gboolean
bw_fix_panes(BalsaWindow *window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_app.show_mblist) {
        gtk_paned_set_position(GTK_PANED(priv->mblist_parent),
                               balsa_app.mblist_width);
    }

    g_signal_connect(priv->mblist_parent, "notify::position",
                     G_CALLBACK(bw_mblist_parent_position_cb), NULL);

    if (priv->notebook_parent != NULL) {
        if (balsa_app.previewpane) {
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent),
                                   balsa_app.notebook_height);
        }
        g_signal_connect(priv->notebook_parent, "notify::position",
                         G_CALLBACK(bw_notebook_parent_position_cb), NULL);
    }

    return FALSE;
}

static void
bw_set_panes(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *index_widget;
    GtkWidget *bindex;
    BalsaIndexWidthPreference width_preference;
    const geometry_t *main_size;

    if (priv->paned_parent != NULL)
        gtk_container_remove(GTK_CONTAINER(priv->content_area), priv->paned_parent);
    index_widget = bw_create_index_widget(window);

    switch (balsa_app.layout_type) {
    case LAYOUT_WIDE_MSG:
	priv->paned_parent = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	priv->paned_child  = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

        priv->mblist_parent = priv->paned_child;
        priv->notebook_parent = priv->paned_parent;

	gtk_paned_pack1(GTK_PANED(priv->paned_child),
			bw_frame(priv->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(priv->paned_child),
			bw_frame(index_widget), TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(priv->paned_parent),
			priv->paned_child, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(priv->paned_parent),
			bw_frame(priv->preview), TRUE, TRUE);

        width_preference = BALSA_INDEX_WIDE;

	break;

    case LAYOUT_WIDE_SCREEN:
        priv->paned_parent = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        priv->paned_child  = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

        priv->mblist_parent = priv->paned_parent;
        priv->notebook_parent = NULL;

        gtk_paned_pack1(GTK_PANED(priv->paned_parent),
                        bw_frame(priv->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(priv->paned_parent), priv->paned_child,
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(priv->paned_child),
                        bw_frame(index_widget), TRUE, FALSE);
        gtk_paned_pack2(GTK_PANED(priv->paned_child),
                        bw_frame(priv->preview), TRUE, TRUE);

        width_preference = BALSA_INDEX_NARROW;

        break;

    case LAYOUT_DEFAULT:
    default:
	priv->paned_parent = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	priv->paned_child  = gtk_paned_new(GTK_ORIENTATION_VERTICAL);

        priv->mblist_parent = priv->paned_parent;
        priv->notebook_parent = priv->paned_child;

	gtk_paned_pack1(GTK_PANED(priv->paned_parent),
                        bw_frame(priv->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(priv->paned_parent), priv->paned_child,
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(priv->paned_child),
                        bw_frame(index_widget), TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(priv->paned_child),
                        bw_frame(priv->preview), TRUE, TRUE);

        width_preference = BALSA_INDEX_WIDE;
    }

    gtk_widget_show(priv->paned_child);
    gtk_widget_show(priv->paned_parent);
    gtk_widget_set_vexpand(priv->paned_parent, TRUE);
    gtk_widget_set_valign(priv->paned_parent, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(priv->content_area), priv->paned_parent);

    if ((bindex = balsa_window_find_current_index(window)) != NULL)
        balsa_index_set_width_preference(BALSA_INDEX(bindex), width_preference);

    main_size = geometry_manager_get("MainWindow");
    g_assert(main_size != NULL);
    if (main_size->maximized) {
        /*
         * When maximized at startup, the window changes from maximized
         * to not maximized a couple of times, so we wait until it has
         * stabilized (100 msec is not enough!).
         */
        g_timeout_add(800, (GSourceFunc) bw_fix_panes, window);
    } else {
        /* No need to wait. */
        g_idle_add((GSourceFunc) bw_fix_panes, window);
    }
}

/* Create the toolbar model for the main window's toolbar.
 */
/* Standard buttons; "" means a separator. */
static const BalsaToolbarEntry main_toolbar[] = {
    { "get-new-mail",     BALSA_PIXMAP_RECEIVE     },
    { "", ""                                       },
    { "move-to-trash",   "edit-delete"             },
    { "", ""                                       },
    { "new-message",      BALSA_PIXMAP_COMPOSE     },
    { "continue",         BALSA_PIXMAP_CONTINUE    },
    { "reply",            BALSA_PIXMAP_REPLY       },
    { "reply-all",        BALSA_PIXMAP_REPLY_ALL   },
    { "forward-attached", BALSA_PIXMAP_FORWARD     },
    { "", ""                                       },
    { "next-unread",      BALSA_PIXMAP_NEXT_UNREAD },
    { "", ""                                       },
    { "print",           "document-print"          }
};

/* Optional extra buttons */
static const BalsaToolbarEntry main_toolbar_extras[] = {
    { "quit",              "application-exit"          },
    { "reply-group",        BALSA_PIXMAP_REPLY_GROUP   },
    { "new-sender",         BALSA_PIXMAP_NEW_TO_SENDER },
    { "previous-message",   BALSA_PIXMAP_PREVIOUS      },
    { "next-message",       BALSA_PIXMAP_NEXT          },
    { "next-flagged",       BALSA_PIXMAP_NEXT_FLAGGED  },
    { "previous-part",      BALSA_PIXMAP_PREVIOUS_PART },
    { "next-part",          BALSA_PIXMAP_NEXT_PART     },
    { "send-queued-mail",   BALSA_PIXMAP_SEND_QUEUED   },
    { "send-and-receive-mail", BALSA_PIXMAP_SEND_RECEIVE  },
    { "save-part",         "document-save"             },
    { "identities",         BALSA_PIXMAP_IDENTITY      },
    { "mailbox-close",     "window-close-symbolic"     },
    { "mailbox-select-all", BALSA_PIXMAP_MARK_ALL      },
    { "show-all-headers",   BALSA_PIXMAP_SHOW_HEADERS  },
	{ "recheck-crypt",      BALSA_PIXMAP_GPG_RECHECK   },
    { "reset-filter",      "gtk-cancel"                },
    { "show-preview-pane",  BALSA_PIXMAP_SHOW_PREVIEW  },
    { "mailbox-expunge",   "edit-clear"                },
    { "empty-trash",       "list-remove"               }
};

BalsaToolbarModel *
balsa_window_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;

    if (model)
        return model;

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_MAIN_WINDOW,
                                main_toolbar,
                                G_N_ELEMENTS(main_toolbar));
    balsa_toolbar_model_add_entries(model, main_toolbar_extras,
                                    G_N_ELEMENTS(main_toolbar_extras));

    return model;
}

#define NEW_MAIL_NOTIFICATION "new-mail-notification"

static void
bw_is_active_notify(GObject * gobject, GParamSpec * pspec,
                    gpointer user_data)
{
    GtkWindow *gtk_window = GTK_WINDOW(gobject);

    if (gtk_window_is_active(gtk_window)) {
        BalsaWindow *window = BALSA_WINDOW(gobject);
        BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

        if (priv->new_mail_notification_sent) {
            GtkApplication *application = gtk_window_get_application(gtk_window);

            g_application_withdraw_notification(G_APPLICATION(application),
                                                NEW_MAIL_NOTIFICATION);

            priv->new_mail_notification_sent = FALSE;
        }
        gtk_window_set_urgency_hint(gtk_window, FALSE);
#ifdef ENABLE_SYSTRAY
        if (balsa_app.enable_systray_icon) {
            libbalsa_systray_icon_attention(FALSE);
        }
#endif
    }
}

/*
 * GMenu stuff
 */

/*
 * GAction helpers
 */

static GAction *
bw_get_action(BalsaWindow * window,
              const gchar * action_name)
{
    GActionMap *action_map;
    GAction *action;

    action_map = G_ACTION_MAP(window);
    action = g_action_map_lookup_action(action_map, action_name);
    if (!action) {
        action_map =
            G_ACTION_MAP(gtk_window_get_application(GTK_WINDOW(window)));
        action = g_action_map_lookup_action(action_map, action_name);
    }
    if (!action)
        g_warning("%s action “%s” not found", __func__, action_name);

    return action;
}

/*
 * Set and get the state of a toggle action
 */
static void
bw_action_set_boolean(BalsaWindow * window,
                      const gchar * action_name,
                      gboolean      state)
{
    GAction *action;

    action = bw_get_action(window, action_name);
    if (action)
        g_action_change_state(action, g_variant_new_boolean(state));
}

static gboolean
bw_action_get_boolean(BalsaWindow * window,
                      const gchar * action_name)
{
    GAction *action;
    gboolean retval = FALSE;

    action = bw_get_action(window, action_name);
    if (action) {
        GVariant *action_state;

        action_state = g_action_get_state(action);
        retval = g_variant_get_boolean(action_state);
        g_variant_unref(action_state);
    }

    return retval;
}

/*
 * Enable or disable an action
 */

static void
bw_action_set_enabled(BalsaWindow * window,
                      const gchar * action_name,
                      gboolean      enabled)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GAction *action;

    g_assert(window != NULL);

    /* Is the window being destroyed? */
    if (priv->preview == NULL)
        return;

    action = bw_get_action(window, action_name);
    if (action)
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
}

/*
 * Enable or disable a group of actions
 */

static void
bw_actions_set_enabled(BalsaWindow         * window,
                       const gchar * const * actions,
                       guint                 n_actions,
                       gboolean              enabled)
{
    guint i;

    for (i = 0; i < n_actions; i++)
        bw_action_set_enabled(window, *actions++, enabled);
}

/*
 * End of GAction helpers
 */

/*
 * Helper for some show/hide actions
 */

static void
bw_show_or_hide_widget(GSimpleAction * action,
                       GVariant      * state,
                       gboolean      * active,
                       GtkWidget     * widget)
{
    *active = g_variant_get_boolean(state);
    if (*active)
        gtk_widget_show(widget);
    else
        gtk_widget_hide(widget);
    g_simple_action_set_state(action, state);
}

/*
 * Callbacks for various actions' "activated" signals
 */

static void
new_message_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaSendmsg *smwindow;

    smwindow = sendmsg_window_compose();

    g_signal_connect(smwindow->window, "destroy",
                     G_CALLBACK(bw_send_msg_window_destroy_cb), user_data);
}

static void
new_mbox_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MBOX);
}

static void
new_maildir_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MAILDIR);
}

static void
new_mh_activated(GSimpleAction * action,
                 GVariant      * parameter,
                 gpointer        user_data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_MH);
}

static void
new_imap_folder_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        user_data)
{
    folder_conf_imap_node(NULL);
}

static void
new_imap_subfolder_activated(GSimpleAction * action,
                             GVariant      * parameter,
                             gpointer        user_data)
{
	BalsaMailboxNode *mbnode = balsa_mblist_get_selected_node(balsa_app.mblist);

	if (balsa_mailbox_node_is_imap(mbnode)) {
		folder_conf_add_imap_sub_cb(NULL, mbnode);
	}
}

static void
toolbars_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    customize_dialog_cb(user_data, user_data);
}

static void
identities_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);

    libbalsa_identity_config_dialog(window,
                                    &balsa_app.identities,
                                    &balsa_app.current_ident,
                                    balsa_app.smtp_servers,
                                    (void(*)(gpointer))
                                    balsa_identities_changed);
}

static void
address_book_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    GtkWidget *ab;

    ab = balsa_ab_window_new(FALSE, window);
    gtk_widget_show(GTK_WIDGET(ab));
}

#ifdef ENABLE_AUTOCRYPT
static void
autocrypt_db_activated(GSimpleAction G_GNUC_UNUSED *action,
                       GVariant      G_GNUC_UNUSED *parameter,
                       gpointer                     user_data)
{
	autocrypt_db_dialog_run(balsa_app.date_string, GTK_WINDOW(user_data));
}
#endif

static void
settings_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    open_preferences_manager(NULL, user_data);
}

static void
help_activated(GSimpleAction * action,
               GVariant      * parameter,
               gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    GError *err = NULL;

    gtk_show_uri_on_window(window, "help:balsa",
                           gtk_get_current_event_time(), &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Error displaying help: %s\n"), err->message);
        g_error_free(err);
    }
}

static void
about_activated(GSimpleAction * action,
                GVariant      * parameter,
                gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    const gchar *authors[] = {
        "Balsa Maintainers <balsa-maintainer@theochem.kth.se>:",
        "Peter Bloomfield <PeterBloomfield@bellsouth.net>",
	"Bart Visscher <magick@linux-fan.com>",
        "Emmanuel Allaud <e.allaud@wanadoo.fr>",
        "Carlos Morgado <chbm@gnome.org>",
        "Pawel Salek <pawsa@theochem.kth.se>",
        "and many others (see AUTHORS file)",
        NULL
    };
    const gchar *documenters[] = {
        NULL
    };

    const gchar *translator_credits = _("translator-credits");
    /* FIXME: do we need error handling for this? */
    GdkPixbuf *balsa_logo =
        gdk_pixbuf_new_from_file(BALSA_DATA_PREFIX
                                 "/pixmaps/balsa_logo.png", NULL);

    gtk_show_about_dialog(window,
                          "version", BALSA_VERSION,
                          "copyright",
                          "Copyright © 1997-2018 The Balsa Developers",
                          "comments",
                          _("The Balsa email client is part of "
                            "the GNOME desktop environment."),
                          "authors", authors,
                          "documenters", documenters,
                          /* license ? */
                          "title", _("About Balsa"),
                          "translator-credits",
                          strcmp(translator_credits, "translator-credits") ?
			  translator_credits : NULL,
			  "logo", balsa_logo,
                          "website", "https://gitlab.gnome.org/GNOME/balsa",
                          "wrap-license", TRUE,
                          NULL);
    g_object_unref(balsa_logo);
}

static void
quit_activated(GSimpleAction * action,
               GVariant      * parameter,
               gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    GdkEventAny e = { GDK_DELETE, NULL, 0 };

    e.window = gtk_widget_get_window(GTK_WIDGET(window));
    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
    	_("Balsa closes files and connections. Please wait…"));
    /* note: add a small delay to ensure that the notification ID has been
     * reported back to us if we use the org.freedesktop.Notifications
     * interface - otherwise, it is apparently impossible to withdraw it. */
    g_usleep(1000);
    while(gtk_events_pending())
        gtk_main_iteration_do(FALSE);
    gdk_event_put((GdkEvent*)&e);
}

static void
continue_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;

    bindex = balsa_window_find_current_index(window);

    if (bindex != NULL &&
        balsa_index_get_mailbox(BALSA_INDEX(bindex)) == balsa_app.draftbox)
        balsa_message_continue(BALSA_INDEX(bindex));
    else
        balsa_mblist_open_mailbox(balsa_app.draftbox);
}

static void
get_new_mail_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    check_new_messages_real(window, FALSE);

    if (balsa_app.check_mail_auto) {
        /* restart the timer */
        update_timer(TRUE, balsa_app.check_mail_timer);
    }
}

static void
send_queued_mail_activated(GSimpleAction * action,
                           GVariant      * parameter,
                           gpointer        user_data)
{
    libbalsa_process_queue(balsa_app.outbox, balsa_find_sentbox_by_url,
                           balsa_app.smtp_servers,
						   balsa_app.send_progress_dialog,
                           (GtkWindow *) balsa_app.main_window);
}

static void
send_and_receive_mail_activated(GSimpleAction * action,
                                GVariant      * parameter,
                                gpointer        user_data)
{
    get_new_mail_activated(action, parameter, user_data);
    send_queued_mail_activated(action, parameter, user_data);
}

static void
page_setup_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);

    message_print_page_setup(window);
}

static void
print_activated(GSimpleAction * action,
                GVariant      * parameter,
                gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *widget;
    BalsaIndex *bindex;
    guint current_msgno;

    widget = balsa_window_find_current_index(window);
    if (widget == NULL)
        return;

    bindex = BALSA_INDEX(widget);
    current_msgno = balsa_index_get_current_msgno(bindex);
    if (current_msgno > 0) {
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(balsa_index_get_mailbox(bindex),
                                         current_msgno);

        if (message == NULL)
            return;

        message_print(message, GTK_WINDOW(window), priv->preview);
        g_object_unref(message);
    }
}

static void
copy_activated(GSimpleAction * action,
               GVariant      * parameter,
               gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    guint signal_id;
    GtkWidget *focus_widget = gtk_window_get_focus(window);

    if (!focus_widget)
	return;

    signal_id = g_signal_lookup("copy-clipboard",
                                G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
#ifdef HAVE_HTML_WIDGET
    else if (libbalsa_html_can_select(focus_widget))
	libbalsa_html_copy(focus_widget);
#endif /* HAVE_HTML_WIDGET */
}

static void
select_all_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);

    balsa_window_select_all(window);
}

static void
select_thread_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;

    if ((bindex = balsa_window_find_current_index(window)))
        balsa_index_select_thread(BALSA_INDEX(bindex));
}

static void
find_activated(GSimpleAction * action,
               GVariant      * parameter,
               gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;

    if ((bindex = balsa_window_find_current_index(window)))
        bw_find_real(window, BALSA_INDEX(bindex), FALSE);
}

static void
find_next_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget * bindex;

    if ((bindex = balsa_window_find_current_index(window)))
	bw_find_real(window, BALSA_INDEX(bindex), TRUE);
}

static void
find_in_message_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_app.previewpane)
        balsa_message_find_in_message(BALSA_MESSAGE(priv->preview));
}

static void
filters_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        user_data)
{
    filters_edit_dialog(GTK_WINDOW(user_data));
}

static void
export_filters_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        user_data)
{
    filters_export_dialog(GTK_WINDOW(user_data));
}

static void
expand_all_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_update_tree(BALSA_INDEX(index), TRUE);
}

static void
collapse_all_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_update_tree(BALSA_INDEX(index), FALSE);
}

#ifdef HAVE_HTML_WIDGET
static void
zoom_in_activated(GSimpleAction * action,
                  GVariant      * parameter,
                  gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *bm = priv->preview;

    balsa_message_zoom(BALSA_MESSAGE(bm), 1);
}

static void
zoom_out_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *bm = priv->preview;

    balsa_message_zoom(BALSA_MESSAGE(bm), -1);
}

static void
zoom_normal_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *bm = priv->preview;

    balsa_message_zoom(BALSA_MESSAGE(bm), 0);
}
#endif				/* HAVE_HTML_WIDGET */

static void
next_message_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_select_next(BALSA_INDEX(index));
}

static void
previous_message_activated(GSimpleAction * action,
                           GVariant      * parameter,
                           gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_select_previous(BALSA_INDEX(index));
}

static void
next_unread_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_window_next_unread(window);
}

static void
next_flagged_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_select_next_flagged(BALSA_INDEX(index));
}

static void
reset_filter_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *index;

    /* do it by resetting the sos filder */
    gtk_entry_set_text(GTK_ENTRY(priv->sos_entry), "");
    index = balsa_window_find_current_index(window);
    bw_set_view_filter(window, balsa_index_get_filter_no(BALSA_INDEX(index)),
                       priv->sos_entry);
}

static void
mailbox_select_all_activated(GSimpleAction * action,
                             GVariant      * parameter,
                             gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    gtk_widget_grab_focus(index);
    balsa_window_select_all(GTK_WINDOW(window));
}

static void
mailbox_edit_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    mailbox_conf_edit_cb(NULL, NULL);
}

static void
mailbox_delete_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        user_data)
{
    mailbox_conf_delete_cb(NULL, NULL);
}

static void
mailbox_expunge_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    balsa_index_expunge(BALSA_INDEX(index));
}

static void
mailbox_close_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;

    bindex = balsa_window_find_current_index(window);
    if (bindex != NULL)
        balsa_mblist_close_mailbox(balsa_index_get_mailbox(BALSA_INDEX(bindex)));
}

static void
empty_trash_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    empty_trash(window);
}

static void
select_filters_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    if (index)
        filters_run_dialog(balsa_index_get_mailbox(BALSA_INDEX(index)),
                           GTK_WINDOW(balsa_app.main_window));
    else
	/* FIXME : Perhaps should we be able to apply filters on folders (ie recurse on all mailboxes in it),
	   but there are problems of infinite recursion (when one mailbox being filtered is also the destination
	   of the filter action (eg a copy)). So let's see that later :) */
	balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("You can apply filters only on mailbox\n"));
}

static void
remove_duplicates_activated(GSimpleAction * action,
                            GVariant      * parameter,
                            gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    if (index) {
        LibBalsaMailbox *mailbox =
            balsa_index_get_mailbox(BALSA_INDEX(index));
        GError *err = NULL;
        gint dup_count =
            libbalsa_mailbox_move_duplicates(mailbox, NULL, &err);
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Removing duplicates failed: %s"),
                              err->message);
            g_error_free(err);
        } else {
	    if(dup_count)
                balsa_information(LIBBALSA_INFORMATION_MESSAGE,
                                  ngettext("Removed %d duplicate",
                                           "Removed %d duplicates",
                                           dup_count), dup_count);
	    else
		balsa_information(LIBBALSA_INFORMATION_MESSAGE,
				  _("No duplicates found"));
	}

    }
}

static void
reply_activated(GSimpleAction * action,
                GVariant      * parameter,
                gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_reply(balsa_window_find_current_index(window));
}

static void
reply_all_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_replytoall(balsa_window_find_current_index(window));
}

static void
reply_group_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_replytogroup(balsa_window_find_current_index(window));
}

static void
new_to_sender_activated(GSimpleAction G_GNUC_UNUSED *action,
                        GVariant G_GNUC_UNUSED *parameter,
                        gpointer user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_newtosender(balsa_window_find_current_index(window));
}

static void
forward_attached_activated(GSimpleAction * action,
                           GVariant      * parameter,
                           gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_forward_attached(balsa_window_find_current_index(window));
}

static void
forward_inline_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_forward_inline(balsa_window_find_current_index(window));
}

static void
pipe_activated(GSimpleAction * action,
               GVariant      * parameter,
               gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_index_pipe(BALSA_INDEX
                     (balsa_window_find_current_index(window)));
}

static void
next_part_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_message_next_part(BALSA_MESSAGE(priv->preview));
}

static void
previous_part_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_message_previous_part(BALSA_MESSAGE(priv->preview));
}

static void
save_part_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_message_save_current_part(BALSA_MESSAGE(priv->preview));
}

static void
view_source_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkApplication *application = gtk_window_get_application(GTK_WINDOW(window));
    GtkWidget *bindex;
    GList *messages, *list;

    bindex = balsa_window_find_current_index(window);
    g_return_if_fail(bindex != NULL);

    messages = balsa_index_selected_list(BALSA_INDEX(bindex));
    for (list = messages; list; list = list->next) {
	LibBalsaMessage *message = list->data;

	libbalsa_show_message_source(application,
                                     message, balsa_app.message_font,
				     &balsa_app.source_escape_specials);
    }
    g_list_free_full(messages, g_object_unref);
}

static void
recheck_crypt_activated(GSimpleAction * action,
    					GVariant      * parameter,
						gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

	balsa_message_recheck_crypto(BALSA_MESSAGE(priv->preview));
}

static void
copy_message_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_message_grab_focus(BALSA_MESSAGE(priv->preview)))
        copy_activated(action, parameter, user_data);
}

static void
select_text_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_message_grab_focus(BALSA_MESSAGE(priv->preview)))
	balsa_window_select_all(user_data);
}

static void
move_to_trash_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_move_to_trash(balsa_window_find_current_index(window));
}

/*
 * Helper for toggling flags
 */
static void
toggle_flag(LibBalsaMessageFlag flag, gpointer user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;

    bindex = balsa_window_find_current_index(window);
    balsa_index_toggle_flag(BALSA_INDEX(bindex), flag);
}

static void
toggle_flagged_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        user_data)
{
    toggle_flag(LIBBALSA_MESSAGE_FLAG_FLAGGED, user_data);
}

static void
toggle_deleted_activated(GSimpleAction * action,
                         GVariant      * parameter,
                         gpointer        user_data)
{
    toggle_flag(LIBBALSA_MESSAGE_FLAG_DELETED, user_data);
}

static void
toggle_new_activated(GSimpleAction * action,
                     GVariant      * parameter,
                     gpointer        user_data)
{
    toggle_flag(LIBBALSA_MESSAGE_FLAG_NEW, user_data);
}

static void
toggle_answered_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        user_data)
{
    toggle_flag(LIBBALSA_MESSAGE_FLAG_REPLIED, user_data);
}

static void
store_address_activated(GSimpleAction * action,
                          GVariant      * parameter,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index = balsa_window_find_current_index(window);
    GList *messages;

    g_assert(index != NULL);

    messages = balsa_index_selected_list(BALSA_INDEX(index));
    balsa_store_address_from_messages(messages);
    g_list_free_full(messages, g_object_unref);
}

/*
 * Callbacks for various toggle actions' "change-state" signals
 */

static void
show_mailbox_tree_change_state(GSimpleAction * action,
                               GVariant      * state,
                               gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_app.show_mblist = g_variant_get_boolean(state);
    bw_show_mbtree(window);
    g_simple_action_set_state(action, state);
}

static void
show_mailbox_tabs_change_state(GSimpleAction * action,
                               GVariant      * state,
                               gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_app.show_notebook_tabs = g_variant_get_boolean(state);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(priv->notebook),
                               balsa_app.show_notebook_tabs);
    g_simple_action_set_state(action, state);
}

static void
show_toolbar_change_state(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    bw_show_or_hide_widget(action, state, &balsa_app.show_main_toolbar,
                           priv->toolbar);
}

static void
show_statusbar_change_state(GSimpleAction * action,
                            GVariant      * state,
                            gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    bw_show_or_hide_widget(action, state, &balsa_app.show_statusbar,
                           priv->bottom_bar);
}

static void
show_sos_bar_change_state(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    bw_show_or_hide_widget(action, state, &balsa_app.show_sos_bar,
                           priv->sos_bar);
}

static void
wrap_change_state(GSimpleAction * action,
                  GVariant      * state,
                  gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_app.browse_wrap = g_variant_get_boolean(state);

    balsa_message_set_wrap(BALSA_MESSAGE(priv->preview),
                           balsa_app.browse_wrap);
    refresh_preferences_manager();

    g_simple_action_set_state(action, state);
}

/*
 * Toggle actions that are used only in the toolbar
 */

static void
show_all_headers_change_state(GSimpleAction * action,
                              GVariant      * state,
                              gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    balsa_app.show_all_headers = g_variant_get_boolean(state);

    balsa_message_set_displayed_headers(BALSA_MESSAGE(priv->preview),
                                        balsa_app.show_all_headers ?
                                        HEADERS_ALL :
                                        balsa_app.shown_headers);

    g_simple_action_set_state(action, state);
}

static void
show_preview_pane_change_state(GSimpleAction * action,
                               GVariant      * state,
                               gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_app.previewpane = g_variant_get_boolean(state);

    balsa_window_refresh(window);

    g_simple_action_set_state(action, state);
}

/* Really, entire mailbox_hide_menu should be build dynamically from
 * the hide_states array since different mailboxes support different
 * set of flags/keywords. */
static const struct {
    LibBalsaMessageFlag flag;
    unsigned set:1;
    gint states_index;
    const gchar *action_name;
} hide_states[] = {
    { LIBBALSA_MESSAGE_FLAG_DELETED, 1, 0, "hide-deleted"   },
    { LIBBALSA_MESSAGE_FLAG_DELETED, 0, 1, "hide-undeleted" },
    { LIBBALSA_MESSAGE_FLAG_NEW,     0, 2, "hide-read"      },
    { LIBBALSA_MESSAGE_FLAG_NEW,     1, 3, "hide-unread"    },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 1, 4, "hide-flagged"   },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 0, 5, "hide-unflagged" },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 1, 6, "hide-answered"  },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 0, 7, "hide-unanswered" }
};

static void bw_hide_changed_set_view_filter(BalsaWindow * window);

static void
hide_change_state(GSimpleAction * action,
                  GVariant      * state,
                  gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    /* PART 1: assure menu consistency */
    if (g_variant_get_boolean(state)) {
        /* we may need to deactivate coupled negated flag. */
        const gchar *action_name = g_action_get_name(G_ACTION(action));
        unsigned curr_idx, i;

        for (i = 0; i < G_N_ELEMENTS(hide_states); i++)
            if (strcmp(action_name, hide_states[i].action_name) == 0)
                break;
        g_assert(i < G_N_ELEMENTS(hide_states));
        curr_idx = hide_states[i].states_index;

        for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
            int states_idx = hide_states[i].states_index;

            if (!bw_action_get_boolean(window,
                                       hide_states[i].action_name))
                continue;

            if (hide_states[states_idx].flag == hide_states[curr_idx].flag
                && hide_states[states_idx].set !=
                hide_states[curr_idx].set) {
                GAction *coupled_action;

                coupled_action =
                    bw_get_action(window, hide_states[i].action_name);
                g_simple_action_set_state(G_SIMPLE_ACTION(coupled_action),
                                          g_variant_new_boolean(FALSE));
            }
        }
    }

    g_simple_action_set_state(action, state);

    /* PART 2: do the job. */
    bw_hide_changed_set_view_filter(window);
}

/*
 * Callbacks for various radio actions' "change-state" signals
 */
static void
bw_reset_show_all_headers(BalsaWindow * window)
{
    if (balsa_app.show_all_headers) {
        bw_action_set_boolean(window, "show-all-headers", FALSE);
        balsa_app.show_all_headers = FALSE;
    }
}

static void
header_change_state(GSimpleAction * action,
                    GVariant      * state,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    const gchar *value;
    ShownHeaders sh;

    value = g_variant_get_string(state, NULL);

    if (strcmp(value, "none") == 0)
        sh = HEADERS_NONE;
    else if (strcmp(value, "selected") == 0)
        sh = HEADERS_SELECTED;
    else if (strcmp(value, "all") == 0)
        sh = HEADERS_ALL;
    else {
        g_warning("%s unknown value “%s”", __func__, value);
        return;
    }

    balsa_app.shown_headers = sh;
    bw_reset_show_all_headers(window);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(priv->preview),
                                        sh);

    g_simple_action_set_state(action, state);
}

static void
threading_change_state(GSimpleAction * action,
                       GVariant      * state,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *index;
    gboolean thread_messages;
    LibBalsaMailbox *mailbox;

    thread_messages = g_variant_get_boolean(state);

    index = balsa_window_find_current_index(window);
    balsa_index_set_thread_messages(BALSA_INDEX(index), thread_messages);

    /* priv->current_index may have been destroyed and cleared during
     * set-threading: */
    index = balsa_window_find_current_index(window);
    if (index != NULL &&
        (mailbox = balsa_index_get_mailbox(BALSA_INDEX(index))) != NULL)
        bw_enable_expand_collapse(window, mailbox);

    g_simple_action_set_state(action, state);
}

/*
 * End of callbacks
 */

static GActionEntry win_entries[] = {
    {"new-message",           new_message_activated},
    {"new-mbox",              new_mbox_activated},
    {"new-maildir",           new_maildir_activated},
    {"new-mh",                new_mh_activated},
    {"new-imap-folder",       new_imap_folder_activated},
    {"new-imap-subfolder",    new_imap_subfolder_activated},
    {"continue",              continue_activated},
    {"get-new-mail",          get_new_mail_activated},
    {"send-queued-mail",      send_queued_mail_activated},
    {"send-and-receive-mail", send_and_receive_mail_activated},
    {"page-setup",            page_setup_activated},
    {"print",                 print_activated},
    {"address-book",          address_book_activated},
#ifdef ENABLE_AUTOCRYPT
    {"autocrypt-db",          autocrypt_db_activated},
#endif
    {"settings",              settings_activated},
    {"toolbars",              toolbars_activated},
    {"identities",            identities_activated},
    {"help",                  help_activated},
    {"about",                 about_activated},
    {"quit",                  quit_activated},
    {"copy",                  copy_activated},
    {"select-all",            select_all_activated},
    {"select-thread",         select_thread_activated},
    {"find",                  find_activated},
    {"find-next",             find_next_activated},
    {"find-in-message",       find_in_message_activated},
    {"filters",               filters_activated},
    {"export-filters",        export_filters_activated},
    {"show-mailbox-tree",     NULL, NULL, "false", show_mailbox_tree_change_state},
    {"show-mailbox-tabs",     NULL, NULL, "false", show_mailbox_tabs_change_state},
    {"show-toolbar",          NULL, NULL, "false", show_toolbar_change_state},
    {"show-statusbar",        NULL, NULL, "false", show_statusbar_change_state},
    {"show-sos-bar",          NULL, NULL, "false", show_sos_bar_change_state},
    {"wrap",                  NULL, NULL, "false", wrap_change_state},
    {"headers",               NULL, "s", "'none'", header_change_state},
    {"threading",             NULL, NULL, "false", threading_change_state},
    {"expand-all",            expand_all_activated},
    {"collapse-all",          collapse_all_activated},
#ifdef HAVE_HTML_WIDGET
    {"zoom-in",               zoom_in_activated},
    {"zoom-out",              zoom_out_activated},
    {"zoom-normal",           zoom_normal_activated},
#endif				/* HAVE_HTML_WIDGET */
    {"next-message",          next_message_activated},
    {"previous-message",      previous_message_activated},
    {"next-unread",           next_unread_activated},
    {"next-flagged",          next_flagged_activated},
    {"hide-deleted",          NULL, NULL, "false", hide_change_state},
    {"hide-undeleted",        NULL, NULL, "false", hide_change_state},
    {"hide-read",             NULL, NULL, "false", hide_change_state},
    {"hide-unread",           NULL, NULL, "false", hide_change_state},
    {"hide-flagged",          NULL, NULL, "false", hide_change_state},
    {"hide-unflagged",        NULL, NULL, "false", hide_change_state},
    {"hide-answered",         NULL, NULL, "false", hide_change_state},
    {"hide-unanswered",       NULL, NULL, "false", hide_change_state},
    {"reset-filter",          reset_filter_activated},
    {"mailbox-select-all",    mailbox_select_all_activated},
    {"mailbox-edit",          mailbox_edit_activated},
    {"mailbox-delete",        mailbox_delete_activated},
    {"mailbox-expunge",       mailbox_expunge_activated},
    {"mailbox-close",         mailbox_close_activated},
    {"empty-trash",           empty_trash_activated},
    {"select-filters",        select_filters_activated},
    {"remove-duplicates",     remove_duplicates_activated},
    {"reply",                 reply_activated},
    {"reply-all",             reply_all_activated},
    {"reply-group",           reply_group_activated},
    {"new-sender",            new_to_sender_activated},
    {"forward-attached",      forward_attached_activated},
    {"forward-inline",        forward_inline_activated},
    {"pipe",                  pipe_activated},
    {"next-part",             next_part_activated},
    {"previous-part",         previous_part_activated},
    {"save-part",             save_part_activated},
    {"view-source",           view_source_activated},
    {"recheck-crypt",         recheck_crypt_activated},
    {"copy-message",          copy_message_activated},
    {"select-text",           select_text_activated},
    {"move-to-trash",         move_to_trash_activated},
    {"toggle-flagged",        toggle_flagged_activated},
    {"toggle-deleted",        toggle_deleted_activated},
    {"toggle-new",            toggle_new_activated},
    {"toggle-answered",       toggle_answered_activated},
    {"store-address",         store_address_activated},
    /* toolbar actions that are not in any menu: */
    {"show-all-headers",      NULL, NULL, "false", show_all_headers_change_state},
    {"show-preview-pane",     NULL, NULL, "true", show_preview_pane_change_state},
};

static void
bw_add_win_action_entries(GActionMap * action_map)
{
    g_action_map_add_action_entries(action_map, win_entries,
                                    G_N_ELEMENTS(win_entries), action_map);
}

void
balsa_window_add_action_entries(GActionMap * action_map)
{
    bw_add_win_action_entries(action_map);
}

static void
bw_set_menus(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    const gchar resource_path[] = "/org/desktop/Balsa/main-window.ui";
    GError *error = NULL;
    GtkWidget *menubar;

    menubar = libbalsa_window_get_menu_bar(GTK_APPLICATION_WINDOW(window),
                                           win_entries,
                                           G_N_ELEMENTS(win_entries),
                                           resource_path, &error, window);
    if (error != NULL) {
        g_warning("%s error: %s", __func__, error->message);
        g_error_free(error);
        return;
    }
    gtk_widget_show(menubar);

    gtk_container_add(GTK_CONTAINER(priv->vbox), menubar);
}

/*
 * Implement <alt>n to switch to the n'th mailbox tab
 * (n = 1, 2, ..., 9);
 * <alt>0 switches to the last tab; useful when more than 9 mailboxes
 * are open.
 *
 * Note that GtkNotebook natively supports <control><alt>page-up to
 * switch one tab to the left and <control><alt>page-down to
 * switch one tab to the right.
 */

static void
bw_alt_n_cb(GtkAccelGroup * accel_group,
            GObject       * acceleratable,
            guint           keyval,
            GdkModifierType modifier,
            gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook),
                                  keyval - GDK_KEY_1);
}

static void
bw_set_alt_bindings(BalsaWindow * window)
{
    GtkAccelGroup *accel_group;
    gint i;

    accel_group = gtk_accel_group_new();

    for (i = 0; i < 10; i++) {
        gchar accel[8];
        guint accel_key;
        GdkModifierType accel_mods;
        GClosure *closure;

        g_snprintf(accel, sizeof(accel), "<alt>%d", i);
        gtk_accelerator_parse(accel, &accel_key, &accel_mods);

        closure = g_cclosure_new(G_CALLBACK(bw_alt_n_cb), window, NULL);
        gtk_accel_group_connect(accel_group, accel_key, accel_mods, 0,
                                closure);
    }
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
}

/*
 * lists of actions that are enabled or disabled as groups
 */
static const gchar *const mailbox_actions[] = {
    "select-all",
    "find", "find-next",
    "next-message", "previous-message", "next-flagged",
#ifdef CAN_HIDE_MENUS_AS_ACTIONS
    "mailbox-hide-menu",
#else
    "hide-deleted", "hide-read", "hide-flagged", "hide-answered",
    "hide-undeleted", "hide-unread", "hide-unflagged", "hide-unanswered",
#endif
    "reset-filter",
    "mailbox-select-all", "mailbox-edit", "mailbox-delete",
    "mailbox-expunge", "mailbox-close", "select-filters", "remove-duplicates"
};

static const gchar *const message_actions[] = {
    "reply", "reply-all", "new-sender",
    "store-address", "view-source", "forward-attached", "forward-inline",
    "pipe", "select-thread"
};

static const gchar *const modify_message_actions[] = {
    "move-to-trash",
#ifdef CAN_HIDE_MENUS_AS_ACTIONS
    "message-toggle-flag-menu",
#else
    "toggle-flagged", "toggle-deleted", "toggle-new", "toggle-answered"
#endif
};

static const gchar *const current_message_actions[] = {
    "print",
    "save-part", "next-part", "previous-part",
    "copy-message", "select-text", "find-in-message"
};

/*
 * end of GMenu stuff
 */

/*
 * The actual BalsaWindow
 */

/*
 * Callback for the mblist's "has-unread-mailbox" signal
 */
static void
bw_enable_next_unread(BalsaWindow * window, gboolean has_unread_mailbox)
{
    bw_action_set_enabled(window, "next-unread", has_unread_mailbox);
}

#ifdef ENABLE_SYSTRAY
static void
on_systray_click(gpointer data)
{
	GtkWindow *window = GTK_WINDOW(data);

	g_return_if_fail(window != NULL);
	if (gtk_window_is_active(window)) {
		gtk_window_iconify(window);
	} else {
		gtk_window_present_with_time(window, gtk_get_current_event_time());
	}
}

static void
on_systray_show_hide(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
	/* process pending events, as otherwise the Balsa window will never be active... */
	while (gtk_events_pending()) {
		gtk_main_iteration();
	}
	on_systray_click(user_data);
}

static void
on_systray_receive(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
	if (g_atomic_int_get(&checking_mail) == 1) {
		check_new_messages_real(BALSA_WINDOW(user_data), TRUE);
	}
}

static void
on_systray_new_msg(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
	new_message_activated(NULL, NULL, user_data);
}

static void
bw_init_systray(BalsaWindow *window)
{
    GtkWidget *menu;
    GtkWidget *menuitem;

    menu = gtk_menu_new();
    menuitem = gtk_menu_item_new_with_label(_("Show/Hide"));
    g_signal_connect(menuitem, "activate", G_CALLBACK(on_systray_show_hide), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Check"));
    g_signal_connect(menuitem, "activate", G_CALLBACK(on_systray_receive), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    menuitem = gtk_menu_item_new_with_label(_("Compose"));
    g_signal_connect(menuitem, "activate", G_CALLBACK(on_systray_new_msg), window);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
    gtk_widget_show_all(menu);
    libbalsa_systray_icon_init(GTK_MENU(menu), on_systray_click, window);
}
#endif

GtkWidget *
balsa_window_new(GtkApplication *application)
{
    BalsaWindow *window;
    BalsaWindowPrivate *priv;
    BalsaToolbarModel *model;
    GtkWidget *hbox;
    static const gchar *const header_targets[] =
        { "none", "selected", "all" };
    GtkAdjustment *hadj, *vadj;
    GAction *action;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS
     * - allows for grey out */
    balsa_register_pixmaps();

    window = g_object_new(BALSA_TYPE_WINDOW,
                          "application", application,
                          NULL);
    priv = balsa_window_get_instance_private(window);

    priv->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(priv->vbox);
    gtk_container_add(GTK_CONTAINER(window), priv->vbox);

    /* Set up the GMenu structures */
    bw_set_menus(window);

    /* Set up <alt>n key bindings */
    bw_set_alt_bindings(window);

    gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    balsa_register_icon_names();

    model = balsa_window_get_toolbar_model();

    priv->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_container_add(GTK_CONTAINER(priv->vbox), priv->toolbar);

    priv->content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(priv->content_area);
    gtk_widget_set_vexpand(priv->content_area, TRUE);
    gtk_widget_set_valign(priv->content_area, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(priv->vbox), priv->content_area);

    priv->bottom_bar = hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
    gtk_container_add(GTK_CONTAINER(priv->vbox), hbox);

    priv->progress_bar = gtk_progress_bar_new();
    g_object_add_weak_pointer(G_OBJECT(priv->progress_bar),
                              (gpointer *) &priv->progress_bar);
    gtk_widget_set_valign(priv->progress_bar, GTK_ALIGN_CENTER);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(priv->progress_bar),
                                    0.01);
    gtk_container_add(GTK_CONTAINER(hbox), priv->progress_bar);

    priv->statusbar = gtk_statusbar_new();
    gtk_widget_set_hexpand(priv->statusbar, TRUE);
    gtk_widget_set_halign(priv->statusbar, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(hbox), priv->statusbar);
    gtk_widget_show_all(hbox);

#if 0
    gnome_app_install_appbar_menu_hints(GNOME_APPBAR(balsa_app.appbar),
                                        main_menu);
#endif

    geometry_manager_attach(GTK_WINDOW(window), "MainWindow");

    priv->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(priv->notebook),
                               balsa_app.show_notebook_tabs);
    gtk_notebook_set_show_border (GTK_NOTEBOOK(priv->notebook), FALSE);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (priv->notebook), TRUE);
    g_signal_connect(priv->notebook, "switch_page",
                     G_CALLBACK(bw_notebook_switch_page_cb), window);
    gtk_drag_dest_set (GTK_WIDGET (priv->notebook), GTK_DEST_DEFAULT_ALL,
                       notebook_drop_types, NUM_DROP_TYPES,
                       GDK_ACTION_DEFAULT | GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(priv->notebook, "drag-data-received",
                     G_CALLBACK (bw_notebook_drag_received_cb), NULL);
    g_signal_connect(priv->notebook, "drag-motion",
                     G_CALLBACK (bw_notebook_drag_motion_cb), NULL);
    balsa_app.notebook = priv->notebook;
    g_object_add_weak_pointer(G_OBJECT(priv->notebook),
			      (gpointer *) &balsa_app.notebook);

    priv->preview = balsa_message_new();
    g_object_add_weak_pointer(G_OBJECT(priv->preview),
                              (gpointer *) &priv->preview);
    gtk_widget_hide(priv->preview);

    g_signal_connect(priv->preview, "select-part",
                     G_CALLBACK(bw_select_part_cb), window);

    /* XXX */
    balsa_app.mblist =  BALSA_MBLIST(balsa_mblist_new());
    gtk_widget_show(GTK_WIDGET(balsa_app.mblist));

    g_object_get(balsa_app.mblist, "hadjustment", &hadj,
                 "vadjustment", &vadj, NULL);
    priv->mblist = gtk_scrolled_window_new(hadj, vadj);

    gtk_container_add(GTK_CONTAINER(priv->mblist),
                      GTK_WIDGET(balsa_app.mblist));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(priv->mblist),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_signal_connect_swapped(balsa_app.mblist, "has-unread-mailbox",
		             G_CALLBACK(bw_enable_next_unread), window);
    balsa_mblist_default_signal_bindings(balsa_app.mblist);

    bw_set_panes(window);

    bw_action_set_boolean(window, "show-mailbox-tree",
                          balsa_app.show_mblist);

    if (balsa_app.show_mblist) {
        gtk_widget_show(priv->mblist);
        gtk_paned_set_position(GTK_PANED(priv->mblist_parent),
                               balsa_app.mblist_width);
    } else {
        gtk_paned_set_position(GTK_PANED(priv->mblist_parent), 0);
    }

    if (priv->notebook_parent != NULL) {
        if (balsa_app.previewpane) {
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent),
                                   balsa_app.notebook_height);
        } else {
            /* Set it to something really high */
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent), G_MAXINT);
        }
    }

    gtk_widget_show(priv->notebook);

    /* set the toolbar style */
    balsa_window_refresh(window);

    action = bw_get_action(window, "headers");
    g_simple_action_set_state(G_SIMPLE_ACTION(action),
                              g_variant_new_string(header_targets
                                                   [balsa_app.
                                                    shown_headers]));

    action = bw_get_action(window, "threading");
    g_simple_action_set_state(G_SIMPLE_ACTION(action),
                              g_variant_new_boolean(FALSE));

    bw_action_set_boolean(window, "show-mailbox-tabs",
                          balsa_app.show_notebook_tabs);
    bw_action_set_boolean(window, "wrap", balsa_app.browse_wrap);
    bw_action_set_boolean(window, "show-toolbar",
                          balsa_app.show_main_toolbar);
    bw_action_set_boolean(window, "show-statusbar",
                          balsa_app.show_statusbar);
    bw_action_set_boolean(window, "show-sos-bar", balsa_app.show_sos_bar);

    /* Disable menu items at start up */
    balsa_window_update_book_menus(window);
    bw_enable_mailbox_menus(window, NULL);
    bw_enable_message_menus(window, 0);
#ifdef HAVE_HTML_WIDGET
    bw_enable_view_menus(window, NULL);
#endif				/* HAVE_HTML_WIDGET */
    balsa_window_enable_continue(window);

    /* set initial state of toggle preview pane button */

    /* set initial state of next-unread controls */
    bw_enable_next_unread(window, FALSE);

    g_signal_connect(window, "delete-event",
                     G_CALLBACK(bw_delete_cb), NULL);

    /* Cancel new-mail notification when we get the focus. */
    g_signal_connect(window, "notify::is-active",
                     G_CALLBACK(bw_is_active_notify), NULL);

    /* set initial state of Get-New-Mail button */
    bw_action_set_enabled(window, "get-new-mail", g_atomic_int_get(&checking_mail) == 1);

    g_timeout_add_seconds(30, (GSourceFunc) bw_close_mailbox_on_timer, window);

#ifdef ENABLE_SYSTRAY
    bw_init_systray(window);
    libbalsa_systray_icon_enable(balsa_app.enable_systray_icon != 0);
#endif

    gtk_widget_show(GTK_WIDGET(window));
    return GTK_WIDGET(window);
}

/*
 * Enable or disable menu items/toolbar buttons which depend
 * on whether there is a mailbox open.
 */
static void
bw_enable_expand_collapse(BalsaWindow * window, LibBalsaMailbox * mailbox)
{
    gboolean enable;

    enable = mailbox != NULL;
    bw_action_set_enabled(window, "threading", enable);

    enable = mailbox != NULL &&
        libbalsa_mailbox_get_threading_type(mailbox) !=
        LB_MAILBOX_THREADING_FLAT;
    bw_action_set_enabled(window, "expand-all", enable);
    bw_action_set_enabled(window, "collapse-all", enable);
}

/*
 * bw_next_unread_mailbox: look for the next mailbox with unread mail,
 * starting at current_mailbox; if no later mailbox has unread messages
 * or current_mailbox == NULL, return the first mailbox with unread mail;
 * never returns current_mailbox if it's nonNULL.
 */

static LibBalsaMailbox *
bw_next_unread_mailbox(LibBalsaMailbox * current_mailbox)
{
    GList *unread, *list;
    LibBalsaMailbox *next_mailbox;

    unread = balsa_mblist_find_all_unread_mboxes(current_mailbox);
    if (!unread)
        return NULL;

    list = g_list_find(unread, NULL);
    next_mailbox = list && list->next ? list->next->data : unread->data;

    g_list_free(unread);

    return next_mailbox;
}

static void
bw_enable_mailbox_menus(BalsaWindow * window, BalsaIndex * index)
{
    LibBalsaMailbox *mailbox = NULL;
    BalsaMailboxNode *mbnode = NULL;
    gboolean enable;

    enable = (index != NULL);
    if (enable) {
        mbnode = balsa_index_get_mailbox_node(index);
        mailbox = balsa_mailbox_node_get_mailbox(mbnode);
    }
    bw_action_set_enabled(window, "mailbox-expunge",
    /* cppcheck-suppress nullPointer */
                          mailbox && !libbalsa_mailbox_get_readonly(mailbox));

    bw_actions_set_enabled(window, mailbox_actions,
                           G_N_ELEMENTS(mailbox_actions), enable);
    bw_action_set_enabled(window, "new-imap-subfolder", balsa_mailbox_node_is_imap(mbnode));
    bw_action_set_enabled(window, "next-message",
                          index != NULL && balsa_index_get_next_message(index));
    bw_action_set_enabled(window, "previous-message",
                          index != NULL && balsa_index_get_prev_message(index));

    bw_action_set_enabled(window, "remove-duplicates", mailbox &&
                          libbalsa_mailbox_can_move_duplicates(mailbox));

    if (mailbox != NULL) {
        bw_action_set_boolean(window, "threading",
                              libbalsa_mailbox_get_threading_type(mailbox) !=
                              LB_MAILBOX_THREADING_FLAT);
        bw_enable_expand_collapse(window, mailbox);
	bw_set_filter_menu(window, libbalsa_mailbox_get_filter(mailbox));
    }

    bw_enable_next_unread(window, libbalsa_mailbox_get_unread(mailbox) > 0
                          || bw_next_unread_mailbox(mailbox));

    bw_enable_expand_collapse(window, mailbox);
}

void
balsa_window_update_book_menus(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    gboolean enabled = balsa_app.address_book_list != NULL;

    bw_action_set_enabled(window, "address-book",  enabled);

    if (enabled && priv->current_index != NULL) {
        guint current_msgno =
            balsa_index_get_current_msgno(BALSA_INDEX(priv->current_index));
        enabled = current_msgno > 0;
    }
    bw_action_set_enabled(window, "store-address", enabled);
}

/*
 * Enable or disable menu items/toolbar buttons which depend
 * on if there is a message selected.
 */
static void
bw_enable_message_menus(BalsaWindow * window, guint msgno)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    gboolean enable, enable_mod, enable_store, enable_reply_group;
    BalsaIndex *bindex = BALSA_INDEX(priv->current_index);

    enable = (msgno != 0 && bindex != NULL);
    bw_actions_set_enabled(window, current_message_actions,
                           G_N_ELEMENTS(current_message_actions), enable);

    enable_reply_group = FALSE;
    if (enable) {
        GList *messages, *list;

        messages = balsa_index_selected_list(BALSA_INDEX(bindex));
        for (list = messages; list != NULL; list = list->next) {
            LibBalsaMessage *message = list->data;

            if (libbalsa_message_get_user_header(message, "list-post") != NULL) {
                enable_reply_group = TRUE;
                break;
            }
        }
        g_list_free_full(messages, g_object_unref);
    }
    bw_action_set_enabled(window, "reply-group", enable_reply_group);

    enable = (bindex != NULL
              && balsa_index_count_selected_messages(bindex) > 0);
    bw_actions_set_enabled(window, message_actions,
                           G_N_ELEMENTS(message_actions), enable);

    enable_mod =
        (enable && !libbalsa_mailbox_get_readonly(balsa_index_get_mailbox(bindex)));
    bw_actions_set_enabled(window, modify_message_actions,
                           G_N_ELEMENTS(modify_message_actions),
                           enable_mod);

    enable_store = (enable && balsa_app.address_book_list != NULL);
    bw_action_set_enabled(window, "store-address", enable_store);

    balsa_window_enable_continue(window);
}

/*
 * Called when the current part has changed: Enable/disable the copy
 * and select all buttons
 */
static void
bw_enable_edit_menus(BalsaWindow * window, BalsaMessage * bm)
{
    static const gchar * const edit_actions[] = {
        "copy", "copy-message", "select-text"
    };
    gboolean enable = (bm && balsa_message_can_select(bm));

    bw_actions_set_enabled(window, edit_actions,
                           G_N_ELEMENTS(edit_actions), enable);
#ifdef HAVE_HTML_WIDGET
    bw_enable_view_menus(window, bm);
#endif				/* HAVE_HTML_WIDGET */
}

#ifdef HAVE_HTML_WIDGET
/*
 * Enable/disable the Zoom menu items on the View menu.
 */
static void
bw_enable_view_menus(BalsaWindow * window, BalsaMessage * bm)
{
    static const gchar * const zoom_actions[] = {
        "zoom-in", "zoom-out", "zoom-normal"
    };
    gboolean enable = bm && balsa_message_can_zoom(bm);

    bw_actions_set_enabled(window, zoom_actions,
                           G_N_ELEMENTS(zoom_actions), enable);
}
#endif				/* HAVE_HTML_WIDGET */

/*
 * Enable/disable menu items/toolbar buttons which depend
 * on the Trash folder containing messages
 *
 * If the trash folder is already open, use the message count
 * to set the icon regardless of the parameter.  Else the
 * value of the parameter is used to either set or clear trash
 * items, or to open the trash folder and get the message count.
 */
void
enable_empty_trash(BalsaWindow * window, TrashState status)
{
    gboolean set = TRUE;
    if (MAILBOX_OPEN(balsa_app.trash)) {
        set = libbalsa_mailbox_total_messages(balsa_app.trash) > 0;
    } else {
        switch(status) {
        case TRASH_CHECK:
            /* Check msg count in trash; this may be expensive...
             * lets just enable empty trash to be on the safe side */
#if CAN_DO_MAILBOX_OPENING_VERY_VERY_FAST
            if (balsa_app.trash) {
                libbalsa_mailbox_open(balsa_app.trash);
		set = libbalsa_mailbox_total_messages(balsa_app.trash) > 0;
                libbalsa_mailbox_close(balsa_app.trash);
            } else set = TRUE;
#else
            set = TRUE;
#endif
            break;
        case TRASH_FULL:
            set = TRUE;
            break;
        case TRASH_EMPTY:
            set = FALSE;
            break;
        }
    }
    bw_action_set_enabled(window, "empty-trash", set);
}

/*
 * Enable/disable the continue buttons
 */
void
balsa_window_enable_continue(BalsaWindow * window)
{
    if (!window)
	return;

    /* Check msg count in draftbox */
    if (balsa_app.draftbox) {
        /* This is commented out because it causes long delays and
         * flickering of the mailbox list if large numbers of messages
         * are selected.  Checking the has_unread_messages flag works
         * almost as well.
         * */
/*      libbalsa_mailbox_open(balsa_app.draftbox, FALSE); */
/*      if (libbalsa_mailbox_total_messages(balsa_app.draftbox) > 0) { */

        gboolean n = !MAILBOX_OPEN(balsa_app.draftbox)
            || libbalsa_mailbox_total_messages(balsa_app.draftbox) > 0;

        bw_action_set_enabled(window, "continue", n);

/*      libbalsa_mailbox_close(balsa_app.draftbox); */
    }
}

static void
bw_enable_part_menu_items(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaMessage *msg = window ? BALSA_MESSAGE(priv->preview) : NULL;
	LibBalsaMessage *lbmessage;

    bw_action_set_enabled(window, "next-part",
                          balsa_message_has_next_part(msg));
    bw_action_set_enabled(window, "previous-part",
                          balsa_message_has_previous_part(msg));
    lbmessage = (msg != NULL) ? balsa_message_get_message(msg) : NULL;
    bw_action_set_enabled(window, "recheck-crypt",
    	(lbmessage != NULL) ? libbalsa_message_has_crypto_content(lbmessage) : FALSE);
}

static void
bw_set_filter_menu(BalsaWindow * window, int mask)
{
    unsigned i;

    for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
        GAction *action;
        gboolean state;

        action = bw_get_action(window, hide_states[i].action_name);
        state = (mask >> hide_states[i].states_index) & 1;
        g_simple_action_set_state(G_SIMPLE_ACTION(action),
                                  g_variant_new_boolean(state));
    }
}

/*
 * bw_filter_to_int() returns an integer mask representing the view
 * filter, as determined by the check-box widgets.
 */
static int
bw_filter_to_int(BalsaWindow * window)
{
    unsigned i;
    int res = 0;
    for (i = 0; i < G_N_ELEMENTS(hide_states); i++)
        if (bw_action_get_boolean(window, hide_states[i].action_name))
            res |= 1 << hide_states[i].states_index;
    return res;
}

/*
 * bw_get_condition_from_int() returns the LibBalsaCondition corresponding
 * to the filter mask.
 */
static LibBalsaCondition *
bw_get_condition_from_int(gint mask)
{
    LibBalsaCondition *filter = NULL;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
        if (((mask >> hide_states[i].states_index) & 1)) {
            LibBalsaCondition *lbc, *res;

            lbc = libbalsa_condition_new_flag_enum(hide_states[i].set,
                                                   hide_states[i].flag);
            res = libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                                  lbc, filter);
            libbalsa_condition_unref(lbc);
            libbalsa_condition_unref(filter);
            filter = res;
        }
    }

    return filter;
}

/* balsa_window_open_mbnode:
   opens mailbox, creates message index. mblist_open_mailbox() is what
   you want most of the time because it can switch between pages if a
   mailbox is already on one of them.
*/
void
balsa_window_open_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode,
                         gboolean set_current)
{
    g_return_if_fail(BALSA_IS_WINDOW(window));

    BALSA_WINDOW_GET_CLASS(window)->open_mbnode(window, mbnode,
                                                      set_current);
}

void
balsa_window_close_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(BALSA_IS_WINDOW(window));

    BALSA_WINDOW_GET_CLASS(window)->close_mbnode(window, mbnode);
}

static void
bw_notebook_label_style(GtkLabel * lab, gboolean has_unread_messages)
{
    gchar *str = has_unread_messages ?
	g_strconcat("<b>", gtk_label_get_text(lab), "</b>", NULL) :
	g_strdup(gtk_label_get_text(lab));
    gtk_label_set_markup(lab, str);
    g_free(str);
}

static void
bw_mailbox_changed(LibBalsaMailbox * mailbox, GtkLabel * lab)
{
    bw_notebook_label_style(lab, libbalsa_mailbox_get_unread(mailbox) > 0);
}

static GtkWidget *
bw_notebook_label_new(BalsaMailboxNode * mbnode)
{
    GtkWidget *lab;
    GtkWidget *close_pix;
    GtkWidget *box;
    GtkWidget *but;
    gint w, h;
    GtkCssProvider *css_provider;

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

    lab = gtk_label_new(libbalsa_mailbox_get_name(balsa_mailbox_node_get_mailbox(mbnode)));
    gtk_widget_set_name(lab, "balsa-notebook-tab-label");

    /* Try to make text not bold: */
    css_provider = gtk_css_provider_new();
    if (!gtk_css_provider_load_from_data(css_provider,
                                         "#balsa-notebook-tab-label"
                                         "{"
                                           "font-weight:normal;"
                                         "}",
                                         -1, NULL))
        g_warning("Could not load label CSS data.");

    gtk_style_context_add_provider(gtk_widget_get_style_context(lab) ,
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    bw_notebook_label_style(GTK_LABEL(lab),
                            libbalsa_mailbox_get_unread(balsa_mailbox_node_get_mailbox(mbnode)) > 0);
    g_signal_connect_object(balsa_mailbox_node_get_mailbox(mbnode), "changed",
                            G_CALLBACK(bw_mailbox_changed), lab, 0);
    gtk_container_add(GTK_CONTAINER(box), lab);

    but = gtk_button_new();
    gtk_widget_set_focus_on_click(but, FALSE);
    gtk_button_set_relief(GTK_BUTTON(but), GTK_RELIEF_NONE);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &w, &h);
    gtk_widget_set_size_request(but, w, h);

    g_signal_connect(but, "clicked",
                     G_CALLBACK(bw_mailbox_tab_close_cb), mbnode);

    close_pix = gtk_image_new_from_icon_name("window-close-symbolic",
                                             GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(but), close_pix);
    gtk_container_add(GTK_CONTAINER(box), but);

    gtk_widget_show_all(box);

    gtk_widget_set_tooltip_text(box, libbalsa_mailbox_get_url(balsa_mailbox_node_get_mailbox(mbnode)));
    return box;
}

/*
 * balsa_window_real_open_mbnode
 */

struct _BalsaWindowRealOpenMbnodeInfo
{
    BalsaIndex       *index;
    BalsaMailboxNode *mbnode;
    BalsaWindow      *window;
    gchar            *message;
    gboolean          set_current;
};

static gboolean
bw_real_open_mbnode_idle_cb(BalsaWindowRealOpenMbnodeInfo * info)
{
    BalsaIndex        *index   = info->index;
    BalsaMailboxNode  *mbnode  = info->mbnode;
    BalsaWindow       *window  = info->window;
    BalsaWindowPrivate *priv   = balsa_window_get_instance_private(window);
    LibBalsaMailbox   *mailbox = balsa_mailbox_node_get_mailbox(mbnode);
    GtkWidget         *label;
    GtkWidget         *scroll;
    gint               page_num;
    LibBalsaCondition *filter;

    if (mbnode == NULL)
        return FALSE;

    balsa_window_decrease_activity(window, info->message);

    g_free(info->message);
    info->message = NULL;

    balsa_index_load_mailbox_node(index);

    g_signal_connect(index, "index-changed",
                     G_CALLBACK(bw_index_changed_cb), window);

    label = bw_notebook_label_new(mbnode);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(index));
    gtk_widget_show(scroll);
    g_signal_connect(scroll, "child-notify::position",
                     G_CALLBACK(bw_notebook_page_notify_cb), priv->notebook);
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(priv->notebook),
                                        scroll, label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(priv->notebook),
                                     scroll, TRUE);

    if (info->set_current) {
        /* change the page to the newly selected notebook item */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(priv->notebook),
                                      page_num);
    }

    libbalsa_mailbox_set_threading(mailbox);

    filter =
        bw_get_condition_from_int(libbalsa_mailbox_get_filter(mailbox));
    libbalsa_mailbox_set_view_filter(mailbox, filter, FALSE);
    libbalsa_condition_unref(filter);

    /* scroll may select the message and GtkTreeView does not like selecting
     * without being shown first. */
    gtk_widget_show(GTK_WIDGET(index));
    balsa_index_scroll_on_open(index);

    g_ptr_array_remove_fast(priv->open_mbnode_info_array, info);

    return FALSE;
}

static void
bw_open_mbnode_info_free(BalsaWindowRealOpenMbnodeInfo * info)
{
    g_free(info->message);
    g_object_unref(g_object_ref_sink(info->index));
    if (info->mbnode != NULL)
        g_object_remove_weak_pointer(G_OBJECT(info->mbnode), (gpointer *) &info->mbnode);
    g_free(info);
}

static void
bw_real_open_mbnode_thread(BalsaWindowRealOpenMbnodeInfo * info)
{
    gint try_cnt;
    LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(info->mbnode);
    GError *err = NULL;
    gboolean successp;

    try_cnt = 0;
    do {
        g_clear_error(&err);
        successp = libbalsa_mailbox_open(mailbox, &err);

        if(successp) break;
        if(err && err->code != LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR)
            break;
        balsa_mblist_close_lru_peer_mbx(balsa_app.mblist, mailbox);
    } while(try_cnt++<3);

    if (successp && balsa_find_notebook_page_num(mailbox) < 0) {
        BalsaWindowPrivate *priv =
            balsa_window_get_instance_private(info->window);

        g_ptr_array_add(priv->open_mbnode_info_array, info);
        balsa_index_set_mailbox_node(info->index, info->mbnode);
        g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,
                        (GSourceFunc) bw_real_open_mbnode_idle_cb, info,
                        (GDestroyNotify) bw_open_mbnode_info_free);
    } else {
        libbalsa_information(
            LIBBALSA_INFORMATION_ERROR,
            _("Unable to Open Mailbox!\n%s."),
	    err ? err->message : _("Unknown error"));
        if (err != NULL)
            g_error_free(err);
        balsa_window_decrease_activity(info->window, info->message);
        bw_open_mbnode_info_free(info);
    }
}

static void
balsa_window_real_open_mbnode(BalsaWindow * window,
                              BalsaMailboxNode * mbnode,
                              gboolean set_current)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaIndex * index;
    gchar *message;
    LibBalsaMailbox *mailbox;
    BalsaWindowRealOpenMbnodeInfo *info;

    if (bw_is_open_mailbox(mailbox = balsa_mailbox_node_get_mailbox(mbnode)))
        return;
    bw_register_open_mailbox(mailbox);

    index = BALSA_INDEX(balsa_index_new());
    balsa_index_set_width_preference
        (index,
         (balsa_app.layout_type == LAYOUT_WIDE_SCREEN)
         ? BALSA_INDEX_NARROW : BALSA_INDEX_WIDE);

    message = g_strdup_printf(_("Opening %s"), libbalsa_mailbox_get_name(mailbox));
    balsa_window_increase_activity(window, message);

    info = g_new(BalsaWindowRealOpenMbnodeInfo, 1);

    info->mbnode = mbnode;
    g_object_add_weak_pointer(G_OBJECT(mbnode), (gpointer *) &info->mbnode);

    info->window = window;
    info->set_current = set_current;
    info->index = index;
    info->message = message;

    g_thread_pool_push(priv->open_mbnode_thread_pool, info, NULL);
}

/* balsa_window_real_close_mbnode:
   this function overloads libbalsa_mailbox_close_mailbox.

*/
static gboolean
bw_focus_idle(LibBalsaMailbox ** mailbox)
{
    if (*mailbox)
	g_object_remove_weak_pointer(G_OBJECT(*mailbox), (gpointer *) mailbox);
    if (balsa_app.mblist_tree_store)
        balsa_mblist_focus_mailbox(balsa_app.mblist, *mailbox);
    g_free(mailbox);
    return FALSE;
}

#define BALSA_INDEX_GRAB_FOCUS "balsa-index-grab-focus"
static void
balsa_window_real_close_mbnode(BalsaWindow * window,
                               BalsaMailboxNode * mbnode)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *index = NULL;
    gint i;
    LibBalsaMailbox **mailbox;

    g_return_if_fail(balsa_mailbox_node_get_mailbox(mbnode));

    i = balsa_find_notebook_page_num(balsa_mailbox_node_get_mailbox(mbnode));

    if (i != -1) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(priv->notebook), i);
        bw_unregister_open_mailbox(balsa_mailbox_node_get_mailbox(mbnode));

        /* If this is the last notebook page clear the message preview
           and the status bar */
        if (balsa_app.notebook
            && gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),
                                         0) == NULL) {
            GtkStatusbar *statusbar;
            guint context_id;

            gtk_window_set_title(GTK_WINDOW(window), "Balsa");
            bw_idle_replace(window, NULL);

            if (!balsa_app.in_destruction) {
                statusbar = GTK_STATUSBAR(priv->statusbar);
                context_id = gtk_statusbar_get_context_id(statusbar, "BalsaWindow mailbox");
                gtk_statusbar_pop(statusbar, context_id);
                gtk_statusbar_push(statusbar, context_id, "Mailbox closed");
            }

            /* Disable menus */
            bw_enable_mailbox_menus(window, NULL);
            bw_enable_message_menus(window, 0);
	    if (priv->current_index)
		g_object_remove_weak_pointer(G_OBJECT(priv->current_index),
					     (gpointer *)
					     &priv->current_index);
            priv->current_index = NULL;

            /* Just in case... */
            g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, NULL);
        }
    }

    index = balsa_window_find_current_index(window);
    mailbox = g_new(LibBalsaMailbox *, 1);
    if (index != NULL) {
	*mailbox = balsa_index_get_mailbox(BALSA_INDEX(index));
	g_object_add_weak_pointer(G_OBJECT(*mailbox), (gpointer *) mailbox);
    } else {
	*mailbox = NULL;
    }
    g_idle_add((GSourceFunc) bw_focus_idle, mailbox);
}

/* balsa_identities_changed is used to notify the listener list that
   the identities list has changed. */
void
balsa_identities_changed(BalsaWindow *bw)
{
    g_return_if_fail(BALSA_IS_WINDOW(bw));

    g_signal_emit(bw, window_signals[IDENTITIES_CHANGED], (GQuark) 0);
}

static gboolean
bw_close_mailbox_on_timer(BalsaWindow * window)
{
    time_t current_time;
    GtkWidget *page;
    int i, c, delta_time;

    if (!balsa_app.notebook)
        return FALSE;
    if (!balsa_app.close_mailbox_auto)
        return TRUE;

    time(&current_time);

    c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

    for (i = 0;
         (page =
          gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
         i++) {
        BalsaIndex *index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));

        if (i == c)
            continue;

        if (balsa_app.close_mailbox_auto &&
            (delta_time = current_time - balsa_index_get_last_use_time(index)) >
            balsa_app.close_mailbox_timeout) {
            g_debug("Closing Page %d unused for %d s", i, delta_time);
            balsa_window_real_close_mbnode(window, balsa_index_get_mailbox_node(index));
            if (i < c)
                c--;
            i--;
        }
    }
    return TRUE;
}

static void
balsa_window_dispose(GObject * object)
{
    BalsaWindow *window = BALSA_WINDOW(object);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (priv->preview != NULL) {
        g_object_remove_weak_pointer(G_OBJECT(priv->preview), (gpointer *) &priv->preview);
        priv->preview = NULL;
    }

    if (priv->set_message_id != 0) {
        g_source_remove(priv->set_message_id);
        priv->set_message_id = 0;
    }

    if (priv->network_changed_source_id != 0) {
        g_source_remove(priv->network_changed_source_id);
        priv->network_changed_source_id = 0;
    }

    if (priv->network_changed_handler_id != 0) {
        GNetworkMonitor *monitor = g_network_monitor_get_default();
        g_signal_handler_disconnect(monitor, priv->network_changed_handler_id);
        priv->network_changed_handler_id = 0;
    }

    if (priv->open_mbnode_thread_pool != NULL) {
        g_thread_pool_free(priv->open_mbnode_thread_pool,
                           TRUE /* pool should shut down immediately */,
                           TRUE /* wait for all tasks to be finished */);
        priv->open_mbnode_thread_pool = NULL;
    }

    if (priv->open_mbnode_info_array != NULL) {
        g_ptr_array_free(priv->open_mbnode_info_array, TRUE);
        priv->open_mbnode_info_array = NULL;
    }

    if (priv->activity_messages != NULL) {
        g_slist_free_full(priv->activity_messages, g_free);
        priv->activity_messages = NULL;
    }

    balsa_app.in_destruction = TRUE;
    G_OBJECT_CLASS(balsa_window_parent_class)->dispose(object);

    balsa_unregister_pixmaps();

#ifdef ENABLE_SYSTRAY
    libbalsa_systray_icon_destroy();
#endif
}

/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *index;
    BalsaIndex *bindex;

    g_return_if_fail(window);

    index = balsa_window_find_current_index(window);
    bindex = (BalsaIndex *) index;
    if (bindex) {
        /* update the date column, only in the current page */
        balsa_index_refresh_date(bindex);
        /* update the size column, only in the current page */
        balsa_index_refresh_size(bindex);

    }
    if (balsa_app.previewpane) {
        bw_idle_replace(window, bindex);
        if (priv->notebook_parent != NULL) {
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent),
                                   balsa_app.notebook_height);
        }
    } else {
        if (priv->notebook_parent != NULL) {
            /* Set the height to something really big (those new hi-res
             * screens and all :) */
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent), G_MAXINT);
        }
    }
}

/* monitored functions for MT-safe manipulation of the open mailbox list
   QUESTION: could they migrate to balsa-app.c?
*/
static GMutex open_list_lock;

static void
bw_register_open_mailbox(LibBalsaMailbox *m)
{
	g_mutex_lock(&open_list_lock);
    balsa_app.open_mailbox_list =
        g_list_prepend(balsa_app.open_mailbox_list, m);
    g_mutex_unlock(&open_list_lock);
    libbalsa_mailbox_set_open(m, TRUE);
}
static void
bw_unregister_open_mailbox(LibBalsaMailbox *m)
{
	g_mutex_lock(&open_list_lock);
    balsa_app.open_mailbox_list =
        g_list_remove(balsa_app.open_mailbox_list, m);
    g_mutex_unlock(&open_list_lock);
    libbalsa_mailbox_set_open(m, FALSE);
}
static gboolean
bw_is_open_mailbox(LibBalsaMailbox *m)
{
    GList *res;
    g_mutex_lock(&open_list_lock);
    res= g_list_find(balsa_app.open_mailbox_list, m);
    g_mutex_unlock(&open_list_lock);
    return (res != NULL);
}

/* Check all mailboxes in a list
 *
 */
static void
bw_check_mailbox_progress_cb(LibBalsaMailbox* mailbox, gint action, gdouble fraction, const gchar *message)
{
	gchar *progress_id;

	progress_id = g_strdup_printf("POP3: %s", libbalsa_mailbox_get_name(mailbox));
	if (action == LIBBALSA_NTFY_INIT) {
		libbalsa_progress_dialog_ensure(&progress_dialog, _("Checking Mail…"), GTK_WINDOW(balsa_app.main_window), progress_id);
	}
	libbalsa_progress_dialog_update(&progress_dialog, progress_id, action == LIBBALSA_NTFY_FINISHED, fraction, "%s", message);
	g_free(progress_id);
}

static void
bw_check_mailbox(LibBalsaMailbox *mailbox)
{
	libbalsa_mailbox_check(mailbox);
	g_thread_exit(NULL);
}

typedef struct {
	LibBalsaMailbox *mailbox;
	GThread *thread;
	gulong notify;
} bw_pop_mbox_t;

static void
bw_check_mailbox_done(bw_pop_mbox_t *bw_pop_mbox)
{
	if (bw_pop_mbox->thread != NULL) {
		g_thread_join(bw_pop_mbox->thread);
		g_debug("joined thread %p", bw_pop_mbox->thread);
	}
	if (bw_pop_mbox->notify > 0U) {
		g_signal_handler_disconnect(bw_pop_mbox->mailbox, bw_pop_mbox->notify);
	}
	g_object_unref(bw_pop_mbox->mailbox);
}

static void
bw_check_mailbox_list(struct check_messages_thread_info *info, GList *mailbox_list)
{
    GList *check_mbx = NULL;

    if (info->window != NULL) {
        BalsaWindowPrivate *priv = balsa_window_get_instance_private(info->window);

        if (!priv->network_available)
            return;
    }

    for ( ; mailbox_list; mailbox_list = mailbox_list->next) {
        LibBalsaMailbox *mailbox = balsa_mailbox_node_get_mailbox(mailbox_list->data);
        LibBalsaMailboxPOP3 *pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
        bw_pop_mbox_t *bw_pop_mbox;

        bw_pop_mbox = g_malloc0(sizeof(bw_pop_mbox_t));
        bw_pop_mbox->mailbox = g_object_ref(mailbox);
        libbalsa_mailbox_pop3_set_inbox(mailbox, balsa_app.inbox);
        libbalsa_mailbox_pop3_set_msg_size_limit(pop3, balsa_app.msg_size_limit * 1024);
        if (info->with_progress_dialog) {
        	bw_pop_mbox->notify =
        		g_signal_connect(mailbox, "progress-notify", G_CALLBACK(bw_check_mailbox_progress_cb), mailbox);
        }
        bw_pop_mbox->thread = g_thread_new(NULL, (GThreadFunc) bw_check_mailbox, mailbox);
        g_debug("launched thread %p for checking POP3 mailbox %s", bw_pop_mbox->thread, libbalsa_mailbox_get_name(mailbox));
        check_mbx = g_list_prepend(check_mbx, bw_pop_mbox);
    }

    /* join all threads, i.e. proceed only after all threads have finished, and disconnect progress notify handlers */
    g_list_foreach(check_mbx, (GFunc) bw_check_mailbox_done, NULL);
    g_debug("all POP3 mailbox threads done");
    g_list_free_full(check_mbx, (GDestroyNotify) g_free);
}

/*Callback to check a mailbox in a balsa-mblist */
static gboolean
bw_add_mbox_to_checklist(GtkTreeModel * model, GtkTreePath * path,
                         GtkTreeIter * iter, GSList ** list)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    g_return_val_if_fail(mbnode, FALSE);

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode))) {	/* mailbox, not a folder */
	if (!LIBBALSA_IS_MAILBOX_IMAP(mailbox) ||
	    bw_imap_check_test(balsa_mailbox_node_get_dir(mbnode) ? balsa_mailbox_node_get_dir(mbnode) :
			    libbalsa_mailbox_imap_get_path
			    (LIBBALSA_MAILBOX_IMAP(mailbox)))) {
            *list = g_slist_prepend(*list, g_object_ref(mailbox));
        }
    }

    g_object_unref(mbnode);

    return FALSE;
}

/*
 * Callback for testing whether to check an IMAP mailbox
 */
static gboolean
bw_imap_check_test(const gchar * path)
{
    /* path has been parsed, so it's just the folder path */
    return balsa_app.check_imap && balsa_app.check_imap_inbox ?
        strcmp(path, "INBOX") == 0 : balsa_app.check_imap;
}


/*
 * Callbacks
 */

/* bw_check_new_messages:
   check new messages the data argument is the BalsaWindow pointer
   or NULL.
*/
void
check_new_messages_real(BalsaWindow * window, gboolean background_check)
{
    GSList *list;
    struct check_messages_thread_info *info;
    GThread *get_mail_thread;

    if (window && !BALSA_IS_WINDOW(window))
        return;

    if (balsa_app.mblist_tree_store == NULL)
        /* Quitt'n time! */
        return;

    /*  Only Run once -- If already checking mail, return.  */
    if (!g_atomic_int_dec_and_test(&checking_mail)) {
    	g_atomic_int_inc(&checking_mail);
        g_debug("Already Checking Mail!");
        g_mutex_lock(&progress_dialog.mutex);
        if (progress_dialog.dialog != NULL) {
        	gtk_window_present_with_time(GTK_WINDOW(progress_dialog.dialog),
                                             gtk_get_current_event_time());
        }
        g_mutex_unlock(&progress_dialog.mutex);
        return;
    }

    if (window)
        bw_action_set_enabled(window, "get-new-mail", FALSE);

    list = NULL;
    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) bw_add_mbox_to_checklist,
			   &list);

    /* initiate thread */
    info = g_new(struct check_messages_thread_info, 1);
    info->list = list;
    info->with_progress_dialog = !background_check && balsa_app.recv_progress_dialog;
    info->window = window ? g_object_ref(window) : window;
    info->with_activity_bar = background_check && !balsa_app.quiet_background_check && (info->window != NULL);
	if (info->with_activity_bar) {
		balsa_window_increase_activity(info->window, _("Checking Mail…"));
	}

    get_mail_thread =
    	g_thread_new("bw_check_messages_thread",
    				 (GThreadFunc) bw_check_messages_thread,
					 info);

    /* Detach so we don't need to g_thread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    g_thread_unref(get_mail_thread);
}

static void
bw_check_new_messages(gpointer data)
{
    check_new_messages_real(data, FALSE);

    if (balsa_app.check_mail_auto) {
        /* restart the timer */
        update_timer(TRUE, balsa_app.check_mail_timer);
    }
}

/** Saves the number of messages as the most recent one the user is
    aware of. If the number has changed since last checking and
    notification was requested, do notify the user as well.  */
void
check_new_messages_count(LibBalsaMailbox * mailbox, gboolean notify)
{
    struct count_info {
        gint unread_messages;
        gint has_unread_messages;
    } *info;
    static const gchar count_info_key[] = "balsa-window-count-info";

    info = g_object_get_data(G_OBJECT(mailbox), count_info_key);
    if (!info) {
        info = g_new0(struct count_info, 1);
        g_object_set_data_full(G_OBJECT(mailbox), count_info_key, info,
                               g_free);
    }

    if (notify) {
        gint num_new, has_new;

        num_new = libbalsa_mailbox_get_unread_messages(mailbox) - info->unread_messages;
        if (num_new < 0)
            num_new = 0;
        has_new = libbalsa_mailbox_get_has_unread_messages(mailbox) - info->has_unread_messages;
        if (has_new < 0)
            has_new = 0;

        if (num_new != 0 || has_new != 0)
	    bw_display_new_mail_notification(num_new, has_new);
    }

    info->unread_messages = libbalsa_mailbox_get_unread_messages(mailbox);
    info->has_unread_messages = libbalsa_mailbox_get_has_unread_messages(mailbox);
}

/* this one is called only in the threaded code */
static void
bw_mailbox_check(LibBalsaMailbox * mailbox, struct check_messages_thread_info *info)
{
    if (balsa_app.main_window == NULL)
        return;
    if (libbalsa_mailbox_get_subscribe(mailbox) == LB_MAILBOX_SUBSCRIBE_NO)
        return;

    g_debug("checking mailbox %s", libbalsa_mailbox_get_name(mailbox));
    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
    	if (info->window != NULL) {
            BalsaWindowPrivate *priv =
                balsa_window_get_instance_private(info->window);

            if (!priv->network_available)
                return;
        }

    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), FALSE, INFINITY,
    			_("IMAP mailbox: %s"), libbalsa_mailbox_get_url(mailbox));
    	}
    } else if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), FALSE, INFINITY,
    			_("Local mailbox: %s"), libbalsa_mailbox_get_name(mailbox));
    	}
    } else {
    	g_assert_not_reached();
    }

    libbalsa_mailbox_check(mailbox);
}

static gboolean
bw_check_messages_thread_idle_cb(BalsaWindow * window)
{
    bw_action_set_enabled(window, "get-new-mail", TRUE);
    g_object_unref(window);

    return FALSE;
}

static void
bw_check_messages_thread(struct check_messages_thread_info *info)
{
    /*
     *  It is assumed that this will always be called as a GThread,
     *  and that the calling procedure will check for an existing lock
     *  and set checking_mail to true before calling.
     */
    bw_check_mailbox_list(info, balsa_app.inbox_input);

    if (info->list!= NULL) {
        GSList *list = info->list;

    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_ensure(&progress_dialog, _("Checking Mail…"), GTK_WINDOW(info->window), _("Mailboxes"));
    	}
    	g_slist_foreach(list, (GFunc) bw_mailbox_check, info);
    	g_slist_free_full(list, g_object_unref);
    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), TRUE, 1.0, NULL);
    	}
    }

	if (info->with_activity_bar) {
		balsa_window_decrease_activity(info->window, _("Checking Mail…"));
	}

    g_atomic_int_inc(&checking_mail);

    if (info->window != NULL) {
        BalsaWindowPrivate *priv = balsa_window_get_instance_private(info->window);

        g_idle_add((GSourceFunc) bw_check_messages_thread_idle_cb,
                   g_object_ref(info->window));
        if (priv->network_available)
            time(&priv->last_check_time);
        g_object_unref(info->window);
    }

    g_free(info);
    g_thread_exit(0);
}


/* note: see https://gitlab.gnome.org/GNOME/balsa/-/issues/39 for the reason of the implementation */
typedef struct {
	GNotification *notification;
	GApplication *application;
	guint timeout_id;
	gint num_new;
	gint num_total;
} notify_ctx_t;


static gboolean
bw_display_new_mail_notification_real(notify_ctx_t *ctx)
{
	gchar *msg;

	if (ctx->num_new > 0) {
		msg = g_strdup_printf(ngettext("You have received %d new message.",
			 	 	 	 	 	 	   "You have received %d new messages.",
									   ctx-> num_total),
			ctx->num_total);
	} else {
		msg = g_strdup(_("You have new mail."));
	}
    g_notification_set_body(ctx->notification, msg);
    g_free(msg);
    g_application_send_notification(ctx->application, NEW_MAIL_NOTIFICATION, ctx->notification);
    ctx->timeout_id = 0U;
	return FALSE;
}


/** Informs the user that new mail arrived. num_new is the number of
    the recently arrived messsages.
*/
static void
bw_display_new_mail_notification(int num_new, int has_new)
{
	static notify_ctx_t notify_ctx;
    GtkWindow *window = GTK_WINDOW(balsa_app.main_window);
    BalsaWindowPrivate *priv =
        balsa_window_get_instance_private(balsa_app.main_window);
#ifdef HAVE_CANBERRA
    static gint64 last_new_mail_sound = -1;
    gint64 now;
#endif

    /* remove a running notification timeout task */
    if (notify_ctx.timeout_id > 0U) {
    	g_source_remove(notify_ctx.timeout_id);
    	notify_ctx.timeout_id = 0U;
    }

    if (gtk_window_is_active(window))
        return;

#ifdef HAVE_CANBERRA
    /* play sound if configured, but not too frequently (min. 30 seconds in between)*/
    now = g_get_monotonic_time();
    if ((balsa_app.notify_new_mail_sound != 0) && (now > (last_new_mail_sound + 30 * 1000000))) {
    	GError *error = NULL;

        if (!libbalsa_play_sound_event("message-new-email", &error)) {
    		g_warning("%s: %s", __func__, error->message);
    		g_clear_error(&error);
    	} else {
    		last_new_mail_sound = now;
    	}
    }
#endif

#ifdef ENABLE_SYSTRAY
    if (balsa_app.enable_systray_icon) {
    	libbalsa_systray_icon_attention(TRUE);
    }
#endif

    if (balsa_app.notify_new_mail_dialog == 0)
        return;

    gtk_window_set_urgency_hint(window, TRUE);

    if (g_once_init_enter(&notify_ctx.notification)) {
    	gchar *balsa_icon;
        GNotification *tmp;
        GIcon *icon = NULL;

        tmp = g_notification_new("Balsa");
        balsa_icon = libbalsa_pixmap_finder("balsa_icon.png");
        if (balsa_icon != NULL) {
        	GFile * icon_file;

        	icon_file = g_file_new_for_path(balsa_icon);
        	g_free(balsa_icon);
        	icon = g_file_icon_new(icon_file);
        	g_object_unref(icon_file);
        }

        if (icon == NULL) {
        	icon = g_themed_icon_new("dialog-information");
        }

        g_notification_set_icon(tmp, icon);
        g_object_unref(icon);
        notify_ctx.application = G_APPLICATION(gtk_window_get_application(window));
        g_once_init_leave(&notify_ctx.notification, tmp);
    }

    if (priv->new_mail_notification_sent) {
        /* the user didn't acknowledge the last info, so we'll
         * accumulate the count */
    	notify_ctx.num_total += num_new;
    } else {
    	notify_ctx.num_total = num_new;
        priv->new_mail_notification_sent = TRUE;
    }
    notify_ctx.num_new = num_new;

    notify_ctx.timeout_id = g_timeout_add(500, (GSourceFunc) bw_display_new_mail_notification_real, &notify_ctx);
}

/*Callback to create or disconnect an IMAP mbox. */

static void
mw_mbox_can_reach_cb(GObject * object,
                     gboolean  can_reach,
                     gpointer  user_data)
{
    LibBalsaMailboxImap *mailbox = (LibBalsaMailboxImap *) object;

    if (can_reach) {
        libbalsa_mailbox_imap_reconnect(mailbox);
    } else {
        libbalsa_mailbox_imap_force_disconnect(mailbox);
    }

    g_object_unref(mailbox);
}

static gboolean
mw_mbox_change_connection_status(GtkTreeModel * model, GtkTreePath * path,
                                 GtkTreeIter * iter, gpointer arg)
{
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    g_return_val_if_fail(mbnode, FALSE);

    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode))) {  /* mailbox, not a folder */
        if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) &&
            bw_imap_check_test(balsa_mailbox_node_get_dir(mbnode) ? balsa_mailbox_node_get_dir(mbnode) :
                               libbalsa_mailbox_imap_get_path(LIBBALSA_MAILBOX_IMAP(mailbox)))) {
            libbalsa_mailbox_test_can_reach(g_object_ref(mailbox),
                                            mw_mbox_can_reach_cb, NULL);
        }
    }

    g_object_unref(mbnode);

    return FALSE;
}

static void
bw_change_connection_status_can_reach_cb(GObject * object,
                                         gboolean  can_reach,
                                         gpointer  user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (can_reach &&
        difftime(time(NULL), priv->last_check_time) >
        balsa_app.check_mail_timer * 60) {
        /* Check the mail now, and reset the timer */
        bw_check_new_messages(window);
    }

    g_object_unref(window);
}

static gboolean
bw_change_connection_status_idle(gpointer user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    priv->network_changed_source_id = 0;

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                           (GtkTreeModelForeachFunc)
                           mw_mbox_change_connection_status, NULL);

    if (!priv->network_available)
        return FALSE;

    /* GLib timeouts are now triggered by g_get_monotonic_time(),
     * which doesn't increment while we're suspended, so we must
     * check for ourselves whether a scheduled mail check was
     * missed. */
    /* Test whether the first POP3 mailbox can be reached */
    if (balsa_app.inbox_input == NULL)
        return FALSE;
    if ((mbnode = balsa_app.inbox_input->data) == NULL)
        return FALSE;
    if ((mailbox = balsa_mailbox_node_get_mailbox(mbnode)) == NULL)
        return FALSE;

    libbalsa_mailbox_test_can_reach(mailbox, bw_change_connection_status_can_reach_cb,
                                    g_object_ref(window));

    return FALSE;
}

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    g_return_val_if_fail(window != NULL, NULL);

    return priv->current_index;
}

static GtkToggleButton*
bw_add_check_button(GtkWidget* grid, const gchar* label, gint x, gint y)
{
    GtkWidget* res = gtk_check_button_new_with_mnemonic(label);
    gtk_widget_set_hexpand(res, TRUE);
    gtk_grid_attach(GTK_GRID(grid), res, x, y, 1, 1);
    return GTK_TOGGLE_BUTTON(res);
}

enum {
    FIND_RESPONSE_FILTER,
    FIND_RESPONSE_RESET
};

static void
bw_find_button_clicked(GtkWidget * widget, gpointer data)
{
    GtkWidget *dialog = gtk_widget_get_toplevel(widget);
    gtk_dialog_response(GTK_DIALOG(dialog), GPOINTER_TO_INT(data));
}

static void
bw_find_real(BalsaWindow * window, BalsaIndex * bindex, gboolean again)
{
    /* Condition set up for the search, it will be of type
       CONDITION_NONE if nothing has been set up */
    static LibBalsaCondition * cnd = NULL;
    static gboolean reverse = FALSE;
    static gboolean wrap    = FALSE;
    static LibBalsaMailboxSearchIter *search_iter = NULL;

    if (!cnd) {
	cnd = libbalsa_condition_new();
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_FROM);
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_SUBJECT);
    }


    /* first search, so set up the match rule(s) */
    if (!again || cnd->type==CONDITION_NONE) {
	GtkWidget* vbox, *dia =
            gtk_dialog_new_with_buttons(_("Search mailbox"),
                                        GTK_WINDOW(window),
                                        GTK_DIALOG_DESTROY_WITH_PARENT |
                                        libbalsa_dialog_flags(),
					_("_Help"),   GTK_RESPONSE_HELP,
                                        _("_Close"), GTK_RESPONSE_CLOSE,
                                        NULL);
	GtkWidget *reverse_button, *wrap_button;
	GtkWidget *search_entry, *w, *page, *grid;
	GtkWidget *frame, *box, *button;
	GtkToggleButton *matching_body, *matching_from;
        GtkToggleButton *matching_to, *matching_cc, *matching_subject;
	gint ok;

        vbox = gtk_dialog_get_content_area(GTK_DIALOG(dia));

	page=gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(page), 2);
        gtk_grid_set_column_spacing(GTK_GRID(page), 2);
	gtk_container_set_border_width(GTK_CONTAINER(page), HIG_PADDING);
	w = gtk_label_new_with_mnemonic(_("_Search for:"));
        gtk_widget_set_hexpand(w, TRUE);
	gtk_grid_attach(GTK_GRID(page), w, 0, 0, 1, 1);
	search_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(search_entry), 30);
        gtk_widget_set_hexpand(search_entry, TRUE);
	gtk_grid_attach(GTK_GRID(page),search_entry,1, 0, 1, 1);
	gtk_label_set_mnemonic_widget(GTK_LABEL(w), search_entry);

        libbalsa_set_vmargins(page, 2);
	gtk_container_add(GTK_CONTAINER(vbox), page);

	/* builds the toggle buttons to specify fields concerned by
         * the search. */

	frame = gtk_frame_new(_("In:"));
	gtk_frame_set_label_align(GTK_FRAME(frame),
				  GTK_POS_LEFT, GTK_POS_TOP);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), HIG_PADDING);

        libbalsa_set_vmargins(frame, 2);
	gtk_container_add(GTK_CONTAINER(vbox), frame);

	grid = gtk_grid_new();
        gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	matching_body    = bw_add_check_button(grid, _("_Body"),    0, 0);
	matching_to      = bw_add_check_button(grid, _("_To:"),     1, 0);
	matching_from    = bw_add_check_button(grid, _("_From:"),   1, 1);
        matching_subject = bw_add_check_button(grid, _("S_ubject"), 2, 0);
	matching_cc      = bw_add_check_button(grid, _("_CC:"),     2, 1);
	gtk_container_add(GTK_CONTAINER(frame), grid);

	/* Frame with Apply and Clear buttons */
	frame = gtk_frame_new(_("Show only matching messages"));
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), HIG_PADDING);

        libbalsa_set_vmargins(frame, 2);
	gtk_container_add(GTK_CONTAINER(vbox), frame);

	/* Button box */
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_set_border_width(GTK_CONTAINER(box), HIG_PADDING);
        button = libbalsa_add_mnemonic_button_to_box(_("_Apply"), box, GTK_ALIGN_START);
	g_signal_connect(button, "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(FIND_RESPONSE_FILTER));
        button = libbalsa_add_mnemonic_button_to_box(_("_Clear"), box, GTK_ALIGN_END);
	g_signal_connect(button, "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(FIND_RESPONSE_RESET));
	gtk_container_add(GTK_CONTAINER(frame), box);

	/* Frame with OK button */
	frame = gtk_frame_new(_("Open next matching message"));
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), HIG_PADDING);

        libbalsa_set_vmargins(frame, 2);
	gtk_container_add(GTK_CONTAINER(vbox), frame);

	/* Reverse and Wrap checkboxes */
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, HIG_PADDING);
	gtk_container_set_border_width(GTK_CONTAINER(box), HIG_PADDING);
	gtk_container_add(GTK_CONTAINER(frame), box);

	w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_set_homogeneous(GTK_BOX(w), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(w), HIG_PADDING);

	reverse_button =
            gtk_check_button_new_with_mnemonic(_("_Reverse search"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_button),
                                     reverse);

        gtk_widget_set_vexpand(reverse_button, TRUE);
        gtk_widget_set_valign(reverse_button, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER(w), reverse_button);

	wrap_button =
            gtk_check_button_new_with_mnemonic(_("_Wrap around"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_button),
                                     wrap);

        gtk_widget_set_vexpand(wrap_button, TRUE);
        gtk_widget_set_valign(wrap_button, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER(w), wrap_button);

        gtk_widget_set_hexpand(w, TRUE);
        gtk_widget_set_halign(w, GTK_ALIGN_FILL);
	gtk_container_add(GTK_CONTAINER(box), w);

	button = gtk_button_new_with_mnemonic(_("_OK"));
	g_signal_connect(button, "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(GTK_RESPONSE_OK));

        gtk_widget_set_hexpand(button, TRUE);
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(box), button);

	gtk_widget_show_all(vbox);

	if (cnd->match.string.string)
	    gtk_entry_set_text(GTK_ENTRY(search_entry),
                               cnd->match.string.string);
	gtk_toggle_button_set_active(matching_body,
				     CONDITION_CHKMATCH(cnd,
							CONDITION_MATCH_BODY));
	gtk_toggle_button_set_active(matching_to,
				     CONDITION_CHKMATCH(cnd,
                                                        CONDITION_MATCH_TO));
	gtk_toggle_button_set_active(matching_from,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_FROM));
	gtk_toggle_button_set_active(matching_subject,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_SUBJECT));
	gtk_toggle_button_set_active(matching_cc,
				     CONDITION_CHKMATCH(cnd,CONDITION_MATCH_CC));

        gtk_widget_grab_focus(search_entry);
	gtk_entry_set_activates_default(GTK_ENTRY(search_entry), TRUE);
        gtk_dialog_set_default_response(GTK_DIALOG(dia), GTK_RESPONSE_OK);
	do {
	    GError *err = NULL;

	    ok=gtk_dialog_run(GTK_DIALOG(dia));
            switch(ok) {
            case GTK_RESPONSE_OK:
            case FIND_RESPONSE_FILTER:
                reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                       (reverse_button));
                wrap    = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON
                                                       (wrap_button));
		g_free(cnd->match.string.string);
		cnd->match.string.string =
                    g_strdup(gtk_entry_get_text(GTK_ENTRY(search_entry)));
		cnd->match.string.fields=CONDITION_EMPTY;

		if (gtk_toggle_button_get_active(matching_body))
		    CONDITION_SETMATCH(cnd,CONDITION_MATCH_BODY);
		if (gtk_toggle_button_get_active(matching_to))
		    CONDITION_SETMATCH(cnd,CONDITION_MATCH_TO);
		if (gtk_toggle_button_get_active(matching_subject))
		    CONDITION_SETMATCH(cnd,CONDITION_MATCH_SUBJECT);
		if (gtk_toggle_button_get_active(matching_from))
		    CONDITION_SETMATCH(cnd,CONDITION_MATCH_FROM);
		if (gtk_toggle_button_get_active(matching_cc))
		    CONDITION_SETMATCH(cnd,CONDITION_MATCH_CC);
		if (!(cnd->match.string.fields!=CONDITION_EMPTY &&
                    cnd->match.string.string[0]))

		    /* FIXME : We should print error messages, but for
		     * that we should first make find dialog non-modal
		     * balsa_information(LIBBALSA_INFORMATION_ERROR,_("You
		     * must specify at least one field to look in"));
		     * *balsa_information(LIBBALSA_INFORMATION_ERROR,_("You
		     * must provide a non-empty string")); */
                    ok = GTK_RESPONSE_CANCEL;
                break;
	    case GTK_RESPONSE_HELP:
                gtk_show_uri_on_window(GTK_WINDOW(window),
                                       "help:balsa/win-search",
                                       gtk_get_current_event_time(), &err);
		if (err) {
		    balsa_information(LIBBALSA_INFORMATION_WARNING,
				      _("Error displaying help: %s\n"),
				      err->message);
		    g_error_free(err);
		}
		break;
            case FIND_RESPONSE_RESET:
		bw_reset_filter(window);
		/* fall through */
            default:
		ok = GTK_RESPONSE_CANCEL;
		break;/* cancel or just close */
            } /* end of switch */
	} while (ok==GTK_RESPONSE_HELP);
	gtk_widget_destroy(dia);
	if (ok == GTK_RESPONSE_CANCEL)
	    return;
	cnd->type = CONDITION_STRING;

	libbalsa_mailbox_search_iter_unref(search_iter);
	search_iter = NULL;

        if(ok == FIND_RESPONSE_FILTER) {
            LibBalsaMailbox *mailbox =
                balsa_index_get_mailbox(BALSA_INDEX(bindex));
            LibBalsaCondition *filter, *res;
            filter = bw_get_view_filter(window);
            res = libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                                  filter, cnd);
            libbalsa_condition_unref(filter);
            libbalsa_condition_unref(cnd);
            cnd = NULL;

            if (libbalsa_mailbox_set_view_filter(mailbox, res, TRUE))
                balsa_index_ensure_visible(BALSA_INDEX(bindex));
            libbalsa_condition_unref(res);
            return;
        }
    }

    if (!search_iter)
	search_iter = libbalsa_mailbox_search_iter_new(cnd);
    balsa_index_find(bindex, search_iter, reverse, wrap);
}

static void
bw_mailbox_tab_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget * window = gtk_widget_get_toplevel(widget);
    balsa_window_real_close_mbnode(BALSA_WINDOW(window),
				   BALSA_MAILBOX_NODE(data));
}

static LibBalsaCondition*
bw_get_view_filter(BalsaWindow *window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    LibBalsaCondition *filter, *flag_filter;
    gint i;

    flag_filter = bw_get_condition_from_int(bw_filter_to_int(window));

    /* add string filter on top of that */

    i = gtk_combo_box_get_active(GTK_COMBO_BOX(priv->filter_choice));
    if (i >= 0) {
        const gchar *str = gtk_entry_get_text(GTK_ENTRY(priv->sos_entry));
        g_assert(((guint) i) < G_N_ELEMENTS(view_filters));
        filter = view_filters[i].filter(str);
    } else filter = NULL;
    /* and merge ... */
    if(flag_filter) {
        LibBalsaCondition *res;
        res = libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                              flag_filter, filter);
        libbalsa_condition_unref(flag_filter);
        libbalsa_condition_unref(filter);
        filter = res;
    }

    return filter;
}

static void
bw_hide_changed_set_view_filter(BalsaWindow * window)
{
    GtkWidget *index;
    LibBalsaMailbox *mailbox;
    gint mask;
    LibBalsaCondition *filter;

    index = balsa_window_find_current_index(window);
    if(!index)
        return;

    mailbox = balsa_index_get_mailbox(BALSA_INDEX(index));
    /* Store the new filter mask in the mailbox view before we set the
     * view filter; rethreading triggers bw_set_filter_menu,
     * which retrieves the mask from the mailbox view, and we want it to
     * be the new one. */
    mask = bw_filter_to_int(window);
    libbalsa_mailbox_set_filter(mailbox, mask);

    /* Set the flags part of this filter as persistent: */
    filter = bw_get_condition_from_int(mask);
    libbalsa_mailbox_set_view_filter(mailbox, filter, FALSE);
    libbalsa_condition_unref(filter);
    libbalsa_mailbox_make_view_filter_persistent(mailbox);

    filter = bw_get_view_filter(window);
    /* libbalsa_mailbox_set_view_filter() will ref the
     * filter.  We need also to rethread to take into account that
     * some messages might have been removed or added to the view. */
    if (libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE))
        balsa_index_ensure_visible(BALSA_INDEX(index));
    libbalsa_condition_unref(filter);
}

static void
bw_reset_filter(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaIndex *bindex = BALSA_INDEX(balsa_window_find_current_index(window));

    /* do it by resetting the sos filder */
    gtk_entry_set_text(GTK_ENTRY(priv->sos_entry), "");
    bw_set_view_filter(window, balsa_index_get_filter_no(bindex), priv->sos_entry);
}

/* empty_trash:
   empty the trash mailbox.
*/
void
empty_trash(BalsaWindow * window)
{
    guint msgno, total;
    GArray *messages;
    GError *err = NULL;

    g_object_ref(balsa_app.trash);
    if(!libbalsa_mailbox_open(balsa_app.trash, &err)) {
        if (window)
            balsa_information_parented(GTK_WINDOW(window),
                                       LIBBALSA_INFORMATION_WARNING,
                                       _("Could not open trash: %s"),
                                       err ?
                                       err->message : _("Unknown error"));

	g_clear_error(&err);
        g_object_unref(balsa_app.trash);
	return;
    }

    messages = g_array_new(FALSE, FALSE, sizeof(guint));
    total = libbalsa_mailbox_total_messages(balsa_app.trash);
    for (msgno = 1; msgno <= total; msgno++)
        g_array_append_val(messages, msgno);
    libbalsa_mailbox_messages_change_flags(balsa_app.trash, messages,
                                           LIBBALSA_MESSAGE_FLAG_DELETED,
                                           0);
    g_array_free(messages, TRUE);

    /* We want to expunge deleted messages: */
    libbalsa_mailbox_close(balsa_app.trash, TRUE);
    g_object_unref(balsa_app.trash);
    if (window)
        enable_empty_trash(window, TRASH_EMPTY);
}

static void
bw_show_mbtree(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_app.show_mblist) {
        gtk_widget_show(priv->mblist);
        gtk_paned_set_position(GTK_PANED(priv->mblist_parent), balsa_app.mblist_width);
    } else {
        gtk_widget_hide(priv->mblist);
        gtk_paned_set_position(GTK_PANED(priv->mblist_parent), 0);
    }
}

void
balsa_change_window_layout(BalsaWindow *window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    g_object_ref(priv->notebook);
    g_object_ref(priv->mblist);
    g_object_ref(priv->preview);

    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(priv->notebook)),
                         priv->notebook);
    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(priv->mblist)),
                         priv->mblist);
    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(priv->preview)),
                         priv->preview);

    bw_set_panes(window);

    g_object_unref(priv->notebook);
    g_object_unref(priv->mblist);
    g_object_unref(priv->preview);

    gtk_paned_set_position(GTK_PANED(priv->mblist_parent),
                           balsa_app.show_mblist ?
                           balsa_app.mblist_width : 0);

    if (priv->notebook_parent != NULL) {
        if (balsa_app.previewpane) {
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent),
                                   balsa_app.notebook_height);
        } else {
            /* Set it to something really high */
            gtk_paned_set_position(GTK_PANED(priv->notebook_parent), G_MAXINT);
        }
    }
}

/* When page is switched we change the preview window and the selected
   mailbox in the mailbox tree.
 */
static void
bw_notebook_switch_page_cb(GtkWidget * notebook,
                        void * notebookpage, guint page_num,
                        gpointer data)
{
    BalsaWindow *window = BALSA_WINDOW(data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GtkWidget *page;
    BalsaIndex *index;
    LibBalsaMailbox *mailbox;
    gchar *title;
    const gchar *filter_string;

    if (priv->current_index) {
	g_object_remove_weak_pointer(G_OBJECT(priv->current_index),
				     (gpointer *) &priv->current_index);
	/* Note when this mailbox was hidden, for use in auto-closing. */
        balsa_index_set_last_use_time(BALSA_INDEX(priv->current_index));
        priv->current_index = NULL;
    }

    if (!balsa_app.mblist_tree_store)
        /* Quitt'n time! */
        return;

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page_num);
    index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));

    priv->current_index = GTK_WIDGET(index);
    g_object_add_weak_pointer(G_OBJECT(index),
			      (gpointer *) &priv->current_index);
    /* Note when this mailbox was exposed, for use in auto-expunge. */
    balsa_index_set_last_use_time(index);

    mailbox = balsa_index_get_mailbox(index);
    if (libbalsa_mailbox_get_name(mailbox)) {
        if (libbalsa_mailbox_get_readonly(mailbox)) {
            title =
                g_strdup_printf(_("Balsa: %s (read-only)"), libbalsa_mailbox_get_name(mailbox));
        } else {
            title = g_strdup_printf(_("Balsa: %s"), libbalsa_mailbox_get_name(mailbox));
        }
        gtk_window_set_title(GTK_WINDOW(window), title);
        g_free(title);
    } else {
        gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    }

    g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, index);
    bw_idle_replace(window, index);
    bw_enable_message_menus(window, balsa_index_get_current_msgno(index));
    bw_enable_mailbox_menus(window, index);

    filter_string = balsa_index_get_filter_string(index);
    gtk_entry_set_text(GTK_ENTRY(priv->sos_entry),
                       filter_string != NULL ? filter_string : "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(priv->filter_choice),
                             balsa_index_get_filter_no(index));

    balsa_mblist_focus_mailbox(balsa_app.mblist, mailbox);
    balsa_window_set_statusbar(window, mailbox);

    balsa_index_refresh_date(index);
    balsa_index_refresh_size(index);
    balsa_index_ensure_visible(index);

    g_free(balsa_app.current_mailbox_url);
    balsa_app.current_mailbox_url = g_strdup(libbalsa_mailbox_get_url(mailbox));
}

static void
bw_index_changed_cb(GtkWidget * widget, gpointer user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaIndex *index;
    LibBalsaMessage *message;
    guint current_msgno;

    if (priv->preview == NULL)
        return;

    if (widget != priv->current_index)
        return;

    index = BALSA_INDEX(widget);
    bw_enable_message_menus(window, balsa_index_get_current_msgno(index));
    bw_enable_mailbox_menus(window, index);

    message = balsa_message_get_message(BALSA_MESSAGE(priv->preview));
    current_msgno = message != NULL ? libbalsa_message_get_msgno(message) : 0;

    if (current_msgno != balsa_index_get_current_msgno(index))
        bw_idle_replace(window, index);
}

static void
bw_idle_replace(BalsaWindow * window, BalsaIndex * bindex)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (priv->set_message_id != 0) {
        g_source_remove(priv->set_message_id);
        priv->set_message_id = 0;
    }

    if (balsa_app.previewpane) {
        /* Skip if the window is being destroyed: */
        if (priv->preview != NULL) {
            priv->set_message_id = g_idle_add((GSourceFunc) bw_idle_cb, window);
            if (balsa_message_get_message(BALSA_MESSAGE(priv->preview)) != NULL)
                gtk_widget_hide(priv->preview);
        }
    }
}

static gboolean
bw_idle_cb(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaIndex *index;

    priv->set_message_id = 0;

    index = (BalsaIndex *) priv->current_index;
    if (index)
        balsa_message_set(BALSA_MESSAGE(priv->preview),
                          balsa_index_get_mailbox(index),
                          balsa_index_get_current_msgno(index));
    else
        balsa_message_set(BALSA_MESSAGE(priv->preview), NULL, 0);

    index = g_object_get_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS);
    if (index) {
        gtk_widget_grab_focus(GTK_WIDGET(index));
        g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, NULL);
    }

    return FALSE;
}

static void
bw_select_part_cb(BalsaMessage * bm, gpointer data)
{
    bw_enable_edit_menus(BALSA_WINDOW(data), bm);
    bw_enable_part_menu_items(BALSA_WINDOW(data));
}

static void
bw_send_msg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    if (balsa_app.main_window)
        balsa_window_enable_continue(BALSA_WINDOW(data));
}


/* notebook_find_page
 *
 * Description: Finds the page from which notebook page tab the
 * coordinates are over.
 **/
static BalsaIndex*
bw_notebook_find_page (GtkNotebook* notebook, gint x, gint y)
{
    GtkWidget* page;
    GtkWidget* label;
    gint page_num = 0;
    gint label_x;
    gint label_y;
    gint label_width;
    gint label_height;
    GtkAllocation allocation;

    /* x and y are relative to the notebook, but the label allocations
     * are relative to the main window. */
    gtk_widget_get_allocation(GTK_WIDGET(notebook), &allocation);
    x += allocation.x;
    y += allocation.y;

    while ((page = gtk_notebook_get_nth_page (notebook, page_num)) != NULL) {
        label = gtk_notebook_get_tab_label (notebook, page);

        gtk_widget_get_allocation(label, &allocation);
        label_x     = allocation.x;
        label_width = allocation.width;

        if (x > label_x && x < label_x + label_width) {
            label_y      = allocation.y;
            label_height = allocation.height;

            if (y > label_y && y < label_y + label_height) {
                return BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));
            }
        }
        ++page_num;
    }

    return NULL;
}


/* bw_notebook_drag_received_cb
 *
 * Description: Signal handler for the drag-data-received signal from
 * the GtkNotebook widget.  Finds the tab the messages were dragged
 * over, then transfers them.
 **/
static void
bw_notebook_drag_received_cb(GtkWidget * widget, GdkDragContext * context,
                             gint x, gint y,
                             GtkSelectionData * selection_data, guint info,
                             guint time, gpointer data)
{
	gboolean dnd_ok = FALSE;

	if ((selection_data != NULL) && (gtk_selection_data_get_data(selection_data) != NULL)) {
		BalsaIndex *orig_index;
		GArray *selected;

		orig_index = *(BalsaIndex **) gtk_selection_data_get_data(selection_data);
		selected = balsa_index_selected_msgnos_new(orig_index);
		if (selected->len > 0U) {
			LibBalsaMailbox* orig_mailbox;
			BalsaIndex* index;

			orig_mailbox = balsa_index_get_mailbox(orig_index);
			index = bw_notebook_find_page(GTK_NOTEBOOK(widget), x, y);
			if (index != NULL) {
				LibBalsaMailbox* mailbox;

				mailbox = balsa_index_get_mailbox(index);

				if ((mailbox != NULL) && (mailbox != orig_mailbox)) {
					balsa_index_transfer(orig_index, selected, mailbox,
							gdk_drag_context_get_selected_action(context) != GDK_ACTION_MOVE);
					dnd_ok = TRUE;
				}
			}
		}
		balsa_index_selected_msgnos_free(orig_index, selected);
	}
	gtk_drag_finish(context, dnd_ok, FALSE, time);
}

static gboolean bw_notebook_drag_motion_cb(GtkWidget * widget,
                                           GdkDragContext * context,
                                           gint x, gint y, guint time,
                                           gpointer user_data)
{
    gdk_drag_status(context,
                    (gdk_drag_context_get_actions(context) ==
                     GDK_ACTION_COPY) ? GDK_ACTION_COPY :
                    GDK_ACTION_MOVE, time);

    return FALSE;
}

static void
bw_notebook_page_notify_cb(GtkWidget  *widget,
                           GParamSpec *child_property,
                           gpointer    notebook)
{
    GtkWidget *child;

    if (balsa_app.in_destruction)
        return;

    child = gtk_bin_get_child(GTK_BIN(widget));

    if (child != NULL) {
        LibBalsaMailbox *mailbox;
        gint page_num;

        mailbox = balsa_index_get_mailbox(BALSA_INDEX(child));
        page_num = gtk_notebook_page_num(notebook, widget);
        libbalsa_mailbox_set_position(mailbox, page_num);
    }
}

/* bw_progress_timeout
 *
 * This function is called at a preset interval to cause the progress
 * bar to move in activity mode.
 *
 * Use of the progress bar to show a fraction of a task takes priority.
 **/
static gboolean
bw_progress_timeout(gpointer user_data)
{
    BalsaWindow *window = *(BalsaWindow **) user_data;
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (balsa_app.show_statusbar
        && window != NULL
        && priv->progress_bar != NULL
        && priv->progress_type == BALSA_PROGRESS_ACTIVITY) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(priv->progress_bar));
    }

    /* return true so it continues to be called */
    return window != NULL;
}

/* bw_update_progress_bar and friends
 **/

typedef struct {
    GtkProgressBar *progress_bar;
    gboolean        set_text;
    gchar          *text;
    gboolean        set_fraction;
    gdouble         fraction;
} BalsaWindowSetupProgressInfo;

static gboolean
bw_update_progress_bar_idle_cb(BalsaWindowSetupProgressInfo * info)
{
    if (info->set_text)
        gtk_progress_bar_set_text(info->progress_bar, info->text);
    if (info->set_fraction)
        gtk_progress_bar_set_fraction(info->progress_bar, info->fraction);

    g_free(info->text);
    g_object_unref(info->progress_bar);
    g_free(info);

    return G_SOURCE_REMOVE;
}

static void
bw_update_progress_bar(BalsaWindow *window,
                       gboolean     set_text,
                       const gchar *text,
                       gboolean     set_fraction,
                       gdouble      fraction)
{
    /* Update the display in an idle callback, in case we were called in
     * a sub-thread.*/
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    BalsaWindowSetupProgressInfo *info;

    info = g_new(BalsaWindowSetupProgressInfo, 1);
    info->progress_bar = GTK_PROGRESS_BAR(g_object_ref(priv->progress_bar));
    info->set_text = set_text;
    info->text = g_strdup(text);
    info->set_fraction = set_fraction;
    info->fraction = fraction;

    g_idle_add((GSourceFunc) bw_update_progress_bar_idle_cb, info);
}

/* balsa_window_increase_activity
 *
 * Calling this causes this to the progress bar of the window to
 * switch into activity mode if it's not already going.  Otherwise it
 * simply increments the counter (so that multiple threads can
 * indicate activity simultaneously).
 **/
void
balsa_window_increase_activity(BalsaWindow * window, const gchar * message)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    static BalsaWindow *window_save = NULL;

    if (priv->progress_bar == NULL)
        return;

    if (!window_save) {
        window_save = window;
        g_object_add_weak_pointer(G_OBJECT(window_save),
                                  (gpointer *) &window_save);
    }

    if (priv->activity_handler == 0)
        /* add a timeout to make the activity bar move */
        priv->activity_handler =
            g_timeout_add(50, (GSourceFunc) bw_progress_timeout,
                          &window_save);

    /* increment the reference counter */
    ++priv->activity_counter;
    if (priv->progress_type == BALSA_PROGRESS_NONE)
        priv->progress_type = BALSA_PROGRESS_ACTIVITY;

    if (priv->progress_type == BALSA_PROGRESS_ACTIVITY)
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(priv->progress_bar),
                                  message);
    priv->activity_messages =
        g_slist_prepend(priv->activity_messages, g_strdup(message));
}


/* balsa_window_decrease_activity
 *
 * When called, decreases the reference counter of the progress
 * activity bar, if it goes to zero the progress bar is stopped and
 * cleared.
 **/
void
balsa_window_decrease_activity(BalsaWindow * window, const gchar * message)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    GSList *link;
    gboolean set_text = FALSE;
    const gchar *new_message = NULL;
    gboolean clear_fraction = FALSE;

    if (priv->progress_bar == NULL || priv->activity_messages == NULL)
        return;

    link = g_slist_find_custom(priv->activity_messages, message,
                               (GCompareFunc) strcmp);
    g_free(link->data);
    priv->activity_messages =
        g_slist_delete_link(priv->activity_messages, link);

    if (priv->progress_type == BALSA_PROGRESS_ACTIVITY) {
        set_text = TRUE;
        if (priv->activity_messages != NULL)
            new_message = priv->activity_messages->data;
    }

    /* decrement the counter if positive */
    if (priv->activity_counter > 0 && --priv->activity_counter == 0) {
        /* clear the bar and make it available for others to use */
        g_source_remove(priv->activity_handler);
        priv->activity_handler = 0;
        if (priv->progress_type == BALSA_PROGRESS_ACTIVITY) {
            priv->progress_type = BALSA_PROGRESS_NONE;
            clear_fraction = TRUE;
        }
    }

    if (set_text || clear_fraction)
        bw_update_progress_bar(window, set_text, new_message, clear_fraction, 0.0);
}


/* balsa_window_setup_progress
 *
 * window: BalsaWindow that contains the progressbar
 * text:   to appear superimposed on the progress bar,
 *         or NULL to clear and release the progress bar.
 *
 * returns: true if initialization is successful, otherwise returns
 * false.
 *
 * Initializes the progress bar for incremental operation with a range
 * from 0 to 1.  If the bar is already in activity mode, the function
 * returns false; if the initialization is successful it returns true.
 **/

gboolean
balsa_window_setup_progress(BalsaWindow * window, const gchar * text)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (priv->progress_bar == NULL)
        return FALSE;

    if (text != NULL) {
        /* make sure the progress bar is currently unused */
        if (priv->progress_type == BALSA_PROGRESS_INCREMENT)
            return FALSE;
        priv->progress_type = BALSA_PROGRESS_INCREMENT;
    } else
        priv->progress_type = BALSA_PROGRESS_NONE;

    bw_update_progress_bar(window, TRUE, text, TRUE, 0.0);

    return TRUE;
}

/* balsa_window_progress_bar_set_fraction
 *
 * If the progress bar has been initialized using
 * balsa_window_setup_progress, this function increments the
 * adjustment by one and executes any pending gtk events.  So the
 * progress bar will be shown as updated even if called within a loop.
 *
 * NOTE: This does not work with threads because a thread cannot
 * process events by itself and it holds the GDK lock preventing the
 * main thread from processing events.
 **/
void
balsa_window_progress_bar_set_fraction(BalsaWindow * window, gdouble fraction)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    if (priv->progress_bar == NULL)
        return;

    /* make sure the progress bar is being incremented */
    if (priv->progress_type != BALSA_PROGRESS_INCREMENT)
        return;

    bw_update_progress_bar(window, FALSE, NULL, TRUE, fraction);
}

/*
 * End of progress bar functions.
 */

/* browse_wrap can also be changed in the preferences window
 *
 * update_view_menu is called to synchronize the view menu check item
 * */
void
update_view_menu(BalsaWindow * window)
{
    bw_action_set_boolean(window, "wrap", balsa_app.browse_wrap);
}

/* Update the notebook tab label when the mailbox name is changed. */
void
balsa_window_update_tab(BalsaMailboxNode * mbnode)
{
    gint i = balsa_find_notebook_page_num(balsa_mailbox_node_get_mailbox(mbnode));
    if (i != -1) {
	GtkWidget *page =
	    gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK(balsa_app.notebook), page,
				   bw_notebook_label_new(mbnode));
    }
}

/* Helper for "Select All" callbacks: if the currently focused widget
 * supports any concept of "select-all", do it.
 *
 * It would be nice if all such widgets had a "select-all" signal, but
 * they don't; in fact, the only one that does (GtkTreeView) is
 * broken--if we emit it when the tree is not in multiple selection
 * mode, bad stuff happens.
 */
void
balsa_window_select_all(GtkWindow * window)
{
    GtkWidget *focus_widget = gtk_window_get_focus(window);

    if (!focus_widget)
	return;

    if (GTK_IS_TEXT_VIEW(focus_widget)) {
        GtkTextBuffer *buffer =
            gtk_text_view_get_buffer((GtkTextView *) focus_widget);
        GtkTextIter start, end;

        gtk_text_buffer_get_bounds(buffer, &start, &end);
        gtk_text_buffer_place_cursor(buffer, &start);
        gtk_text_buffer_move_mark_by_name(buffer, "selection_bound", &end);
    } else if (GTK_IS_EDITABLE(focus_widget)) {
        gtk_editable_select_region((GtkEditable *) focus_widget, 0, -1);
    } else if (GTK_IS_TREE_VIEW(focus_widget)) {
        GtkTreeSelection *selection =
            gtk_tree_view_get_selection((GtkTreeView *) focus_widget);
        if (gtk_tree_selection_get_mode(selection) ==
            GTK_SELECTION_MULTIPLE) {
	    if (BALSA_IS_INDEX(focus_widget))
		balsa_index_select_all((BalsaIndex *) focus_widget);
	    else {
		gtk_tree_view_expand_all((GtkTreeView *) focus_widget);
                gtk_tree_selection_select_all(selection);
            }
	}
#ifdef    HAVE_HTML_WIDGET
    } else if (libbalsa_html_can_select(focus_widget)) {
	libbalsa_html_select_all(focus_widget);
#endif /* HAVE_HTML_WIDGET */
    }
}

void
balsa_window_set_statusbar(BalsaWindow * window, LibBalsaMailbox * mailbox)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);
    gint total_messages = libbalsa_mailbox_total_messages(mailbox);
    gint unread_messages = libbalsa_mailbox_get_unread_messages(mailbox);
    gint hidden_messages;
    GString *desc = g_string_new(NULL);
    GtkStatusbar *statusbar;
    guint context_id;

    hidden_messages =
        libbalsa_mailbox_get_msg_tree(mailbox) ? total_messages -
        (g_node_n_nodes(libbalsa_mailbox_get_msg_tree(mailbox), G_TRAVERSE_ALL) - 1) : 0;

    /* xgettext: this is the first part of the message
     * "Shown mailbox: %s with %d messages, %d new, %d hidden". */
    g_string_append_printf(desc, _("Shown mailbox: %s "), libbalsa_mailbox_get_name(mailbox));
    if (total_messages > 0) {
        /* xgettext: this is the second part of the message
         * "Shown mailbox: %s with %d messages, %d new, %d hidden". */
        g_string_append_printf(desc,
                               ngettext("with %d message",
                                        "with %d messages",
                                        total_messages), total_messages);
        if (unread_messages > 0)
            /* xgettext: this is the third part of the message
             * "Shown mailbox: %s with %d messages, %d new, %d hidden". */
            g_string_append_printf(desc,
                                   ngettext(", %d new", ", %d new",
                                            unread_messages),
                                   unread_messages);
        if (hidden_messages > 0)
            /* xgettext: this is the fourth part of the message
             * "Shown mailbox: %s with %d messages, %d new, %d hidden". */
            g_string_append_printf(desc,
                                   ngettext(", %d hidden", ", %d hidden",
                                            hidden_messages),
                                   hidden_messages);
    }

    statusbar = GTK_STATUSBAR(priv->statusbar);
    context_id =
        gtk_statusbar_get_context_id(statusbar, "BalsaMBList message");
    gtk_statusbar_pop(statusbar, context_id);
    gtk_statusbar_push(statusbar, context_id, desc->str);

    g_string_free(desc, TRUE);
}

/* Select next unread message, changing mailboxes if necessary;
 * returns TRUE if mailbox was changed. */
gboolean
balsa_window_next_unread(BalsaWindow * window)
{
    BalsaIndex *index =
        BALSA_INDEX(balsa_window_find_current_index(window));
    LibBalsaMailbox *mailbox = index ? balsa_index_get_mailbox(index): NULL;

    if (libbalsa_mailbox_get_unread(mailbox) > 0) {
        balsa_index_select_next_unread(index);

        return FALSE;
    }

    mailbox = bw_next_unread_mailbox(mailbox);
    if (!mailbox || libbalsa_mailbox_get_unread(mailbox) == 0)
        return FALSE;

    if (balsa_app.ask_before_select) {
        GtkWidget *dialog;
        gint response;

        dialog =
            gtk_message_dialog_new(GTK_WINDOW(window), 0,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_YES_NO,
                                   _("The next unread message is in %s"),
                                   libbalsa_mailbox_get_name(mailbox));
        gtk_message_dialog_format_secondary_text
            (GTK_MESSAGE_DIALOG(dialog),
             _("Do you want to select %s?"), libbalsa_mailbox_get_name(mailbox));
        gtk_dialog_set_default_response(GTK_DIALOG(dialog),
                                        GTK_RESPONSE_YES);
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (response != GTK_RESPONSE_YES)
            return FALSE;
    }

    balsa_mblist_open_mailbox(mailbox);
    index = balsa_find_index_by_mailbox(mailbox);
    if (index)
        balsa_index_select_next_unread(index);
    else
        g_object_set_data(G_OBJECT(mailbox),
                          BALSA_INDEX_VIEW_ON_OPEN, GINT_TO_POINTER(TRUE));
    return TRUE;
}

/*
 * Getter
 */

GtkStatusbar *
balsa_window_get_statusbar(BalsaWindow * window)
{
    BalsaWindowPrivate *priv = balsa_window_get_instance_private(window);

    return (GtkStatusbar *) priv->statusbar;
}
