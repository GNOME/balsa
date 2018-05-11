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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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

#if HAVE_MACOSX_DESKTOP
#  include <ige-mac-integration.h>
#  include "macosx-helpers.h"
#endif

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

#include "filter.h"
#include "filter-funcs.h"

#include "libinit_balsa/assistant_init.h"

#include "libbalsa/libbalsa-marshal.h"

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

static void balsa_window_class_init(BalsaWindowClass * klass);
static void balsa_window_init(BalsaWindow * window);
static void balsa_window_real_open_mbnode(BalsaWindow *window,
                                          BalsaMailboxNode *mbnode,
                                          gboolean set_current);
static void balsa_window_real_close_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_destroy(GObject * object);

static gboolean bw_close_mailbox_on_timer(BalsaWindow * window);

static void bw_index_changed_cb(GtkWidget * widget, gpointer data);
static void bw_idle_replace(BalsaWindow * window, BalsaIndex * bindex);
static void bw_idle_remove(BalsaWindow * window);
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

static void bw_set_threading_menu(BalsaWindow * window, int option);
static void bw_show_mbtree(BalsaWindow * window);
static void bw_set_filter_menu(BalsaWindow * window, int gui_filter);
static LibBalsaCondition *bw_get_view_filter(BalsaWindow * window);

static void bw_select_part_cb(BalsaMessage * bm, gpointer data);

static void bw_find_real(BalsaWindow * window, BalsaIndex * bindex,
                         gboolean again);

static void bw_slave_position_cb(GtkPaned   * paned_slave,
                                 GParamSpec * pspec,
                                 gpointer     user_data);
static void bw_size_allocate_cb(GtkWidget * window, GtkAllocation * alloc);

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


static GtkWidget *bw_notebook_label_new (BalsaMailboxNode* mbnode);
static void bw_reset_filter(BalsaWindow * bw);


/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

G_DEFINE_TYPE (BalsaWindow, balsa_window, GTK_TYPE_APPLICATION_WINDOW)

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
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    object_class->dispose = balsa_window_destroy;

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

    g_printerr("Network is %s (%s)\n",
               available ? "available  " : "unavailable",
               datetime_string);
    g_free(datetime_string);
}

static void
bw_network_changed_cb(GNetworkMonitor * monitor,
                      gboolean          available,
                      gpointer          user_data)
{
    BalsaWindow *window = user_data;

    if (window->network_available != available) {
        window->network_available = available;
        print_network_status(available);
    }

    if (window->network_changed_source_id == 0) {
        /* Wait 2 seconds or so to let the network stabilize */
        window->network_changed_source_id =
            g_timeout_add_seconds(2, bw_change_connection_status_idle, window);
    }
}

static void
balsa_window_init(BalsaWindow * window)
{
    GNetworkMonitor *monitor;

    monitor = g_network_monitor_get_default();
    window->network_available =
        g_network_monitor_get_network_available(monitor);
    print_network_status(window->network_available);
    g_signal_connect(monitor, "network-changed",
                     G_CALLBACK(bw_network_changed_cb), window);
    window->last_check_time = 0;
}

static gboolean
bw_delete_cb(GtkWidget* main_window)
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
bw_master_position_cb(GtkPaned   * paned_master,
                      GParamSpec * pspec,
                      gpointer     user_data)
{
    if (balsa_app.show_mblist)
        balsa_app.mblist_width = /* FIXME: this makes some assumptions... */
            gtk_paned_get_position(paned_master);
}

static GtkWidget *
bw_frame(GtkWidget * widget)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame), widget);
    gtk_widget_show(frame);
    return frame;
}
/* Filter entry widget creation code. We must carefully pass the typed
   characters FIRST to the entry widget and only if the widget did not
   process them, pass them further to main window, menu etc.
   Otherwise, typing eg. 'c' would open the draftbox instead of
   actually insert the 'c' character in the entry. */
static gboolean
bw_pass_to_filter(BalsaWindow *bw, GdkEventKey *event, gpointer data)
{
    gboolean res = FALSE;
    g_signal_emit_by_name(bw->sos_entry, "key_press_event", event, &res, data);
    return res;
}
static gboolean
bw_enable_filter(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    g_signal_connect(G_OBJECT(data), "key_press_event",
                     G_CALLBACK(bw_pass_to_filter), NULL);
    return FALSE;
}
static gboolean
bw_disable_filter(GtkWidget *widget, GdkEventFocus *event, gpointer data)
{
    g_signal_handlers_disconnect_by_func(G_OBJECT(data),
                                         G_CALLBACK(bw_pass_to_filter),
                                         NULL);
    return FALSE;
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
    BalsaWindow *bw = balsa_app.main_window;
    int filter_no =
        gtk_combo_box_get_active(GTK_COMBO_BOX(bw->filter_choice));

    bw_set_view_filter(bw, filter_no, entry);
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

static GtkWidget*
bw_create_index_widget(BalsaWindow *bw)
{
    GtkWidget *vbox, *button;
    unsigned i;
    GList *focusable_widgets;

    if(!view_filters_translated) {
        for(i=0; i<ELEMENTS(view_filters); i++)
            view_filters[i].str = _(view_filters[i].str);
        view_filters_translated = TRUE;
    }

    bw->sos_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    bw->filter_choice = gtk_combo_box_text_new();
    gtk_box_pack_start(GTK_BOX(bw->sos_bar), bw->filter_choice,
                       FALSE, FALSE, 0);
    for(i=0; i<ELEMENTS(view_filters); i++)
        gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(bw->filter_choice),
                                       i, view_filters[i].str);
    gtk_combo_box_set_active(GTK_COMBO_BOX(bw->filter_choice), 0);
    gtk_widget_show(bw->filter_choice);
    bw->sos_entry = gtk_entry_new();
    /* gtk_label_set_mnemonic_widget(GTK_LABEL(bw->filter_choice),
       bw->sos_entry); */
    g_signal_connect(G_OBJECT(bw->sos_entry), "focus_in_event",
                     G_CALLBACK(bw_enable_filter), bw);
    g_signal_connect(G_OBJECT(bw->sos_entry), "focus_out_event",
                     G_CALLBACK(bw_disable_filter), bw);
    gtk_box_pack_start(GTK_BOX(bw->sos_bar), bw->sos_entry, TRUE, TRUE, 0);
    gtk_widget_show(bw->sos_entry);
    gtk_box_pack_start(GTK_BOX(bw->sos_bar),
                       button = gtk_button_new(),
                       FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_icon_name("gtk-ok",
                                                    GTK_ICON_SIZE_BUTTON));
    g_signal_connect(G_OBJECT(bw->sos_entry), "activate",
                     G_CALLBACK(bw_filter_entry_activate),
                     button);
    g_signal_connect_swapped(G_OBJECT(button), "clicked",
                             G_CALLBACK(bw_filter_entry_activate),
                             bw->sos_entry);
    g_signal_connect(G_OBJECT(bw->sos_entry), "changed",
                             G_CALLBACK(bw_filter_entry_changed),
                             button);
    g_signal_connect(G_OBJECT(bw->filter_choice), "changed",
                     G_CALLBACK(bw_filter_entry_changed), button);
    gtk_widget_show_all(button);
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(bw->sos_bar);
    gtk_box_pack_start(GTK_BOX(vbox), bw->sos_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bw->notebook, TRUE, TRUE, 0);

    focusable_widgets = g_list_append(NULL, bw->notebook);
    gtk_container_set_focus_chain(GTK_CONTAINER(vbox), focusable_widgets);
    g_list_free(focusable_widgets);

    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_show(vbox);
    return vbox;
}

static void
bw_set_panes(BalsaWindow * window)
{
    GtkWidget *index_widget = bw_create_index_widget(window);
    GtkWidget *bindex;
    BalsaIndexWidthPreference width_preference;

    switch (balsa_app.layout_type) {
    case LAYOUT_WIDE_MSG:
	window->paned_master = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
	window->paned_slave  = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        if (window->content)
            gtk_container_remove(GTK_CONTAINER(window->vbox),
                                 window->content);
        window->content = window->paned_master;
        gtk_box_pack_start(GTK_BOX(window->vbox), window->content,
                           TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(window->paned_slave),
			bw_frame(window->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(window->paned_slave),
			bw_frame(index_widget), TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->paned_master),
			window->paned_slave, TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(window->paned_master),
			bw_frame(window->preview), TRUE, TRUE);
        width_preference = BALSA_INDEX_WIDE;
	break;
    case LAYOUT_WIDE_SCREEN:
	window->paned_master = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	window->paned_slave  = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        if (window->content)
            gtk_container_remove(GTK_CONTAINER(window->vbox),
                                 window->content);
        window->content = window->paned_master;
        gtk_box_pack_start(GTK_BOX(window->vbox), window->content,
                           TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(window->paned_master),
                        bw_frame(window->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(window->paned_master), window->paned_slave,
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->paned_slave),
                        bw_frame(index_widget), TRUE, FALSE);
	gtk_paned_pack2(GTK_PANED(window->paned_slave),
                        bw_frame(window->preview), TRUE, TRUE);
        width_preference = BALSA_INDEX_NARROW;
	break;
    case LAYOUT_DEFAULT:
    default:
	window->paned_master = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	window->paned_slave  = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
        if (window->content)
            gtk_container_remove(GTK_CONTAINER(window->vbox),
                                 window->content);
        window->content = window->paned_master;
        gtk_box_pack_start(GTK_BOX(window->vbox), window->content,
                           TRUE, TRUE, 0);
	gtk_paned_pack1(GTK_PANED(window->paned_master),
                        bw_frame(window->mblist), TRUE, TRUE);
        gtk_paned_pack2(GTK_PANED(window->paned_master), window->paned_slave,
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->paned_slave),
                        bw_frame(index_widget), TRUE, TRUE);
	gtk_paned_pack2(GTK_PANED(window->paned_slave),
                        bw_frame(window->preview), TRUE, TRUE);
        width_preference = BALSA_INDEX_WIDE;
    }
    if ( (bindex=balsa_window_find_current_index(window)) != NULL)
        balsa_index_set_width_preference(BALSA_INDEX(bindex), width_preference);
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

