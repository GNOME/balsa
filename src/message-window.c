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

typedef struct _MessageWindow MessageWindow;

/* callbacks */
static void destroy_message_window(GtkWidget * widget, MessageWindow * mw);
static void close_message_window(GtkWidget * widget, gpointer data);

static void replyto_message_cb(GtkWidget * widget, gpointer data);
static void replytoall_message_cb(GtkWidget * widget, gpointer data);
static void replytogroup_message_cb(GtkWidget * widget, gpointer data);
static void forward_message_attached_cb(GtkWidget * widget, gpointer data);
static void forward_message_inline_cb(GtkWidget * widget, gpointer data);
static void forward_message_default_cb(GtkWidget * widget, gpointer data);

static void next_part_cb(GtkWidget * widget, gpointer data);
static void previous_part_cb(GtkWidget * widget, gpointer data);
static void save_current_part_cb(GtkWidget * widget, gpointer data);
static void view_msg_source_cb(GtkWidget * widget, gpointer data);

static void show_no_headers_cb(GtkWidget * widget, gpointer data);
static void show_selected_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_tool_cb(GtkWidget * widget, gpointer data);
static void wrap_message_cb(GtkWidget * widget, gpointer data);
static void size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);
static void mw_set_buttons_sensitive(MessageWindow * mw);

static void copy_cb(GtkWidget * widget, MessageWindow * mw);
static void select_all_cb(GtkWidget * widget, gpointer);

static void next_unread_cb(GtkWidget * widget, gpointer);
static void next_flagged_cb(GtkWidget * widget, gpointer);
static void print_cb(GtkWidget * widget, gpointer);
static void trash_cb(GtkWidget * widget, gpointer);

static void message_window_move_message (MessageWindow * mw,
					 LibBalsaMailbox * mailbox);
static void reset_show_all_headers(MessageWindow *mw);
#ifdef HAVE_GTKHTML
static void mw_zoom_cb(GtkWidget * widget, MessageWindow * mw);
static void mw_select_part_cb(BalsaMessage * bm, MessageWindow * mw);
#endif                          /* HAVE_GTKHTML */

