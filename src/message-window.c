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
#include "message-window.h"

#include "application-helpers.h"
#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "main-window.h"
#include "sendmsg-window.h"
#include "print.h"
#include "mailbox-node.h"

#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

struct _MessageWindow {
    GtkWidget *window;

    GtkWidget *bmessage;
    GtkWidget *toolbar;

    LibBalsaMailbox *mailbox;
    LibBalsaMessage *message;
    BalsaIndex *bindex;
    int headers_shown;
    int show_all_headers;
    guint idle_handler_id;
};

/*
 * GAction helpers
 */

/*
 * Enable a GAction
 */

static void
mw_set_enabled(MessageWindow * mw, const gchar * action_name,
               gboolean enabled)
{
    GAction *action =
        g_action_map_lookup_action(G_ACTION_MAP(mw->window), action_name);

    if (action)
        g_simple_action_set_enabled(G_SIMPLE_ACTION(action), enabled);
    else
        g_print("%s action “%s” not found\n", __func__, action_name);
}

/*
 * Set the state of a toggle GAction
 */
static void
mw_set_active(MessageWindow * mw,
              const gchar   * action_name,
              gboolean        state)
{
    GAction *action =
        g_action_map_lookup_action(G_ACTION_MAP(mw->window), action_name);

    if (action)
        g_action_change_state(action, g_variant_new_boolean(state));
    else
        g_print("%s action “%s” not found\n", __func__, action_name);
}

/*
 * end of GAction helpers
 */

static void
mw_set_part_buttons_sensitive(MessageWindow * mw, BalsaMessage * msg)
{
    if (!msg || !msg->treeview)
	return;

    mw_set_enabled(mw, "next-part",
                   balsa_message_has_next_part(msg));
    mw_set_enabled(mw, "previous-part",
                   balsa_message_has_previous_part(msg));
}

static gboolean
message_window_idle_handler(MessageWindow * mw)
{
    BalsaMessage *msg;

    mw->idle_handler_id = 0;

    msg = BALSA_MESSAGE(mw->bmessage);
    if (!balsa_message_set(msg, mw->message->mailbox, mw->message->msgno)) {
        gtk_widget_destroy(mw->window);
        return FALSE;
    }
    balsa_message_grab_focus(msg);

    return FALSE;
}

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

/* Standard buttons; "" means a separator. */
static const BalsaToolbarEntry message_toolbar[] = {
    { "next-unread",      BALSA_PIXMAP_NEXT_UNREAD   },
    { "", ""                                         },
    { "reply",            BALSA_PIXMAP_REPLY         },
    { "reply-all",        BALSA_PIXMAP_REPLY_ALL     },
    { "reply-group",      BALSA_PIXMAP_REPLY_GROUP   },
    { "forward-attached", BALSA_PIXMAP_FORWARD       },
    { "", ""                                         },
    { "previous-part",    BALSA_PIXMAP_PREVIOUS_PART },
    { "next-part",        BALSA_PIXMAP_NEXT_PART     },
    { "save-part",       "document-save"             },
    { "", ""                                         },
    { "print",           "document-print"            },
    { "", ""                                         },
    { "move-to-trash",   "edit-delete"               }
};

/* Optional extra buttons */
static const BalsaToolbarEntry message_toolbar_extras[] = {
    { "previous-message", BALSA_PIXMAP_PREVIOUS      },
    { "next-message",     BALSA_PIXMAP_NEXT          },
    { "next-flagged",     BALSA_PIXMAP_NEXT_FLAGGED  },
    { "previous-part",    BALSA_PIXMAP_PREVIOUS_PART },
    { "close",           "window-close-symbolic"     },
    { "show-all-headers", BALSA_PIXMAP_SHOW_HEADERS  }
};

/* Create the toolbar model for the message window's toolbar.
 */

BalsaToolbarModel *
message_window_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;

    if (model)
        return model;

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_MESSAGE_WINDOW,
                                message_toolbar,
                                G_N_ELEMENTS(message_toolbar));
    balsa_toolbar_model_add_entries(model, message_toolbar_extras,
                                    G_N_ELEMENTS(message_toolbar_extras));

    return model;
}

/*
 * end of UI definitions and functions
 */

#define BALSA_MESSAGE_WINDOW_KEY "balsa-message-window"

static void
mw_bindex_closed_cb(gpointer data, GObject *bindex)
{
    MessageWindow *mw = data;
    mw->bindex = NULL;
    gtk_widget_destroy(mw->window);
}

static void
mw_disable_trash(MessageWindow * mw)
{
    mw_set_enabled(mw, "move-to-trash", FALSE);
}