/*
 * "notify::is-maximized" signal handler
 */
static void
bw_notify_is_maximized_cb(GtkWindow  * window,
                          GParamSpec * pspec,
                          gpointer     user_data)
{
    /* Note when we are either maximized or fullscreen, to avoid saving
     * nonsensical geometry. */
    balsa_app.mw_maximized = gtk_window_is_maximized(window);
}

static void
bw_is_active_notify(GObject * gobject, GParamSpec * pspec,
                    gpointer user_data)
{
    GtkWindow *gtk_window = GTK_WINDOW(gobject);

    if (gtk_window_is_active(gtk_window)) {
#ifdef HAVE_NOTIFY
        BalsaWindow *window = BALSA_WINDOW(gobject);

        if (window->new_mail_note)
            notify_notification_close(window->new_mail_note, NULL);
#endif                          /* HAVE_NOTIFY */
        gtk_window_set_urgency_hint(gtk_window, FALSE);
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
        g_print("%s action “%s” not found\n", __func__, action_name);

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
 * Set the state of a radio action
 */

static void
bw_action_set_string(BalsaWindow * window,
                     const gchar * action_name,
                     const gchar * state)
{
    GAction *action;

    action = bw_get_action(window, action_name);
    if (action)
        g_simple_action_set_state(G_SIMPLE_ACTION(action),
                                  g_variant_new_string(state));
}

/*
 * Enable or disable an action
 */

static void
bw_action_set_enabled(BalsaWindow * window,
                      const gchar * action_name,
                      gboolean      enabled)
{
    GAction *action;

    g_assert(window != NULL);

    /* Is the window being destroyed? */
    if (window->preview == NULL)
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

    g_signal_connect(G_OBJECT(smwindow->window), "destroy",
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
new_imap_box_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    mailbox_conf_new(LIBBALSA_TYPE_MAILBOX_IMAP);
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
    folder_conf_imap_sub_node(NULL);
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

static void
prefs_activated(GSimpleAction * action,
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
#if !GTK_CHECK_VERSION(3, 22, 0)
    GdkScreen *screen;
#endif /* GTK_CHECK_VERSION(3, 22, 0) */
    GError *err = NULL;

#if GTK_CHECK_VERSION(3, 22, 0)
    gtk_show_uri_on_window(window, "help:balsa",
                           gtk_get_current_event_time(), &err);
#else /* GTK_CHECK_VERSION(3, 22, 0) */
    screen = gtk_window_get_screen(window);
    gtk_show_uri(screen, "help:balsa", gtk_get_current_event_time(),
                 &err);
#endif /* GTK_CHECK_VERSION(3, 22, 0) */
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
                          "Copyright \xc2\xa9 1997-2018 The Balsa Developers",
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
                          "website", "http://balsa.gnome.org",
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
    libbalsa_information_parented(window,
                                  LIBBALSA_INFORMATION_MESSAGE,
                                  _("Balsa closes files and connections."
                                    " Please wait…"));
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
    GtkWidget *index;

    index = balsa_window_find_current_index(window);

    if (index
        && BALSA_INDEX(index)->mailbox_node->mailbox == balsa_app.draftbox)
        balsa_message_continue(BALSA_INDEX(index));
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
    GtkWidget *index;
    BalsaIndex *bindex;

    index = balsa_window_find_current_index(window);
    if (!index)
        return;

    bindex = BALSA_INDEX(index);
    if (bindex->current_msgno) {
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(bindex->mailbox_node->mailbox,
                                         bindex->current_msgno);
        if (!message)
            return;
        message_print(message, GTK_WINDOW(window));
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

    if (balsa_app.previewpane)
        balsa_message_find_in_message(BALSA_MESSAGE(window->preview));
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
    GtkWidget *bm = window->preview;

    balsa_message_zoom(BALSA_MESSAGE(bm), 1);
}

static void
zoom_out_activated(GSimpleAction * action,
                   GVariant      * parameter,
                   gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bm = window->preview;

    balsa_message_zoom(BALSA_MESSAGE(bm), -1);
}

static void
zoom_normal_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bm = window->preview;

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
    GtkWidget *index;

    /* do it by resetting the sos filder */
    gtk_entry_set_text(GTK_ENTRY(window->sos_entry), "");
    index = balsa_window_find_current_index(window);
    bw_set_view_filter(window, BALSA_INDEX(index)->filter_no,
                       window->sos_entry);
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
    GtkWidget *index;

    index = balsa_window_find_current_index(window);
    if (index)
        balsa_mblist_close_mailbox(BALSA_INDEX(index)->mailbox_node->
                                   mailbox);
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
        filters_run_dialog(BALSA_INDEX(index)->mailbox_node->mailbox,
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
            BALSA_INDEX(index)->mailbox_node->mailbox;
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

    balsa_message_next_part(BALSA_MESSAGE(window->preview));
}

static void
previous_part_activated(GSimpleAction * action,
                        GVariant      * parameter,
                        gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_previous_part(BALSA_MESSAGE(window->preview));
}

static void
save_part_activated(GSimpleAction * action,
                    GVariant      * parameter,
                    gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_message_save_current_part(BALSA_MESSAGE(window->preview));
}

static void
view_source_activated(GSimpleAction * action,
                      GVariant      * parameter,
                      gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);
    GtkWidget *bindex;
    GList *messages, *list;

    bindex = balsa_window_find_current_index(window);
    g_return_if_fail(bindex != NULL);

    messages = balsa_index_selected_list(BALSA_INDEX(bindex));
    for (list = messages; list; list = list->next) {
	LibBalsaMessage *message = list->data;

	libbalsa_show_message_source(balsa_app.application,
                                     message, balsa_app.message_font,
				     &balsa_app.source_escape_specials,
                                     &balsa_app.source_width,
                                     &balsa_app.source_height);
    }

    g_list_free_full(messages, g_object_unref);
}

static void
copy_message_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    if (balsa_message_grab_focus(BALSA_MESSAGE(window->preview)))
        copy_activated(action, parameter, user_data);
}

static void
select_text_activated(GSimpleAction * action,
                       GVariant      * parameter,
                       gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    if (balsa_message_grab_focus(BALSA_MESSAGE(window->preview)))
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

    balsa_app.show_notebook_tabs = g_variant_get_boolean(state);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook),
                               balsa_app.show_notebook_tabs);
    g_simple_action_set_state(action, state);
}

static void
show_toolbar_change_state(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    bw_show_or_hide_widget(action, state, &balsa_app.show_main_toolbar,
                           window->toolbar);
}

static void
show_statusbar_change_state(GSimpleAction * action,
                            GVariant      * state,
                            gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    bw_show_or_hide_widget(action, state, &balsa_app.show_statusbar,
                           window->bottom_bar);
}

static void
show_sos_bar_change_state(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    bw_show_or_hide_widget(action, state, &balsa_app.show_sos_bar,
                           window->sos_bar);
}

static void
wrap_change_state(GSimpleAction * action,
                  GVariant      * state,
                  gpointer        user_data)
{
    BalsaWindow *window = BALSA_WINDOW(user_data);

    balsa_app.browse_wrap = g_variant_get_boolean(state);

    balsa_message_set_wrap(BALSA_MESSAGE(window->preview),
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

    balsa_app.show_all_headers = g_variant_get_boolean(state);

    balsa_message_set_displayed_headers(BALSA_MESSAGE(window->preview),
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
        g_print("%s unknown value “%s”\n", __func__, value);
        return;
    }

    balsa_app.shown_headers = sh;
    bw_reset_show_all_headers(window);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(window->preview),
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
    const gchar *value;
    LibBalsaMailboxThreadingType type;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    value = g_variant_get_string(state, NULL);

    if (strcmp(value, "flat") == 0)
        type = LB_MAILBOX_THREADING_FLAT;
    else if (strcmp(value, "simple") == 0)
        type = LB_MAILBOX_THREADING_SIMPLE;
    else if (strcmp(value, "jwz") == 0)
        type = LB_MAILBOX_THREADING_JWZ;
    else {
        g_print("%s unknown value “%s”\n", __func__, value);
        return;
    }

    index = balsa_window_find_current_index(window);
    balsa_index_set_threading_type(BALSA_INDEX(index), type);

    /* bw->current_index may have been destroyed and cleared during
     * set-threading: */
    index = balsa_window_find_current_index(window);
    if (index && (mbnode = BALSA_INDEX(index)->mailbox_node)
        && (mailbox = mbnode->mailbox))
        bw_enable_expand_collapse(window, mailbox);

    g_simple_action_set_state(action, state);
}

/*
 * End of callbacks
 */

static void
bw_add_app_action_entries(GActionMap * action_map, gpointer user_data)
{
    static GActionEntry app_entries[] = {
        {"new-message",           new_message_activated},
        {"new-mbox",              new_mbox_activated},
        {"new-maildir",           new_maildir_activated},
        {"new-mh",                new_mh_activated},
        {"new-imap-box",          new_imap_box_activated},
        {"new-imap-folder",       new_imap_folder_activated},
        {"new-imap-subfolder",    new_imap_subfolder_activated},
        {"toolbars",              toolbars_activated},
        {"identities",            identities_activated},
        {"address-book",          address_book_activated},
        {"prefs",                 prefs_activated},
        {"help",                  help_activated},
        {"about",                 about_activated},
        {"quit",                  quit_activated}
    };

    g_action_map_add_action_entries(action_map, app_entries,
                                    G_N_ELEMENTS(app_entries), user_data);
}

static void
bw_add_win_action_entries(GActionMap * action_map)
{
    static GActionEntry win_entries[] = {
        {"new-message",           new_message_activated},
        {"continue",              continue_activated},
        {"get-new-mail",          get_new_mail_activated},
        {"send-queued-mail",      send_queued_mail_activated},
        {"send-and-receive-mail", send_and_receive_mail_activated},
        {"page-setup",            page_setup_activated},
        {"print",                 print_activated},
        {"address-book",          address_book_activated},
        {"quit",                  quit_activated},
        {"copy",                  copy_activated},
        {"select-all",            select_all_activated},
        {"select-thread",         select_thread_activated},
        {"find",                  find_activated},
        {"find-next",             find_next_activated},
        {"find-in-message",       find_in_message_activated},
        {"filters",               filters_activated},
        {"export-filters",        export_filters_activated},
        {"show-mailbox-tree",     libbalsa_toggle_activated, NULL, "false",
                                  show_mailbox_tree_change_state},
        {"show-mailbox-tabs",     libbalsa_toggle_activated, NULL, "false",
                                  show_mailbox_tabs_change_state},
        {"show-toolbar",          libbalsa_toggle_activated, NULL, "false",
                                  show_toolbar_change_state},
        {"show-statusbar",        libbalsa_toggle_activated, NULL, "false",
                                  show_statusbar_change_state},
        {"show-sos-bar",          libbalsa_toggle_activated, NULL, "false",
                                  show_sos_bar_change_state},
        {"wrap",                  libbalsa_toggle_activated, NULL, "false",
                                  wrap_change_state},
        {"headers",               libbalsa_radio_activated, "s", "'none'",
                                  header_change_state},
        {"threading",             libbalsa_radio_activated, "s", "'flat'",
                                  threading_change_state},
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
        {"hide-deleted",          libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-undeleted",        libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-read",             libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-unread",           libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-flagged",          libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-unflagged",        libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-answered",         libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
        {"hide-unanswered",       libbalsa_toggle_activated, NULL, "false",
                                  hide_change_state},
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
        {"forward-attached",      forward_attached_activated},
        {"forward-inline",        forward_inline_activated},
        {"pipe",                  pipe_activated},
        {"next-part",             next_part_activated},
        {"previous-part",         previous_part_activated},
        {"save-part",             save_part_activated},
        {"view-source",           view_source_activated},
        {"copy-message",          copy_message_activated},
        {"select-text",           select_text_activated},
        {"move-to-trash",         move_to_trash_activated},
        {"toggle-flagged",        toggle_flagged_activated},
        {"toggle-deleted",        toggle_deleted_activated},
        {"toggle-new",            toggle_new_activated},
        {"toggle-answered",       toggle_answered_activated},
        {"store-address",         store_address_activated},
        /* toolbar actions that are not in any menu: */
        {"show-all-headers",      libbalsa_toggle_activated, NULL, "false",
                                  show_all_headers_change_state},
        {"show-preview-pane",     libbalsa_toggle_activated, NULL, "true",
                                  show_preview_pane_change_state},
    };

    g_action_map_add_action_entries(action_map, win_entries,
                                    G_N_ELEMENTS(win_entries), action_map);
}

void
balsa_window_add_action_entries(GActionMap * action_map)
{
    bw_add_app_action_entries(action_map, NULL);
    bw_add_win_action_entries(action_map);
}

static void
bw_set_menus(BalsaWindow * window)
{
    GtkBuilder *builder;
    const gchar resource_path[] = "/org/desktop/Balsa/main-window.ui";
    GError *err = NULL;

    bw_add_app_action_entries(G_ACTION_MAP(balsa_app.application), window);
    bw_add_win_action_entries(G_ACTION_MAP(window));

    builder = gtk_builder_new();
    if (gtk_builder_add_from_resource(builder, resource_path, &err)) {
        gtk_application_set_app_menu(balsa_app.application,
                                     G_MENU_MODEL(gtk_builder_get_object
                                                  (builder, "app-menu")));
        gtk_application_set_menubar(balsa_app.application,
                                    G_MENU_MODEL(gtk_builder_get_object
                                                 (builder, "menubar")));
    } else {
        g_print("%s error: %s\n", __func__, err->message);
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Error adding from %s: %s\n"), resource_path,
                          err->message);
        g_error_free(err);
    }
    g_object_unref(builder);
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
    BalsaWindow *window = user_data;

    gtk_notebook_set_current_page(GTK_NOTEBOOK(window->notebook),
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
    "reply", "reply-all", "reply-group",
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

GtkWidget *
balsa_window_new()
{
    BalsaWindow *window;
    BalsaToolbarModel *model;
    GtkWidget *hbox;
    static const gchar *const header_targets[] =
        { "none", "selected", "all" };
#if HAVE_MACOSX_DESKTOP
    IgeMacMenuGroup *group;
#endif
    GtkAdjustment *hadj, *vadj;
    GAction *action;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS
     * - allows for grey out */
    balsa_register_pixmaps();

    window = g_object_new(BALSA_TYPE_WINDOW,
                          "application", balsa_app.application,
                          NULL);

    /* Set up the GMenu structures */
    bw_set_menus(window);

    /* Set up <alt>n key bindings */
    bw_set_alt_bindings(window);

    window->vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_show(window->vbox);
    gtk_container_add(GTK_CONTAINER(window), window->vbox);

    gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    balsa_register_pixbufs(GTK_WIDGET(window));

    model = balsa_window_get_toolbar_model();

    window->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_box_pack_start(GTK_BOX(window->vbox), window->toolbar,
                       FALSE, FALSE, 0);

    window->bottom_bar = hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_end(GTK_BOX(window->vbox), hbox, FALSE, FALSE, 0);

    window->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_valign(window->progress_bar, GTK_ALIGN_CENTER);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(window->progress_bar),
                                    0.01);
    gtk_box_pack_start(GTK_BOX(hbox), window->progress_bar, FALSE, FALSE,
                       0);

    window->statusbar = gtk_statusbar_new();
    g_signal_connect(window, "notify::is-maximized",
                     G_CALLBACK(bw_notify_is_maximized_cb),
                     window->statusbar);
    gtk_box_pack_start(GTK_BOX(hbox), window->statusbar, TRUE, TRUE, 0);
    gtk_widget_show_all(hbox);

#if 0
    gnome_app_install_appbar_menu_hints(GNOME_APPBAR(balsa_app.appbar),
                                        main_menu);
#endif

    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), balsa_app.mw_width,
                                balsa_app.mw_height);
    if (balsa_app.mw_maximized)
        gtk_window_maximize(GTK_WINDOW(window));

    window->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook),
                               balsa_app.show_notebook_tabs);
    gtk_notebook_set_show_border (GTK_NOTEBOOK(window->notebook), FALSE);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (window->notebook), TRUE);
    g_signal_connect(G_OBJECT(window->notebook), "switch_page",
                     G_CALLBACK(bw_notebook_switch_page_cb), window);
    gtk_drag_dest_set (GTK_WIDGET (window->notebook), GTK_DEST_DEFAULT_ALL,
                       notebook_drop_types, NUM_DROP_TYPES,
                       GDK_ACTION_DEFAULT | GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(G_OBJECT (window->notebook), "drag-data-received",
                     G_CALLBACK (bw_notebook_drag_received_cb), NULL);
    g_signal_connect(G_OBJECT (window->notebook), "drag-motion",
                     G_CALLBACK (bw_notebook_drag_motion_cb), NULL);
    balsa_app.notebook = window->notebook;
    g_object_add_weak_pointer(G_OBJECT(window->notebook),
			      (gpointer) &balsa_app.notebook);

    window->preview = balsa_message_new();
    gtk_widget_hide(window->preview);

    g_signal_connect(G_OBJECT(window->preview), "select-part",
                     G_CALLBACK(bw_select_part_cb), window);

    /* XXX */
    balsa_app.mblist =  BALSA_MBLIST(balsa_mblist_new());
    gtk_widget_show(GTK_WIDGET(balsa_app.mblist));

    g_object_get(G_OBJECT(balsa_app.mblist), "hadjustment", &hadj,
                 "vadjustment", &vadj, NULL);
    window->mblist = gtk_scrolled_window_new(hadj, vadj);

    gtk_container_add(GTK_CONTAINER(window->mblist),
                      GTK_WIDGET(balsa_app.mblist));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->mblist),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_signal_connect_swapped(balsa_app.mblist, "has-unread-mailbox",
		             G_CALLBACK(bw_enable_next_unread), window);
    balsa_mblist_default_signal_bindings(balsa_app.mblist);

    bw_set_panes(window);

    /*PKGW: do it this way, without the usizes. */
    bw_action_set_boolean(window, "show-mailbox-tree",
                          balsa_app.show_mblist);

    if (balsa_app.show_mblist) {
        gtk_widget_show(window->mblist);
        gtk_paned_set_position(GTK_PANED(window->paned_master),
                               balsa_app.mblist_width);
    } else {
        gtk_paned_set_position(GTK_PANED(window->paned_master), 0);
    }

    /*PKGW: do it this way, without the usizes. */
    if (balsa_app.previewpane)
        gtk_paned_set_position(GTK_PANED(window->paned_slave),
                               balsa_app.notebook_height);
    else
        /* Set it to something really high */
        gtk_paned_set_position(GTK_PANED(window->paned_slave), G_MAXINT);

    gtk_widget_show(window->paned_slave);
    gtk_widget_show(window->paned_master);
    gtk_widget_show(window->notebook);

    /* set the toolbar style */
    balsa_window_refresh(window);

    action = bw_get_action(window, "headers");
    g_simple_action_set_state(G_SIMPLE_ACTION(action),
                              g_variant_new_string(header_targets
                                                   [balsa_app.
                                                    shown_headers]));

    action = bw_get_action(window, "threading");
    g_simple_action_set_state(G_SIMPLE_ACTION(action),
                              g_variant_new_string("flat"));

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

    g_signal_connect(window, "size_allocate",
                     G_CALLBACK(bw_size_allocate_cb), NULL);
    g_signal_connect(window, "destroy",
                     G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect(window, "delete-event",
                     G_CALLBACK(bw_delete_cb), NULL);

    /* Cancel new-mail notification when we get the focus. */
    g_signal_connect(window, "notify::is-active",
                     G_CALLBACK(bw_is_active_notify), NULL);

    /* set initial state of Get-New-Mail button */
    bw_action_set_enabled(window, "get-new-mail", g_atomic_int_get(&checking_mail) == 1);

    g_timeout_add_seconds(30, (GSourceFunc) bw_close_mailbox_on_timer, window);

    gtk_widget_show(GTK_WIDGET(window));
    return GTK_WIDGET(window);
}