static GnomeUIInfo shown_hdrs_menu[] = {
    GNOMEUIINFO_RADIOITEM(N_("N_o Headers"), NULL,
			  show_no_headers_cb, NULL),
    GNOMEUIINFO_RADIOITEM(N_("_Selected Headers"), NULL,
			  show_selected_cb, NULL),
    GNOMEUIINFO_RADIOITEM(N_("All _Headers"), NULL,
			  show_all_headers_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] = {
    GNOMEUIINFO_MENU_PRINT_ITEM(print_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_MENU_CLOSE_ITEM(close_message_window, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] = {
    /* FIXME: Features to hook up... */
    /*  GNOMEUIINFO_MENU_UNDO_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_MENU_REDO_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_SEPARATOR, */
#define MENU_EDIT_COPY_POS 0
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define MENU_EDIT_SELECT_ALL_POS 1
    GNOMEUIINFO_MENU_SELECT_ALL_ITEM(select_all_cb, NULL),
    /*  GNOMEUINFO_SEPARATOR, */
    /*  GNOMEUIINFO_MENU_FIND_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_MENU_REPLACE_ITEM(NULL, NULL); */
    GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] = {
#define MENU_VIEW_WRAP_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Wrap"), NULL, wrap_message_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(shown_hdrs_menu),
#ifdef HAVE_GTKHTML
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_ZOOM_IN MENU_VIEW_WRAP_POS + 4
    { GNOME_APP_UI_ITEM, N_("Zoom _In"), N_("Increase magnification"),
	mw_zoom_cb, GINT_TO_POINTER(1), NULL, GNOME_APP_PIXMAP_STOCK,
	GTK_STOCK_ZOOM_IN, '+', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_ZOOM_OUT MENU_VIEW_ZOOM_IN + 1
    { GNOME_APP_UI_ITEM, N_("Zoom _Out"), N_("Decrease magnification"),
	mw_zoom_cb, GINT_TO_POINTER(-1), NULL, GNOME_APP_PIXMAP_STOCK,
	GTK_STOCK_ZOOM_OUT, '-', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_ZOOM_100 MENU_VIEW_ZOOM_OUT + 1
	/* To warn msgfmt that the % sign isn't a
	 * format specifier: */
	/* xgettext:no-c-format */
    { GNOME_APP_UI_ITEM, N_("Zoom _100%"), N_("No magnification"),
	mw_zoom_cb, GINT_TO_POINTER(0), NULL, GNOME_APP_PIXMAP_STOCK,
	GTK_STOCK_ZOOM_100, 0, 0, NULL},
#endif                          /* HAVE_GTKHTML */
    GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] = {
    /* R */
    {
     GNOME_APP_UI_ITEM, N_("_Reply..."), N_("Reply to this message"),
     replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_REPLY, 'R', 0, NULL},
    /* A */
    {
     GNOME_APP_UI_ITEM, N_("Reply to _All..."),
     N_("Reply to all recipients of this message"),
     replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_REPLY_ALL, 'A', 0, NULL},
    /* G */
    {
     GNOME_APP_UI_ITEM, N_("Reply to _Group..."),
     N_("Reply to mailing list"),
     replytogroup_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_REPLY_GROUP, 'G', 0, NULL},
    /* F */
    {
     GNOME_APP_UI_ITEM, N_("_Forward attached..."),
     N_("Forward this message as attachment"),
     forward_message_attached_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_FORWARD, 'F', 0, NULL},
    {
     GNOME_APP_UI_ITEM, N_("Forward inline..."), 
     N_("Forward this message inline"),
     forward_message_inline_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_FORWARD, 'F', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
    {
     GNOME_APP_UI_ITEM, N_("Next Part"), N_("Next part in message"),
     next_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_NEXT, '.', GDK_CONTROL_MASK, NULL},
    {
     GNOME_APP_UI_ITEM, N_("Previous Part"),
     N_("Previous part in message"),
     previous_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_PREVIOUS, ',', GDK_CONTROL_MASK, NULL},
    {
     GNOME_APP_UI_ITEM, N_("Save Current Part..."),
     N_("Save current part in message"),
     save_current_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_SAVE, 's', GDK_CONTROL_MASK, NULL},
    {
     GNOME_APP_UI_ITEM, N_("_View Source..."),
     N_("View source form of the message"),
     view_msg_source_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_SAVE, 'v', GDK_CONTROL_MASK, NULL},
     GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_NEXT_UNREAD_POS 11
    {
     GNOME_APP_UI_ITEM, N_("Next Unread Message"),
     N_("Next Unread Message"), next_unread_cb, NULL, NULL,
     GNOME_APP_PIXMAP_STOCK, BALSA_PIXMAP_MENU_NEXT_UNREAD, 'N',
     GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_END
};

static GnomeUIInfo move_menu[]={
    GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] = {
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
    GNOMEUIINFO_MENU_VIEW_TREE(view_menu),
#define MAIN_MENU_MOVE_POS 3
    GNOMEUIINFO_SUBTREE(N_("M_ove"), move_menu),
    GNOMEUIINFO_SUBTREE(N_("_Message"), message_menu),
    GNOMEUIINFO_END
};

struct _MessageWindow {
    GtkWidget *window;

    GtkWidget *bmessage;

    LibBalsaMessage *message;
    BalsaIndex *bindex;
    int headers_shown;
    int show_all_headers;
    guint idle_handler_id;

    /* Pointers to our copies of widgets. */
    GtkWidget *next_unread;
#ifdef HAVE_GTKHTML
    GtkWidget *zoom_in;
    GtkWidget *zoom_out;
    GtkWidget *zoom_100;
#endif /* HAVE_GTKHTML */
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
    GList *list;

    g_return_if_fail(mailbox != NULL);

   /*Transferring to same mailbox? */
    if (mw->message->mailbox == mailbox)
	return;

    list = g_list_append(NULL, mw->message);
    balsa_index_transfer(mw->bindex, list, mailbox, FALSE);
    g_list_free_1(list);
    
    close_message_window(NULL, (gpointer) mw);
}

static gboolean
message_window_idle_handler(MessageWindow* mw)
{
    BalsaMessage *msg = BALSA_MESSAGE(mw->bmessage);
    LibBalsaMessage *message = mw->message;
    gchar *title;

    gdk_threads_enter();

    mw->idle_handler_id = 0;

    title = libbalsa_message_title(message,
                                   balsa_app.message_title_format);
    gtk_window_set_title(GTK_WINDOW(mw->window), title);
    g_free(title);
    balsa_message_set(msg, message);

    if(msg && msg->treeview) {
	if (msg->info_count > 1) {
            GtkWidget *toolbar =
                balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));
	    balsa_toolbar_set_button_sensitive(toolbar,
                                               BALSA_PIXMAP_PREVIOUS, TRUE);
	    balsa_toolbar_set_button_sensitive(toolbar,
                                               BALSA_PIXMAP_NEXT, TRUE);
	}
    }

    /* Update the style and message counts in the mailbox list */
    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
                                message->mailbox);

    gdk_threads_leave();
    return FALSE;
}

/* Toolbar buttons and their callbacks. */
static const struct callback_item {
    const char* icon_id;
    void (*callback)(GtkWidget *, gpointer);
} callback_table[] = {
    { BALSA_PIXMAP_REPLY,        replyto_message_cb },
    { BALSA_PIXMAP_REPLY_ALL,    replytoall_message_cb },
    { BALSA_PIXMAP_REPLY_GROUP,  replytogroup_message_cb },
    { BALSA_PIXMAP_FORWARD,      forward_message_default_cb },
    { BALSA_PIXMAP_PREVIOUS,     previous_part_cb },
    { BALSA_PIXMAP_NEXT,         next_part_cb },
    { BALSA_PIXMAP_NEXT_UNREAD,  next_unread_cb },
    { BALSA_PIXMAP_NEXT_FLAGGED, next_flagged_cb },
    { BALSA_PIXMAP_TRASH,        trash_cb },
    { BALSA_PIXMAP_PRINT,        print_cb },
    { BALSA_PIXMAP_SAVE,         save_current_part_cb },
    { GTK_STOCK_CLOSE,           close_message_window },
    { BALSA_PIXMAP_SHOW_HEADERS, show_all_headers_tool_cb }
};

/* Standard buttons; "" means a separator. */
static const gchar* message_toolbar[] = {
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_REPLY_GROUP,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_PREVIOUS,
    BALSA_PIXMAP_NEXT,
    BALSA_PIXMAP_SAVE,
    "",
    BALSA_PIXMAP_PRINT,
    "",
    BALSA_PIXMAP_TRASH,
};

/* Create the toolbar model for the message window's toolbar.
 */
BalsaToolbarModel *
message_window_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;
    GSList *legal;
    GSList *standard;
    GSList **current;
    guint i;

    if (model)
        return model;

    legal = NULL;
    for (i = 0; i < ELEMENTS(callback_table); i++)
        legal = g_slist_append(legal, g_strdup(callback_table[i].icon_id));

    standard = NULL;
    for (i = 0; i < ELEMENTS(message_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(message_toolbar[i]));

    current = &balsa_app.message_window_toolbar_current;

    model = balsa_toolbar_model_new(legal, standard, current);

    return model;
}

#define BALSA_MESSAGE_WINDOW_KEY "balsa-message-window"

static void
mw_set_message(MessageWindow *mw, LibBalsaMessage * message)
{
    mw->message = message;
    mw_set_buttons_sensitive(mw);
    g_object_set_data(G_OBJECT(message), BALSA_MESSAGE_WINDOW_KEY, mw);

    g_object_ref(message); /* protect from destroying */
    mw->idle_handler_id =
        g_idle_add((GSourceFunc) message_window_idle_handler, mw);
}

static void
bindex_closed_cb(gpointer data, GObject *bindex)
{
    MessageWindow *mw = data;
    mw->bindex = NULL;
    gtk_widget_destroy(mw->window);
}

void
message_window_new(LibBalsaMessage * message)
{
    MessageWindow *mw;
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    guint i;
    GtkWidget *move_menu, *submenu;

    if (!message)
	return;

    /*
     * Check to see if this message is already displayed
     */
    mw = g_object_get_data(G_OBJECT(message), BALSA_MESSAGE_WINDOW_KEY);
    if (mw != NULL) {
        /*
         * The message is already displayed in a window, so just use
         * that one.
         */
        gdk_window_raise(mw->window->window);
        return;
    }

    mw = g_malloc0(sizeof(MessageWindow));

    mw->window = gnome_app_new("balsa", NULL);

    mw->headers_shown=balsa_app.shown_headers;
    mw->show_all_headers = FALSE;

    model = message_window_get_toolbar_model();
    toolbar = balsa_toolbar_new(model);
    for (i = 0; i < ELEMENTS(callback_table); i++)
        balsa_toolbar_set_callback(toolbar, callback_table[i].icon_id,
                                   G_CALLBACK(callback_table[i].callback),
                                   mw);
    gnome_app_set_toolbar(GNOME_APP(mw->window), GTK_TOOLBAR(toolbar));

    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_PREVIOUS, FALSE);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT, FALSE);

    gtk_window_set_wmclass(GTK_WINDOW(mw->window), "message", "Balsa");

    g_signal_connect(G_OBJECT(mw->window), "destroy",
		     G_CALLBACK(destroy_message_window), mw);
    g_signal_connect(G_OBJECT(mw->window), "size_allocate",
                     G_CALLBACK(size_alloc_cb), NULL);
    
    mw->bindex = balsa_find_index_by_mailbox(message->mailbox);
    g_object_weak_ref(G_OBJECT(mw->bindex), bindex_closed_cb, mw);
    g_signal_connect_swapped(G_OBJECT(mw->bindex), "index-changed",
			     G_CALLBACK(mw_set_buttons_sensitive), mw);

    gnome_app_create_menus_with_data(GNOME_APP(mw->window), main_menu, mw);

    /* Save the widgets that we need to change--they'll be overwritten
     * if another message window is opened. */
    mw->next_unread = message_menu[MENU_MESSAGE_NEXT_UNREAD_POS].widget;
#ifdef HAVE_GTKHTML
    mw->zoom_in  = view_menu[MENU_VIEW_ZOOM_IN].widget;
    mw->zoom_out = view_menu[MENU_VIEW_ZOOM_OUT].widget;
    mw->zoom_100 = view_menu[MENU_VIEW_ZOOM_100].widget;
    /* Use Ctrl+= as an alternative accelerator for zoom-in, because
     * Ctrl++ is a 3-key combination. */
    gtk_widget_add_accelerator(view_menu[MENU_VIEW_ZOOM_IN].widget,
			       "activate",
			       GNOME_APP(mw->window)->accel_group,
			       '=', GDK_CONTROL_MASK, (GtkAccelFlags) 0);
#endif                          /* HAVE_GTKHTML */

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(mw->window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mru_menu_cb), mw);
    move_menu = main_menu[MAIN_MENU_MOVE_POS].widget;
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(move_menu), submenu);
    if(message->mailbox->readonly)
	gtk_widget_set_sensitive(move_menu, FALSE);
    mw->bmessage = balsa_message_new();
    balsa_message_set_close(BALSA_MESSAGE(mw->bmessage), TRUE);
    
    gnome_app_set_contents(GNOME_APP(mw->window), mw->bmessage);

#ifdef HAVE_GTKHTML
    g_signal_connect(mw->bmessage, "select-part",
		     G_CALLBACK(mw_select_part_cb), mw);
#endif				/* HAVE_GTKHTML */

    if (balsa_app.shown_headers >= HEADERS_NONE &&
	balsa_app.shown_headers <= HEADERS_ALL)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (shown_hdrs_menu
					[balsa_app.shown_headers].widget),
				       TRUE);

    gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(view_menu[MENU_VIEW_WRAP_POS].widget),
	 balsa_app.browse_wrap);

    gtk_window_set_default_size(GTK_WINDOW(mw->window),
                                balsa_app.message_window_width, 
                                balsa_app.message_window_height);

    gtk_widget_show_all(mw->window);
    mw_set_message(mw, message);
}

