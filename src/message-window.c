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
#include "geometry-manager.h"

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
        g_warning("%s action “%s” not found", __func__, action_name);
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
        g_warning("%s action “%s” not found", __func__, action_name);
}

/*
 * end of GAction helpers
 */

static void
mw_set_part_buttons_sensitive(MessageWindow * mw, BalsaMessage * msg)
{
	LibBalsaMessage *lbmsg;

	if (msg == NULL || balsa_message_get_tree_view(msg) == NULL)
	return;

    mw_set_enabled(mw, "next-part",
                   balsa_message_has_next_part(msg));
    mw_set_enabled(mw, "previous-part",
                   balsa_message_has_previous_part(msg));
    lbmsg = balsa_message_get_message(msg);
    mw_set_enabled(mw, "recheck-crypt",
    	(lbmsg != NULL) ? libbalsa_message_has_crypto_content(lbmsg) : FALSE);
}

static gboolean
message_window_idle_handler(gpointer user_data)
{
    MessageWindow *mw = user_data;
    BalsaMessage *msg;

    mw->idle_handler_id = 0;

    msg = BALSA_MESSAGE(mw->bmessage);
    if (balsa_message_set(msg,
                          libbalsa_message_get_mailbox(mw->message),
                          libbalsa_message_get_msgno(mw->message))) {
        balsa_message_grab_focus(msg);
    } else {
        gtk_window_destroy(GTK_WINDOW(mw->window));
    }

    return G_SOURCE_REMOVE;
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
    { "show-all-headers", BALSA_PIXMAP_SHOW_HEADERS  },
	{ "recheck-crypt",    BALSA_PIXMAP_GPG_RECHECK   },
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
    gtk_window_destroy(GTK_WINDOW(mw->window));
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
        g_warning("%s unknown value “%s”", __func__, value);
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
mw_set_buttons_sensitive(MessageWindow * mw)
{
    LibBalsaMailbox *mailbox;
    BalsaIndex *index = mw->bindex;
    guint current_msgno = libbalsa_message_get_msgno(mw->message);
    gboolean enable;

    mailbox = libbalsa_message_get_mailbox(mw->message);
    if (mailbox == NULL) {
        gtk_window_destroy(GTK_WINDOW(mw->window));
        return;
    }

    enable = index && balsa_index_next_msgno(index, current_msgno) > 0;
    mw_set_enabled(mw, "next-message", enable);

    enable = index && balsa_index_previous_msgno(index, current_msgno) > 0;
    mw_set_enabled(mw, "previous-message", enable);

    mailbox = index != NULL ? balsa_index_get_mailbox(index): NULL;

    enable = mailbox != NULL && libbalsa_mailbox_get_unread_messages(mailbox) > 0;
    mw_set_enabled(mw, "next-unread", enable);

    enable = mailbox != NULL && libbalsa_mailbox_total_messages(mailbox) > 0;
    mw_set_enabled(mw, "next-flagged", enable);
}

static void
mw_set_message(MessageWindow * mw, LibBalsaMessage * message)
{
    if (mw->idle_handler_id && !message) {
	g_source_remove(mw->idle_handler_id);
	mw->idle_handler_id = 0;
    } 

    if (mw->message) {
        g_object_set_data(G_OBJECT(mw->message), BALSA_MESSAGE_WINDOW_KEY, NULL);
        g_object_unref(mw->message);
    }

    mw->message = message;

    if (message) {
        g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_WINDOW_KEY, mw);
        if (mw->idle_handler_id == 0)
            mw->idle_handler_id = g_idle_add(message_window_idle_handler, mw);
        mw_set_buttons_sensitive(mw);
    }
}