gboolean
balsa_window_fix_paned(BalsaWindow *window)
{
    if (balsa_app.show_mblist) {
        gtk_paned_set_position(GTK_PANED(window->paned_master),
                               balsa_app.mblist_width);
    }
    if (balsa_app.previewpane) {
        gtk_paned_set_position(GTK_PANED(window->paned_slave),
                               balsa_app.notebook_height);
    }

    g_signal_connect(window->paned_master, "notify::position",
                     G_CALLBACK(bw_master_position_cb), NULL);
    g_signal_connect(window->paned_slave, "notify::position",
                     G_CALLBACK(bw_slave_position_cb), NULL);

    return FALSE;
}

/*
 * Enable or disable menu items/toolbar buttons which depend
 * on whether there is a mailbox open.
 */
static void
bw_enable_expand_collapse(BalsaWindow * window, LibBalsaMailbox * mailbox)
{
    gboolean enable;

    enable = mailbox &&
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
        mbnode = index->mailbox_node;
        mailbox = mbnode->mailbox;
    }
    bw_action_set_enabled(window, "mailbox-expunge",
    /* cppcheck-suppress nullPointer */
                          mailbox && !mailbox->readonly);

    bw_actions_set_enabled(window, mailbox_actions,
                           G_N_ELEMENTS(mailbox_actions), enable);
    bw_action_set_enabled(window, "next-message",
                          index && index->next_message);
    bw_action_set_enabled(window, "previous-message",
                          index && index->prev_message);

    bw_action_set_enabled(window, "remove-duplicates", mailbox &&
                          libbalsa_mailbox_can_move_duplicates(mailbox));

    if (mailbox) {
	bw_set_threading_menu(window,
					libbalsa_mailbox_get_threading_type
					(mailbox));
	bw_set_filter_menu(window,
				     libbalsa_mailbox_get_filter(mailbox));
    }

    bw_enable_next_unread(window, libbalsa_mailbox_get_unread(mailbox) > 0
                          || bw_next_unread_mailbox(mailbox));

    bw_enable_expand_collapse(window, mailbox);
}