/*
 * GAction callbacks for toggle and radio buttons
 */

static void
mw_show_toolbar_change_state(GSimpleAction * action,
                             GVariant      * state,
                             gpointer        data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_app.show_message_toolbar = g_variant_get_boolean(state);
    if (balsa_app.show_message_toolbar)
        gtk_widget_show(mw->toolbar);
    else
        gtk_widget_hide(mw->toolbar);

    g_simple_action_set_state(action, state);
}

static void
mw_wrap_change_state(GSimpleAction * action,
                     GVariant      * state,
                     gpointer        data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_set_wrap(BALSA_MESSAGE(mw->bmessage),
			   g_variant_get_boolean(state));

    g_simple_action_set_state(action, state);
}

static void
mw_reset_show_all_headers(MessageWindow * mw)
{
    if (mw->show_all_headers) {
        mw_set_active(mw, "show-all-headers", FALSE);
        mw->show_all_headers = FALSE;
    }
}

static void
mw_header_change_state(GSimpleAction * action,
                       GVariant      * state,
                       gpointer        data)
{
    MessageWindow *mw = (MessageWindow *) data;
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

    mw->headers_shown = sh;
    mw_reset_show_all_headers(mw);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage), sh);

    g_simple_action_set_state(action, state);
}

static void
mw_show_all_headers_change_state(GSimpleAction * action,
                                 GVariant      * state,
                                 gpointer        data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw->show_all_headers = g_variant_get_boolean(state);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
                                        mw->show_all_headers ?
                                        HEADERS_ALL : mw->headers_shown);

    g_simple_action_set_state(action, state);
}

/*
 * end of GAction callbacks for toggle and radio buttons
 */

static void
mw_menubar_foreach(GtkWidget *widget, gpointer data)
{
    GtkWidget **move_menu = data;
    GtkMenuItem *item = GTK_MENU_ITEM(widget);

    if (strcmp(gtk_menu_item_get_label(item), _("M_ove")) == 0)
        *move_menu = widget;
}

static void
mw_set_buttons_sensitive(MessageWindow * mw)
{
    LibBalsaMailbox *mailbox = mw->message->mailbox;
    BalsaIndex *index = mw->bindex;
    guint current_msgno = mw->message->msgno;
    gboolean enable;

    if (!mailbox) {
        gtk_widget_destroy(mw->window);
        return;
    }

    enable = index && balsa_index_next_msgno(index, current_msgno) > 0;
    mw_set_enabled(mw, "next-message", enable);

    enable = index && balsa_index_previous_msgno(index, current_msgno) > 0;
    mw_set_enabled(mw, "previous-message", enable);

    enable = index && index->mailbox_node->mailbox->unread_messages > 0;
    mw_set_enabled(mw, "next-unread", enable);

    enable = index
        && libbalsa_mailbox_total_messages(index->mailbox_node->mailbox) >
        0;
    mw_set_enabled(mw, "next-flagged", enable);
}

static void
mw_set_message(MessageWindow * mw, LibBalsaMessage * message)
{
    if (message == NULL) {
	libbalsa_clear_source_id(&mw->idle_handler_id);
    }

    if (mw->message) {
        g_object_set_data(G_OBJECT(mw->message), BALSA_MESSAGE_WINDOW_KEY, NULL);
        g_object_unref(mw->message);
    }

    mw->message = message;

    if (message) {
        g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_WINDOW_KEY, mw);
        if (mw->idle_handler_id == 0U)
            mw->idle_handler_id =
                g_idle_add((GSourceFunc) message_window_idle_handler, mw);
        mw_set_buttons_sensitive(mw);
    }
}

/* Handler for the "destroy" signal for mw->window. */
static void
destroy_message_window(GtkWidget * widget, MessageWindow * mw)
{
    if (mw->bindex) {           /* BalsaIndex still exists */
        g_object_weak_unref(G_OBJECT(mw->bindex), mw_bindex_closed_cb, mw);
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->bindex),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
        mw->bindex = NULL;
    }

    if (mw->mailbox) {
        g_object_remove_weak_pointer(G_OBJECT(mw->mailbox), (gpointer) &mw->mailbox);
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->mailbox),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
        mw->mailbox = NULL;
    }

    if (mw->bmessage)
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->bmessage),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);

    mw_set_message(mw, NULL);

    g_free(mw);
}

/* Handler for the mailbox's "message-expunged" signal */
static void
mw_expunged_cb(LibBalsaMailbox * mailbox, guint msgno, MessageWindow * mw)
{
    if (mw->message && (guint) mw->message->msgno == msgno)
        gtk_widget_destroy(mw->window);
}

