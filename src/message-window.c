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
#include "balsa-app.h"
#include "balsa-message.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "main-window.h"
#include "sendmsg-window.h"
#include "message-window.h"
#include "print.h"
#include "toolbar-factory.h"
#include "mailbox-node.h"

#include "libbalsa.h"
#include <glib/gi18n.h>

typedef struct _MessageWindow MessageWindow;

/* callbacks */
static void destroy_message_window(GtkWidget * widget, MessageWindow * mw);
static void mw_expunged_cb(LibBalsaMailbox * mailbox, guint msgno,
                           MessageWindow * mw);

static void close_message_window_cb    (GtkAction * action, gpointer data);
static void replyto_message_cb         (GtkAction * action, gpointer data);
static void replytoall_message_cb      (GtkAction * action, gpointer data);
static void replytogroup_message_cb    (GtkAction * action, gpointer data);
static void forward_message_attached_cb(GtkAction * action, gpointer data);
static void forward_message_inline_cb  (GtkAction * action, gpointer data);
static void forward_message_default_cb (GtkAction * action, gpointer data);

static void next_part_cb               (GtkAction * action, gpointer data);
static void previous_part_cb           (GtkAction * action, gpointer data);
static void save_current_part_cb       (GtkAction * action, gpointer data);
static void view_msg_source_cb         (GtkAction * action, gpointer data);

static void copy_cb                    (GtkAction * action, MessageWindow * mw);
static void select_all_cb              (GtkAction * action, gpointer);

static void mw_header_activate_cb      (GtkAction * action, gpointer data);

static void next_message_cb            (GtkAction * action, gpointer data);
static void previous_message_cb        (GtkAction * action, gpointer data);
static void next_unread_cb             (GtkAction * action, gpointer);
static void next_flagged_cb            (GtkAction * action, gpointer);
#ifdef HAVE_GTK_PRINT
static void page_setup_cb              (GtkAction * action, gpointer data);
#endif
static void print_cb                   (GtkAction * action, gpointer);
static void trash_cb                   (GtkAction * action, gpointer);
#ifdef HAVE_GTKHTML
static void mw_zoom_in_cb              (GtkAction * action, MessageWindow * mw);
static void mw_zoom_out_cb             (GtkAction * action, MessageWindow * mw);
static void mw_zoom_100_cb             (GtkAction * action, MessageWindow * mw);
#endif                          /* HAVE_GTKHTML */

static void size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static void mw_set_buttons_sensitive(MessageWindow * mw);

static void mw_set_selected(MessageWindow * mw, void (*select_func)(BalsaIndex *));

static void wrap_message_cb         (GtkToggleAction * action, gpointer data);
static void show_all_headers_tool_cb(GtkToggleAction * action, gpointer data);

static void mw_select_part_cb(BalsaMessage * bm, MessageWindow * mw);

static void message_window_move_message (MessageWindow * mw,
					 LibBalsaMailbox * mailbox);
static void reset_show_all_headers(MessageWindow *mw);

struct _MessageWindow {
    GtkWidget *window;

    GtkWidget *bmessage;

    LibBalsaMessage *message;
    BalsaIndex *bindex;
    int headers_shown;
    int show_all_headers;
    guint idle_handler_id;

    GtkActionGroup *action_group;
};

static void
mru_menu_cb(gchar * url, gpointer data)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);
    MessageWindow *mw = data;

    message_window_move_message(mw, mailbox);
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

/*
 * GtkAction helpers
 */

static void
mw_set_sensitive(MessageWindow * mw, const gchar * action_name,
                 gboolean sensitive)
{
    GtkAction *action =
        gtk_action_group_get_action(mw->action_group, action_name);
    gtk_action_set_sensitive(action, sensitive);
}

/* Set the state of a GtkToggleAction; if block_action_name != NULL,
 * block the handling of signals emitted on that action.
 * Note: if action_name is a GtkRadioAction, block_action_name must be
 * the name of the first action in the group; otherwise it must be the
 * same as action_name.
 */