void
balsa_window_update_book_menus(BalsaWindow * window)
{
    gboolean has_books = balsa_app.address_book_list != NULL;

    bw_action_set_enabled(window, "address-book",  has_books);
    bw_action_set_enabled(window, "store-address", has_books &&
                          window->current_index &&
                          BALSA_INDEX(window->current_index)->current_msgno);
}

/*
 * Enable or disable menu items/toolbar buttons which depend
 * on if there is a message selected.
 */
static void
bw_enable_message_menus(BalsaWindow * window, guint msgno)
{
    gboolean enable, enable_mod, enable_store;
    BalsaIndex *bindex = BALSA_INDEX(window->current_index);

    enable = (msgno != 0 && bindex != NULL);
    bw_actions_set_enabled(window, current_message_actions,
                           G_N_ELEMENTS(current_message_actions), enable);

    enable = (bindex != NULL
              && balsa_index_count_selected_messages(bindex) > 0);
    bw_actions_set_enabled(window, message_actions,
                           G_N_ELEMENTS(message_actions), enable);

    enable_mod = (enable && !bindex->mailbox_node->mailbox->readonly);
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
    BalsaMessage *msg = window ? BALSA_MESSAGE(window->preview) : NULL;

    bw_action_set_enabled(window, "next-part",
                          balsa_message_has_next_part(msg));
    bw_action_set_enabled(window, "previous-part",
                          balsa_message_has_previous_part(msg));
}

static void
bw_set_threading_menu(BalsaWindow * window, int option)
{
    GtkWidget *index;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;
    const gchar *const threading_types[] = { "flat", "simple", "jwz" };

    bw_action_set_string(window, "threading", threading_types[option]);

    if ((index = balsa_window_find_current_index(window))
	&& (mbnode = BALSA_INDEX(index)->mailbox_node)
	&& (mailbox = mbnode->mailbox))
	bw_enable_expand_collapse(window, mailbox);
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

    lab = gtk_label_new(mbnode->mailbox->name);
    gtk_widget_set_name(lab, "balsa-notebook-tab-label");

    /* Try to make text not bold: */
    css_provider = gtk_css_provider_new();
    if (!gtk_css_provider_load_from_data(css_provider,
                                         "#balsa-notebook-tab-label"
                                         "{"
                                           "font-weight:normal;"
                                         "}",
                                         -1, NULL))
        g_print("Could not load label CSS data.\n");

    gtk_style_context_add_provider(gtk_widget_get_style_context(lab) ,
                                   GTK_STYLE_PROVIDER(css_provider),
                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    bw_notebook_label_style(GTK_LABEL(lab),
                            libbalsa_mailbox_get_unread(mbnode->mailbox) > 0);
    g_signal_connect_object(mbnode->mailbox, "changed",
                            G_CALLBACK(bw_mailbox_changed), lab, 0);
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 0);

    but = gtk_button_new();
#if GTK_CHECK_VERSION(3, 19, 0)
    gtk_widget_set_focus_on_click(but, FALSE);
#else                           /* GTK_CHECK_VERSION(3, 20, 0) */
    gtk_button_set_focus_on_click(GTK_BUTTON(but), FALSE);
#endif                          /* GTK_CHECK_VERSION(3, 20, 0) */
    gtk_button_set_relief(GTK_BUTTON(but), GTK_RELIEF_NONE);

    gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &w, &h);
    gtk_widget_set_size_request(but, w, h);

    g_signal_connect(but, "clicked",
                     G_CALLBACK(bw_mailbox_tab_close_cb), mbnode);

    close_pix = gtk_image_new_from_icon_name("window-close-symbolic",
                                             GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(but), close_pix);
    gtk_box_pack_start(GTK_BOX(box), but, FALSE, FALSE, 0);

    gtk_widget_show_all(box);

    gtk_widget_set_tooltip_text(box, mbnode->mailbox->url);
    return box;
}

/*
 * balsa_window_real_open_mbnode
 */

