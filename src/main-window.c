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

#include <string.h>
#include <gnome.h>
#include <gdk/gdkx.h>

#ifdef USE_PIXBUF
#include <gdk-pixbuf/gdk-pixbuf.h>
#endif

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "balsa-index-page.h"
#include "main.h"
#include "message-window.h"
#include "pref-manager.h"
#include "sendmsg-window.h"
#include "mailbox-conf.h"
#include "mblist-window.h"
#include "main-window.h"
#include "print.h"
#include "address-book.h"
#include "save-restore.h"	/*mailbox_get_pkey */
#include "store-address.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include "libinit_balsa/init_balsa.h"

#define MAILBOX_DATA "mailbox_data"

#define APPBAR_KEY "balsa_appbar"

enum {
    OPEN_MAILBOX,
    CLOSE_MAILBOX,
    LAST_SIGNAL
};

#ifdef BALSA_USE_THREADS
/* Define thread-related globals, including dialogs */
GtkWidget *progress_dialog = NULL;
GtkWidget *progress_dialog_source = NULL;
GtkWidget *progress_dialog_message = NULL;
GtkWidget *progress_dialog_bar = NULL;
GtkWidget *send_progress = NULL;
GtkWidget *send_progress_message = NULL;
GtkWidget *send_dialog = NULL;
GtkWidget *send_dialog_bar = NULL;
/*  int total_messages_left;*/
GSList *list = NULL;

void progress_dialog_destroy_cb(GtkWidget *, gpointer data);
static void check_messages_thread(gpointer data);
#endif

static void balsa_window_class_init(BalsaWindowClass * klass);
static void balsa_window_init(BalsaWindow * window);
static void balsa_window_real_open_mailbox(BalsaWindow * window,
					   LibBalsaMailbox * mailbox);
static void balsa_window_real_close_mailbox(BalsaWindow * window,
					    LibBalsaMailbox * mailbox);
static void balsa_window_destroy(GtkObject * object);

GtkWidget *balsa_window_find_current_index(BalsaWindow * window);
void balsa_window_open_mailbox(BalsaWindow * window,
			       LibBalsaMailbox * mailbox);
void balsa_window_close_mailbox(BalsaWindow * window,
				LibBalsaMailbox * mailbox);

static void balsa_window_select_message_cb(GtkWidget * widget,
					   LibBalsaMessage * message,
					   GdkEventButton * bevent,
					   gpointer data);
static void balsa_window_unselect_message_cb(GtkWidget * widget,
					     LibBalsaMessage * message,
					     GdkEventButton * bevent,
					     gpointer data);

static void check_mailbox_list(GList * list);
static gint mailbox_check_func(GNode * mbox, gpointer data);

static void enable_mailbox_menus(LibBalsaMailbox * mailbox);
static void enable_message_menus(LibBalsaMessage * message);
static void enable_edit_menus(BalsaMessage * bm);

static gint about_box_visible = FALSE;

/* dialogs */
static void show_about_box(void);

/* callbacks */
static void check_new_messages_cb(GtkWidget *, gpointer data);
static void send_outbox_messages_cb(GtkWidget *, gpointer data);

static void new_message_cb(GtkWidget * widget, gpointer data);
static void replyto_message_cb(GtkWidget * widget, gpointer data);
static void replytoall_message_cb(GtkWidget * widget, gpointer data);
static void forward_message_cb(GtkWidget * widget, gpointer data);
static void continue_message_cb(GtkWidget * widget, gpointer data);

static void next_message_cb(GtkWidget * widget, gpointer data);
static void next_unread_message_cb(GtkWidget * widget, gpointer data);
static void previous_message_cb(GtkWidget * widget, gpointer data);

static void next_part_cb(GtkWidget * widget, gpointer data);
static void previous_part_cb(GtkWidget * widget, gpointer data);
static void save_current_part_cb(GtkWidget * widget, gpointer data);

static void delete_message_cb(GtkWidget * widget, gpointer data);
static void undelete_message_cb(GtkWidget * widget, gpointer data);
static void toggle_flagged_message_cb(GtkWidget * widget, gpointer data);
static void store_address_cb(GtkWidget * widget, gpointer data);
static void wrap_message_cb(GtkWidget * widget, gpointer data);
static void show_no_headers_cb(GtkWidget * widget, gpointer data);
static void show_selected_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_cb(GtkWidget * widget, gpointer data);

static void copy_cb(GtkWidget * widget, gpointer data);
static void select_all_cb(GtkWidget * widget, gpointer);

static void select_part_cb(BalsaMessage * bm, gpointer data);

#ifdef BALSA_SHOW_ALL
static void filter_dlg_cb(GtkWidget * widget, gpointer data);
#endif

gboolean balsa_close_mailbox_on_timer(GtkWidget * widget, gpointer * data);

static void mailbox_close_cb(GtkWidget * widget, gpointer data);
static void mailbox_commit_changes(GtkWidget * widget, gpointer data);
static void mailbox_empty_trash(GtkWidget * widget, gpointer data);

static void show_mbtree_cb(GtkWidget * widget, gpointer data);
static void show_mbtabs_cb(GtkWidget * widget, gpointer data);
static void about_box_destroy_cb(void);

static void set_icon(GnomeApp * app);

static void notebook_size_alloc_cb(GtkWidget * notebook,
				   GtkAllocation * alloc);
static void mw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc);

static void notebook_switch_page_cb(GtkWidget * notebook,
				    GtkNotebookPage * page,
				    guint page_num);
static void send_msg_window_destroy_cb(GtkWidget * widget, gpointer data);