static void
mw_clear_message(MessageWindow * mw)
{
    if (mw->idle_handler_id) {
	g_source_remove(mw->idle_handler_id);
	mw->idle_handler_id = 0;
    } 
    if (mw->message) {
	g_object_set_data(G_OBJECT(mw->message), BALSA_MESSAGE_WINDOW_KEY,
			  NULL);
	g_object_unref(mw->message);
	mw->message = NULL;
    }
}

/* Handler for the "destroy" signal for mw->window. */
static void
destroy_message_window(GtkWidget * widget, MessageWindow * mw)
{
    if (mw->bindex) { /* BalsaIndex still exists */
        g_object_weak_unref(G_OBJECT(mw->bindex), bindex_closed_cb, mw);
        g_signal_handlers_disconnect_matched(G_OBJECT(mw->bindex),
                                             G_SIGNAL_MATCH_DATA, 0, 0,
                                             NULL, NULL, mw);
    }

    mw_clear_message(mw);

    g_free(mw);
}

static void
replyto_message_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, SEND_REPLY);
}

static void
replytoall_message_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, SEND_REPLY_ALL);
}

static void
replytogroup_message_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, SEND_REPLY_GROUP);
}

static void
forward_message_attached_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, SEND_FORWARD_ATTACH);
}

static void
forward_message_inline_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, SEND_FORWARD_INLINE);
}

