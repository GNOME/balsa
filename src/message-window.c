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
static void destroy_message_window(GtkWidget * widget, gpointer data);
static void close_message_window(GtkWidget * widget, gpointer data);
static void mw_message_weak_ref_cb(MessageWindow * mw,
                                   LibBalsaMessage * message);

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

static void copy_cb(GtkWidget * widget, gpointer data);
static void select_all_cb(GtkWidget * widget, gpointer);

static void select_part_cb(BalsaMessage * bm, gpointer data);

static void next_unread_cb(GtkWidget * widget, gpointer);
static void next_flagged_cb(GtkWidget * widget, gpointer);
static void print_cb(GtkWidget * widget, gpointer);
static void trash_cb(GtkWidget * widget, gpointer);

/* helper */
static void weak_unref_and_destroy(MessageWindow * mw);

/*
 * The list of messages which are being displayed.
 */
static GHashTable *displayed_messages = NULL;

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
     GNOME_APP_UI_ITEM, N_("Next Part"), N_("Next part in Message"),
     next_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_NEXT, '.', GDK_CONTROL_MASK, NULL},
    {
     GNOME_APP_UI_ITEM, N_("Previous Part"),
     N_("Previous part in Message"),
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
    GNOMEUIINFO_SUBTREE("M_ove", move_menu),
    GNOMEUIINFO_SUBTREE("_Message", message_menu),
    GNOMEUIINFO_END
};

struct _MessageWindow {
    GtkWidget *window;

    GtkWidget *bmessage;

    LibBalsaMessage *message;
    int show_all_headers_save;
    int headers_shown;
};

void reset_show_all_headers(MessageWindow *mw);

static void
mru_menu_cb(gchar * url, gpointer data)
{
    LibBalsaMailbox *mailbox = balsa_find_mailbox_by_url(url);
    MessageWindow *mw = data;
    GList *list;
    BalsaIndex* bindex;

    g_return_if_fail(mailbox != NULL);

   /*Transferring to same mailbox? */
    if (mw->message->mailbox == mailbox)
	return;

    list = g_list_append(NULL, mw->message);
    bindex = balsa_find_index_by_mailbox(mw->message->mailbox);
    balsa_index_transfer(bindex, list, mailbox, FALSE);
    g_list_free_1(list);
    
    close_message_window(NULL, (gpointer) mw);
}

/* FIXME: any protection for destroying message window before idle
   handler is called?
*/
static gboolean
message_window_idle_handler(MessageWindow* mw)
{
    BalsaMessage *msg = BALSA_MESSAGE(mw->bmessage);
    LibBalsaMessage *message;

    gdk_threads_enter();
    message = g_object_get_data(G_OBJECT(msg), "message");

    balsa_message_set(msg, message);

    if(msg && msg->part_list) {
        GnomeIconList *gil = GNOME_ICON_LIST(msg->part_list);
	if(gnome_icon_list_get_num_icons(gil) >= 2) {
            GtkWidget *toolbar =
                balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));
	    balsa_toolbar_set_button_sensitive(toolbar,
                                               BALSA_PIXMAP_PREVIOUS, TRUE);
	    balsa_toolbar_set_button_sensitive(toolbar,
                                               BALSA_PIXMAP_NEXT, TRUE);
	}
    }

    g_object_weak_ref(G_OBJECT(message),
                      (GWeakNotify) mw_message_weak_ref_cb, mw);
    g_object_unref(G_OBJECT(message)); 

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

void
message_window_new(LibBalsaMessage * message)
{
    MessageWindow *mw;
    gchar *title;
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    guint i;
    GtkWidget *scroll;
    GtkWidget *move_menu, *submenu;

    if (!message)
	return;

    /*
     * Check to see if this message is already displayed
     */
    if (displayed_messages != NULL) {
	mw = (MessageWindow *) g_hash_table_lookup(displayed_messages,
						   message);
	if (mw != NULL) {
	    /*
	     * The message is already displayed in a window, so just use
	     * that one.
	     */
	    gdk_window_raise(GTK_WIDGET(mw->window)->window);
	    return;
	}
    } else {
	/*
	 * We've never displayed a message before; initialize the hash
	 * table.
	 */
	displayed_messages =
	    g_hash_table_new(g_direct_hash, g_direct_equal);
    }

    mw = g_malloc0(sizeof(MessageWindow));

    g_hash_table_insert(displayed_messages, message, mw);

    mw->message = message;

    title = libbalsa_message_title(message,
                                   balsa_app.message_title_format);
    mw->window = gnome_app_new("balsa", title);
    g_free(title);

    mw->show_all_headers_save=-1;
    mw->headers_shown=balsa_app.shown_headers;

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
    
    gnome_app_create_menus_with_data(GNOME_APP(mw->window), main_menu, mw);

    submenu = balsa_mblist_mru_menu(GTK_WINDOW(mw->window),
                                    &balsa_app.folder_mru,
                                    G_CALLBACK(mru_menu_cb), mw);
    move_menu = main_menu[MAIN_MENU_MOVE_POS].widget;
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(move_menu), submenu);
    if(mw->message->mailbox->readonly)
	gtk_widget_set_sensitive(move_menu, FALSE);
    mw->bmessage = balsa_message_new();
    
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);


    g_signal_connect(G_OBJECT(mw->bmessage), "select-part",
		     G_CALLBACK(select_part_cb), mw);

    gtk_container_add(GTK_CONTAINER(scroll), mw->bmessage);
    gtk_widget_show(scroll);

    gnome_app_set_contents(GNOME_APP(mw->window), scroll);

    if (balsa_app.shown_headers >= HEADERS_NONE &&
	balsa_app.shown_headers <= HEADERS_ALL)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (shown_hdrs_menu
					[balsa_app.shown_headers].widget),
				       TRUE);

    gtk_check_menu_item_set_active
	(GTK_CHECK_MENU_ITEM(view_menu[MENU_VIEW_WRAP_POS].widget),
	 balsa_app.browse_wrap);

    /* FIXME: set it to the size of the canvas, unless it is
     * bigger than the desktop, in which case it should be at about a
     * 2/3 proportional size based on the size of the desktop and the
     * height and width of the canvas.  [save and restore window size too]
     */

    gtk_window_set_default_size(GTK_WINDOW(mw->window), 400, 500);

    gtk_widget_show(mw->bmessage);
    gtk_widget_show(mw->window);

    g_object_set_data(G_OBJECT(mw->bmessage), "message", message);
    g_object_ref(G_OBJECT(message)); /* protect from destroying */
    gtk_idle_add((GtkFunction)message_window_idle_handler, mw);
}