static GnomeUIInfo file_new_menu[] = {
#define MENU_FILE_NEW_MESSAGE_POS 0
    {
     GNOME_APP_UI_ITEM, N_("_Message..."), N_("Compose a new message"),
     new_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_NEW, 'M', 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_NEW_NEW_MAILBOX_POS 1
    GNOMEUIINFO_ITEM_STOCK(N_("_Mailbox..."), N_("Add a new mailbox"),
			   mblist_menu_add_cb, GNOME_STOCK_PIXMAP_ADD),
    GNOMEUIINFO_END
};

static GnomeUIInfo file_menu[] = {
#define MENU_FILE_NEW_POS 0
    GNOMEUIINFO_SUBTREE(N_("_New"), file_new_menu),
#define MENU_FILE_CONTINUE_POS 1
    /* C */
    {
     GNOME_APP_UI_ITEM, N_("_Continue"),
     N_("Continue editing current message"),
     continue_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL, 'C', 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_GET_NEW_MAIL_POS 3
    /* Ctrl-M */
    {
     GNOME_APP_UI_ITEM, N_("_Get New Mail"), N_("Fetch new incoming mail"),
     check_new_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_RCV, 'M', GDK_CONTROL_MASK, NULL},
#define MENU_FILE_SEND_QUEUED_POS 4
    /* Ctrl-S */
    {
     GNOME_APP_UI_ITEM, N_("_Send Queued Mail"),
     N_("Send mail from the outbox"),
     send_outbox_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_SND, 'A', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_PRINT_POS 6
    GNOMEUIINFO_MENU_PRINT_ITEM(message_print_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    {
     GNOME_APP_UI_ITEM, N_("_Address Book..."),
     N_("Opens the address book"),
     address_book_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_BOOK_RED, 'B', 0, NULL},

    GNOMEUIINFO_SEPARATOR,

#if 0
    {
     GNOME_APP_UI_ITEM, "Test new init",
     "Test the new initialization druid",
     balsa_init_begin, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_RCV, '\0', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#endif

    GNOMEUIINFO_MENU_EXIT_ITEM(balsa_exit, NULL),

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
    /* GNOMEUINFO_SEPARATOR, */
    /*  GNOMEUIINFO_MENU_FIND_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(NULL, NULL); */
    /*  GNOMEUIINFO_MENU_REPLACE_ITEM(NULL, NULL); */
/*     GNOMEUIINFO_SEPARATOR, */
/* #define MENU_EDIT_PREFERENCES_POS 3 */
/*     GNOMEUIINFO_MENU_PREFERENCES_ITEM(open_preferences_manager, NULL), */
#ifdef BALSA_SHOW_ALL
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_ITEM_STOCK(N_("_Filters..."), N_("Manage filters"),
			   filter_dlg_cb, GNOME_STOCK_MENU_PROP),
#endif
    GNOMEUIINFO_END
};

static GnomeUIInfo shown_hdrs_menu[] = {
    GNOMEUIINFO_RADIOITEM(N_("N_o Headers"), N_("Display no headers"),
			  show_no_headers_cb, NULL),
    GNOMEUIINFO_RADIOITEM(N_("S_elected Headers"),
			  N_("Display selected headers"),
			  show_selected_cb, NULL),
    GNOMEUIINFO_RADIOITEM(N_("All _Headers"), N_("Display all headers"),
			  show_all_headers_cb, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] = {
#define MENU_VIEW_MAILBOX_LIST_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Show Mailbox Tree"),
			   "Toggle display of mailbox and folder tree",
			   show_mbtree_cb, NULL),
#define MENU_VIEW_MAILBOX_TABS_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("Show Mailbox _Tabs"),
			   "Toggle display of mailbox notebook tabs",
			   show_mbtabs_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_WRAP_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Wrap"), "Wrap message lines",
			   wrap_message_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(shown_hdrs_menu),
    GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] = {
#define MENU_MESSAGE_REPLY_POS 0
    /* R */
    {
     GNOME_APP_UI_ITEM, N_("_Reply..."),
     N_("Reply to the current message"),
     replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_RPL, 'R', 0, NULL},
#define MENU_MESSAGE_REPLY_ALL_POS 1
    /* A */
    {
     GNOME_APP_UI_ITEM, N_("Reply To _All..."),
     N_("Reply to all recipients of the current message"),
     replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MAIL_RPL_ALL_MENU, 'A', 0, NULL},
#define MENU_MESSAGE_FORWARD_POS 2
    /* F */
    {
     GNOME_APP_UI_ITEM, N_("_Forward..."),
     N_("Forward the current message"),
     forward_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_MAIL_FWD, 'F', 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_NEXT_PART_POS 4
    {
     GNOME_APP_UI_ITEM, N_("Next Part"), N_("Next Part in Message"),
     next_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_FORWARD, '.', GDK_CONTROL_MASK, NULL},
#define MENU_MESSAGE_PREVIOUS_PART_POS 5
    {
     GNOME_APP_UI_ITEM, N_("Previous Part"),
     N_("Previous Part in Message"),
     previous_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_BACK, ',', GDK_CONTROL_MASK, NULL},
#define MENU_MESSAGE_SAVE_PART_POS 6
    {
     GNOME_APP_UI_ITEM, N_("Save Current Part..."),
     N_("Save Current Part in Message"),
     save_current_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_SAVE, 's', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_DELETE_POS 8
    /* D */
    {
     GNOME_APP_UI_ITEM, N_("_Delete"), N_("Delete the current message"),
     delete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_TRASH, 'D', 0, NULL},
#define MENU_MESSAGE_UNDEL_POS 9
    /* U */
    {
     GNOME_APP_UI_ITEM, N_("_Undelete"), N_("Undelete the message"),
     undelete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_UNDELETE, 'U', 0, NULL},
#define MENU_MESSAGE_TOGGLE_FLAGGED_POS 10
    /* ! */
    {
     GNOME_APP_UI_ITEM, N_("_Toggle Flagged"), N_("Toggle flagged"),
     toggle_flagged_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_FLAGGED, 'X', 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_STORE_ADDRESS_POS 12
    /* S */
    {
     GNOME_APP_UI_ITEM, N_("_Store Address..."),
     N_("Store address of sender in addressbook"),
     store_address_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_BOOK_RED, 'S', 0, NULL},
    GNOMEUIINFO_END
};

static GnomeUIInfo mailbox_menu[] = {
#define MENU_MAILBOX_NEXT_POS 0
    {
     GNOME_APP_UI_ITEM, N_("Next Message"), N_("Next Message"),
     next_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_FORWARD, 'N', 0, NULL},
#define MENU_MAILBOX_PREV_POS 1
    {
     GNOME_APP_UI_ITEM, N_("Previous Message"), N_("Previous Message"),
     previous_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_MENU_BACK, 'P', 0, NULL},
#define MENU_MAILBOX_NEXT_UNREAD_POS 2
    {
     GNOME_APP_UI_ITEM, N_("Next Unread Message"),
     N_("Next Unread Message"),
     next_unread_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_NEXT_UNREAD_MENU, 'N', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EDIT_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("_Edit..."), N_("Edit the selected mailbox"),
			   mblist_menu_edit_cb, GNOME_STOCK_MENU_PREF),
#define MENU_MAILBOX_DELETE_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("_Delete..."),
			   N_("Delete the selected mailbox"),
			   mblist_menu_delete_cb,
			   GNOME_STOCK_PIXMAP_REMOVE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_COMMIT_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("Co_mmit Current"),
			   N_
			   ("Commit the changes in the currently opened mailbox"),
			   mailbox_commit_changes,
			   GNOME_STOCK_MENU_REFRESH),
#define MENU_MAILBOX_CLOSE_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("_Close"), N_("Close mailbox"),
			   mailbox_close_cb, GNOME_STOCK_MENU_CLOSE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EMPTY_TRASH_POS 10
    GNOMEUIINFO_ITEM_STOCK(N_("Empty _Trash"),
			   N_("Delete Messages from the trash mailbox"),
			   mailbox_empty_trash, GNOME_STOCK_PIXMAP_REMOVE),
    GNOMEUIINFO_END
};

static GnomeUIInfo settings_menu[] = {
#define MENU_SETTINGS_PREFERENCES_POS 0
    GNOMEUIINFO_MENU_PREFERENCES_ITEM (open_preferences_manager, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo help_menu[] = {
    GNOMEUIINFO_MENU_ABOUT_ITEM(show_about_box, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_HELP("balsa"),
    GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] = {
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
    GNOMEUIINFO_MENU_VIEW_TREE(view_menu),
    GNOMEUIINFO_SUBTREE(N_("_Message"), message_menu),
    GNOMEUIINFO_SUBTREE(N_("Mail_box"), mailbox_menu),
    GNOMEUIINFO_MENU_SETTINGS_TREE (settings_menu),
    GNOMEUIINFO_MENU_HELP_TREE(help_menu),
    GNOMEUIINFO_END
};

static GnomeUIInfo main_toolbar[] = {
#define TOOLBAR_CHECK_POS 0
    GNOMEUIINFO_ITEM_STOCK(N_("Check"), N_("Check Email"),
			   check_new_messages_cb,
			   GNOME_STOCK_PIXMAP_MAIL_RCV),
    GNOMEUIINFO_SEPARATOR,
#define TOOLBAR_DELETE_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("Delete"), N_("Delete Message"),
			   delete_message_cb,
			   GNOME_STOCK_PIXMAP_TRASH),
    GNOMEUIINFO_SEPARATOR,
#define TOOLBAR_COMPOSE_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("Compose"), N_("Compose Message"),
			   new_message_cb,
			   GNOME_STOCK_PIXMAP_MAIL_NEW),
#define TOOLBAR_CONTINUE_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("Continue"), N_("Continue"),
			   continue_message_cb,
			   GNOME_STOCK_PIXMAP_MAIL),
#define TOOLBAR_REPLY_POS 6
    GNOMEUIINFO_ITEM_STOCK(N_("Reply"), N_("Reply"),
			   replyto_message_cb,
			   GNOME_STOCK_PIXMAP_MAIL_RPL),
#define TOOLBAR_REPLY_ALL_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("Reply\nTo All"), N_("Reply to all"),
			   replytoall_message_cb,
			   BALSA_PIXMAP_MAIL_RPL_ALL),
#define TOOLBAR_FORWARD_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("Forward"), N_("Forward"),
			   forward_message_cb,
			   GNOME_STOCK_PIXMAP_MAIL_FWD),
    GNOMEUIINFO_SEPARATOR,
#define TOOLBAR_PREVIOUS_POS 10
    {
     GNOME_APP_UI_ITEM, N_("Previous"), N_("Open Previous message"),
     previous_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_PIXMAP_BACK, 0, 0, NULL},
#define TOOLBAR_NEXT_POS 11
    {
     GNOME_APP_UI_ITEM, N_("Next"), N_("Open Next message"),
     next_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     GNOME_STOCK_PIXMAP_FORWARD, 0, 0, NULL},
#define TOOLBAR_NEXT_UNREAD_POS 12
    GNOMEUIINFO_ITEM_STOCK(N_("Next\nUnread"), N_("Open Next Unread Message"),
     next_unread_message_cb, BALSA_PIXMAP_NEXT_UNREAD),
    GNOMEUIINFO_SEPARATOR,
#define TOOLBAR_PRINT_POS 14
    GNOMEUIINFO_ITEM_STOCK(N_("Print"), N_("Print current message"),
			   message_print_cb, GNOME_STOCK_PIXMAP_PRINT),

    GNOMEUIINFO_END
};

static GnomeAppClass *parent_class = NULL;
static guint window_signals[LAST_SIGNAL] = { 0 };

GtkType
balsa_window_get_type(void)
{
    static GtkType window_type = 0;

    if (!window_type) {
	static const GtkTypeInfo window_info = {
	    "BalsaWindow",
	    sizeof(BalsaWindow),
	    sizeof(BalsaWindowClass),
	    (GtkClassInitFunc) balsa_window_class_init,
	    (GtkObjectInitFunc) balsa_window_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	window_type = gtk_type_unique(gnome_app_get_type(), &window_info);
    }

    return window_type;
}

static void
balsa_window_class_init(BalsaWindowClass * klass)
{
    GtkObjectClass *object_class;
    GtkWidgetClass *widget_class;

    object_class = (GtkObjectClass *) klass;
    widget_class = (GtkWidgetClass *) klass;

    parent_class = gtk_type_class(gnome_app_get_type());

#if 0
    window_signals[SET_CURSOR] =
	gtk_signal_new("set_cursor",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaWindowClass, set_cursor),
		       gtk_marshal_NONE__POINTER,
		       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);
#endif
    window_signals[OPEN_MAILBOX] =
	gtk_signal_new("open_mailbox",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaWindowClass, open_mailbox),
		       gtk_marshal_NONE__POINTER,
		       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

    window_signals[CLOSE_MAILBOX] =
	gtk_signal_new("close_mailbox",
		       GTK_RUN_LAST,
		       object_class->type,
		       GTK_SIGNAL_OFFSET(BalsaWindowClass, close_mailbox),
		       gtk_marshal_NONE__POINTER,
		       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

    gtk_object_class_add_signals(object_class, window_signals,
				 LAST_SIGNAL);

    object_class->destroy = balsa_window_destroy;

    klass->open_mailbox = balsa_window_real_open_mailbox;
    klass->close_mailbox = balsa_window_real_close_mailbox;

    gtk_timeout_add(30000, (GtkFunction) balsa_close_mailbox_on_timer,
		    NULL);

}

static void
balsa_window_init(BalsaWindow * window)
{
}

GtkWidget *
balsa_window_new()
{
    BalsaWindow *window;
    GnomeAppBar *appbar;
    GtkWidget *hpaned;
    GtkWidget *vpaned;
    GtkWidget *scroll;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS - allows for grey out */
    register_balsa_pixmaps();

    window = gtk_type_new(BALSA_TYPE_WINDOW);
    gnome_app_construct(GNOME_APP(window), "balsa", "Balsa");

    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);
    gnome_app_create_toolbar_with_data(GNOME_APP(window), main_toolbar,
				       window);

    /* Disable menu items at start up */
    enable_mailbox_menus(NULL);
    enable_message_menus(NULL);
    enable_edit_menus(NULL);
    balsa_window_enable_continue();

    /* we can only set icon after realization, as we have no windows before. */
    gtk_signal_connect(GTK_OBJECT(window), "realize",
		       GTK_SIGNAL_FUNC(set_icon), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "size_allocate",
		       GTK_SIGNAL_FUNC(mw_size_alloc_cb), NULL);

    appbar =
	GNOME_APPBAR(gnome_appbar_new(TRUE, TRUE, GNOME_PREFERENCES_USER));
    gnome_app_set_statusbar(GNOME_APP(window), GTK_WIDGET(appbar));
    gtk_object_set_data(GTK_OBJECT(window), APPBAR_KEY, appbar);
    balsa_app.appbar = appbar;
    gnome_app_install_appbar_menu_hints(GNOME_APPBAR(balsa_app.appbar),
					main_menu);

    gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), balsa_app.mw_width,
				balsa_app.mw_height);

    vpaned = gtk_vpaned_new();
    hpaned = gtk_hpaned_new();
    window->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook),
			       balsa_app.show_notebook_tabs);
    gtk_notebook_set_show_border(GTK_NOTEBOOK(window->notebook), FALSE);
    gtk_signal_connect(GTK_OBJECT(window->notebook), "size_allocate",
		       GTK_SIGNAL_FUNC(notebook_size_alloc_cb), NULL);
    gtk_signal_connect(GTK_OBJECT(window->notebook), "switch_page",
		       GTK_SIGNAL_FUNC(notebook_switch_page_cb), NULL);
    balsa_app.notebook = window->notebook;

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);

    window->preview = balsa_message_new();

    gtk_signal_connect(GTK_OBJECT(window->preview), "select-part",
		       GTK_SIGNAL_FUNC(select_part_cb), window);

    gtk_container_add(GTK_CONTAINER(scroll), window->preview);
    gtk_widget_show(scroll);

    gnome_app_set_contents(GNOME_APP(window), hpaned);

    /* XXX */
    window->mblist = balsa_mailbox_list_window_new(window);
    gtk_paned_pack1(GTK_PANED(hpaned), window->mblist, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(hpaned), vpaned, TRUE, TRUE);
    /*PKGW: do it this way, without the usizes. */
    if (balsa_app.show_mblist)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (view_menu[MENU_VIEW_MAILBOX_LIST_POS].widget),
				       balsa_app.show_mblist);

    gtk_paned_pack1(GTK_PANED(vpaned), window->notebook, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(vpaned), scroll, TRUE, TRUE);

    /*PKGW: do it this way, without the usizes. */
    if (balsa_app.previewpane)
	gtk_paned_set_position(GTK_PANED(vpaned),
			       balsa_app.notebook_height);
    else
	/* Set it to something really high */
	gtk_paned_set_position(GTK_PANED(vpaned), G_MAXINT);

    gtk_widget_show(vpaned);
    gtk_widget_show(hpaned);
    gtk_widget_show(window->notebook);
    gtk_widget_show(window->preview);

    /* set the toolbar style */
    balsa_window_refresh(window);

    if (balsa_app.check_mail_upon_startup)
	check_new_messages_cb(NULL, NULL);

    if (balsa_app.browse_wrap)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (view_menu[MENU_VIEW_WRAP_POS].widget),
				       TRUE);

    if (balsa_app.shown_headers >= HEADERS_NONE
	&& balsa_app.shown_headers <= HEADERS_ALL)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (shown_hdrs_menu[balsa_app.shown_headers].widget),
				       TRUE);

    if (balsa_app.show_notebook_tabs)
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
				       (view_menu[MENU_VIEW_MAILBOX_TABS_POS].widget),
				       TRUE);

    return GTK_WIDGET(window);
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a mailbox open. 
 */