static void
mw_reply_activated(GSimpleAction * action, GVariant * parameter,
                   gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY);
}

static void
mw_reply_all_activated(GSimpleAction * action, GVariant * parameter,
                       gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY_ALL);
}

static void
mw_reply_group_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY_GROUP);
}

static void
mw_forward_attached_activated(GSimpleAction * action, GVariant * parameter,
                              gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno, TRUE);
}

static void
mw_forward_inline_activated(GSimpleAction * action, GVariant * parameter,
                            gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno,
                           FALSE);
}

#if 0
static void
mw_forward_default_activated(GSimpleAction * action, GVariant * parameter,
                             gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno,
                           balsa_app.forward_attached);
}
#endif

static void
mw_next_part_activated(GSimpleAction * action, GVariant * parameter,
                       gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_next_part(BALSA_MESSAGE(mw->bmessage));
}

static void
mw_previous_part_activated(GSimpleAction * action, GVariant * parameter,
                           gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_previous_part(BALSA_MESSAGE(mw->bmessage));
}

static void
mw_save_part_activated(GSimpleAction * action, GVariant * parameter,
                       gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_save_current_part(BALSA_MESSAGE(mw->bmessage));
}

static void
mw_view_source_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    libbalsa_show_message_source(balsa_app.application,
                                 mw->message, balsa_app.message_font,
                                 &balsa_app.source_escape_specials,
                                 &balsa_app.source_width,
                                 &balsa_app.source_height);
}

static void
mw_close_activated(GSimpleAction * action, GVariant * parameter,
                   gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    gtk_widget_destroy(mw->window);
}

static void
size_alloc_cb(GtkWidget * window)
{
    GdkWindow *gdk_window;

    gdk_window = gtk_widget_get_window(window);
    if (gdk_window == NULL)
        return;

    balsa_app.message_window_maximized =
        (gdk_window_get_state(gdk_window) &
         (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) != 0;

    if (!balsa_app.message_window_maximized)
        gtk_window_get_size(GTK_WINDOW(window),
                            & balsa_app.message_window_width,
                            & balsa_app.message_window_height);
}

static void
mw_copy_activated(GSimpleAction * action, GVariant * parameter,
                  gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    guint signal_id;
    GtkWidget *focus_widget = gtk_window_get_focus(GTK_WINDOW(mw->window));

    signal_id = g_signal_lookup("copy-clipboard",
                                G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
}

static void
mw_select_text_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_window_select_all(GTK_WINDOW(mw->window));
}

static void
mw_find_in_message_activated(GSimpleAction * action, GVariant * parameter,
                             gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_find_in_message(BALSA_MESSAGE(mw->bmessage));
}

static void
mw_set_selected(MessageWindow * mw, void (*select_func) (BalsaIndex *))
{
    guint msgno;
    LibBalsaMessage *message;
    MessageWindow *tmp;

    balsa_index_set_next_msgno(mw->bindex, mw->message->msgno);
    select_func(mw->bindex);
    msgno = balsa_index_get_next_msgno(mw->bindex);
    message = libbalsa_mailbox_get_message(mw->message->mailbox, msgno);
    if (!message)
        return;

    if ((tmp = g_object_get_data(G_OBJECT(message),
                                 BALSA_MESSAGE_WINDOW_KEY))) {
        if (tmp == mw) {
            gtk_window_present(GTK_WINDOW(tmp->window));
            g_object_unref(message);
            return;
        }
        /* Close the other window */
        gtk_widget_destroy(tmp->window);
    }

    mw_set_message(mw, message);
}

static void
message_window_move_message(MessageWindow * mw, LibBalsaMailbox * mailbox)
{
    GArray *messages;
    LibBalsaMessage *original = mw->message;

    g_return_if_fail(mailbox != NULL);

    /* Transferring to same mailbox? */
    if (mw->message->mailbox == mailbox)
        return;

    messages = g_array_new(FALSE, FALSE, sizeof(guint));
    g_array_append_val(messages, mw->message->msgno);

    if (balsa_app.mw_action_after_move == NEXT_UNREAD)
        /* Try selecting the next unread message. */
        mw_set_selected(mw, ((void (*)(BalsaIndex *))
                             balsa_index_select_next_unread));
    else if (balsa_app.mw_action_after_move == NEXT)
        mw_set_selected(mw, balsa_index_select_next);

    balsa_index_transfer(mw->bindex, messages, mailbox, FALSE);
    g_array_free(messages, TRUE);

    if (mw->message == original)
        /* Either action-after-move was CLOSE, or we failed to select
         * another message; either way, we close the window. */
        gtk_widget_destroy(mw->window);
}

static void
mw_mru_menu_cb(gchar * url, gpointer data)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);
    MessageWindow *mw = data;

    message_window_move_message(mw, mailbox);
}