static void
mw_set_active(MessageWindow * mw, const gchar * action_name,
              gboolean active, const gchar * block_action_name)
{
    GtkAction *action =
        gtk_action_group_get_action(mw->action_group, action_name);
    GtkAction *block_action = block_action_name ?
        gtk_action_group_get_action(mw->action_group, block_action_name) :
        NULL;

    if (block_action)
        g_signal_handlers_block_matched(block_action,
                                        G_SIGNAL_MATCH_DATA, 0,
                                        (GQuark) 0, NULL, NULL, mw);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), active);
    if (block_action)
        g_signal_handlers_unblock_matched(block_action,
                                          G_SIGNAL_MATCH_DATA, 0,
                                          (GQuark) 0, NULL, NULL, mw);
}

/*
 * end of GtkAction helpers
 */

static void
mw_set_part_buttons_sensitive(MessageWindow * mw, BalsaMessage * msg)
{
    gboolean enable;

    if (!msg || !msg->treeview)
	return;

    enable = balsa_message_has_next_part(msg);
    mw_set_sensitive(mw, "NextPart", enable);

    enable = balsa_message_has_previous_part(msg);
    mw_set_sensitive(mw, "PreviousPart", enable);
}

static gboolean
message_window_idle_handler(MessageWindow* mw)
{
    BalsaMessage *msg;

    gdk_threads_enter();

    mw->idle_handler_id = 0;

    msg = BALSA_MESSAGE(mw->bmessage);
    if (!balsa_message_set(msg, mw->message->mailbox, mw->message->msgno)) {
	gtk_widget_destroy(mw->window);
	gdk_threads_leave();
	return FALSE;
    }
    balsa_message_grab_focus(msg);
    balsa_message_set_close(msg, TRUE);

    gdk_threads_leave();
    return FALSE;
}

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

static const GtkActionEntry entries[] = {
    {"FileMenu", NULL, N_("_File")},
    {"EditMenu", NULL, N_("_Edit")},
    {"ViewMenu", NULL, N_("_View")},
    {"MoveMenu", NULL, N_("M_ove")},
    {"MessageMenu", NULL, N_("_Message")},
#ifdef HAVE_GTK_PRINT
    {"PageSetup", NULL, N_("Page _Setup"), "<control>S",
     N_("Set up page for printing"), G_CALLBACK(page_setup_cb)},
#endif                          /* HAVE_GTK_PRINT */
    {"Print", GTK_STOCK_PRINT, N_("_Print..."), "<control>P",
     N_("Print current message"), G_CALLBACK(print_cb)},
    {"Close", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
     N_("Close the message window"),
     G_CALLBACK(close_message_window_cb)},
    {"Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C", NULL,
     G_CALLBACK(copy_cb)},
    {"SelectAll", NULL, N_("Select _All"), "<control>A", NULL,
     G_CALLBACK(select_all_cb)},
#ifdef HAVE_GTKHTML
    {"ZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom _In"), "<control>plus",
     N_("Increase magnification"), G_CALLBACK(mw_zoom_in_cb)},
    {"ZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>minus",
     N_("Decrease magnification"), G_CALLBACK(mw_zoom_out_cb)},
    /* To warn msgfmt that the % sign isn't a format specifier: */
    /* xgettext:no-c-format */
    {"Zoom100", GTK_STOCK_ZOOM_100, N_("Zoom _100%"), NULL,
     N_("No magnification"), G_CALLBACK(mw_zoom_100_cb)},