typedef struct {
    BalsaIndex       *index;
    BalsaMailboxNode *mbnode;
    BalsaWindow      *window;
    gchar            *message;
    gboolean          set_current;
} BalsaWindowRealOpenMbnodeInfo;

static gboolean
bw_real_open_mbnode_idle_cb(BalsaWindowRealOpenMbnodeInfo * info)
{
    BalsaIndex       *index   = info->index;
    BalsaMailboxNode *mbnode  = info->mbnode;
    BalsaWindow      *window  = info->window;
    LibBalsaMailbox  *mailbox = mbnode->mailbox;
    GtkWidget        *label;
    GtkWidget        *scroll;
    gint              page_num;
    LibBalsaCondition *filter;

    if (!window) {
        g_free(info->message);
        g_object_unref(g_object_ref_sink(index));
        g_object_unref(mbnode);
        g_free(info);
        return FALSE;
    }

    balsa_window_decrease_activity(window, info->message);
    g_object_remove_weak_pointer(G_OBJECT(window),
                                 (gpointer) &info->window);
    g_free(info->message);

    if (balsa_find_notebook_page_num(mailbox) >= 0) {
        g_object_unref(g_object_ref_sink(index));
        g_object_unref(mbnode);
        g_free(info);
        return FALSE;
    }

    balsa_index_load_mailbox_node(index, mbnode);

    g_signal_connect(index, "index-changed",
                     G_CALLBACK(bw_index_changed_cb), window);

    label = bw_notebook_label_new(mbnode);
    g_object_unref(mbnode);

    /* store for easy access */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(index));
    gtk_widget_show(scroll);
    page_num = gtk_notebook_append_page(GTK_NOTEBOOK(window->notebook),
                                        scroll, label);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(window->notebook),
                                     scroll, TRUE);

    if (info->set_current)
        /* change the page to the newly selected notebook item */
        gtk_notebook_set_current_page(GTK_NOTEBOOK(window->notebook),
                                      page_num);

    bw_register_open_mailbox(mailbox);
    libbalsa_mailbox_set_threading(mailbox,
                                   libbalsa_mailbox_get_threading_type
                                   (mailbox));

    filter =
        bw_get_condition_from_int(libbalsa_mailbox_get_filter(mailbox));
    libbalsa_mailbox_set_view_filter(mailbox, filter, FALSE);
    libbalsa_condition_unref(filter);

    /* scroll may select the message and GtkTreeView does not like selecting
     * without being shown first. */
    balsa_index_scroll_on_open(index);

    g_free(info);

    return FALSE;
}

static void
bw_real_open_mbnode_thread(BalsaWindowRealOpenMbnodeInfo * info)
{
    static GMutex open_lock;
    gint try_cnt;
    LibBalsaMailbox *mailbox = info->mbnode->mailbox;
    GError *err = NULL;
    gboolean successp;

    /* Use a mutex to ensure we open only one mailbox at a time */
    g_mutex_lock(&open_lock);

    try_cnt = 0;
    do {
        g_clear_error(&err);
        successp = libbalsa_mailbox_open(mailbox, &err);
        if (!balsa_app.main_window)
            return;

        if(successp) break;
        if(err && err->code != LIBBALSA_MAILBOX_TOOMANYOPEN_ERROR)
            break;
        balsa_mblist_close_lru_peer_mbx(balsa_app.mblist, mailbox);
    } while(try_cnt++<3);

    if (successp) {
        g_idle_add((GSourceFunc) bw_real_open_mbnode_idle_cb, info);
    } else {
        libbalsa_information(
            LIBBALSA_INFORMATION_ERROR,
            _("Unable to Open Mailbox!\n%s."),
	    err ? err->message : _("Unknown error"));
        if (info->window) {
            balsa_window_decrease_activity(info->window, info->message);
            g_object_remove_weak_pointer(G_OBJECT(info->window),
                                         (gpointer) &info->window);
        }
        g_free(info->message);
        g_object_unref(g_object_ref_sink(info->index));
        g_object_unref(info->mbnode);
        g_free(info);
    }
    g_mutex_unlock(&open_lock);
}

static void
balsa_window_real_open_mbnode(BalsaWindow * window,
                              BalsaMailboxNode * mbnode,
                              gboolean set_current)
{
    BalsaIndex * index;
    gchar *message;
    LibBalsaMailbox *mailbox;
    GThread *open_thread;
    BalsaWindowRealOpenMbnodeInfo *info;

    if (bw_is_open_mailbox(mailbox = mbnode->mailbox))
        return;

    index = BALSA_INDEX(balsa_index_new());
    balsa_index_set_width_preference
        (index,
         (balsa_app.layout_type == LAYOUT_WIDE_SCREEN)
         ? BALSA_INDEX_NARROW : BALSA_INDEX_WIDE);

    message = g_strdup_printf(_("Opening %s"), mailbox->name);
    balsa_window_increase_activity(window, message);

    info = g_new(BalsaWindowRealOpenMbnodeInfo, 1);
    info->window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer) &info->window);
    info->mbnode = g_object_ref(mbnode);
    info->set_current = set_current;
    info->index = index;
    info->message = message;
    open_thread =
    	g_thread_new("bw_real_open_mbnode_thread",
    				 (GThreadFunc) bw_real_open_mbnode_thread,
					 info);
    g_thread_unref(open_thread);
}

/* balsa_window_real_close_mbnode:
   this function overloads libbalsa_mailbox_close_mailbox.

*/
static gboolean
bw_focus_idle(LibBalsaMailbox ** mailbox)
{
    if (*mailbox)
	g_object_remove_weak_pointer(G_OBJECT(*mailbox), (gpointer) mailbox);
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
    GtkWidget *index = NULL;
    gint i;
    LibBalsaMailbox **mailbox;

    g_return_if_fail(mbnode->mailbox);

    i = balsa_find_notebook_page_num(mbnode->mailbox);

    if (i != -1) {
        gtk_notebook_remove_page(GTK_NOTEBOOK(window->notebook), i);
        bw_unregister_open_mailbox(mbnode->mailbox);

        /* If this is the last notebook page clear the message preview
           and the status bar */
        if (balsa_app.notebook
            && gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),
                                         0) == NULL) {
            GtkStatusbar *statusbar;
            guint context_id;

            gtk_window_set_title(GTK_WINDOW(window), "Balsa");
            bw_idle_replace(window, NULL);

            statusbar = GTK_STATUSBAR(window->statusbar);
            context_id = gtk_statusbar_get_context_id(statusbar, "BalsaWindow mailbox");
            gtk_statusbar_pop(statusbar, context_id);
            gtk_statusbar_push(statusbar, context_id, "Mailbox closed");

            /* Disable menus */
            bw_enable_mailbox_menus(window, NULL);
            bw_enable_message_menus(window, 0);
	    if (window->current_index)
		g_object_remove_weak_pointer(G_OBJECT(window->current_index),
					     (gpointer)
					     &window->current_index);
            window->current_index = NULL;

            /* Just in case... */
            g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, NULL);
        }
    }

    index = balsa_window_find_current_index(window);
    mailbox = g_new(LibBalsaMailbox *, 1);
    if (index) {
	*mailbox = BALSA_INDEX(index)->mailbox_node-> mailbox;
	g_object_add_weak_pointer(G_OBJECT(*mailbox), (gpointer) mailbox);
    } else
	*mailbox = NULL;
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
            (delta_time = current_time - index->mailbox_node->last_use) >
            balsa_app.close_mailbox_timeout) {
            if (balsa_app.debug)
                fprintf(stderr, "Closing Page %d unused for %d s\n",
                        i, delta_time);
            balsa_window_real_close_mbnode(window, index->mailbox_node);
            if (i < c)
                c--;
            i--;
        }
    }
    return TRUE;
}

static void
balsa_window_destroy(GObject * object)
{
    BalsaWindow *window;

    window = BALSA_WINDOW(object);
    bw_idle_remove(window);
    /* The preview window seems to get finalized without notification;
     * we no longer need it, so we just drop our pointer: */
    window->preview = NULL;

    if (window->network_changed_source_id != 0) {
        g_source_remove(window->network_changed_source_id);
        window->network_changed_source_id = 0;
    }

    if (G_OBJECT_CLASS(balsa_window_parent_class)->dispose != NULL)
        G_OBJECT_CLASS(balsa_window_parent_class)->dispose(object);

    balsa_unregister_pixmaps();
}