static void
mw_next_message_activated(GSimpleAction * action, GVariant * parameter,
                          gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw_set_selected(mw, balsa_index_select_next);
}

static void
mw_previous_message_activated(GSimpleAction * action, GVariant * parameter,
                              gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw_set_selected(mw, balsa_index_select_previous);
}

static void
mw_next_unread_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw_set_selected(mw, ((void (*)(BalsaIndex *))
                         balsa_index_select_next_unread));
}

static void
mw_next_flagged_activated(GSimpleAction * action, GVariant * parameter,
                          gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw_set_selected(mw, balsa_index_select_next_flagged);
}


static void
mw_page_setup_activated(GSimpleAction * action, GVariant * parameter,
                        gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    message_print_page_setup(GTK_WINDOW(mw->window));
}


static void
mw_print_activated(GSimpleAction * action, GVariant * parameter,
                   gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    message_print(mw->message, GTK_WINDOW(mw->window));
}

static void
mw_move_to_trash_activated(GSimpleAction * action, GVariant * parameter,
                           gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    message_window_move_message(mw, balsa_app.trash);
}

#ifdef HAVE_HTML_WIDGET
static void
mw_zoom_in_activated(GSimpleAction * action, GVariant * parameter,
                     gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), 1);
}

static void
mw_zoom_out_activated(GSimpleAction * action, GVariant * parameter,
                      gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), -1);
}

static void
mw_zoom_normal_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), 0);
}
#endif                          /* HAVE_HTML_WIDGET */

static void
mw_select_part_cb(BalsaMessage * bm, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    gchar *title;
    gchar *from;
#ifdef HAVE_HTML_WIDGET
    gboolean enable = bm && balsa_message_can_zoom(bm);

    mw_set_enabled(mw, "zoom-in", enable);
    mw_set_enabled(mw, "zoom-out", enable);
    mw_set_enabled(mw, "zoom-normal", enable);
#endif                          /* HAVE_HTML_WIDGET */

    /* set window title */
    if (bm && bm->message) {
        from = internet_address_list_to_string(bm->message->headers->from,
                                               FALSE);
        title = g_strdup_printf(_("Message from %s: %s"), from,
                                LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
        g_free(from);
        gtk_window_set_title(GTK_WINDOW(mw->window), title);
        g_free(title);
    }

    mw_set_part_buttons_sensitive(mw, bm);
}

static GActionEntry win_entries[] = {
    {"page-setup",            mw_page_setup_activated},
    {"print",                 mw_print_activated},
    {"copy",                  mw_copy_activated},
    {"close",                 mw_close_activated},
    {"select-all",            mw_select_text_activated},
    {"find-in-message",       mw_find_in_message_activated},
    {"show-toolbar",          libbalsa_toggle_activated, NULL, "false",
                              mw_show_toolbar_change_state},
    {"wrap",                  libbalsa_toggle_activated, NULL, "false",
                              mw_wrap_change_state},
    {"headers",               libbalsa_radio_activated, "s", "'none'",
                              mw_header_change_state},
#ifdef HAVE_HTML_WIDGET
    {"zoom-in",               mw_zoom_in_activated},
    {"zoom-out",              mw_zoom_out_activated},
    {"zoom-normal",           mw_zoom_normal_activated},
#endif				/* HAVE_HTML_WIDGET */
    {"next-message",          mw_next_message_activated},
    {"previous-message",      mw_previous_message_activated},
    {"next-unread",           mw_next_unread_activated},
    {"next-flagged",          mw_next_flagged_activated},
    {"reply",                 mw_reply_activated},
    {"reply-all",             mw_reply_all_activated},
    {"reply-group",           mw_reply_group_activated},
    {"forward-attached",      mw_forward_attached_activated},
    {"forward-inline",        mw_forward_inline_activated},
    {"next-part",             mw_next_part_activated},
    {"previous-part",         mw_previous_part_activated},
    {"save-part",             mw_save_part_activated},
    {"view-source",           mw_view_source_activated},
    {"select-text",           mw_select_text_activated},
    {"move-to-trash",         mw_move_to_trash_activated},
    /* Only a toolbar button: */
    {"show-all-headers",      libbalsa_toggle_activated, NULL, "false",
                              mw_show_all_headers_change_state}
};

void
message_window_add_action_entries(GActionMap * action_map)
{
    g_action_map_add_action_entries(action_map, win_entries,
                                    G_N_ELEMENTS(win_entries), action_map);
}

void
message_window_new(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;
    MessageWindow *mw;
    GtkWidget *window;
    BalsaToolbarModel *model;
    GError *error = NULL;
    GtkWidget *menubar;
    GtkWidget *move_menu, *submenu;
    GtkWidget *vbox;
    static const gchar *const header_options[] =
        { "none", "selected", "all" };
    gchar *ui_file;
    GAction *action;

    if (!mailbox || !msgno)
	return;

    message = libbalsa_mailbox_get_message(mailbox, msgno);
    if (message 
        && (mw = g_object_get_data(G_OBJECT(message), 
                                   BALSA_MESSAGE_WINDOW_KEY))) {
        gtk_window_present(GTK_WINDOW(mw->window));
        g_object_unref(message);
        return;
    }

    mw = g_malloc0(sizeof(MessageWindow));

    mw->window = window =
        gtk_application_window_new(balsa_app.application);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    /* Set up the GMenu structures */
    ui_file = g_build_filename(BALSA_DATA_PREFIX, "ui",
                               "message-window.ui", NULL);
    menubar = libbalsa_window_get_menu_bar(GTK_APPLICATION_WINDOW(window),
                                           win_entries,
                                           G_N_ELEMENTS(win_entries),
                                           ui_file, &error, mw);
    if (!menubar) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error adding from %s: %s\n"), ui_file,
                             error->message);
        g_free(ui_file);
        g_error_free(error);
        return;
    }
    g_free(ui_file);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu(window, GTK_MENU_SHELL(menubar));