#endif                          /* HAVE_GTKHTML */
    {"Reply", BALSA_PIXMAP_REPLY, N_("_Reply..."), "R",
     N_("Reply to the current message"), G_CALLBACK(replyto_message_cb)},
    {"ReplyAll", BALSA_PIXMAP_REPLY_ALL, N_("Reply to _All..."), "A",
     N_("Reply to all recipients of the current message"),
     G_CALLBACK(replytoall_message_cb)},
    {"ReplyGroup", BALSA_PIXMAP_REPLY_GROUP, N_("Reply to _Group..."), "G",
     N_("Reply to mailing list"), G_CALLBACK(replytogroup_message_cb)},
    {"SavePart", GTK_STOCK_SAVE, N_("Save Current Part..."), "<control>S",
     N_("Save currently displayed part of message"),
     G_CALLBACK(save_current_part_cb)},
    {"ViewSource", BALSA_PIXMAP_BOOK_OPEN, N_("_View Source..."),
     "<control>U", N_("View source form of the message"),
     G_CALLBACK(view_msg_source_cb)},
    /* All three "Forward" actions have the same stock_id; the first in
     * this list defines the action tied to the toolbar's Forward
     * button, so "ForwardDefault" must come before the others. */
    {"ForwardDefault", BALSA_PIXMAP_FORWARD, NULL, NULL,
     N_("Forward the current message"),
     G_CALLBACK(forward_message_default_cb)},
    {"ForwardAttached", BALSA_PIXMAP_FORWARD, N_("_Forward attached..."), "F",
     N_("Forward the current message as attachment"),
     G_CALLBACK(forward_message_attached_cb)},
    {"ForwardInline", BALSA_PIXMAP_FORWARD, N_("Forward _inline..."), NULL,
     N_("Forward the current message inline"),
     G_CALLBACK(forward_message_inline_cb)},
    {"NextPart", BALSA_PIXMAP_NEXT_PART, N_("_Next Part"), "<control>period",
     N_("Next part in message"), G_CALLBACK(next_part_cb)},
    {"PreviousPart", BALSA_PIXMAP_PREVIOUS_PART, N_("_Previous Part"),
     "<control>comma", N_("Previous part in message"),
     G_CALLBACK(previous_part_cb)},
    {"Next", BALSA_PIXMAP_NEXT, N_("Next Message"), "N",
     N_("Next Message"), G_CALLBACK(next_message_cb)},
    {"NextUnread", BALSA_PIXMAP_NEXT_UNREAD, N_("Next Unread Message"),
     "<control>N", N_("Next Unread Message"),
     G_CALLBACK(next_unread_cb)},
    {"Previous", BALSA_PIXMAP_PREVIOUS, N_("Previous Message"), "P",
     N_("Previous Message"), G_CALLBACK(previous_message_cb)},
    {"NextFlagged", BALSA_PIXMAP_NEXT_FLAGGED, N_("Next Flagged Message"),
     "<control><alt>F", N_("Next Flagged Message"),
     G_CALLBACK(next_flagged_cb)},
    {"MoveToTrash", GTK_STOCK_DELETE, N_("_Move to Trash"), "D",
     N_("Move the message to Trash mailbox"),
     G_CALLBACK(trash_cb)}
};

/* Toggle items */
static const GtkToggleActionEntry toggle_entries[] = {
    {"Wrap", NULL, N_("_Wrap"), NULL, N_("Wrap message lines"),
     G_CALLBACK(wrap_message_cb), FALSE},
    {"ShowAllHeaders", BALSA_PIXMAP_SHOW_HEADERS, NULL, NULL, 
     N_("Show all headers"), G_CALLBACK(show_all_headers_tool_cb),
     FALSE}
};

/* Radio items */
static const GtkRadioActionEntry shown_hdrs_radio_entries[] = {
    {"NoHeaders", NULL, N_("N_o Headers"), NULL,
     N_("Display no headers"), HEADERS_NONE},
    {"SelectedHeaders", NULL, N_("_Selected Headers"), NULL,
     N_("Display selected headers"), HEADERS_SELECTED},
    {"AllHeaders", NULL, N_("All _Headers"), NULL,
     N_("Display all headers"), HEADERS_ALL}
};