static void
enable_mailbox_menus(LibBalsaMailbox * mailbox)
{
    gboolean enable;

    if (mailbox == NULL)
	enable = FALSE;
    else
	enable = TRUE;

    if (mailbox && mailbox->readonly) {
	gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget, FALSE);
    } else {
	gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget, enable);
    }

    /* Toolbar */
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_PREVIOUS_POS].widget,
			     enable);
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_NEXT_POS].widget,
			     enable);
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_NEXT_UNREAD_POS].widget,
			     enable);

    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_DELETE_POS].widget,
			     enable);

    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_NEXT_POS].widget,
			     enable);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_PREV_POS].widget,
			     enable);
    gtk_widget_set_sensitive(mailbox_menu
			     [MENU_MAILBOX_NEXT_UNREAD_POS].widget,
			     enable);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_CLOSE_POS].widget,
			     enable);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_EDIT_POS].widget,
			     enable);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_DELETE_POS].widget,
			     enable);
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a message selected. 
 */
static void
enable_message_menus(LibBalsaMessage * message)
{
    gboolean enable;

    if (message)
	enable = TRUE;
    else
	enable = FALSE;

    /* Handle menu items which require write access to mailbox */
    if (message && message->mailbox->readonly) {
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_DELETE_POS].widget, FALSE);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_UNDEL_POS].widget, FALSE);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TOGGLE_FLAGGED_POS].widget,
				 FALSE);

	gtk_widget_set_sensitive(main_toolbar[TOOLBAR_DELETE_POS].widget, FALSE);
    } else {
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_DELETE_POS].widget, enable);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_UNDEL_POS].widget, enable);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TOGGLE_FLAGGED_POS].widget, enable);

	gtk_widget_set_sensitive(main_toolbar[TOOLBAR_DELETE_POS].widget, enable);
    }

    /* Handle items which require multiple parts to the mail */
    if (message && !libbalsa_message_has_attachment(message)) {
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_NEXT_PART_POS].widget, FALSE);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_PREVIOUS_PART_POS].widget, FALSE);
    } else {
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_NEXT_PART_POS].widget, enable);
	gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_PREVIOUS_PART_POS].widget, enable);
    }


    gtk_widget_set_sensitive(file_menu[MENU_FILE_PRINT_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_SAVE_PART_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_REPLY_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_REPLY_ALL_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_FORWARD_POS].widget, enable);

    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_STORE_ADDRESS_POS].widget, enable);

    /* Toolbar */
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_REPLY_POS].widget, enable);
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_REPLY_ALL_POS].widget, enable);
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_FORWARD_POS].widget, enable);
    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_PRINT_POS].widget, enable);

    balsa_window_enable_continue();
}