static void
destroy_message_window(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) data;

    g_hash_table_remove(displayed_messages, mw->message);

    gtk_widget_destroy(mw->bmessage);

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
    libbalsa_show_message_source(mw->message);
}

static void
close_message_window(GtkWidget * widget, gpointer data)
{
    weak_unref_and_destroy(data);
}

static void
mw_message_weak_ref_cb(MessageWindow * mw, LibBalsaMessage * message)
{
    if (mw->message == message) {
        /* mw->bmessage might not have been notified yet */
        ((BalsaMessage *) mw->bmessage)->message = NULL;
        gtk_widget_destroy(mw->window);
    }
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
select_part_cb(BalsaMessage * bm, gpointer data)
{
    gboolean enable;

    if (balsa_message_can_select(bm))
	enable = TRUE;
    else
	enable = FALSE;

    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_COPY_POS].widget, enable);
    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_SELECT_ALL_POS].widget,
			     enable);
}


static void
copy_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    if (mw->bmessage)
	balsa_message_copy_clipboard(BALSA_MESSAGE(mw->bmessage));
}

static void
select_all_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);

    if (mw->bmessage)
	balsa_message_select_all(BALSA_MESSAGE(mw->bmessage));
}

static void next_unread_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    BalsaIndex *idx;
    GList *list;
    LibBalsaMessage *msg;

    weak_unref_and_destroy(mw);

    balsa_index_select_next_unread(
	idx=BALSA_INDEX(
	    balsa_window_find_current_index(balsa_app.main_window)));

    list = balsa_index_selected_list(idx);
    if (g_list_length(list) != 1) {
        g_list_free(list);
        return;
    }

    msg = LIBBALSA_MESSAGE(list->data);
    g_list_free(list);
    if(!msg)
	return;

    message_window_new(msg);
}

static void next_flagged_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    BalsaIndex *idx;
    GList *list;
    LibBalsaMessage *msg;

    weak_unref_and_destroy(mw);

    balsa_index_select_next_flagged(
	idx=BALSA_INDEX(
	    balsa_window_find_current_index(balsa_app.main_window)));

    list = balsa_index_selected_list(idx);
    if (g_list_length(list) != 1) {
        g_list_free(list);
        return;
    }

    msg = LIBBALSA_MESSAGE(list->data);
    g_list_free(list);
    if(!msg)
	return;

    message_window_new(msg);
}


static void print_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    LibBalsaMessage *msg;
    
    msg=mw->message;
    message_print(msg);
}

static void trash_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    LibBalsaMailbox *mailbox = mw->message->mailbox;

    balsa_message_move_to_trash(widget,
                                balsa_find_index_by_mailbox(mailbox));

    weak_unref_and_destroy(mw);
}

static void
show_all_headers_tool_cb(GtkWidget * widget, gpointer data)
{
    MessageWindow *mw = (MessageWindow *) (data);
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));

    if (balsa_toolbar_get_button_active(toolbar,
                                        BALSA_PIXMAP_SHOW_HEADERS)) {
        mw->show_all_headers_save=mw->headers_shown;
		mw->headers_shown=HEADERS_ALL;
	balsa_message_set_displayed_headers(BALSA_MESSAGE(mw->bmessage),
					    HEADERS_ALL);
    } else {
        if(mw->show_all_headers_save == -1)
            return;

        switch(mw->show_all_headers_save) {
        case HEADERS_NONE:
            show_no_headers_cb(widget, data);
            break;
        case HEADERS_ALL:
            show_all_headers_cb(widget, data);
            break;
        case HEADERS_SELECTED:
        default:
            show_selected_cb(widget, data);
            break;
        }
        mw->show_all_headers_save=-1;
    }
}

void reset_show_all_headers(MessageWindow *mw)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(mw->window));

    mw->show_all_headers_save = -1;
    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_SHOW_HEADERS, FALSE);
}

/* helper */
static void
weak_unref_and_destroy(MessageWindow * mw)
{
    g_object_weak_unref(G_OBJECT(mw->message),
                        (GWeakNotify) mw_message_weak_ref_cb, mw);
    gtk_widget_destroy(mw->window);
}