static const char *ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
#ifdef HAVE_GTK_PRINT
"      <menuitem action='PageSetup'/>"
#endif                          /* HAVE_GTK_PRINT */
"      <menuitem action='Print'/>"
"      <separator/>"
"      <menuitem action='Close'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Copy'/>"
"      <menuitem action='SelectAll'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='Wrap'/>"
"      <separator/>"
"      <menuitem action='NoHeaders'/>"
"      <menuitem action='SelectedHeaders'/>"
"      <menuitem action='AllHeaders'/>"
#ifdef HAVE_GTKHTML
"      <separator/>"
"      <menuitem action='ZoomIn'/>"
"      <menuitem action='ZoomOut'/>"
"      <menuitem action='Zoom100'/>"
#endif                          /* HAVE_GTKHTML */
"    </menu>"
"    <menu action='MoveMenu'>"
"    </menu>"
"    <menu action='MessageMenu'>"
"      <menuitem action='Reply'/>"
"      <menuitem action='ReplyAll'/>"
"      <menuitem action='ReplyGroup'/>"
"      <menuitem action='ForwardAttached'/>"
"      <menuitem action='ForwardInline'/>"
"      <separator/>"
"      <menuitem action='NextPart'/>"
"      <menuitem action='PreviousPart'/>"
"      <menuitem action='SavePart'/>"
"      <menuitem action='ViewSource'/>"
"      <separator/>"
"      <menuitem action='Next'/>"
"      <menuitem action='NextUnread'/>"
"      <menuitem action='Previous'/>"
"      <menuitem action='NextFlagged'/>"
"      <separator/>"
"      <menuitem action='MoveToTrash'/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='Toolbar'>"
"  </toolbar>"
"</ui>";

/* Create a GtkUIManager for a message window, with all the actions, but no
 * ui.
 */
static GtkUIManager *
mw_get_ui_manager(MessageWindow * mw)
{
    GtkUIManager *ui_manager;
    GtkActionGroup *action_group;

    ui_manager = gtk_ui_manager_new();

    action_group = gtk_action_group_new("MessageWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (mw)
        mw->action_group = action_group;
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), mw);
    gtk_action_group_add_toggle_actions(action_group, toggle_entries,
                                        G_N_ELEMENTS(toggle_entries), mw);
    gtk_action_group_add_radio_actions(action_group,
                                       shown_hdrs_radio_entries,
                                       G_N_ELEMENTS
                                       (shown_hdrs_radio_entries), 0,
                                       NULL, /* no callback */
                                       mw);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    return ui_manager;
}

/* Standard buttons; "" means a separator. */
static const gchar* message_toolbar[] = {
#if defined(ENABLE_TOUCH_UI)
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_FORWARD,
    "",
    GTK_STOCK_PRINT,
    "",
    GTK_STOCK_DELETE,
    "",
    GTK_STOCK_CLOSE
#else /* ENABLE_TOUCH_UI */
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_REPLY_GROUP,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_PREVIOUS_PART,
    BALSA_PIXMAP_NEXT_PART,
    GTK_STOCK_SAVE,
    "",
    GTK_STOCK_PRINT,
    "",
    GTK_STOCK_DELETE
#endif /* ENEBLE_TOUCH_UI */
};

/* Create the toolbar model for the message window's toolbar.
 */
static BalsaToolbarModel *
mw_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;
    GSList *standard;
    guint i;

    if (model)
        return model;

    standard = NULL;
    for (i = 0; i < ELEMENTS(message_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(message_toolbar[i]));

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_MESSAGE_WINDOW,
                                standard);
    balsa_toolbar_model_add_actions(model, entries, G_N_ELEMENTS(entries));
    balsa_toolbar_model_add_toggle_actions(model, toggle_entries,
                                           G_N_ELEMENTS(toggle_entries));

    return model;
}

static BalsaToolbarModel *
mw_get_toolbar_model_and_ui_manager(MessageWindow * window,
                                    GtkUIManager ** ui_manager)
{
    BalsaToolbarModel *model = mw_get_toolbar_model();

    if (ui_manager)
        *ui_manager = mw_get_ui_manager(window);

    return model;
}