/*
 * Enable/disable the copy and select all buttons
 */
static void
enable_edit_menus(BalsaMessage * bm)
{
    gboolean enable;
    if (bm && balsa_message_can_select(bm))
	enable = TRUE;
    else
	enable = FALSE;

    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_COPY_POS].widget, enable);
    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_SELECT_ALL_POS].widget,
			     enable);
}

/*
 * Enable/disable the continue buttons
 */
void
balsa_window_enable_continue(void)
{
    /* Check msg count in draftbox */
    if (balsa_app.draftbox) {

        /* This is commented out because it causes long delays and
         * flickering of the mailbox list if large numbers of messages
         * are selected.  Checking the has_unread_messages flag works
         * almost as well. 
         * */
/* 	libbalsa_mailbox_open(balsa_app.draftbox, FALSE); */
/* 	if (balsa_app.draftbox->total_messages > 0) { */

        if (balsa_app.draftbox->has_unread_messages) {
	    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_CONTINUE_POS].widget, TRUE);
	    gtk_widget_set_sensitive(file_menu[MENU_FILE_CONTINUE_POS].widget,
				     TRUE);
	} else {
	    gtk_widget_set_sensitive(main_toolbar[TOOLBAR_CONTINUE_POS].widget, FALSE);
	    gtk_widget_set_sensitive(file_menu[MENU_FILE_CONTINUE_POS].widget,
				     FALSE);
	}

/* 	libbalsa_mailbox_close(balsa_app.draftbox); */
    }
}

#if 0
void
balsa_window_set_cursor(BalsaWindow * window, GdkCursor * cursor)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    gtk_signal_emit(GTK_OBJECT(window), window_signals[SET_CURSOR],
		    cursor);
}

static void
balsa_window_real_set_cursor(BalsaWindow * window, GdkCursor * cursor)
{
    /* XXX fixme to work with NULL cursors
       gtk_widget_set_sensitive (GTK_WIDGET(window->progress_bar), FALSE);
       gtk_progress_set_activity_mode (GTK_WIDGET(window->progress_bar), FALSE);
       gtk_timeout_remove (pbar_timeout);
       gtk_progress_set_value (GTK_PROGRESS (pbar), 0.0); */
    gdk_window_set_cursor(GTK_WIDGET(window)->window, cursor);
}
#endif

/* balsa_window_open_mailbox: 
   opens mailbox, creates message index. mblist_open_mailbox() is what
   you want most of the time because it can switch between pages if a
   mailbox is already on one of them.
*/
void
balsa_window_open_mailbox(BalsaWindow * window, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    gtk_signal_emit(GTK_OBJECT(window), window_signals[OPEN_MAILBOX],
		    mailbox);
}

void
balsa_window_close_mailbox(BalsaWindow * window, LibBalsaMailbox * mailbox)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    gtk_signal_emit(GTK_OBJECT(window), window_signals[CLOSE_MAILBOX],
		    mailbox);
}

static void
balsa_window_real_open_mailbox(BalsaWindow * window,
			       LibBalsaMailbox * mailbox)
{
    GtkObject *page;
    BalsaIndex *index;
    GtkWidget *label;

    page = balsa_index_page_new(window);

    index = BALSA_INDEX(BALSA_INDEX_PAGE(page)->index);

    gtk_signal_connect(GTK_OBJECT(index), "select_message",
		       GTK_SIGNAL_FUNC(balsa_window_select_message_cb),
		       window);
    gtk_signal_connect(GTK_OBJECT(index), "unselect_message",
		       GTK_SIGNAL_FUNC(balsa_window_unselect_message_cb),
		       window);

    if (balsa_index_page_load_mailbox(BALSA_INDEX_PAGE(page), mailbox)) {
	/* The function will display a dialog on error */
	gtk_object_destroy(GTK_OBJECT(page));
	return;
    }
    label =
	gtk_label_new(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->
		      mailbox->name);

    /* store for easy access */
    gtk_object_set_data(GTK_OBJECT(BALSA_INDEX_PAGE(page)->sw),
			"indexpage", page);
    gtk_notebook_append_page(GTK_NOTEBOOK(window->notebook),
			     GTK_WIDGET(BALSA_INDEX_PAGE(page)->sw),
			     label);

    /* change the page to the newly selected notebook item */
    gtk_notebook_set_page(GTK_NOTEBOOK(window->notebook),
			  gtk_notebook_page_num(GTK_NOTEBOOK
						(window->notebook),
						GTK_WIDGET(BALSA_INDEX_PAGE
							   (page)->sw)));

    balsa_app.open_mailbox_list =
	g_list_prepend(balsa_app.open_mailbox_list, mailbox);

    /* Enable relavent menu items... */
    enable_mailbox_menus(mailbox);
}


static void
balsa_window_real_close_mailbox(BalsaWindow * window,
				LibBalsaMailbox * mailbox)
{
    GtkWidget *page;
    BalsaIndex *index = NULL;
    gint i;

    i = balsa_find_notebook_page_num(mailbox);

    if (i != -1) {
	page =
	    gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);
	page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");
	gtk_notebook_remove_page(GTK_NOTEBOOK(window->notebook), i);

	(BALSA_INDEX_PAGE(page))->sw = NULL;	/* This was just toasted */
	gtk_object_destroy(GTK_OBJECT(page));

	/* If this is the last notebook page clear the message preview
	   and the status bar */
	page =
	    gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), 0);

	if (page == NULL) {
	    gtk_window_set_title(GTK_WINDOW(window), "Balsa");
	    balsa_message_clear(BALSA_MESSAGE(window->preview));
	    gnome_appbar_set_default(balsa_app.appbar, "Mailbox closed");

	    /* Unselect any mailbox */
	    gtk_clist_unselect_all(GTK_CLIST(balsa_app.mblist));

	    /* Disable menus */
	    enable_mailbox_menus(NULL);
	    enable_message_menus(NULL);
	    enable_edit_menus(NULL);
	}

	balsa_app.open_mailbox_list =
	    g_list_remove(balsa_app.open_mailbox_list, mailbox);
    }

    /* we use (BalsaIndex*) instead of BALSA_INDEX because we don't want
       ugly conversion warning when balsa_window_find_current_index
       returns NULL. */
    index = (BalsaIndex *) balsa_window_find_current_index(window);
    if (index)
	balsa_mblist_focus_mailbox(balsa_app.mblist, index->mailbox);
}