static void
forward_message_default_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(mw != NULL);

    sendmsg_window_new(widget, mw->message, balsa_app.forward_attached 
		       ? SEND_FORWARD_ATTACH : SEND_FORWARD_INLINE);
}

static void
next_part_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_next_part(BALSA_MESSAGE(mw->bmessage));
}

static void
previous_part_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_previous_part(BALSA_MESSAGE(mw->bmessage));
}

static void
save_current_part_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_save_current_part(BALSA_MESSAGE(mw->bmessage));
}

static void
view_msg_source_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    libbalsa_show_message_source(mw->message, balsa_app.message_font,
                                 &balsa_app.source_escape_specials);
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;
    gtk_widget_destroy(mw->window);
}

static void
show_no_headers_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

	mw->headers_shown=HEADERS_NONE;

    reset_show_all_headers(mw);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					HEADERS_NONE);
}

static void
show_selected_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    mw->headers_shown=HEADERS_SELECTED;

    reset_show_all_headers(mw);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					HEADERS_SELECTED);
}

static void
show_all_headers_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

	mw->headers_shown=HEADERS_ALL;

    reset_show_all_headers(mw);
    balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					HEADERS_ALL);
}

static void
wrap_message_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    balsa_message_set_wrap(BALSA_MESSAGE(mw->bmessage),
			   GTK_CHECK_MENU_ITEM(widget)->active);
}