/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow * window)
{
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
	gtk_paned_set_position(GTK_PANED(window->paned_slave),
                                balsa_app.notebook_height);
    } else {
	/* Set the height to something really big (those new hi-res
	   screens and all :) */
	gtk_paned_set_position(GTK_PANED(window->paned_slave), G_MAXINT);
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

	progress_id = g_strdup_printf("POP3: %s", mailbox->name);
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

    if ((info->window != NULL) && !info->window->network_available) {
        return;
    }

    for ( ; mailbox_list; mailbox_list = mailbox_list->next) {
        LibBalsaMailbox *mailbox = BALSA_MAILBOX_NODE(mailbox_list->data)->mailbox;
        LibBalsaMailboxPop3 *pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
        bw_pop_mbox_t *bw_pop_mbox;

        bw_pop_mbox = g_malloc0(sizeof(bw_pop_mbox_t));
        bw_pop_mbox->mailbox = g_object_ref(mailbox);
        libbalsa_mailbox_pop3_set_inbox(mailbox, balsa_app.inbox);
        libbalsa_mailbox_pop3_set_msg_size_limit(pop3, balsa_app.msg_size_limit * 1024);
        if (info->with_progress_dialog) {
        	bw_pop_mbox->notify =
        		g_signal_connect(G_OBJECT(mailbox), "progress-notify", G_CALLBACK(bw_check_mailbox_progress_cb), mailbox);
        }
        bw_pop_mbox->thread = g_thread_new(NULL, (GThreadFunc) bw_check_mailbox, mailbox);
        g_debug("launched thread %p for checking POP3 mailbox %s", bw_pop_mbox->thread, mailbox->name);
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

    if ((mailbox = mbnode->mailbox)) {	/* mailbox, not a folder */
	if (!LIBBALSA_IS_MAILBOX_IMAP(mailbox) ||
	    bw_imap_check_test(mbnode->dir ? mbnode->dir :
			    libbalsa_mailbox_imap_get_path
			    (LIBBALSA_MAILBOX_IMAP(mailbox))))
	    *list = g_slist_prepend(*list, g_object_ref(mailbox));
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

    list = NULL;
    /*  Only Run once -- If already checking mail, return.  */
    if (!g_atomic_int_dec_and_test(&checking_mail)) {
    	g_atomic_int_inc(&checking_mail);
        g_debug("Already Checking Mail!");
        g_mutex_lock(&progress_dialog.mutex);
        if (progress_dialog.dialog != NULL) {
        	gtk_window_present(GTK_WINDOW(progress_dialog.dialog));
        }
        g_mutex_unlock(&progress_dialog.mutex);
        return;
    }

    if (window)
        bw_action_set_enabled(window, "get-new-mail", FALSE);

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

        num_new = mailbox->unread_messages - info->unread_messages;
        if (num_new < 0)
            num_new = 0;
        has_new = mailbox->has_unread_messages - info->has_unread_messages;
        if (has_new < 0)
            has_new = 0;

        if (num_new || has_new)
	    bw_display_new_mail_notification(num_new, has_new);
    }

    info->unread_messages = mailbox->unread_messages;
    info->has_unread_messages = mailbox->has_unread_messages;
}

/* this one is called only in the threaded code */
static void
bw_mailbox_check(LibBalsaMailbox * mailbox, struct check_messages_thread_info *info)
{
    if (libbalsa_mailbox_get_subscribe(mailbox) == LB_MAILBOX_SUBSCRIBE_NO)
        return;

    g_debug("checking mailbox %s", mailbox->name);
    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
    	if ((info->window != NULL) && !info->window->network_available) {
    		return;
    	}

    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), FALSE, INFINITY,
    			_("IMAP mailbox: %s"), mailbox->url);
    	}
    } else if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox)) {
    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), FALSE, INFINITY,
    			_("Local mailbox: %s"), mailbox->name);
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
    	g_slist_foreach(list, (GFunc) g_object_unref, NULL);
    	g_slist_free(list);
    	if (info->with_progress_dialog) {
    		libbalsa_progress_dialog_update(&progress_dialog, _("Mailboxes"), TRUE, 1.0, NULL);
    	}
    }

	if (info->with_activity_bar) {
		balsa_window_decrease_activity(info->window, _("Checking Mail…"));
	}

    g_atomic_int_inc(&checking_mail);

    if (info->window) {
        g_idle_add((GSourceFunc) bw_check_messages_thread_idle_cb,
                   g_object_ref(info->window));
        if (info->window->network_available)
            time(&info->window->last_check_time);
        g_object_unref(info->window);
    }

    g_free(info);
    g_thread_exit(0);
}


/** Returns properly formatted string informing the user about the
    amount of the new mail.
    @param num_new if larger than zero informs that the total number
    of messages is known and trusted.
    @param num_total says how many actually new messages are in the
    mailbox.
*/
static gchar*
bw_get_new_message_notification_string(int num_new, int num_total)
{
    return num_new > 0 ?
	g_strdup_printf(ngettext("You have received %d new message.",
				 "You have received %d new messages.",
				 num_total), num_total) :
	g_strdup(_("You have new mail."));
}

/** Informs the user that new mail arrived. num_new is the number of
    the recently arrived messsages.
*/
static void
bw_display_new_mail_notification(int num_new, int has_new)
{
    GtkWindow *window = GTK_WINDOW(balsa_app.main_window);
    static GtkWidget *dlg = NULL;
    static gint num_total = 0;
    gchar *msg = NULL;

    if (num_new <= 0 && has_new <= 0)
        return;

    if (!gtk_window_is_active(window))
        gtk_window_set_urgency_hint(window, TRUE);

    if (!balsa_app.notify_new_mail_dialog)
        return;

#ifdef HAVE_NOTIFY
    /* Before attemtping to use the notifications check whether they
       are actually available - perhaps the underlying connection to
       dbus could not be created? In any case, we must not continue or
       ugly things will happen, at least with libnotify-0.4.2. */
    if (notify_is_initted()) {
        if (gtk_window_is_active(window))
            return;

        if (balsa_app.main_window->new_mail_note) {
            /* the user didn't acknowledge the last info, so we'll
             * accumulate the count */
            num_total += num_new;
        } else {
            num_total = num_new;
#if HAVE_NOTIFY >=7
            balsa_app.main_window->new_mail_note =
                notify_notification_new("Balsa", NULL, NULL);
            notify_notification_set_hint(balsa_app.main_window->
                                         new_mail_note, "desktop-entry",
                                         g_variant_new_string("balsa"));
#else
            balsa_app.main_window->new_mail_note =
                notify_notification_new("Balsa", NULL, NULL, NULL);
#endif
            g_object_add_weak_pointer(G_OBJECT(balsa_app.main_window->
                                               new_mail_note),
                                      (gpointer) & balsa_app.main_window->
                                      new_mail_note);
            g_signal_connect(balsa_app.main_window->new_mail_note,
                             "closed", G_CALLBACK(g_object_unref), NULL);
        }
    } else {
        if (dlg) {
            /* the user didn't acknowledge the last info, so we'll
             * accumulate the count */
            num_total += num_new;
            gtk_window_present(GTK_WINDOW(dlg));
        } else {
            num_total = num_new;
            dlg = gtk_message_dialog_new(NULL, /* NOT transient for
                                                * Balsa's main window */
                    (GtkDialogFlags) 0,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK, "%s", msg);
            gtk_window_set_title(GTK_WINDOW(dlg), _("Balsa: New mail"));
            gtk_window_set_role(GTK_WINDOW(dlg), "new_mail_dialog");
            gtk_window_set_type_hint(GTK_WINDOW(dlg),
                    GDK_WINDOW_TYPE_HINT_NORMAL);
            g_signal_connect(G_OBJECT(dlg), "response",
                    G_CALLBACK(gtk_widget_destroy), NULL);
            g_object_add_weak_pointer(G_OBJECT(dlg), (gpointer) & dlg);
            gtk_widget_show_all(GTK_WIDGET(dlg));
        }
    }

    msg = bw_get_new_message_notification_string(num_new, num_total);
    if (balsa_app.main_window->new_mail_note) {
        notify_notification_update(balsa_app.main_window->new_mail_note,
                                   "Balsa", msg, "dialog-information");
        /* 30 seconds: */
        notify_notification_set_timeout(balsa_app.main_window->
                                        new_mail_note, 30000);
        notify_notification_show(balsa_app.main_window->new_mail_note,
                                 NULL);
    } else
        gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg), msg);
#else
    if (dlg) {
        /* the user didn't acknowledge the last info, so we'll
         * accumulate the count */
        num_total += num_new;
        gtk_window_present(GTK_WINDOW(dlg));
    } else {
        num_total = num_new;
        dlg = gtk_message_dialog_new(NULL, /* NOT transient for
                                            * Balsa's main window */
                                     (GtkDialogFlags) 0,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_OK, "%s", msg);
        gtk_window_set_title(GTK_WINDOW(dlg), _("Balsa: New mail"));
        gtk_window_set_role(GTK_WINDOW(dlg), "new_mail_dialog");
        gtk_window_set_type_hint(GTK_WINDOW(dlg),
                                 GDK_WINDOW_TYPE_HINT_NORMAL);
        g_signal_connect(G_OBJECT(dlg), "response",
                         G_CALLBACK(gtk_widget_destroy), NULL);
        g_object_add_weak_pointer(G_OBJECT(dlg), (gpointer) & dlg);
        gtk_widget_show_all(GTK_WIDGET(dlg));
    }

    msg = bw_get_new_message_notification_string(num_new, num_total);
    gtk_message_dialog_set_markup(GTK_MESSAGE_DIALOG(dlg), msg);