gboolean
balsa_close_mailbox_on_timer(GtkWidget * widget, gpointer * data)
{
    GTimeVal current_time;
    GtkWidget *page, *index_page;
    int i, c, time;

    g_get_current_time(&current_time);

    c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

    for (i = 0;
	 (page =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {
	if (i == c)
	    continue;
	index_page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");
	time =
	    current_time.tv_sec -
	    BALSA_INDEX_PAGE(index_page)->last_use.tv_sec;
	if (time > 600) {
	    if (balsa_app.debug)
		fprintf(stderr, "Closing Page %d, time: %d\n", i, time);
	    gtk_notebook_remove_page(GTK_NOTEBOOK(balsa_app.notebook), i);
	    BALSA_INDEX_PAGE(index_page)->sw = NULL;
	    gtk_object_destroy(GTK_OBJECT(index_page));
	    if (i < c)
		c--;
	    i--;
	}
    }
    return TRUE;
}

static void
balsa_window_destroy(GtkObject * object)
{
    BalsaWindow *window;

    window = BALSA_WINDOW(object);

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

    /* don't try to use notebook later in empty_trash */
    balsa_app.notebook = NULL;
    balsa_exit();
}

/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow * window)
{
    GnomeDockItem *item;
    GtkWidget *toolbar;
    GtkWidget *index;
    BalsaMessage *bmsg;
    GtkWidget *paned;

    g_return_if_fail(window);

    index = balsa_window_find_current_index(window);
    if (index) {
	paned = GTK_WIDGET(balsa_app.notebook)->parent;

	if (balsa_app.previewpane) {
	    balsa_index_redraw_current(BALSA_INDEX(index));
	    gtk_paned_set_position(GTK_PANED(paned),
				   balsa_app.notebook_height);
	} else {
	    bmsg = BALSA_MESSAGE(BALSA_WINDOW(window)->preview);
	    if (bmsg)
		balsa_message_clear(bmsg);
	    /* Set the height to something really big (those new hi-res
	       screens and all :) */
	    gtk_paned_set_position(GTK_PANED(paned), G_MAXINT);
	}
    }
    /*
     * set the toolbar style
     */
    item = gnome_app_get_dock_item_by_name(GNOME_APP(window),
					   GNOME_APP_TOOLBAR_NAME);
    toolbar = gnome_dock_item_get_child(item);
    gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), balsa_app.toolbar_style);

    /* I don't know if this is a bug of gtk or not but if this is not here
       it doesn't properly resize after a toolbar style change */
    gtk_widget_queue_resize(GTK_WIDGET(window));
}

/*
 * show the about box for Balsa
 */
static void
show_about_box(void)
{
    GtkWidget *about;
    const gchar *authors[] = {
	"Héctor García Álvarez <hector@scouts-es.org>",
	"Ian Campbell <ijc25@cam.ac.uk>",
	"Bruno Pires Marinho <bapm@camoes.rnl.ist.utl.pt>",
	"Jay Painter <jpaint@gimp.org>",
	"Stuart Parmenter <pavlov@pavlov.net>",
	"David Pickens <dpickens@iaesthetic.com>",
	"Pawel Salek <pawsa@theochem.kth.se>",
	"Peter Williams <peter@newton.cx>",
	"Matthew Guenther <guentherm@asme.org>",
	"All the folks on Balsa-List <balsa-list-request@gnome.org>",
	NULL
    };


    /* only show one about box at a time */
    if (about_box_visible)
	return;
    else
	about_box_visible = TRUE;

    about = gnome_about_new("Balsa",
			    BALSA_VERSION,
			    _("Copyright (C) 1997-2000"),
			    authors,
			    _
			    ("The Balsa email client is part of the GNOME desktop environment.  Information on Balsa can be found at http://www.balsa.net/\n\nIf you need to report bugs, please do so at: http://bugs.gnome.org/"),
			    "balsa/balsa_logo.png");

    gtk_signal_connect(GTK_OBJECT(about),
		       "destroy",
		       (GtkSignalFunc) about_box_destroy_cb, NULL);

    gtk_widget_show(about);
}


/* Check all mailboxes in a list
 *
 * FIXME: Some (all) of these are POP mailboxes, and so grabbing the lock causes a delay.
 * We might as well not have the thread :-(
 */
static void
check_mailbox_list(GList * mailbox_list)
{
    GList *list;
    LibBalsaMailbox *mailbox;

    list = g_list_first(mailbox_list);
    while (list) {
	mailbox = LIBBALSA_MAILBOX(list->data);

#ifdef BALSA_USE_THREADS
	gdk_threads_enter();
#endif
	libbalsa_mailbox_check(mailbox);

#ifdef BALSA_USE_THREADS
	gdk_threads_leave();
#endif

	list = g_list_next(list);

    }

}

/*Callback to check a mailbox in a balsa-mblist */
static gint
mailbox_check_func(GNode * node, gpointer data)
{
    MailboxNode *mbnode = (MailboxNode *) node->data;

    if (!mbnode || mbnode->IsDir)
	return FALSE;

#ifdef BALSA_USE_THREADS
    gdk_threads_enter();
#endif

    libbalsa_mailbox_check(mbnode->mailbox);

#ifdef BALSA_USE_THREADS
    gdk_threads_leave();
#endif
    return FALSE;
}

/*
 * Callbacks
 */

gint
check_new_messages_auto_cb(gpointer data)
{
    check_new_messages_cb((GtkWidget *) NULL, data);

    if (balsa_app.debug)
	fprintf(stderr, "Auto-checked for new messages...\n");

    /*  preserver timer */
    return TRUE;
}

/* check_new_messages_cb:
   check new messages the data argument is the BalsaWindow pointer
   or NULL.
*/
static void
check_new_messages_cb(GtkWidget * widget, gpointer data)
{
    libbalsa_notify_start_check();

#ifdef BALSA_USE_THREADS
    /*  Only Run once -- If already checking mail, return.  */
    pthread_mutex_lock(&mailbox_lock);
    if (checking_mail) {
	pthread_mutex_unlock(&mailbox_lock);
	fprintf(stderr, "Already Checking Mail!  \n");
	return;
    }
    checking_mail = 1;
    pthread_mutex_unlock(&mailbox_lock);

    if (balsa_app.pwindow_option == WHILERETR ||
	(balsa_app.pwindow_option == UNTILCLOSED &&
	 !(progress_dialog && GTK_IS_WIDGET(progress_dialog)))) {
	if (progress_dialog && GTK_IS_WIDGET(progress_dialog))
	    gtk_widget_destroy(GTK_WIDGET(progress_dialog));

	progress_dialog =
	    gnome_dialog_new("Checking Mail...", "Hide", NULL);
	if (balsa_app.main_window)
	    gnome_dialog_set_parent(GNOME_DIALOG(progress_dialog),
				    GTK_WINDOW(balsa_app.main_window));
	gtk_signal_connect(GTK_OBJECT(progress_dialog), "destroy",
			   GTK_SIGNAL_FUNC(progress_dialog_destroy_cb),
			   NULL);

	gnome_dialog_set_close(GNOME_DIALOG(progress_dialog), TRUE);

	progress_dialog_source = gtk_label_new("Checking Mail....");
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(progress_dialog)->vbox),
			   progress_dialog_source, FALSE, FALSE, 0);

	progress_dialog_message = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(progress_dialog)->vbox),
			   progress_dialog_message, FALSE, FALSE, 0);

	progress_dialog_bar = gtk_progress_bar_new();
	gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(progress_dialog)->vbox),
			   progress_dialog_bar, FALSE, FALSE, 0);

	gtk_widget_show_all(progress_dialog);
    }

    /* initiate threads */
    pthread_create(&get_mail_thread,
		   NULL, (void *) &check_messages_thread, NULL);
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(get_mail_thread);