BalsaToolbarModel *
message_window_get_toolbar_model(GtkUIManager ** ui_manager)
{
    return mw_get_toolbar_model_and_ui_manager(NULL, ui_manager);
}

/*
 * end of UI definitions and functions
 */

#define BALSA_MESSAGE_WINDOW_KEY "balsa-message-window"

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
        if (!mw->idle_handler_id)
            mw->idle_handler_id =
                g_idle_add((GSourceFunc) message_window_idle_handler, mw);
        mw_set_buttons_sensitive(mw);
    }
}

static void
bindex_closed_cb(gpointer data, GObject *bindex)
{
    MessageWindow *mw = data;
    mw->bindex = NULL;
    gtk_widget_destroy(mw->window);
}

static void mw_disable_trash(MessageWindow * mw)
{
    mw_set_sensitive(mw, "MoveToTrash", FALSE);
}

void
message_window_new(LibBalsaMailbox * mailbox, guint msgno)
{
    LibBalsaMessage *message;
    MessageWindow *mw;
    GtkWidget *window;
    BalsaToolbarModel *model;
    GtkUIManager *ui_manager;
    GtkAccelGroup *accel_group; 
    GError *error;
    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *move_menu, *submenu;
    GtkWidget *close_widget;
    GtkWidget *vbox;
    static const gchar *const header_options[] =
        { "NoHeaders", "SelectedHeaders", "AllHeaders" };
    guint i;

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

    mw->window = window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    mw->headers_shown=balsa_app.shown_headers;
    mw->show_all_headers = FALSE;

    model = mw_get_toolbar_model_and_ui_manager(mw, &ui_manager);

    accel_group = gtk_ui_manager_get_accel_group(ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
    g_object_unref(accel_group);

    error = NULL;
    if (!gtk_ui_manager_add_ui_from_string
        (ui_manager, ui_description, -1, &error)) {
        g_message("building menus failed: %s", error->message);
        g_error_free(error);
        g_object_unref(ui_manager);
        g_object_unref(window);
        g_free(mw);
        return;
    }

    menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");
    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);

    toolbar = balsa_toolbar_new(model, ui_manager);
    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    /* Now that we have installed the menubar and toolbar, we no longer
     * need the UIManager. */
    g_object_unref(ui_manager);

    gtk_window_set_wmclass(GTK_WINDOW(window), "message", "Balsa");

    g_signal_connect(G_OBJECT(window), "destroy",
		     G_CALLBACK(destroy_message_window), mw);
    g_signal_connect(G_OBJECT(window), "size_allocate",
                     G_CALLBACK(size_alloc_cb), NULL);
    
    mw->bindex = balsa_find_index_by_mailbox(mailbox);
    g_object_weak_ref(G_OBJECT(mw->bindex), bindex_closed_cb, mw);
    g_signal_connect_swapped(G_OBJECT(mw->bindex), "index-changed",
			     G_CALLBACK(mw_set_buttons_sensitive), mw);

    g_signal_connect(mailbox, "message_expunged",
                     G_CALLBACK(mw_expunged_cb), mw);

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mru_menu_cb), mw);
    move_menu =
        gtk_ui_manager_get_widget(ui_manager, "/MainMenu/MoveMenu");
    /* The menu-item is initially hidden, because it is empty. */
    gtk_widget_show(move_menu);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(move_menu), submenu);

    if (mailbox->readonly) {
	gtk_widget_set_sensitive(move_menu, FALSE);
	mw_disable_trash(mw);
    }
    if (mailbox == balsa_app.trash)
	mw_disable_trash(mw);
    mw->bmessage = balsa_message_new();
    
    gtk_box_pack_start(GTK_BOX(vbox), mw->bmessage, TRUE, TRUE, 0);

    g_signal_connect(mw->bmessage, "select-part",
		     G_CALLBACK(mw_select_part_cb), mw);

    for (i = 0; i < G_N_ELEMENTS(header_options); i++) {
        GtkAction *action =
            gtk_action_group_get_action(mw->action_group,
                                        header_options[i]);
        if (i == balsa_app.shown_headers)
            gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), TRUE);
        g_signal_connect(action, "activate",
                         G_CALLBACK(mw_header_activate_cb), mw);
    }

    mw_set_active(mw, "Wrap", balsa_app.browse_wrap, NULL);

    gtk_window_set_default_size(GTK_WINDOW(window),
                                balsa_app.message_window_width, 
                                balsa_app.message_window_height);
    if (balsa_app.message_window_maximized)
        gtk_window_maximize(GTK_WINDOW(window));

    gtk_widget_show_all(window);
    mw_set_message(mw, message);

    close_widget =
        gtk_ui_manager_get_widget(ui_manager, "/MainMenu/FileMenu/Close");
    gtk_widget_add_accelerator(close_widget, "activate", accel_group,
                               GDK_Escape, (GdkModifierType) 0,
                               (GtkAccelFlags) 0);
}