#endif
    g_free(msg);
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

    if ((mailbox = mbnode->mailbox)) {  /* mailbox, not a folder */
        if (LIBBALSA_IS_MAILBOX_IMAP(mailbox) &&
            bw_imap_check_test(mbnode->dir ? mbnode->dir :
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
    BalsaWindow *window = user_data;

    if (can_reach &&
        difftime(time(NULL), window->last_check_time) >
        balsa_app.check_mail_timer * 60) {
        /* Check the mail now, and reset the timer */
        bw_check_new_messages(window);
    }

    g_object_unref(window);
}

static gboolean
bw_change_connection_status_idle(gpointer user_data)
{
    BalsaWindow *window = user_data;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    window->network_changed_source_id = 0;

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
                           (GtkTreeModelForeachFunc)
                           mw_mbox_change_connection_status, NULL);

    if (!window->network_available)
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
    if ((mailbox = mbnode->mailbox) == NULL)
        return FALSE;

    libbalsa_mailbox_test_can_reach(mailbox, bw_change_connection_status_can_reach_cb,
                                    g_object_ref(window));

    return FALSE;
}

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    g_return_val_if_fail(window != NULL, NULL);

    return window->current_index;
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
#if !GTK_CHECK_VERSION(3, 22, 0)
        GdkScreen *screen;
#endif /* GTK_CHECK_VERSION(3, 22, 0) */

#if HAVE_MACOSX_DESKTOP
	libbalsa_macosx_menu_for_parent(dia, GTK_WINDOW(window));
#endif
        vbox = gtk_dialog_get_content_area(GTK_DIALOG(dia));

	page=gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(page), 2);
        gtk_grid_set_column_spacing(GTK_GRID(page), 2);
	gtk_container_set_border_width(GTK_CONTAINER(page), 6);
	w = gtk_label_new_with_mnemonic(_("_Search for:"));
        gtk_widget_set_hexpand(w, TRUE);
	gtk_grid_attach(GTK_GRID(page), w, 0, 0, 1, 1);
	search_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(search_entry), 30);
        gtk_widget_set_hexpand(search_entry, TRUE);
	gtk_grid_attach(GTK_GRID(page),search_entry,1, 0, 1, 1);
	gtk_label_set_mnemonic_widget(GTK_LABEL(w), search_entry);
	gtk_box_pack_start(GTK_BOX(vbox), page, FALSE, FALSE, 2);

	/* builds the toggle buttons to specify fields concerned by
         * the search. */

	frame = gtk_frame_new(_("In:"));
	gtk_frame_set_label_align(GTK_FRAME(frame),
				  GTK_POS_LEFT, GTK_POS_TOP);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

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
	gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	/* Button box */
	box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_container_set_border_width(GTK_CONTAINER(box), 6);
	button = gtk_button_new_with_mnemonic(_("_Apply"));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(FIND_RESPONSE_FILTER));
	gtk_container_add(GTK_CONTAINER(box), button);
	button = gtk_button_new_with_mnemonic(_("_Clear"));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(FIND_RESPONSE_RESET));
	gtk_container_add(GTK_CONTAINER(box), button);
	gtk_container_add(GTK_CONTAINER(frame), box);

	/* Frame with OK button */
	frame = gtk_frame_new(_("Open next matching message"));
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	/* Reverse and Wrap checkboxes */
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_add(GTK_CONTAINER(frame), box);
	w = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_set_homogeneous(GTK_BOX(w), TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(w), 6);
	reverse_button =
            gtk_check_button_new_with_mnemonic(_("_Reverse search"));
	gtk_box_pack_start(GTK_BOX(w), reverse_button, TRUE, TRUE, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_button),
                                     reverse);
	wrap_button =
            gtk_check_button_new_with_mnemonic(_("_Wrap around"));
	gtk_box_pack_start(GTK_BOX(w), wrap_button, TRUE, TRUE, 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_button),
                                     wrap);
	gtk_box_pack_start(GTK_BOX(box), w, TRUE, TRUE, 0);

	button = gtk_button_new_with_mnemonic(_("_OK"));
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(bw_find_button_clicked),
			 GINT_TO_POINTER(GTK_RESPONSE_OK));
        gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);

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
#if GTK_CHECK_VERSION(3, 22, 0)
                gtk_show_uri_on_window(GTK_WINDOW(window),
                                       "help:balsa/win-search",
                                       gtk_get_current_event_time(), &err);
#else /* GTK_CHECK_VERSION(3, 22, 0) */
                screen = gtk_widget_get_screen(GTK_WIDGET(window));
                gtk_show_uri(screen, "help:balsa/win-search",
                             gtk_get_current_event_time(), &err);
#endif /* GTK_CHECK_VERSION(3, 22, 0) */
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
                BALSA_INDEX(bindex)->mailbox_node->mailbox;
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
    LibBalsaCondition *filter, *flag_filter;
    gint i;

    flag_filter = bw_get_condition_from_int(bw_filter_to_int(window));

    /* add string filter on top of that */

    i = gtk_combo_box_get_active(GTK_COMBO_BOX(window->filter_choice));
    if (i >= 0) {
        const gchar *str = gtk_entry_get_text(GTK_ENTRY(window->sos_entry));
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

    mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
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
bw_reset_filter(BalsaWindow * bw)
{
    BalsaIndex *bindex = BALSA_INDEX(balsa_window_find_current_index(bw));

    /* do it by resetting the sos filder */
    gtk_entry_set_text(GTK_ENTRY(bw->sos_entry), "");
    bw_set_view_filter(bw, bindex->filter_no, bw->sos_entry);
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

	g_error_free(err);
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
bw_show_mbtree(BalsaWindow * bw)
{
    GtkWidget *parent;

    parent = gtk_widget_get_ancestor(bw->mblist, GTK_TYPE_PANED);
    while (gtk_orientable_get_orientation(GTK_ORIENTABLE(parent)) !=
           GTK_ORIENTATION_HORIZONTAL) {
        parent = gtk_widget_get_ancestor(parent, GTK_TYPE_PANED);
    }

    if (balsa_app.show_mblist) {
        gtk_widget_show(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), balsa_app.mblist_width);
    } else {
        gtk_widget_hide(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), 0);
    }
}

void
balsa_change_window_layout(BalsaWindow *window)
{

    g_object_ref(window->notebook);
    g_object_ref(window->mblist);
    g_object_ref(window->preview);

    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(window->notebook)),
                         window->notebook);
    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(window->mblist)),
                         window->mblist);
    gtk_container_remove(GTK_CONTAINER
                         (gtk_widget_get_parent(window->preview)),
                         window->preview);

    bw_set_panes(window);

    g_object_unref(window->notebook);
    g_object_unref(window->mblist);
    g_object_unref(window->preview);

    gtk_paned_set_position(GTK_PANED(window->paned_master),
                           balsa_app.show_mblist ?
                           balsa_app.mblist_width : 0);
    gtk_widget_show(window->paned_slave);
    gtk_widget_show(window->paned_master);

}

/* PKGW: remember when they change the position of the vpaned. */
static void
bw_slave_position_cb(GtkPaned   * paned_slave,
                     GParamSpec * pspec,
                     gpointer     user_data)
{
    if (balsa_app.previewpane)
        balsa_app.notebook_height =
            gtk_paned_get_position(paned_slave);
}

    static void
bw_size_allocate_cb(GtkWidget * window, GtkAllocation * alloc)
{
    gtk_window_get_size(GTK_WINDOW(window),
                        & balsa_app.mw_width,
                        & balsa_app.mw_height);
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
    GtkWidget *page;
    BalsaIndex *index;
    LibBalsaMailbox *mailbox;
    gchar *title;

    if (window->current_index) {
	g_object_remove_weak_pointer(G_OBJECT(window->current_index),
				     (gpointer) &window->current_index);
	/* Note when this mailbox was hidden, for use in auto-closing. */
	time(&BALSA_INDEX(window->current_index)->mailbox_node->last_use);
        window->current_index = NULL;
    }

    if (!balsa_app.mblist_tree_store)
        /* Quitt'n time! */
        return;

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page_num);
    index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));

    window->current_index = GTK_WIDGET(index);
    g_object_add_weak_pointer(G_OBJECT(index),
			      (gpointer) &window->current_index);
    /* Note when this mailbox was exposed, for use in auto-expunge. */
    time(&index->mailbox_node->last_use);

    mailbox = index->mailbox_node->mailbox;
    if (mailbox->name) {
        if (mailbox->readonly) {
            title =
                g_strdup_printf(_("Balsa: %s (read-only)"), mailbox->name);
        } else {
            title = g_strdup_printf(_("Balsa: %s"), mailbox->name);
        }
        gtk_window_set_title(GTK_WINDOW(window), title);
        g_free(title);
    } else {
        gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    }

    g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, index);
    bw_idle_replace(window, index);
    bw_enable_message_menus(window, index->current_msgno);
    bw_enable_mailbox_menus(window, index);

    gtk_entry_set_text(GTK_ENTRY(window->sos_entry),
                       index->filter_string ? index->filter_string : "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(window->filter_choice),
                             index->filter_no);

    balsa_mblist_focus_mailbox(balsa_app.mblist, mailbox);
    balsa_window_set_statusbar(window, mailbox);

    balsa_index_refresh_date(index);
    balsa_index_refresh_size(index);
    balsa_index_ensure_visible(index);

    g_free(balsa_app.current_mailbox_url);
    balsa_app.current_mailbox_url = g_strdup(mailbox->url);
}