#else
    check_mailbox_list(balsa_app.inbox_input);

    libbalsa_mailbox_check(balsa_app.inbox);
    libbalsa_mailbox_check(balsa_app.sentbox);
    libbalsa_mailbox_check(balsa_app.draftbox);
    libbalsa_mailbox_check(balsa_app.outbox);
    libbalsa_mailbox_check(balsa_app.trash);

    g_node_traverse(balsa_app.mailbox_nodes, G_LEVEL_ORDER, G_TRAVERSE_ALL,
		    -1, (GNodeTraverseFunc) mailbox_check_func, NULL);

    balsa_mblist_have_new(balsa_app.mblist);
#endif
}

/* send_outbox_messages_cb:
   tries again to send the messages queued in outbox.
*/
static void
send_outbox_messages_cb(GtkWidget * widget, gpointer data)
{
    libbalsa_message_send(NULL, balsa_app.outbox,
			  balsa_app.encoding_style);
}

/* this one is called only in the threaded code */
#ifdef BALSA_USE_THREADS

static void
check_messages_thread(gpointer data)
{
    /*  
     *  It is assumed that this will always be called as a pthread,
     *  and that the calling procedure will check for an existing lock
     *  and set checking_mail to true before calling.
     */
    MailThreadMessage *threadmessage;

    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_SOURCE, NULL, "POP3", 0, 0);
    check_mailbox_list(balsa_app.inbox_input);

    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_SOURCE, NULL, "Local Mail",
		  0, 0);

    gdk_threads_enter();
    libbalsa_mailbox_check(balsa_app.inbox);
    libbalsa_mailbox_check(balsa_app.sentbox);
    libbalsa_mailbox_check(balsa_app.draftbox);
    libbalsa_mailbox_check(balsa_app.outbox);
    libbalsa_mailbox_check(balsa_app.trash);
    gdk_threads_leave();

    g_node_traverse(balsa_app.mailbox_nodes, G_LEVEL_ORDER, G_TRAVERSE_ALL,
		    -1, (GNodeTraverseFunc) mailbox_check_func, NULL);

    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_FINISHED, NULL, "Finished",
		  0, 0);

    pthread_mutex_lock(&mailbox_lock);
    checking_mail = 0;
    pthread_mutex_unlock(&mailbox_lock);

    pthread_exit(0);
}

/* mail_progress_notify_cb:
   called from the thread checking the new mail. Basically does the GUI
   interaction because checking thread cannot do it.
*/
gboolean
mail_progress_notify_cb()
{
    const int MSG_BUFFER_SIZE = 512 * sizeof(MailThreadMessage *);
    MailThreadMessage *threadmessage;
    MailThreadMessage **currentpos;
    void *msgbuffer;
    uint count;
    gfloat percent;
    GtkWidget *errorbox;

    msgbuffer = g_malloc(MSG_BUFFER_SIZE);

    g_io_channel_read(mail_thread_msg_receive, msgbuffer,
		      MSG_BUFFER_SIZE, &count);

    /* FIXME: imagine reading just half of the pointer. The sync is gone.. */
    if (count < sizeof(MailThreadMessage *)) {
	g_free(msgbuffer);
	return TRUE;
    }

    currentpos = (MailThreadMessage **) msgbuffer;

    gdk_threads_enter();

    while (count) {
	threadmessage = *currentpos;

	if (balsa_app.debug)
	    fprintf(stderr, "Message: %lu, %d, %s\n",
		    (unsigned long) threadmessage,
		    threadmessage->message_type,
		    threadmessage->message_string);
	switch (threadmessage->message_type) {
	case MSGMAILTHREAD_SOURCE:
	    if (progress_dialog && GTK_IS_WIDGET(progress_dialog)) {
		gtk_label_set_text(GTK_LABEL(progress_dialog_source),
				   threadmessage->message_string);
		gtk_label_set_text(GTK_LABEL(progress_dialog_message), "");
		gtk_widget_show_all(progress_dialog);
	    } else {
		gnome_appbar_set_status(balsa_app.appbar,
					threadmessage->message_string);
	    }
	    break;
	case MSGMAILTHREAD_MSGINFO:
	    if (progress_dialog && GTK_IS_WIDGET(progress_dialog)) {
		gtk_label_set_text(GTK_LABEL(progress_dialog_message),
				   threadmessage->message_string);
		gtk_widget_show_all(progress_dialog);
	    } else {
		gnome_appbar_set_status(balsa_app.appbar,
					threadmessage->message_string);
	    }
	    break;
	case MSGMAILTHREAD_UPDATECONFIG:
	    config_mailbox_update(threadmessage->mailbox);
	    break;

	case MSGMAILTHREAD_PROGRESS:
	    percent = (gfloat) threadmessage->num_bytes /
		(gfloat) threadmessage->tot_bytes;
	    if (percent > 1.0 || percent < 0.0) {
		if (balsa_app.debug)
		    fprintf(stderr,
			    "progress bar percentage out of range %f\n",
			    percent);
		percent = 1.0;
	    }
	    if (progress_dialog && GTK_IS_WIDGET(progress_dialog))
		gtk_progress_bar_update(GTK_PROGRESS_BAR
					(progress_dialog_bar), percent);
	    else
		gnome_appbar_set_progress(balsa_app.appbar, percent);
	    break;
	case MSGMAILTHREAD_FINISHED:

	    if (balsa_app.pwindow_option == WHILERETR && progress_dialog
		&& GTK_IS_WIDGET(progress_dialog)) {
		gtk_widget_destroy(progress_dialog);
		progress_dialog = NULL;
	    } else if (progress_dialog && GTK_IS_WIDGET(progress_dialog)) {
		gtk_label_set_text(GTK_LABEL(progress_dialog_source),
				   "Finished Checking.");
		gtk_progress_bar_update(GTK_PROGRESS_BAR
					(progress_dialog_bar), 0.0);
	    } else {
		gnome_appbar_refresh(balsa_app.appbar);
		gnome_appbar_set_progress(balsa_app.appbar, 0.0);
	    }
	    balsa_mblist_have_new(balsa_app.mblist);
	    break;

	case MSGMAILTHREAD_ERROR:
	    errorbox = gnome_message_box_new(threadmessage->message_string,
					     GNOME_MESSAGE_BOX_ERROR,
					     GNOME_STOCK_BUTTON_OK, NULL);
	    gnome_dialog_run(GNOME_DIALOG(errorbox));
	    break;

	default:
	    fprintf(stderr, " Unknown (%d): %s \n",
		    threadmessage->message_type,
		    threadmessage->message_string);

	}
	g_free(threadmessage);
	currentpos++;
	count -= sizeof(void *);
    }
    g_free(msgbuffer);

    gdk_threads_leave();

    return TRUE;
}


void
progress_dialog_destroy_cb(GtkWidget * widget, gpointer data)
{
    gtk_widget_destroy(widget);
    progress_dialog = NULL;
    progress_dialog_source = NULL;
    progress_dialog_message = NULL;
    progress_dialog_bar = NULL;
}