static void
size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    balsa_app.message_window_height = alloc->height;
    balsa_app.message_window_width = alloc->width;
}

static void
mw_set_buttons_sensitive(MessageWindow * mw)
{
    GtkWidget *toolbar =
	balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));
    LibBalsaMailbox *mailbox = mw->message->mailbox;
    gint other_unread =
	mailbox->unread_messages - LIBBALSA_MESSAGE_IS_UNREAD(mw->message);

    gtk_widget_set_sensitive(mw->next_unread, other_unread > 0);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT_UNREAD,
				       other_unread > 0);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT_FLAGGED,
				       mailbox
				       && libbalsa_mailbox_total_messages
				       (mailbox) > 0);
}

static void
copy_cb(GtkWidget * widget, MessageWindow * mw)
{
    guint signal_id;
    GtkWidget *focus_widget = gtk_window_get_focus(GTK_WINDOW(mw->window));

    signal_id = g_signal_lookup("copy-clipboard",
                                G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
}

static void
select_all_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    libbalsa_window_select_all(GTK_WINDOW(mw->window));
}

static void
mw_set_selected(MessageWindow * mw)
{
    GList *list;

    mw_clear_message(mw);

    list = balsa_index_selected_list(mw->bindex);
    if (g_list_length(list) == 1)
	mw_set_message(mw, LIBBALSA_MESSAGE(list->data));
    g_list_foreach(list, (GFunc) g_object_unref, NULL);
    g_list_free(list);
}

static void
next_unread_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    balsa_index_select_next_unread(mw->bindex);
    mw_set_selected(mw);
}

static void next_flagged_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    balsa_index_select_next_flagged(mw->bindex);
    mw_set_selected(mw);
}


static void print_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    
    message_print(mw->message, GTK_WINDOW(mw->window));
}

static void trash_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    message_window_move_message(mw, balsa_app.trash);
}

static void
show_all_headers_tool_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));

    if (balsa_toolbar_get_button_active(toolbar,
                                        BALSA_PIXMAP_SHOW_HEADERS)) {
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
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));

    mw->show_all_headers = FALSE;
    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_SHOW_HEADERS, FALSE);
}

#ifdef HAVE_GTKHTML
static void
mw_zoom_cb(GtkWidget * widget, MessageWindow * mw)
{
    GtkWidget *bm = mw->bmessage;
    gint in_out =
	GPOINTER_TO_INT(g_object_get_data
			(G_OBJECT(widget), GNOMEUIINFO_KEY_UIDATA));
    balsa_message_zoom(BALSA_MESSAGE(bm), in_out);
}

static void
mw_select_part_cb(BalsaMessage * bm, MessageWindow * mw)
{
    gboolean enable = bm && balsa_message_can_zoom(bm);
    gtk_widget_set_sensitive(mw->zoom_in,  enable);
    gtk_widget_set_sensitive(mw->zoom_out, enable);
    gtk_widget_set_sensitive(mw->zoom_100, enable);
}
#endif				/* HAVE_GTKHTML */