#else
    gtk_box_pack_start(GTK_BOX(vbox), menubar);
#endif

    mw->headers_shown = balsa_app.shown_headers;
    mw->show_all_headers = FALSE;

    model = message_window_get_toolbar_model();

    mw->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_box_pack_start(GTK_BOX(vbox), mw->toolbar);

    gtk_window_set_role(GTK_WINDOW(window), "message");

    g_signal_connect(G_OBJECT(window), "destroy",
		     G_CALLBACK(destroy_message_window), mw);
    g_signal_connect(G_OBJECT(window), "size_allocate",
                     G_CALLBACK(size_alloc_cb), NULL);
    
    mw->bindex = balsa_find_index_by_mailbox(mailbox);
    g_object_weak_ref(G_OBJECT(mw->bindex), mw_bindex_closed_cb, mw);
    g_signal_connect_swapped(G_OBJECT(mw->bindex), "index-changed",
			     G_CALLBACK(mw_set_buttons_sensitive), mw);

    mw->mailbox = mailbox;
    g_object_add_weak_pointer(G_OBJECT(mailbox), (gpointer) &mw->mailbox);
    g_signal_connect(mailbox, "message_expunged",
                     G_CALLBACK(mw_expunged_cb), mw);

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mw_mru_menu_cb), mw);
    gtk_container_foreach(GTK_CONTAINER(menubar), mw_menubar_foreach,
                          &move_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(move_menu), submenu);

    if (mailbox->readonly) {
	gtk_widget_set_sensitive(move_menu, FALSE);
	mw_disable_trash(mw);
    }
    if (mailbox == balsa_app.trash)
	mw_disable_trash(mw);
    mw->bmessage = balsa_message_new();
    
    gtk_widget_set_vexpand(mw->bmessage, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), mw->bmessage);

    g_signal_connect(mw->bmessage, "select-part",
		     G_CALLBACK(mw_select_part_cb), mw);

    action = g_action_map_lookup_action(G_ACTION_MAP(window), "headers");
    g_action_change_state(action,
                          g_variant_new_string(header_options
                                               [balsa_app.shown_headers]));

    mw_set_active(mw, "wrap", balsa_app.browse_wrap);
    mw_set_active(mw, "show-toolbar", balsa_app.show_message_toolbar);
    if (!balsa_app.show_message_toolbar)
        gtk_widget_hide(mw->toolbar);

    gtk_window_set_default_size(GTK_WINDOW(window),
                                balsa_app.message_window_width, 
                                balsa_app.message_window_height);
    if (balsa_app.message_window_maximized)
        gtk_window_maximize(GTK_WINDOW(window));

    gtk_widget_show(window);
    mw_set_message(mw, message);

    libbalsa_window_add_accelerator(GTK_APPLICATION_WINDOW(window),
                                    "Escape", "close");
}