gboolean
send_progress_notify_cb()
{
    SendThreadMessage *threadmessage;
    SendThreadMessage **currentpos;
    void *msgbuffer;
    uint count;
    GSList *node;
    LibBalsaMessage *message;
    float percent;

    msgbuffer = malloc(2049);

    g_io_channel_read(send_thread_msg_receive, msgbuffer, 2048, &count);

    if (count < sizeof(void *)) {
	free(msgbuffer);
	return TRUE;
    }

    currentpos = (SendThreadMessage **) msgbuffer;

    gdk_threads_enter();

    while (count) {
	threadmessage = *currentpos;

	if (balsa_app.debug)
	    fprintf(stderr, "Send_Message: %lu, %d, %s\n",
		    (unsigned long) threadmessage,
		    threadmessage->message_type,
		    threadmessage->message_string);

	switch (threadmessage->message_type) {
	case MSGSENDTHREADERROR:
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("Sending error: %s"),
			      threadmessage->message_string);
	    break;

	case MSGSENDTHREADPOSTPONE:

	    fprintf(stderr, "Send Postpone %s\n",
		    threadmessage->message_string);
	    break;

	case MSGSENDTHREADPROGRESS:

	    percent = threadmessage->of_total;

	    if (percent == 0) {
		gtk_label_set_text(GTK_LABEL(send_progress_message),
				   threadmessage->message_string);
		gtk_widget_show_all(send_dialog);
	    }

	    if (percent > 1.0 || percent < 0.0) {
		if (balsa_app.debug)
		    fprintf(stderr,
			    "progress bar percentage out of range %f\n",
			    percent);
		percent = 1.0;

	    }

	    if (GTK_IS_WIDGET(send_dialog))
		gtk_progress_bar_update(GTK_PROGRESS_BAR(send_dialog_bar),
					percent);
	    else
		gnome_appbar_set_progress(balsa_app.appbar, percent);

	    /* display progress x of y, y = of_total */
	    break;

	case MSGSENDTHREADDELETE:
	    /* passes message to be deleted */

	    if (threadmessage->msg != NULL) {
		if (threadmessage->msg->mailbox != NULL)
		    list = g_slist_append(list, threadmessage->msg);
	    }

	    if (!strcmp(threadmessage->message_string, "LAST")
		&& threadmessage->msg == NULL) {
		libbalsa_mailbox_open(balsa_app.outbox, FALSE);
		node = list;

		while (node != NULL) {
		    message = node->data;
		    if (message->mailbox)
			libbalsa_message_delete(message);
		    gtk_object_destroy(GTK_OBJECT(message));
		    node = node->next;
		}
		libbalsa_mailbox_close(balsa_app.outbox);
		g_slist_free(list);
		list = NULL;
	    }

	    break;

	case MSGSENDTHREADFINISHED:
	    /* closes progress dialog */

	    if (GTK_IS_WIDGET(send_dialog)) {
		gtk_widget_destroy(send_dialog);
		send_dialog = NULL;
	    }
	    if (balsa_app.compose_email)
		balsa_exit();

	    break;

	default:
	    fprintf(stderr, " Unknown: %s \n",
		    threadmessage->message_string);
	}
	free(threadmessage);
	currentpos++;
	count -= sizeof(void *);
    }

    gdk_threads_leave();

    free(msgbuffer);

    return TRUE;
}
#endif

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    GtkWidget *page;

    g_return_val_if_fail(window != NULL, NULL);

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(window->notebook),
				     gtk_notebook_get_current_page
				     (GTK_NOTEBOOK(window->notebook)));

    if (!page)
	return NULL;

    /* get the real page.. not the scrolled window */
    page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");

    if (!page)
	return NULL;

    return GTK_WIDGET(BALSA_INDEX_PAGE(page)->index);
}


static void
new_message_cb(GtkWidget * widget, gpointer data)
{
    BalsaSendmsg *smwindow;

    smwindow = sendmsg_window_new(widget, NULL, SEND_NORMAL);

    gtk_signal_connect(GTK_OBJECT(smwindow->window), "destroy",
		       GTK_SIGNAL_FUNC(send_msg_window_destroy_cb), NULL);
}


static void
replyto_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_reply(widget,
			balsa_window_find_current_index(BALSA_WINDOW
							(data)));
}

static void
replytoall_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_replytoall(widget,
			     balsa_window_find_current_index(BALSA_WINDOW
							     (data)));
}

static void
forward_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_forward(widget,
			  balsa_window_find_current_index(BALSA_WINDOW
							  (data)));
}


static void
continue_message_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index && BALSA_INDEX(index)->mailbox == balsa_app.draftbox)
	balsa_message_continue(widget, BALSA_INDEX(index));
    else
	mblist_open_mailbox(balsa_app.draftbox);
}


static void
next_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_next(widget,
		       balsa_window_find_current_index(BALSA_WINDOW
						       (data)));
}

static void
next_unread_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_next_unread(widget,
			      balsa_window_find_current_index(BALSA_WINDOW
							      (data)));
}

static void
previous_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_previous(widget,
			   balsa_window_find_current_index(BALSA_WINDOW
							   (data)));
}

static void
next_part_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    bw = BALSA_WINDOW(data);

    if (bw->preview) {
	balsa_message_next_part(BALSA_MESSAGE(bw->preview));
	enable_edit_menus(BALSA_MESSAGE(bw->preview));
    }
}

static void
previous_part_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    bw = BALSA_WINDOW(data);
    if (bw->preview) {
	balsa_message_previous_part(BALSA_MESSAGE(bw->preview));
	enable_edit_menus(BALSA_MESSAGE(bw->preview));
    }
}

static void
copy_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    bw = BALSA_WINDOW(data);

    if (bw->preview)
	balsa_message_copy_clipboard(BALSA_MESSAGE(bw->preview));
}

static void
select_all_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    bw = BALSA_WINDOW(data);

    if (bw->preview)
	balsa_message_select_all(BALSA_MESSAGE(bw->preview));
}

static void
save_current_part_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    bw = BALSA_WINDOW(data);
    if (bw->preview)
	balsa_message_save_current_part(BALSA_MESSAGE(bw->preview));
}

static void
delete_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_delete(widget,
			 balsa_window_find_current_index(BALSA_WINDOW
							 (data)));
}


static void
toggle_flagged_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_toggle_flagged(widget,
				 balsa_window_find_current_index
				 (BALSA_WINDOW(data)));
}

static void
undelete_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_undelete(widget,
			   balsa_window_find_current_index(BALSA_WINDOW
							   (data)));
}

static void
store_address_cb(GtkWidget * widget, gpointer data)
{
    g_return_if_fail(balsa_window_find_current_index(BALSA_WINDOW(data)) !=
		     NULL);

    balsa_store_address(widget,
			balsa_window_find_current_index(BALSA_WINDOW
							(data)));
}

static void
wrap_message_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    balsa_app.browse_wrap = GTK_CHECK_MENU_ITEM(widget)->active;

    bw = BALSA_WINDOW(data);
    if (bw->preview)
	balsa_message_set_wrap(BALSA_MESSAGE(bw->preview),
			       balsa_app.browse_wrap);
}

static void
show_no_headers_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    if (!GTK_CHECK_MENU_ITEM(widget)->active)
	return;

    balsa_app.shown_headers = HEADERS_NONE;

    bw = BALSA_WINDOW(data);

    if (bw->preview)
	balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
					    HEADERS_NONE);
}

static void
show_selected_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    if (!GTK_CHECK_MENU_ITEM(widget)->active)
	return;

    balsa_app.shown_headers = HEADERS_SELECTED;

    bw = BALSA_WINDOW(data);
    if (bw->preview)
	balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
					    HEADERS_SELECTED);
}

static void
show_all_headers_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    if (!GTK_CHECK_MENU_ITEM(widget)->active)
	return;

    balsa_app.shown_headers = HEADERS_ALL;

    bw = BALSA_WINDOW(data);
    if (bw->preview)
	balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
					    HEADERS_ALL);
}

#ifdef BALSA_SHOW_ALL
static void
filter_dlg_cb(GtkWidget * widget, gpointer data)
{
    filter_edit_dialog(NULL);
}
#endif

/* closes the mailbox on the notebook's active page */
static void
mailbox_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
	balsa_window_close_mailbox(BALSA_WINDOW(data),
				   BALSA_INDEX(index)->mailbox);
}