/* Handler for the "destroy" signal for mw->window. */
static void
destroy_message_window(GtkWidget * widget, MessageWindow * mw)
{
    if (mw->bindex) {           /* BalsaIndex still exists */
        g_object_weak_unref(G_OBJECT(mw->bindex), bindex_closed_cb, mw);
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->bindex),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
        mw->bindex = NULL;
    }

    if (mw->message && mw->message->mailbox)
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->message->mailbox),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);

    mw_set_message(mw, NULL);

    g_free(mw);
}

/* Handler for the mailbox's "message-expunged" signal */
static void
mw_expunged_cb(LibBalsaMailbox * mailbox, guint msgno, MessageWindow * mw)
{
    if ((guint) mw->message->msgno == msgno)
        gtk_widget_destroy(mw->window);
}

static void
replyto_message_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY);
}

static void
replytoall_message_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY_ALL);
}

static void
replytogroup_message_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_reply(mw->message->mailbox, mw->message->msgno,
                         SEND_REPLY_GROUP);
}

static void
forward_message_attached_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno, TRUE);
}

static void
forward_message_inline_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno,
                           FALSE);
}

static void
forward_message_default_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(mw != NULL);

    sendmsg_window_forward(mw->message->mailbox, mw->message->msgno,
                           balsa_app.forward_attached);
}

static void
next_part_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_next_part(BALSA_MESSAGE(mw->bmessage));
}

static void
previous_part_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_previous_part(BALSA_MESSAGE(mw->bmessage));
}

static void
save_current_part_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_save_current_part(BALSA_MESSAGE(mw->bmessage));
}

static void
view_msg_source_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    libbalsa_show_message_source(mw->message, balsa_app.message_font,
                                 &balsa_app.source_escape_specials);
}

static void
close_message_window_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    gtk_widget_destroy(mw->window);
}

static void
wrap_message_cb(GtkToggleAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_set_wrap(BALSA_MESSAGE(mw->bmessage),
			   gtk_toggle_action_get_active(action));
}

static void
mw_header_activate_cb(GtkAction * action, gpointer data)
{
    if (gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action))) {
        ShownHeaders sh =
            gtk_radio_action_get_current_value(GTK_RADIO_ACTION(action));
        MessageWindow *mw = (MessageWindow *) data;

        balsa_app.shown_headers = sh;
        reset_show_all_headers(mw);
        balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
                                            sh);
    }
}

static void
size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    if (!GTK_WIDGET_REALIZED(window))
        return;

    if (!(balsa_app.message_window_maximized =
          gdk_window_get_state(window->window)
          & GDK_WINDOW_STATE_MAXIMIZED)) {
        balsa_app.message_window_height = alloc->height;
        balsa_app.message_window_width = alloc->width;
    }
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
    mw_set_sensitive(mw, "Next", enable);

    enable = index && balsa_index_previous_msgno(index, current_msgno) > 0;
    mw_set_sensitive(mw, "Previous", enable);

    enable = index && index->mailbox_node->mailbox->unread_messages > 0;
    mw_set_sensitive(mw, "NextUnread", enable);

    enable = index
        && libbalsa_mailbox_total_messages(index->mailbox_node->mailbox) > 0;
    mw_set_sensitive(mw, "NextFlagged", enable);
}