/* Handler for the "destroy" signal for mw->window. */
static void
destroy_message_window(GtkWidget * widget, MessageWindow * mw)
{
    if (mw->bindex) {           /* BalsaIndex still exists */
        g_object_weak_unref(G_OBJECT(mw->bindex), mw_bindex_closed_cb, mw);
        g_signal_handlers_disconnect_matched(mw->bindex,
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
        mw->bindex = NULL;
    }

    if (mw->mailbox) {
        g_object_remove_weak_pointer(G_OBJECT(mw->mailbox), (gpointer *) &mw->mailbox);
        g_signal_handlers_disconnect_matched(mw->mailbox,
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
        mw->mailbox = NULL;
    }

    if (mw->bmessage)
        g_signal_handlers_disconnect_matched(mw->bmessage,
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);

    mw_set_message(mw, NULL);

    g_free(mw);
}

/* Handler for the mailbox's "message-expunged" signal */
static void
mw_expunged_cb(LibBalsaMailbox * mailbox, guint msgno, MessageWindow * mw)
{
    if (mw->message != NULL && (guint) libbalsa_message_get_msgno(mw->message) == msgno)
        gtk_window_destroy(GTK_WINDOW(mw->window));
}

static void
mw_reply_activated(GSimpleAction * action, GVariant * parameter,
                   gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(libbalsa_message_get_mailbox(mw->message),
                         libbalsa_message_get_msgno(mw->message),
                         SEND_REPLY);
}

static void
mw_reply_all_activated(GSimpleAction * action, GVariant * parameter,
                       gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(libbalsa_message_get_mailbox(mw->message),
                         libbalsa_message_get_msgno(mw->message),
                         SEND_REPLY_ALL);
}

static void
mw_reply_group_activated(GSimpleAction * action, GVariant * parameter,
                         gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(libbalsa_message_get_mailbox(mw->message),
                         libbalsa_message_get_msgno(mw->message),
                         SEND_REPLY_GROUP);
}

static void
mw_forward_attached_activated(GSimpleAction * action, GVariant * parameter,
                              gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(libbalsa_message_get_mailbox(mw->message),
                           libbalsa_message_get_msgno(mw->message),
                           TRUE);
}

static void
mw_forward_inline_activated(GSimpleAction * action, GVariant * parameter,
                            gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(libbalsa_message_get_mailbox(mw->message),
                           libbalsa_message_get_msgno(mw->message),
                           FALSE);
}

#if 0
static void
mw_forward_default_activated(GSimpleAction * action, GVariant * parameter,
                             gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(libbalsa_message_get_mailbox(mw->message),
                           libbalsa_message_get_msgno(mw->message),
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
                                 &balsa_app.source_escape_specials);
}

static void
mw_recheck_crypt_activated(GSimpleAction * action,
    					   GVariant      * parameter,
						   gpointer        data)
{
    MessageWindow *mw = (MessageWindow *) data;
	balsa_message_recheck_crypto(BALSA_MESSAGE(mw->bmessage));
}

static void
mw_close_activated(GSimpleAction * action, GVariant * parameter,
                   gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    gtk_window_destroy(GTK_WINDOW(mw->window));
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

    balsa_index_set_next_msgno(mw->bindex, libbalsa_message_get_msgno(mw->message));
    select_func(mw->bindex);
    msgno = balsa_index_get_next_msgno(mw->bindex);
    message = libbalsa_mailbox_get_message(libbalsa_message_get_mailbox(mw->message), msgno);
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
        gtk_window_destroy(GTK_WINDOW(tmp->window));
    }

    mw_set_message(mw, message);
}

static void
message_window_move_message(MessageWindow * mw, LibBalsaMailbox * mailbox)
{
    GArray *messages;
    guint msgno;
    LibBalsaMessage *original = mw->message;

    /* Transferring to same mailbox? */
    if (libbalsa_message_get_mailbox(mw->message) == mailbox)
        return;

    messages = g_array_new(FALSE, FALSE, sizeof(guint));
    msgno = libbalsa_message_get_msgno(mw->message);
    g_array_append_val(messages, msgno);

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
        gtk_window_destroy(GTK_WINDOW(mw->window));
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
    if (bm != NULL) {
        LibBalsaMessage *message = balsa_message_get_message(bm);

        if (message != NULL) {
            from = internet_address_list_to_string(libbalsa_message_get_headers(message)->from, NULL,
                                                   FALSE);
            title = g_strdup_printf(_("Message from %s: %s"), from,
                                    LIBBALSA_MESSAGE_GET_SUBJECT(message));
            g_free(from);
            gtk_window_set_title(GTK_WINDOW(mw->window), title);
            g_free(title);
        }
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
    {"show-toolbar",          NULL, NULL, "false", mw_show_toolbar_change_state},
    {"wrap",                  NULL, NULL, "false", mw_wrap_change_state},
    {"headers",               NULL, "s", "'none'", mw_header_change_state},
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
	{"recheck-crypt",		  mw_recheck_crypt_activated},
    {"select-text",           mw_select_text_activated},
    {"move-to-trash",         mw_move_to_trash_activated},
    /* Only a toolbar button: */
    {"show-all-headers",      NULL, NULL, "false", mw_show_all_headers_change_state}
};

void
message_window_add_action_entries(GActionMap * action_map)
{
    g_action_map_add_action_entries(action_map, win_entries,
                                    G_N_ELEMENTS(win_entries), action_map);
}

static void
move_to_activated(GSimpleAction *action,
                  GVariant      *parameter,
                  gpointer       user_data)
{
    MessageWindow *mw = user_data;
    LibBalsaMailbox *mailbox;

    mailbox = balsa_mblist_mru_get_mailbox_from_variant(parameter, mw->window);

    if (mailbox != NULL) {
        balsa_mblist_mru_add(&balsa_app.folder_mru, libbalsa_mailbox_get_url(mailbox));
        message_window_move_message(mw, mailbox);
    }
}

static void
mw_menubar_set_mru_menu(GtkWidget *menubar)
{
    GMenuModel *model;
    int i;
    int n;
    int move_position = -1;
    GMenuItem *item;
    GMenu *mru_menu;

    model = gtk_popover_menu_bar_get_menu_model(GTK_POPOVER_MENU_BAR(menubar));
    n = g_menu_model_get_n_items(model);
    for (i = 0; i < n; i++) {
        const char *label;

        if (g_menu_model_get_item_attribute(model, i, "label", "s", &label) &&
            strcmp(label, "M_ove") == 0) {
            move_position = i;
            break;
        }
    }

    g_return_if_fail(move_position >= 0);

    item = g_menu_item_new_from_model(model, move_position);

    mru_menu = balsa_mblist_mru_menu(&balsa_app.folder_mru, "message-window.move-to");
    g_menu_item_set_submenu(item, G_MENU_MODEL(mru_menu));
    g_object_unref(mru_menu);

    /* Replace the existing submenu */
    g_menu_remove(G_MENU(model), move_position);
    g_menu_insert_item(G_MENU(model), move_position, item);
    g_object_unref(item);
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
    GtkWidget *vbox;
    static const gchar *const header_options[] =
        { "none", "selected", "all" };
    const gchar resource_path[] = "/org/desktop/Balsa/message-window.ui";
    GAction *action;
    GSimpleActionGroup *simple;
    static const GActionEntry message_window_entries[] = {
        {"move-to", move_to_activated, "s"},
    };

    if (mailbox == NULL || msgno == 0)
	return;

    message = libbalsa_mailbox_get_message(mailbox, msgno);
    if (message != NULL &&
        (mw = g_object_get_data(G_OBJECT(message), BALSA_MESSAGE_WINDOW_KEY)) != NULL) {
        gtk_window_present(GTK_WINDOW(mw->window));
        g_object_unref(message);
        return;
    }

    mw = g_malloc0(sizeof(MessageWindow));

    mw->window = window = gtk_application_window_new(balsa_app.application);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(GTK_WINDOW(window), vbox);

    /* Set up the GMenu structures */
    menubar = libbalsa_window_get_menu_bar(GTK_APPLICATION_WINDOW(window),
                                           win_entries,
                                           G_N_ELEMENTS(win_entries),
                                           resource_path, &error, mw);
    if (menubar == NULL) {
        libbalsa_information(LIBBALSA_INFORMATION_WARNING,
                             _("Error adding from %s: %s\n"), resource_path,
                             error->message);
        g_error_free(error);
        return;
    }
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu(window, GTK_MENU_SHELL(menubar));
#else
    gtk_box_append(GTK_BOX(vbox), menubar);
#endif

    mw_menubar_set_mru_menu(menubar);

    mw->headers_shown = balsa_app.shown_headers;
    mw->show_all_headers = FALSE;

    model = message_window_get_toolbar_model();

    mw->toolbar = balsa_toolbar_new(model, G_ACTION_MAP(window));
    gtk_box_append(GTK_BOX(vbox), mw->toolbar);

    g_signal_connect(window, "destroy",
		     G_CALLBACK(destroy_message_window), mw);

    mw->bindex = balsa_find_index_by_mailbox(mailbox);
    g_object_weak_ref(G_OBJECT(mw->bindex), mw_bindex_closed_cb, mw);
    g_signal_connect_swapped(mw->bindex, "index-changed",
			     G_CALLBACK(mw_set_buttons_sensitive), mw);

    mw->mailbox = mailbox;
    g_object_add_weak_pointer(G_OBJECT(mailbox), (gpointer *) &mw->mailbox);
    g_signal_connect(mailbox, "message_expunged",
                     G_CALLBACK(mw_expunged_cb), mw);

    /* Set up the "move-to" action */
    simple = g_simple_action_group_new();
    g_action_map_add_action_entries(G_ACTION_MAP(simple),
                                    message_window_entries,
                                    G_N_ELEMENTS(message_window_entries),
                                    mw);
    gtk_widget_insert_action_group(GTK_WIDGET(mw->window),
                                   "message-window",
                                   G_ACTION_GROUP(simple));
    action = g_action_map_lookup_action(G_ACTION_MAP(simple), "move-to");
    g_object_unref(simple);

    if (libbalsa_mailbox_get_readonly(mailbox)) {
	g_simple_action_set_enabled(G_SIMPLE_ACTION(action), FALSE);
	mw_disable_trash(mw);
    }
    if (mailbox == balsa_app.trash)
	mw_disable_trash(mw);
    mw->bmessage = balsa_message_new();

    gtk_widget_set_vexpand(mw->bmessage, TRUE);
    gtk_widget_set_valign(mw->bmessage, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(vbox), mw->bmessage);

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

    mw_set_enabled(mw, "reply-group",
                   libbalsa_message_get_user_header(message, "list-post") != NULL);

    geometry_manager_attach(GTK_WINDOW(window),"MessageWindow" );

    gtk_widget_show(window);
    mw_set_message(mw, message);

    libbalsa_window_add_accelerator(GTK_APPLICATION_WINDOW(window),
                                    "Escape", "close");
}