static void
mailbox_commit_changes(GtkWidget * widget, gpointer data)
{
    LibBalsaMailbox *current_mailbox;
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    g_return_if_fail(index != NULL);

    current_mailbox = BALSA_INDEX(index)->mailbox;
    if (libbalsa_mailbox_commit_changes(current_mailbox) != 0)
	balsa_information(LIBBALSA_INFORMATION_WARNING,
			  _("Commiting mailbox %s failed."),
			  current_mailbox->name);
}

static void
mailbox_empty_trash(GtkWidget * widget, gpointer data)
{
    BalsaIndexPage *page;
    GList *message;

    libbalsa_mailbox_open(balsa_app.trash, FALSE);

    message = balsa_app.trash->message_list;

    while (message) {
	libbalsa_message_delete(message->data);
	message = message->next;
    }
    libbalsa_mailbox_commit_changes(balsa_app.trash);

    libbalsa_mailbox_close(balsa_app.trash);

    if ((page = balsa_find_notebook_page(balsa_app.trash)))
	balsa_index_page_reset(page);

}

static void
show_mbtree_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *parent;
    parent = GTK_WIDGET(BALSA_WINDOW(data)->mblist)->parent;
    g_assert(parent != NULL);

    balsa_app.show_mblist = GTK_CHECK_MENU_ITEM(widget)->active;
    if (balsa_app.show_mblist) {
	gtk_widget_show(BALSA_WINDOW(data)->mblist);
	gtk_paned_set_position(GTK_PANED(parent), balsa_app.mblist_width);
    } else {
	gtk_widget_hide(BALSA_WINDOW(data)->mblist);
	gtk_paned_set_position(GTK_PANED(parent), 0);
    }
}

static void
show_mbtabs_cb(GtkWidget * widget, gpointer data)
{
    balsa_app.show_notebook_tabs = GTK_CHECK_MENU_ITEM(widget)->active;
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(balsa_app.notebook),
			       balsa_app.show_notebook_tabs);
}

static void
about_box_destroy_cb(void)
{
    about_box_visible = FALSE;
}

static void
set_icon(GnomeApp * app)
{
#ifdef USE_PIXBUF
    GdkPixbuf *pb = NULL;
#else
    GdkImlibImage *im = NULL;
#endif
    GdkWindow *ic_win, *w;
    GdkWindowAttr att;
    XIconSize *is;
    gint i, count, j;
    GdkPixmap *pmap, *mask;

    w = GTK_WIDGET(app)->window;

    if ((XGetIconSizes(GDK_DISPLAY(), GDK_ROOT_WINDOW(), &is, &count))
	&& (count > 0)) {
	i = 0;			/* use first icon size - not much point using the others */
	att.width = is[i].max_width;
	att.height = is[i].max_height;
	/*
	 * raster had:
	 * att.height = 3 * att.width / 4;
	 * but this didn't work  (it scaled the icons incorrectly
	 */

	/* make sure the icon is inside the min and max sizes */
	if (att.height < is[i].min_height)
	    att.height = is[i].min_height;
	if (att.height > is[i].max_height)
	    att.height = is[i].max_height;
	if (is[i].width_inc > 0) {
	    j = ((att.width - is[i].min_width) / is[i].width_inc);
	    att.width = is[i].min_width + (j * is[i].width_inc);
	}
	if (is[i].height_inc > 0) {
	    j = ((att.height - is[i].min_height) / is[i].height_inc);
	    att.height = is[i].min_height + (j * is[i].height_inc);
	}
	XFree(is);
    } else {
	/* no icon size hints at all? ok - invent our own size */
	att.width = 32;
	att.height = 24;
    }
    att.event_mask = GDK_ALL_EVENTS_MASK;
    att.wclass = GDK_INPUT_OUTPUT;
    att.window_type = GDK_WINDOW_TOPLEVEL;
    att.x = 0;
    att.y = 0;
#ifdef USE_PIXBUF
    att.visual = gdk_rgb_get_visual();
    att.colormap = gdk_rgb_get_cmap();
#else
    att.visual = gdk_imlib_get_visual();
    att.colormap = gdk_imlib_get_colormap();
#endif
    ic_win = gdk_window_new(NULL, &att, GDK_WA_VISUAL | GDK_WA_COLORMAP);
    {
	/*char *filename = gnome_unconditional_pixmap_file ("balsa/balsa_icon.png"); */
	char *filename = balsa_pixmap_finder("balsa/balsa_icon.png");

#ifdef USE_PIXBUF
	pb = gdk_pixbuf_new_from_file(filename);
#else
	im = gdk_imlib_load_image(filename);
#endif
	g_free(filename);
    }
    gdk_window_set_icon(w, ic_win, NULL, NULL);
#ifndef USE_PIXBUF
    gdk_imlib_render(im, att.width, att.height);
    pmap = gdk_imlib_move_image(im);
    mask = gdk_imlib_move_mask(im);
    gdk_window_set_back_pixmap(ic_win, pmap, FALSE);
#endif
    gdk_window_clear(ic_win);
#ifndef USE_PIXBUF
    gdk_window_shape_combine_mask(ic_win, mask, 0, 0);
    gdk_imlib_free_pixmap(pmap);
    gdk_imlib_destroy_image(im);
#endif
#ifdef USE_PIXBUF
    gdk_pixbuf_unref(pb);
#endif
}

/* PKGW: remember when they change the position of the vpaned. */
static void
notebook_size_alloc_cb(GtkWidget * notebook, GtkAllocation * alloc)
{
    if (balsa_app.previewpane)
	balsa_app.notebook_height = alloc->height;
}

static void
mw_size_alloc_cb(GtkWidget * window, GtkAllocation * alloc)
{
    balsa_app.mw_height = alloc->height;
    balsa_app.mw_width = alloc->width;
}

/* When page is switched we change the preview window and the selected 
   mailbox in the mailbox tree.
 */
static void
notebook_switch_page_cb(GtkWidget * notebook,
			GtkNotebookPage * page, guint page_num)
{
    GtkWidget *index_page;
    GtkWidget *window;
    GtkWidget *index;
    LibBalsaMailbox *mailbox;
    LibBalsaMessage *message;
    gchar *title;

    index_page = gtk_object_get_data(GTK_OBJECT(page->child), "indexpage");

    mailbox = BALSA_INDEX_PAGE(index_page)->mailbox;
    window = BALSA_INDEX_PAGE(index_page)->window;
    index = BALSA_INDEX_PAGE(index_page)->index;

    if (mailbox->name) {
	if (mailbox->readonly) {
	    title =
		g_strdup_printf(_("Balsa: %s (readonly)"), mailbox->name);
	} else {
	    title = g_strdup_printf(_("Balsa: %s"), mailbox->name);
	}
	gtk_window_set_title(GTK_WINDOW(window), title);
	g_free(title);
    } else {
	gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    }

    balsa_index_update_message(BALSA_INDEX_PAGE(index_page));

    if (GTK_CLIST(index)->selection) {
	message = message = gtk_clist_get_row_data(GTK_CLIST(index),
						   GPOINTER_TO_INT
						   (GTK_CLIST
						    (index)->selection->
						    data));
	enable_message_menus(message);
    } else {
	enable_message_menus(NULL);
    }

    enable_mailbox_menus(mailbox);
}

static void
balsa_window_select_message_cb(GtkWidget * widget,
			       LibBalsaMessage * message,
			       GdkEventButton * bevent, gpointer data)
{
    enable_mailbox_menus(message->mailbox);
    enable_message_menus(message);
}

static void
balsa_window_unselect_message_cb(GtkWidget * widget,
				 LibBalsaMessage * message,
				 GdkEventButton * bevent, gpointer data)
{
    enable_mailbox_menus(message->mailbox);
    enable_message_menus(NULL);
    enable_edit_menus(NULL);
}

static void
select_part_cb(BalsaMessage * bm, gpointer data)
{
    enable_edit_menus(bm);
}

static void
send_msg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue();
}