static void
bw_index_changed_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *window = data;
    BalsaIndex *index;
    guint current_msgno;

    if (widget != window->current_index)
        return;

    index = BALSA_INDEX(widget);
    bw_enable_message_menus(window, index->current_msgno);
    bw_enable_mailbox_menus(window, index);

    current_msgno = BALSA_MESSAGE(window->preview)->message ?
        BALSA_MESSAGE(window->preview)->message->msgno : 0;

    if (current_msgno != index->current_msgno)
        bw_idle_replace(window, index);
}

static void
bw_idle_replace(BalsaWindow * window, BalsaIndex * bindex)
{
    if (balsa_app.previewpane) {
        bw_idle_remove(window);
        /* Skip if the window is being destroyed: */
        if (window->preview != NULL) {
            window->set_message_id = g_idle_add((GSourceFunc) bw_idle_cb, window);
            if (BALSA_MESSAGE(window->preview)->message != NULL)
                gtk_widget_hide(window->preview);
        }
    }
}

static void
bw_idle_remove(BalsaWindow * window)
{
    if (window->set_message_id) {
        g_source_remove(window->set_message_id);
        window->set_message_id = 0;
    }
}


static volatile gboolean bw_idle_cb_active = FALSE;

static gboolean
bw_idle_cb(BalsaWindow * window)
{
    BalsaIndex *index;

    if (window->set_message_id == 0) {
        return FALSE;
    }
    if (bw_idle_cb_active) {
	return TRUE;
    }
    bw_idle_cb_active = TRUE;

    window->set_message_id = 0;

    index = (BalsaIndex *) window->current_index;
    if (index)
        balsa_message_set(BALSA_MESSAGE(window->preview),
                          index->mailbox_node->mailbox,
                          index->current_msgno);
    else
        balsa_message_set(BALSA_MESSAGE(window->preview), NULL, 0);

    index = g_object_get_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS);
    if (index) {
        gtk_widget_grab_focus(GTK_WIDGET(index));
        g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, NULL);
    }

    bw_idle_cb_active = FALSE;

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
                             guint32 time, gpointer data)
{
    BalsaIndex* index;
    LibBalsaMailbox* mailbox;
    BalsaIndex *orig_index;
    GArray *selected;
    LibBalsaMailbox* orig_mailbox;

    if (!selection_data)
	/* Drag'n'drop is weird... */
	return;

    orig_index =
        *(BalsaIndex **) gtk_selection_data_get_data(selection_data);
    selected = balsa_index_selected_msgnos_new(orig_index);
    if (selected->len == 0) {
        /* it is actually possible to drag from GtkTreeView when no rows
         * are selected: Disable preview for that. */
        balsa_index_selected_msgnos_free(orig_index, selected);
        return;
    }

    orig_mailbox = orig_index->mailbox_node->mailbox;

    index = bw_notebook_find_page (GTK_NOTEBOOK(widget), x, y);

    if (index == NULL)
        return;

    mailbox = index->mailbox_node->mailbox;

    if (mailbox != NULL && mailbox != orig_mailbox)
        balsa_index_transfer(orig_index, selected, mailbox,
                             gdk_drag_context_get_selected_action(context) != GDK_ACTION_MOVE);
    balsa_index_selected_msgnos_free(orig_index, selected);
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

/* bw_progress_timeout
 *
 * This function is called at a preset interval to cause the progress
 * bar to move in activity mode.
 *
 * Use of the progress bar to show a fraction of a task takes priority.
 **/
static gint
bw_progress_timeout(BalsaWindow ** window)
{
    if (balsa_app.show_statusbar
        && *window && (*window)->progress_type == BALSA_PROGRESS_ACTIVITY)
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR((*window)->progress_bar));

    /* return true so it continues to be called */
    return *window != NULL;
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
    static BalsaWindow *window_save = NULL;

    if (!window_save) {
        window_save = window;
        g_object_add_weak_pointer(G_OBJECT(window_save),
                                  (gpointer) &window_save);
    }

    if (!window->activity_handler)
        /* add a timeout to make the activity bar move */
        window->activity_handler =
            g_timeout_add(50, (GSourceFunc) bw_progress_timeout,
                          &window_save);

    /* increment the reference counter */
    ++window->activity_counter;
    if (window->progress_type == BALSA_PROGRESS_NONE)
        window->progress_type = BALSA_PROGRESS_ACTIVITY;

    if (window->progress_type == BALSA_PROGRESS_ACTIVITY)
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(window->progress_bar),
                                  message);
    window->activity_messages =
        g_slist_prepend(window->activity_messages, g_strdup(message));
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
    GSList *link;
    GtkProgressBar *progress_bar;

    link = g_slist_find_custom(window->activity_messages, message,
                               (GCompareFunc) strcmp);
    g_free(link->data);
    window->activity_messages =
        g_slist_delete_link(window->activity_messages, link);

    progress_bar = GTK_PROGRESS_BAR(window->progress_bar);
    if (window->progress_type == BALSA_PROGRESS_ACTIVITY)
        gtk_progress_bar_set_text(progress_bar,
                                  window->activity_messages ?
                                  window->activity_messages->data : NULL);

    /* decrement the counter if positive */
    if (window->activity_counter > 0 && --window->activity_counter == 0) {
        /* clear the bar and make it available for others to use */
        g_source_remove(window->activity_handler);
        window->activity_handler = 0;
        if (window->progress_type == BALSA_PROGRESS_ACTIVITY) {
            window->progress_type = BALSA_PROGRESS_NONE;
            gtk_progress_bar_set_fraction(progress_bar, 0);
        }
    }
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

typedef struct {
    GtkProgressBar *progress_bar;
    gchar          *text;
} BalsaWindowSetupProgressInfo;

static gboolean
bw_setup_progress_idle_cb(BalsaWindowSetupProgressInfo * info)
{
    gtk_progress_bar_set_text(info->progress_bar, info->text);
    gtk_progress_bar_set_fraction(info->progress_bar, 0);

    g_object_unref(info->progress_bar);
    g_free(info->text);
    g_free(info);

    return FALSE;
}

gboolean
balsa_window_setup_progress(BalsaWindow * window, const gchar * text)
{
    BalsaWindowSetupProgressInfo *info;

    if (text) {
        /* make sure the progress bar is currently unused */
        if (window->progress_type == BALSA_PROGRESS_INCREMENT)
            return FALSE;
        window->progress_type = BALSA_PROGRESS_INCREMENT;
    } else
        window->progress_type = BALSA_PROGRESS_NONE;

    /* Update the display in an idle callback, in case we were called in
     * a sub-thread.*/
    info = g_new(BalsaWindowSetupProgressInfo, 1);
    info->progress_bar = GTK_PROGRESS_BAR(g_object_ref(window->progress_bar));
    info->text = g_strdup(text);
    g_idle_add((GSourceFunc) bw_setup_progress_idle_cb, info);

    return TRUE;
}

/* balsa_window_increment_progress
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
balsa_window_increment_progress(BalsaWindow * window, gdouble fraction,
                                gboolean flush)
{
    /* make sure the progress bar is being incremented */
    if (window->progress_type != BALSA_PROGRESS_INCREMENT)
        return;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(window->progress_bar),
                                  fraction);

    if (flush)
        while (gtk_events_pending())
            gtk_main_iteration_do(FALSE);
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
    gint i = balsa_find_notebook_page_num(mbnode->mailbox);
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
    gint total_messages = libbalsa_mailbox_total_messages(mailbox);
    gint unread_messages = mailbox->unread_messages;
    gint hidden_messages;
    GString *desc = g_string_new(NULL);
    GtkStatusbar *statusbar;
    guint context_id;

    hidden_messages =
        mailbox->msg_tree ? total_messages -
        (g_node_n_nodes(mailbox->msg_tree, G_TRAVERSE_ALL) - 1) : 0;

    /* xgettext: this is the first part of the message
     * "Shown mailbox: %s with %d messages, %d new, %d hidden". */
    g_string_append_printf(desc, _("Shown mailbox: %s "), mailbox->name);
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

    statusbar = GTK_STATUSBAR(window->statusbar);
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
    LibBalsaMailbox *mailbox = index ? index->mailbox_node->mailbox : NULL;

    if (libbalsa_mailbox_get_unread(mailbox) > 0) {
        if (!balsa_index_select_next_unread(index)) {
            /* All unread messages must be hidden; we assume that the
             * user wants to see them, and try again. */
            bw_reset_filter(window);
            balsa_index_select_next_unread(index);
        }
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
                                   mailbox->name);
#if HAVE_MACOSX_DESKTOP
        libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(window));
#endif
        gtk_message_dialog_format_secondary_text
            (GTK_MESSAGE_DIALOG(dialog),
             _("Do you want to select %s?"), mailbox->name);
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