static void
copy_cb(GtkAction * action, MessageWindow * mw)
{
    guint signal_id;
    GtkWidget *focus_widget = gtk_window_get_focus(GTK_WINDOW(mw->window));

    signal_id = g_signal_lookup("copy-clipboard",
                                G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
}

static void
select_all_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    balsa_window_select_all(GTK_WINDOW(mw->window));
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

    /* Temporarily tell the BalsaMessage not to close when its message
     * is finalized, so we can safely unref it in mw_set_message.
     * We'll restore the usual close-with-message behavior after setting
     * the new message. */
    balsa_message_set_close(BALSA_MESSAGE(mw->bmessage), FALSE);
    mw_set_message(mw, message);
}

static void
next_message_cb(GtkAction * action, gpointer data)
{
    mw_set_selected((MessageWindow *) data, balsa_index_select_next);
}

static void
previous_message_cb(GtkAction * action, gpointer data)
{
    mw_set_selected((MessageWindow *) data, balsa_index_select_previous);
}

static void
next_unread_cb(GtkAction * action, gpointer data)
{
    mw_set_selected((MessageWindow *) data,
                    ((void (*)(BalsaIndex *))
                     balsa_index_select_next_unread));
}

static void
next_flagged_cb(GtkAction * action, gpointer data)
{
    mw_set_selected((MessageWindow *) data, balsa_index_select_next_flagged);
}


#ifdef HAVE_GTK_PRINT
static void
page_setup_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    message_print_page_setup(GTK_WINDOW(mw->window));
}
#endif


static void
print_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    message_print(mw->message, GTK_WINDOW(mw->window));
}

static void
trash_cb(GtkAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    message_window_move_message(mw, balsa_app.trash);
}

static void
show_all_headers_tool_cb(GtkToggleAction * action, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    if (gtk_toggle_action_get_active(action)) {
        mw->show_all_headers = TRUE;
	balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					    HEADERS_ALL);
    } else {
        mw->show_all_headers = FALSE;
	balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					    mw->headers_shown);
    }
}

static void
reset_show_all_headers(MessageWindow *mw)
{
    if (mw->show_all_headers) {
        mw_set_active(mw, "ShowAllHeaders", FALSE, "ShowAllHeaders");
        mw->show_all_headers = FALSE;
    }
}

#ifdef HAVE_GTKHTML
static void
mw_zoom_in_cb(GtkAction * action, MessageWindow * mw)
{
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), 1);
}

static void
mw_zoom_out_cb(GtkAction * action, MessageWindow * mw)
{
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), -1);
}

static void
mw_zoom_100_cb(GtkAction * action, MessageWindow * mw)
{
    GtkWidget *bm = mw->bmessage;
    balsa_message_zoom(BALSA_MESSAGE(bm), 0);
}
#endif				/* HAVE_GTKHTML */

static void
mw_select_part_cb(BalsaMessage * bm, MessageWindow * mw)
{
    gchar *title;
    gchar *from;
#ifdef HAVE_GTKHTML
    gboolean enable = bm && balsa_message_can_zoom(bm);

    mw_set_sensitive(mw, "ZoomIn",  enable);
    mw_set_sensitive(mw, "ZoomOut", enable);
    mw_set_sensitive(mw, "Zoom100", enable);
#endif				/* HAVE_GTKHTML */

    /* set window title */
    from = 
        internet_address_list_to_string(bm->message->headers->from, FALSE);
    title = g_strdup_printf(_("Message from %s: %s"), from,
                            LIBBALSA_MESSAGE_GET_SUBJECT(bm->message));
    g_free(from);
    gtk_window_set_title(GTK_WINDOW(mw->window), title);
    g_free(title);
    mw_set_part_buttons_sensitive(mw, bm);
}
