/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* vim:set ts=4 sw=4 ai et: */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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
#include <gdk/gdkx.h> /* for XIconSize */
#include <gdk-pixbuf/gdk-pixbuf.h>


#ifdef BALSA_USE_THREADS
#include <pthread.h>
#endif

#include "libbalsa.h"

#include "address-book.h"
#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "folder-conf.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "message-window.h"
#include "pref-manager.h"
#include "print.h"
#include "sendmsg-window.h"
#include "store-address.h"
#include "save-restore.h"
#include "toolbar-prefs.h"
#include "toolbar-factory.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#ifdef BALSA_SHOW_ALL
#include "filter.h"
#include "filter-funcs.h"
#endif

#include "libinit_balsa/init_balsa.h"

#define MAILBOX_DATA "mailbox_data"

#define APPBAR_KEY "balsa_appbar"

enum {
    OPEN_MAILBOX_NODE,
    CLOSE_MAILBOX_NODE,
    LAST_SIGNAL
};

gint balsa_window_progress_timeout(gpointer user_data);
enum {
    BALSA_PROGRESS_NONE = 0,
    BALSA_PROGRESS_ACTIVITY,
    BALSA_PROGRESS_INCREMENT
};

enum {
    TARGET_MESSAGES
};

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

#define NUM_DROP_TYPES 1
static GtkTargetEntry notebook_drop_types[NUM_DROP_TYPES] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

#ifdef BALSA_USE_THREADS
/* Define thread-related globals, including dialogs */
GtkWidget *progress_dialog = NULL;
GtkWidget *progress_dialog_source = NULL;
GtkWidget *progress_dialog_message = NULL;
GtkWidget *progress_dialog_bar = NULL;
GSList *list = NULL;
static gint new_mail_dialog_visible = FALSE;
static int quiet_check=0;

static void check_messages_thread(gpointer data);
static void count_unread_msgs_func(GtkCTree * ctree, GtkCTreeNode * node,
				   gpointer data);
static void display_new_mail_notification(int);

#endif

static int show_all_headers_save=-1;

static void balsa_window_class_init(BalsaWindowClass * klass);
static void balsa_window_init(BalsaWindow * window);
static void balsa_window_real_open_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_real_close_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_destroy(GtkObject * object);

GtkWidget *balsa_window_find_current_index(BalsaWindow * window);
static gboolean balsa_close_mailbox_on_timer(GtkWidget * widget, 
					     gpointer * data);

static void balsa_window_select_message_cb(GtkWidget * widget,
					   LibBalsaMessage * message,
					   GdkEventButton * bevent,
					   gpointer data);
static void balsa_window_unselect_message_cb(GtkWidget * widget,
					     LibBalsaMessage * message,
					     GdkEventButton * bevent,
					     gpointer data);
static void balsa_window_unselect_all_messages_cb (GtkWidget* widget, 
                                                   gpointer data);


static void check_mailbox_list(GList * list);
static void mailbox_check_func(GtkCTree * ctree, GtkCTreeNode * node,
			       gpointer data);
static gboolean imap_check_test(const gchar * path);

static void enable_mailbox_menus(BalsaMailboxNode * mbnode);
static void enable_message_menus(LibBalsaMessage * message);
static void enable_edit_menus(BalsaMessage * bm);
static void register_open_mailbox(LibBalsaMailbox *m);
static void unregister_open_mailbox(LibBalsaMailbox *m);
static gboolean is_open_mailbox(LibBalsaMailbox *m);

static gint about_box_visible = FALSE;

/* dialogs */
static void show_about_box(void);

/* callbacks */
static void send_outbox_messages_cb(GtkWidget *, gpointer data);
static void send_receive_messages_cb(GtkWidget *, gpointer data);

static void new_message_cb(GtkWidget * widget, gpointer data);
static void replyto_message_cb(GtkWidget * widget, gpointer data);
static void replytoall_message_cb(GtkWidget * widget, gpointer data);
static void replytogroup_message_cb(GtkWidget * widget, gpointer data);
static void forward_message_attached_cb(GtkWidget * widget, gpointer data);
static void forward_message_inline_cb(GtkWidget * widget, gpointer data);
static void forward_message_default_cb(GtkWidget * widget, gpointer data);
static void continue_message_cb(GtkWidget * widget, gpointer data);

static void next_message_cb(GtkWidget * widget, gpointer data);
static void next_unread_message_cb(GtkWidget * widget, gpointer data);
static void next_flagged_message_cb(GtkWidget * widget, gpointer data);
static void previous_message_cb(GtkWidget * widget, gpointer data);

static void next_part_cb(GtkWidget * widget, gpointer data);
static void previous_part_cb(GtkWidget * widget, gpointer data);
static void save_current_part_cb(GtkWidget * widget, gpointer data);
static void view_msg_source_cb(GtkWidget * widget, gpointer data);

static void trash_message_cb(GtkWidget * widget, gpointer data);
static void delete_message_cb(GtkWidget * widget, gpointer data);
static void undelete_message_cb(GtkWidget * widget, gpointer data);
static void toggle_flagged_message_cb(GtkWidget * widget, gpointer data);
static void toggle_new_message_cb(GtkWidget * widget, gpointer data);
static void store_address_cb(GtkWidget * widget, gpointer data);
static void wrap_message_cb(GtkWidget * widget, gpointer data);
static void show_no_headers_cb(GtkWidget * widget, gpointer data);
static void show_selected_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_tool_cb(GtkWidget * widget, gpointer data);
static void reset_show_all_headers(void);
static void show_preview_pane_cb(GtkWidget * widget, gpointer data);

static void threading_flat_cb(GtkWidget * widget, gpointer data);
static void threading_simple_cb(GtkWidget * widget, gpointer data);
static void threading_jwz_cb(GtkWidget * widget, gpointer data);
static void expand_all_cb(GtkWidget * widget, gpointer data);
static void collapse_all_cb(GtkWidget * widget, gpointer data);

static void address_book_cb(GtkWindow *widget, gpointer data);

static void copy_cb(GtkWidget * widget, gpointer data);
static void select_all_cb(GtkWidget * widget, gpointer);
static void mark_all_cb(GtkWidget * widget, gpointer);

static void select_part_cb(BalsaMessage * bm, gpointer data);

#ifdef BALSA_SHOW_ALL
static void find_real(BalsaIndex * bindex,gboolean again);
static void find_cb(GtkWidget * widget, gpointer data);
static void find_again_cb(GtkWidget * widget, gpointer data);
static void filter_dlg_cb(GtkWidget * widget, gpointer data);
static void filter_export_cb(GtkWidget * widget, gpointer data);
#endif

static void mailbox_close_cb(GtkWidget * widget, gpointer data);
static void mailbox_tab_close_cb(GtkWidget * widget, gpointer data);

static void mailbox_commit_changes(GtkWidget * widget, gpointer data);
static void mailbox_commit_all(GtkWidget * widget, gpointer data);

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
static BalsaIndex* balsa_window_notebook_find_page (GtkNotebook* notebook, 
                                                    gint x, gint y);
static void notebook_drag_received_cb (GtkWidget* widget, 
                                            GdkDragContext* context, 
                                            gint x, gint y, 
                                            GtkSelectionData* selection_data, 
                                            guint info, guint32 time, 
                                            gpointer data);
static gboolean notebook_drag_motion_cb (GtkWidget* widget,
                                       GdkDragContext* context,
                                       gint x, gint y, guint time,
                                       gpointer user_data);


static GtkWidget *balsa_notebook_label_new (BalsaMailboxNode* mbnode);
static void ident_manage_dialog_cb(GtkWidget*, gpointer);


static void
balsa_quit_nicely(void)
{
    GdkEventAny e = { GDK_DELETE, NULL, 0 };
    e.window = GTK_WIDGET(balsa_app.main_window)->window;
    gdk_event_put((GdkEvent*)&e);
}

static GnomeUIInfo file_new_menu[] = {
#define MENU_FILE_NEW_MESSAGE_POS 0
    {
        GNOME_APP_UI_ITEM, N_("_Message..."), N_("Compose a new message"),
        new_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_COMPOSE, 'M', 0, NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_NEW_MBOX_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("Local mbox mailbox..."), 
                           N_("Add a new mbox style mailbox"),
                           mailbox_conf_add_mbox_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
#define MENU_FILE_NEW_MAILDIR_POS 2
    GNOMEUIINFO_ITEM_STOCK(N_("Local Maildir mailbox..."), 
                           N_("Add a new Maildir style mailbox"),
                           mailbox_conf_add_maildir_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
#define MENU_FILE_NEW_MH_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Local MH mailbox..."), 
                           N_("Add a new MH style mailbox"),
                           mailbox_conf_add_mh_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
#define MENU_FILE_NEW_IMAP_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP mailbox..."), 
                           N_("Add a new IMAP mailbox"),
                           mailbox_conf_add_imap_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_NEW_IMAP_FOLDER_POS 6
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP folder..."), 
                           N_("Add a new IMAP folder"),
                           folder_conf_add_imap_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
#define MENU_FILE_NEW_IMAP_SUBFOLDER_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP subfolder..."), 
                           N_("Add new IMAP subfolder"),
                           folder_conf_add_imap_sub_cb, 
                           GNOME_STOCK_PIXMAP_ADD),
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
     BALSA_PIXMAP_MENU_NEW, 'C', 0, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_GET_NEW_MAIL_POS 3
    /* Ctrl-M */
    {
     GNOME_APP_UI_ITEM, N_("_Get New Mail"), N_("Fetch new incoming mail"),
     check_new_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_RECEIVE, 'M', GDK_CONTROL_MASK, NULL},
#define MENU_FILE_SEND_QUEUED_POS 4
    /* Ctrl-S */
    {
     GNOME_APP_UI_ITEM, N_("_Send Queued Mail"),
     N_("Send messages from the outbox"),
     send_outbox_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_SEND, 'T', GDK_CONTROL_MASK, NULL},
#define MENU_FILE_SEND_RECEIVE_POS 5
    /* Ctrl-B */
    {
     GNOME_APP_UI_ITEM, N_("Send and _Receive Mail"),
     N_("Send and Receive messages"),
     send_receive_messages_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
     BALSA_PIXMAP_MENU_SEND_RECEIVE, 'B', GDK_CONTROL_MASK, NULL},
     GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_PRINT_POS 7
    { GNOME_APP_UI_ITEM, N_("_Print..."), 
      N_("Print currently selected messages"),
      message_print_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_PRINT, 'P', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_ADDRESS_POS 9
    {
	GNOME_APP_UI_ITEM, N_("_Address Book..."),
	N_("Open the address book"),
	address_book_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
	GNOME_STOCK_MENU_BOOK_RED, 'B', 0, NULL
    },
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_MENU_EXIT_ITEM(balsa_quit_nicely, NULL),

    GNOMEUIINFO_END
};

static GnomeUIInfo edit_menu[] = {
    /* FIXME: Features to hook up... */
    /*  GNOMEUIINFO_MENU_UNDO_ITEM(NULL, NULL), */
    /*  GNOMEUIINFO_MENU_REDO_ITEM(NULL, NULL), */
    /*  GNOMEUIINFO_SEPARATOR, */
#define MENU_EDIT_COPY_POS 0
    GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define MENU_EDIT_SELECT_ALL_POS 1
    GNOMEUIINFO_MENU_SELECT_ALL_ITEM(select_all_cb, NULL),
#ifdef BALSA_SHOW_ALL
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_MENU_FIND_ITEM(find_cb, NULL),
    GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(find_again_cb, NULL),
    /*  GNOMEUIINFO_MENU_REPLACE_ITEM(NULL, NULL); */
/*     GNOMEUIINFO_SEPARATOR, */
/* #define MENU_EDIT_PREFERENCES_POS 3 */
/*     GNOMEUIINFO_MENU_PREFERENCES_ITEM(open_preferences_manager, NULL), */
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_ITEM_STOCK(N_("_Filters..."), N_("Manage filters"),
                           filter_dlg_cb, GNOME_STOCK_MENU_PROP),
    GNOMEUIINFO_ITEM_STOCK(N_("_Export filters"), N_("Export filters as Sieve scripts"),
			   filter_export_cb, GNOME_STOCK_MENU_PROP),
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

static GnomeUIInfo threading_menu[] = {
#define MENU_THREADING_FLAT_POS 0
    GNOMEUIINFO_RADIOITEM(N_("_Flat index"), N_("No threading at all"),
                         threading_flat_cb, NULL),
#define MENU_THREADING_SIMPLE_POS 1
    GNOMEUIINFO_RADIOITEM(N_("S_imple threading"),
                          N_("Simple threading algorithm"),
                          threading_simple_cb, NULL),
#define MENU_THREADING_JWZ_POS 2
    GNOMEUIINFO_RADIOITEM(N_("_JWZ threading"), 
                          N_("Elaborate JWZ threading"),
                          threading_jwz_cb, NULL),
    GNOMEUIINFO_END
};


static GnomeUIInfo view_menu[] = {
#define MENU_VIEW_MAILBOX_LIST_POS 0
    GNOMEUIINFO_TOGGLEITEM(N_("_Show Mailbox Tree"),
                           N_("Toggle display of mailbox and folder tree"),
                           show_mbtree_cb, NULL),
#define MENU_VIEW_MAILBOX_TABS_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("Show Mailbox _Tabs"),
                           N_("Toggle display of mailbox notebook tabs"),
                           show_mbtabs_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_WRAP_POS 3
    GNOMEUIINFO_TOGGLEITEM(N_("_Wrap"), N_("Wrap message lines"),
                           wrap_message_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(shown_hdrs_menu),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(threading_menu),
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_EXPAND_ALL_POS 9
    { GNOME_APP_UI_ITEM, N_("_Expand All"),
     N_("Expand all threads"),
     expand_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'E', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_COLLAPSE_ALL_POS 10
    { GNOME_APP_UI_ITEM, N_("_Collapse All"),
     N_("Collapse all expanded threads"),
     collapse_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'C', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_END
};

static GnomeUIInfo message_toggle_menu[] = {
#define MENU_MESSAGE_TOGGLE_FLAGGED_POS 0
    /* ! */
    {
        GNOME_APP_UI_ITEM, N_("Flagged"), N_("Toggle flagged"),
        toggle_flagged_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_FLAGGED, 'X', 0, NULL
    },
#define MENU_MESSAGE_TOGGLE_NEW_POS 1
    /* ! */
    {
        GNOME_APP_UI_ITEM, N_("New"), N_("Toggle New"),
        toggle_new_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEW, 0, 0, NULL
    },
    GNOMEUIINFO_END
};

static GnomeUIInfo message_menu[] = {
#define MENU_MESSAGE_REPLY_POS 0
    /* R */
    {
        GNOME_APP_UI_ITEM, N_("_Reply..."),
        N_("Reply to the current message"),
        replyto_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_REPLY, 'R', 0, NULL
    },
#define MENU_MESSAGE_REPLY_ALL_POS 1
    /* A */
    {
        GNOME_APP_UI_ITEM, N_("Reply to _All..."),
        N_("Reply to all recipients of the current message"),
        replytoall_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_REPLY_ALL, 'A', 0, NULL
    },
#define MENU_MESSAGE_REPLY_GROUP_POS 2
    /* G */
    {
        GNOME_APP_UI_ITEM, N_("Reply to _Group..."),
        N_("Reply to mailing list"),
        replytogroup_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_REPLY_GROUP, 'G', 0, NULL
    },
#define MENU_MESSAGE_FORWARD_ATTACH_POS 3
    /* F */
    {
        GNOME_APP_UI_ITEM, N_("_Forward attached..."),
        N_("Forward the current message as attachment"),
        forward_message_attached_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_FORWARD, 'F', 0, NULL
    },
#define MENU_MESSAGE_FORWARD_INLINE_POS 4
    {
        GNOME_APP_UI_ITEM, N_("Forward inline..."),
        N_("Forward the current message inline"),
        forward_message_inline_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_FORWARD, 'F', GDK_CONTROL_MASK, NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_NEXT_PART_POS 6
    {
        GNOME_APP_UI_ITEM, N_("Next Part"), N_("Next part in message"),
        next_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT, '.', GDK_CONTROL_MASK, NULL
    },
#define MENU_MESSAGE_PREVIOUS_PART_POS 7
    {
        GNOME_APP_UI_ITEM, N_("Previous Part"),
        N_("Previous part in message"),
        previous_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_PREVIOUS, ',', GDK_CONTROL_MASK, NULL
    },
#define MENU_MESSAGE_SAVE_PART_POS 8
    {
        GNOME_APP_UI_ITEM, N_("Save Current Part..."),
        N_("Save current part in message"),
        save_current_part_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_SAVE, 's', GDK_CONTROL_MASK, NULL
    },
#define MENU_MESSAGE_SOURCE_POS 9
    {
        GNOME_APP_UI_ITEM, N_("_View Source..."),
        N_("View source form of the message"),
        view_msg_source_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_MENU_BOOK_OPEN, 'v', GDK_CONTROL_MASK, NULL
    },
	GNOMEUIINFO_SEPARATOR,   
#define MENU_MESSAGE_COPY_POS 11
	GNOMEUIINFO_MENU_COPY_ITEM(copy_cb, NULL),
#define MENU_MESSAGE_SELECT_ALL_POS 12
	{
		GNOME_APP_UI_ITEM, N_("_Select Text"),
		N_("Select entire mail"),
		select_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
		NULL, 'A', GDK_CONTROL_MASK, NULL
    },  
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_TRASH_POS 15
    /* D */
    {
        GNOME_APP_UI_ITEM, N_("_Move to Trash"), 
        N_("Move the current message to Trash mailbox"),
        trash_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_MENU_TRASH, 'D', 0, NULL
    },
#define MENU_MESSAGE_DELETE_POS 16
    { GNOME_APP_UI_ITEM, N_("_Delete"), 
      N_("Delete the current message"),
      delete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      GNOME_STOCK_MENU_TRASH, 'D', GDK_CONTROL_MASK, NULL },
#define MENU_MESSAGE_UNDEL_POS 17
    /* U */
    {
        GNOME_APP_UI_ITEM, N_("_Undelete"), N_("Undelete the message"),
        undelete_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_MENU_UNDELETE, 'U', 0, NULL
    },
#define MENU_MESSAGE_TOGGLE_POS 18
    /* ! */
    GNOMEUIINFO_SUBTREE(N_("_Toggle"), message_toggle_menu),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_STORE_ADDRESS_POS 19
    /* S */
    {
        GNOME_APP_UI_ITEM, N_("_Store Address..."),
        N_("Store address of sender in addressbook"),
        store_address_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_MENU_BOOK_RED, 'S', 0, NULL
    },
    GNOMEUIINFO_END
};

static GnomeUIInfo mailbox_menu[] = {
#define MENU_MAILBOX_NEXT_POS 0
    {
        GNOME_APP_UI_ITEM, N_("Next Message"), N_("Next Message"),
        next_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT, 'N', 0, NULL
    },
#define MENU_MAILBOX_PREV_POS 1
    {
        GNOME_APP_UI_ITEM, N_("Previous Message"), N_("Previous Message"),
        previous_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_PREVIOUS, 'P', 0, NULL
    },
#define MENU_MAILBOX_NEXT_UNREAD_POS 2
    {
        GNOME_APP_UI_ITEM, N_("Next Unread Message"),
        N_("Next Unread Message"),
        next_unread_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT_UNREAD, 'N', GDK_CONTROL_MASK, NULL
    },
#define MENU_MAILBOX_NEXT_FLAGGED_POS 3
    {
        GNOME_APP_UI_ITEM, N_("Next Flagged Message"),
        N_("Next Flagged Message"),
        next_flagged_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT_FLAGGED, 'F',GDK_MOD1_MASK|GDK_CONTROL_MASK, NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_MARK_ALL_POS 5
    {
        GNOME_APP_UI_ITEM, N_("Select all"),
        N_("Select all messages in current mailbox"),
        mark_all_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_MARK_ALL, 'A',GDK_CONTROL_MASK, NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EDIT_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("_Edit..."), N_("Edit the selected mailbox"),
                           mailbox_conf_edit_cb,
                           GNOME_STOCK_MENU_PREF),
#define MENU_MAILBOX_DELETE_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("_Delete..."),
                           N_("Delete the selected mailbox"),
                           mailbox_conf_delete_cb,
                           GNOME_STOCK_PIXMAP_REMOVE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_COMMIT_POS 10
    GNOMEUIINFO_ITEM_STOCK(
        N_("Co_mmit Current"),
        N_("Commit the changes in the currently opened mailbox"),
        mailbox_commit_changes,
        GNOME_STOCK_MENU_REFRESH),
    GNOMEUIINFO_ITEM_STOCK(
        N_("Commit _All"),
        N_("Commit the changes in all mailboxes"),
        mailbox_commit_all,
        GNOME_STOCK_MENU_REFRESH),
#define MENU_MAILBOX_CLOSE_POS 12
    GNOMEUIINFO_ITEM_STOCK(N_("_Close"), N_("Close mailbox"),
                           mailbox_close_cb, GNOME_STOCK_MENU_CLOSE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EMPTY_TRASH_POS 14
    GNOMEUIINFO_ITEM_STOCK(N_("Empty _Trash"),
                           N_("Delete messages from the Trash mailbox"),
                           empty_trash, GNOME_STOCK_PIXMAP_REMOVE),
    GNOMEUIINFO_END
};

static GnomeUIInfo settings_menu[] = {
#define MENU_SETTINGS_PREFERENCES_POS 0
    GNOMEUIINFO_MENU_PREFERENCES_ITEM (open_preferences_manager, NULL),
    GNOMEUIINFO_ITEM_STOCK(N_("_Customize..."),
                           N_("Customize toolbars and menus"),
                           customize_dialog_cb,
                           GNOME_STOCK_MENU_EXEC),
    GNOMEUIINFO_ITEM_STOCK(N_("_Identities..."), 
                           N_("Create and set current identities"), 
                           ident_manage_dialog_cb, 
                           BALSA_PIXMAP_MENU_IDENTITY),
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

    window_signals[OPEN_MAILBOX_NODE] =
        gtk_signal_new("open_mailbox_node",
                       GTK_RUN_LAST,
                       object_class->type,
                       GTK_SIGNAL_OFFSET(BalsaWindowClass, open_mbnode),
                       gtk_marshal_NONE__POINTER,
                       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

    window_signals[CLOSE_MAILBOX_NODE] =
        gtk_signal_new("close_mailbox_node",
                       GTK_RUN_LAST,
                       object_class->type,
                       GTK_SIGNAL_OFFSET(BalsaWindowClass, close_mbnode),
                       gtk_marshal_NONE__POINTER,
                       GTK_TYPE_NONE, 1, GTK_TYPE_POINTER);

    gtk_object_class_add_signals(object_class, window_signals,
                                 LAST_SIGNAL);

    object_class->destroy = balsa_window_destroy;

    klass->open_mbnode = balsa_window_real_open_mbnode;
    klass->close_mbnode = balsa_window_real_close_mbnode;

    gtk_timeout_add(30000, (GtkFunction) balsa_close_mailbox_on_timer,
                    NULL);

}

static void
balsa_window_init(BalsaWindow * window)
{
}

static gboolean
delete_cb(GtkWidget* main_window)
{
#ifdef BALSA_USE_THREADS
    /* we cannot leave main window disabled because compose windows
     * (for example) could refuse to get deleted and we would be left
     * with disabled main window. */
    if(libbalsa_is_sending_mail()) {
        GtkWidget* d = 
            gnome_ok_cancel_dialog_parented(_("Balsa is sending a mail now.\n"
                                              "Abort sending?"),
                                            NULL, NULL,
                                            GTK_WINDOW(main_window));
        int retval = gnome_dialog_run_and_close(GNOME_DIALOG(d));
        /* FIXME: we should terminate sending thread nicely here,
         * but we must know their ids. */
        return retval!=0; /* keep running unless OK */
    }                                          
#endif
    return FALSE; /* allow delete */
}
static void
size_allocate_cb(GtkWidget * widget, GtkAllocation * alloc)
{
    if (balsa_app.show_mblist)
	balsa_app.mblist_width = widget->parent->allocation.width;
}

GtkWidget *
balsa_window_new()
{
    static const struct callback_item {
        const char* icon_id;
        void (*callback)(GtkWidget *, gpointer);
    } callback_table[] = {
        { BALSA_PIXMAP_SEND_RECEIVE,     send_receive_messages_cb },
        { BALSA_PIXMAP_RECEIVE,          check_new_messages_cb },
        { BALSA_PIXMAP_TRASH,            trash_message_cb },
        { BALSA_PIXMAP_NEW,              new_message_cb },
        { BALSA_PIXMAP_CONTINUE,         continue_message_cb },
        { BALSA_PIXMAP_REPLY,            replyto_message_cb },
        { BALSA_PIXMAP_REPLY_ALL,        replytoall_message_cb },
        { BALSA_PIXMAP_REPLY_GROUP,      replytogroup_message_cb },
        { BALSA_PIXMAP_FORWARD,          forward_message_default_cb },
        { BALSA_PIXMAP_PREVIOUS,         previous_message_cb },
        { BALSA_PIXMAP_NEXT,             next_message_cb },
        { BALSA_PIXMAP_NEXT_UNREAD,      next_unread_message_cb },
        { BALSA_PIXMAP_NEXT_FLAGGED,     next_flagged_message_cb },
        { BALSA_PIXMAP_PRINT,            message_print_cb },
        { BALSA_PIXMAP_MARKED_NEW,       toggle_new_message_cb },
        { BALSA_PIXMAP_MARKED_ALL,       mark_all_cb },
        { BALSA_PIXMAP_SHOW_HEADERS,     show_all_headers_tool_cb },
        { BALSA_PIXMAP_TRASH_EMPTY,      (void(*)())empty_trash },
        { BALSA_PIXMAP_CLOSE_MBOX,       mailbox_close_cb },
        { BALSA_PIXMAP_SHOW_PREVIEW,     show_preview_pane_cb }
    };

    BalsaWindow *window;
    GnomeAppBar *appbar;
    GtkWidget *scroll;
    GtkWidget* btn;
    unsigned i;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS
     * - allows for grey out */
    register_balsa_pixmaps();

    window = gtk_type_new(BALSA_TYPE_WINDOW);

    balsa_app.main_window=window;

    gnome_app_construct(GNOME_APP(window), "balsa", "Balsa");

    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);

    for(i=0; i < ELEMENTS(callback_table); i++)
        set_toolbar_button_callback(TOOLBAR_MAIN, callback_table[i].icon_id,
                                    callback_table[i].callback, window);

    gnome_app_set_toolbar(GNOME_APP(window),
                          get_toolbar(GTK_WIDGET(window), TOOLBAR_MAIN));
    
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

    window->vpaned = gtk_vpaned_new();
    window->hpaned = gtk_hpaned_new();
    window->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook),
                               balsa_app.show_notebook_tabs);
    gtk_notebook_set_show_border (GTK_NOTEBOOK(window->notebook), FALSE);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (window->notebook), TRUE);
    gtk_signal_connect(GTK_OBJECT(window->notebook), "size_allocate",
                       GTK_SIGNAL_FUNC(notebook_size_alloc_cb), NULL);
    gtk_signal_connect(GTK_OBJECT(window->notebook), "switch_page",
                       GTK_SIGNAL_FUNC(notebook_switch_page_cb), NULL);
    gtk_drag_dest_set (GTK_WIDGET (window->notebook), GTK_DEST_DEFAULT_ALL,
                       notebook_drop_types, NUM_DROP_TYPES,
                       GDK_ACTION_DEFAULT | GDK_ACTION_COPY | GDK_ACTION_MOVE);
    gtk_signal_connect (GTK_OBJECT (window->notebook), "drag-data-received",
                        GTK_SIGNAL_FUNC (notebook_drag_received_cb), NULL);
    gtk_signal_connect (GTK_OBJECT (window->notebook), "drag-motion",
                        GTK_SIGNAL_FUNC (notebook_drag_motion_cb), NULL);
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

    /* XXX */
    balsa_app.mblist =  BALSA_MBLIST(balsa_mblist_new());
    window->mblist = gtk_scrolled_window_new(
        GTK_CLIST(balsa_app.mblist)->hadjustment,
        GTK_CLIST(balsa_app.mblist)->vadjustment);
    gtk_container_add(GTK_CONTAINER(window->mblist), 
                      GTK_WIDGET(balsa_app.mblist));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->mblist),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_signal_connect(GTK_OBJECT(balsa_app.mblist), "size_allocate",
		       GTK_SIGNAL_FUNC(size_allocate_cb), NULL);
    mblist_default_signal_bindings(balsa_app.mblist);
    gtk_widget_show_all(window->mblist);

    gtk_paned_pack1(GTK_PANED(window->hpaned), window->mblist, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(window->vpaned), scroll, TRUE, TRUE);
    if  (balsa_app.alternative_layout){
        gnome_app_set_contents(GNOME_APP(window), window->vpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->notebook,TRUE,TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->hpaned,  TRUE,TRUE);
    } else {
        gnome_app_set_contents(GNOME_APP(window), window->hpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->vpaned,  TRUE,TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->notebook,TRUE,TRUE);
    }

    /*PKGW: do it this way, without the usizes. */
    if (balsa_app.show_mblist)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                       (view_menu[MENU_VIEW_MAILBOX_LIST_POS].widget),
                                       balsa_app.show_mblist);

    gtk_paned_set_position(GTK_PANED(window->hpaned), 
                           balsa_app.show_mblist 
                           ? balsa_app.mblist_width
                           : 0);

    /*PKGW: do it this way, without the usizes. */
    if (balsa_app.previewpane)
        gtk_paned_set_position(GTK_PANED(window->vpaned),
                               balsa_app.notebook_height);
    else
        /* Set it to something really high */
        gtk_paned_set_position(GTK_PANED(window->vpaned), G_MAXINT);

    gtk_widget_show(window->vpaned);
    gtk_widget_show(window->hpaned);
    gtk_widget_show(window->notebook);
    gtk_widget_show(window->preview);

    /* set the toolbar style */
    balsa_window_refresh(window);

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

    /* Disable menu items at start up */
    enable_mailbox_menus(NULL);
    enable_message_menus(NULL);
    enable_edit_menus(NULL);
    balsa_window_enable_continue();
    /* gdk_threads_*() is needed, or balsa hangs on startup. */
    /* gdk_threads_enter();
    enable_empty_trash(TRASH_CHECK);
    gdk_threads_leave();*/

    /* set initial state of toggle preview pane button */
    btn=get_tool_widget(GTK_WIDGET(balsa_app.main_window), 0, BALSA_PIXMAP_SHOW_PREVIEW);
    if (btn)
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), balsa_app.previewpane);

    /* we can only set icon after realization, as we have no windows before. */
    gtk_signal_connect(GTK_OBJECT(window), "realize",
                       GTK_SIGNAL_FUNC(set_icon), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "size_allocate",
                       GTK_SIGNAL_FUNC(mw_size_alloc_cb), NULL);
    gtk_signal_connect (GTK_OBJECT (window), "destroy",
                        GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
    gtk_signal_connect(GTK_OBJECT(window), "delete-event",
       GTK_SIGNAL_FUNC(delete_cb), NULL);

    return GTK_WIDGET(window);
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a mailbox open. 
 */
static void
enable_mailbox_menus(BalsaMailboxNode * mbnode)
{
    const static int mailbox_menu_entries[] = {
        MENU_MAILBOX_NEXT_POS,        MENU_MAILBOX_PREV_POS,
        MENU_MAILBOX_NEXT_UNREAD_POS, MENU_MAILBOX_NEXT_FLAGGED_POS,
        MENU_MAILBOX_MARK_ALL_POS,    MENU_MAILBOX_DELETE_POS,
        MENU_MAILBOX_EDIT_POS,
        MENU_MAILBOX_COMMIT_POS,      MENU_MAILBOX_CLOSE_POS };

    const static int threading_menu_entries[] = {
        MENU_THREADING_FLAT_POS,      MENU_THREADING_SIMPLE_POS,
        MENU_THREADING_JWZ_POS
    };

    const static int view_menu_entries[] = {
        MENU_VIEW_EXPAND_ALL_POS,     MENU_VIEW_COLLAPSE_ALL_POS
    };

    LibBalsaMailbox *mailbox = NULL;
    gboolean enable;
    unsigned i;

    enable =  (mbnode != NULL);
    if(mbnode) mailbox = mbnode->mailbox;
    if (mailbox && mailbox->readonly) {
        gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget,
                                 FALSE);
    } else {
        gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget,
                                 enable);
    }

    /* Toolbar */
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_PREVIOUS, enable);
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_NEXT, enable);
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_NEXT_UNREAD, enable);
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_NEXT_FLAGGED, enable);
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_CLOSE_MBOX, enable);
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                                 0, BALSA_PIXMAP_MARKED_ALL, enable);

    /* Menu entries */
    for(i=0; i < ELEMENTS(mailbox_menu_entries); i++)
        gtk_widget_set_sensitive(mailbox_menu[mailbox_menu_entries[i]].widget, enable);
    for(i=0; i < ELEMENTS(threading_menu_entries); i++)
        gtk_widget_set_sensitive(threading_menu[threading_menu_entries[i]].widget, enable);
    for(i=0; i < ELEMENTS(view_menu_entries); i++)
        gtk_widget_set_sensitive(view_menu[view_menu_entries[i]].widget, enable);

    if(mbnode)
        balsa_window_set_threading_menu(mbnode->threading_type);
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a message selected. 
 */
static void
enable_message_menus(LibBalsaMessage * message)
{
    gboolean enable;

    enable = (message != NULL);

    /* Handle menu items which require write access to mailbox */
    if (message && message->mailbox->readonly) {
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_DELETE_POS].widget, FALSE);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TRASH_POS].widget, FALSE);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_UNDEL_POS].widget, FALSE);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TOGGLE_POS].widget,
                                 FALSE);
        gtk_widget_set_sensitive(message_toggle_menu[MENU_MESSAGE_TOGGLE_FLAGGED_POS].widget,
                                 FALSE);
        gtk_widget_set_sensitive(message_toggle_menu[MENU_MESSAGE_TOGGLE_NEW_POS].widget,
                                 FALSE);

        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_TRASH, FALSE);
    } else {
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_DELETE_POS].widget, enable);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TRASH_POS].widget, enable);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_UNDEL_POS].widget, enable);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_TOGGLE_POS].widget, enable);
        gtk_widget_set_sensitive(message_toggle_menu[MENU_MESSAGE_TOGGLE_FLAGGED_POS].widget, enable);
        gtk_widget_set_sensitive(message_toggle_menu[MENU_MESSAGE_TOGGLE_NEW_POS].widget, enable);

        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_TRASH, enable);
    }

    /* Handle items which require multiple parts to the mail */
    if (message && !libbalsa_message_is_multipart(message)) {
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_NEXT_PART_POS].widget, FALSE);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_PREVIOUS_PART_POS].widget, FALSE);
    } else {
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_NEXT_PART_POS].widget, enable);
        gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_PREVIOUS_PART_POS].widget, enable);
    }


    gtk_widget_set_sensitive(file_menu[MENU_FILE_PRINT_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_SAVE_PART_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_SOURCE_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_REPLY_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_REPLY_ALL_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_REPLY_GROUP_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_FORWARD_ATTACH_POS].widget, enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_FORWARD_INLINE_POS].widget, enable);

    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_STORE_ADDRESS_POS].widget, enable);

    /* Toolbar */
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_REPLY, enable);
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_REPLY_ALL, enable);
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_REPLY_GROUP, enable);
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_FORWARD, enable);
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_MARKED_NEW, enable);
        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_PRINT, enable);

    balsa_window_enable_continue();
}

/*
 * Enable/disable the copy and select all buttons
 */
static void
enable_edit_menus(BalsaMessage * bm)
{
    gboolean enable;
    enable = (bm && balsa_message_can_select(bm));

    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_COPY_POS].widget, enable);
    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_SELECT_ALL_POS].widget,
                             enable);

    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_COPY_POS].widget, 
                             enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_SELECT_ALL_POS].widget,
                             enable);
}

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
enable_empty_trash(TrashState status)
{
    gboolean set = TRUE;
    if (balsa_app.trash->open_ref) {
        set = balsa_app.trash->total_messages > 0;
    } else {
        switch(status) {
        case TRASH_CHECK:
            /* Check msg count in trash; this may be expensive... 
             * lets just enable empty trash to be on the safe side */
#if CAN_DO_MAILBOX_OPENING_VERY_VERY_FAST
            if (balsa_app.trash) {
                libbalsa_mailbox_open(balsa_app.trash);
                set = balsa_app.trash->total_messages > 0;
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
    set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window), 0,
                                 BALSA_PIXMAP_TRASH_EMPTY, set);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_EMPTY_TRASH_POS].widget,
                             set);
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
/*      libbalsa_mailbox_open(balsa_app.draftbox, FALSE); */
/*      if (balsa_app.draftbox->total_messages > 0) { */

        gboolean n = balsa_app.draftbox->open_ref == 0
            || balsa_app.draftbox->total_messages;

        set_toolbar_button_sensitive(GTK_WIDGET(balsa_app.main_window),
                        0, BALSA_PIXMAP_CONTINUE, n);
        gtk_widget_set_sensitive(file_menu[MENU_FILE_CONTINUE_POS].widget, n);

/*      libbalsa_mailbox_close(balsa_app.draftbox); */
    }
}

void
balsa_window_set_threading_menu(int option)
{
    int pos;
    switch(option) {
    case BALSA_INDEX_THREADING_FLAT:
    pos = MENU_THREADING_FLAT_POS; break;
    case BALSA_INDEX_THREADING_SIMPLE:
    pos = MENU_THREADING_SIMPLE_POS; break;
    case BALSA_INDEX_THREADING_JWZ:
    pos = MENU_THREADING_JWZ_POS; break;
    default: return;
    }
    gtk_signal_handler_block_by_func(GTK_OBJECT(threading_menu[pos].widget),
                                     threading_menu[pos].moreinfo, 
                                     balsa_app.main_window);
    gtk_check_menu_item_set_active
        (GTK_CHECK_MENU_ITEM(threading_menu[pos].widget), TRUE);
    gtk_signal_handler_unblock_by_func(GTK_OBJECT(threading_menu[pos].widget),
                                       threading_menu[pos].moreinfo,
                                       balsa_app.main_window);

    /* FIXME: the print below reveals that the threading is reset on
       every message preview change. It means: much too often. */
    /* printf("balsa_window_set_threading_menu::Threading set to %d\n", 
       balsa_app.threading_type); */
}

/* balsa_window_open_mbnode: 
   opens mailbox, creates message index. mblist_open_mailbox() is what
   you want most of the time because it can switch between pages if a
   mailbox is already on one of them.
*/
void
balsa_window_open_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    gtk_signal_emit(GTK_OBJECT(window), window_signals[OPEN_MAILBOX_NODE],
                    mbnode);
}

void
balsa_window_close_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    gtk_signal_emit(GTK_OBJECT(window), window_signals[CLOSE_MAILBOX_NODE],
                    mbnode);
}

static GtkWidget *
balsa_notebook_label_new (BalsaMailboxNode* mbnode)
{
       GtkWidget *close_pix;
       GtkWidget *box = gtk_hbox_new(FALSE, 4);
       GtkWidget *lab = gtk_label_new(mbnode->mailbox->name);
       GtkWidget *but = gtk_button_new();

       close_pix = gnome_stock_pixmap_widget(GTK_WIDGET(balsa_app.main_window),
             BALSA_PIXMAP_OTHER_CLOSE);

       gtk_button_set_relief(GTK_BUTTON (but), GTK_RELIEF_NONE);
       gtk_container_add(GTK_CONTAINER (but), close_pix);

       gtk_box_pack_start(GTK_BOX (box), lab, TRUE, TRUE, 0);
       gtk_box_pack_start(GTK_BOX (box), but, FALSE, FALSE, 0);
       gtk_widget_show_all(box);

       gtk_signal_connect(GTK_OBJECT (but), "clicked", 
              GTK_SIGNAL_FUNC(mailbox_tab_close_cb), mbnode);
                                                                                                                   
       return box;
}

static void
real_open_mbnode(BalsaMailboxNode* mbnode)
{
    BalsaIndex * index;
    GtkWidget *label;
    gint page_num;
    gboolean failurep;

#ifdef BALSA_USE_THREADS
    static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&open_lock);
#endif
    /* FIXME: the check is not needed in non-MT-mode */
    if(is_open_mailbox(mbnode->mailbox)) {
        g_warning("mailbox %s is already open.", mbnode->mailbox->name);
#ifdef BALSA_USE_THREADS
        pthread_mutex_unlock(&open_lock);
#endif
        return;
    }

    gdk_threads_enter();
    index = BALSA_INDEX(balsa_index_new());
    index->window = GTK_WIDGET(balsa_app.main_window);

    balsa_window_increase_activity(balsa_app.main_window);
    failurep = balsa_index_load_mailbox_node(BALSA_INDEX (index), mbnode);
    balsa_window_decrease_activity(balsa_app.main_window);

    if(failurep) {
        libbalsa_information(
            LIBBALSA_INFORMATION_ERROR,
            _("Unable to Open Mailbox!\nPlease check the mailbox settings."));
        gtk_object_destroy(GTK_OBJECT(index));
        gdk_threads_leave();
#ifdef BALSA_USE_THREADS
        pthread_mutex_unlock(&open_lock);
#endif
        return;
    }

    gtk_signal_connect(GTK_OBJECT (index), "select_message",
                       GTK_SIGNAL_FUNC (balsa_window_select_message_cb),
                       balsa_app.main_window);
    gtk_signal_connect(GTK_OBJECT (index), "unselect_message",
                       GTK_SIGNAL_FUNC (balsa_window_unselect_message_cb),
                       balsa_app.main_window);
    gtk_signal_connect(GTK_OBJECT (index), "unselect_all_messages",
                       GTK_SIGNAL_FUNC(balsa_window_unselect_all_messages_cb),
                       balsa_app.main_window);

    /* if(config_short_label) label = gtk_label_new(mbnode->mailbox->name);
       else */
    label = balsa_notebook_label_new(mbnode);

    /* for updating date when settings change */
    index->date_string = g_strdup (balsa_app.date_string);
    index->line_length = balsa_app.line_length;

    /* store for easy access */
    gtk_notebook_append_page(GTK_NOTEBOOK(balsa_app.main_window->notebook),
                             GTK_WIDGET(index), label);

    /* change the page to the newly selected notebook item */
    page_num = gtk_notebook_page_num
        (GTK_NOTEBOOK (balsa_app.main_window->notebook),
         GTK_WIDGET (index));
    gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.main_window->notebook),
                          page_num);
    register_open_mailbox(mbnode->mailbox);

    /* Enable relavent menu items... */
    enable_mailbox_menus(mbnode);
    gdk_threads_leave();
#ifdef BALSA_USE_THREADS
    pthread_mutex_unlock(&open_lock);
#endif
}

static void
balsa_window_real_open_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
#ifdef BALSA_USE_THREADS
    pthread_t open_thread;
    pthread_create(&open_thread, NULL, (void*(*)(void*))real_open_mbnode, 
                   mbnode);
    pthread_detach(open_thread);
#else
    real_open_mbnode(mbnode);
#endif
}

/* balsa_window_real_close_mbnode:
   this function overloads libbalsa_mailbox_close_mailbox.

*/
static void
balsa_window_real_close_mbnode(BalsaWindow * window,
                               BalsaMailboxNode * mbnode)
{
    GtkWidget *index = NULL;
    gint i;

    g_return_if_fail(mbnode->mailbox);

    i = balsa_find_notebook_page_num(mbnode->mailbox);

    if (i != -1) {
        index = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);
        gtk_notebook_remove_page(GTK_NOTEBOOK(window->notebook), i);

        gtk_object_destroy(GTK_OBJECT(index));
        unregister_open_mailbox(mbnode->mailbox);

        /* If this is the last notebook page clear the message preview
           and the status bar */
        index =
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), 0);

        if (index == NULL) {
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
    }

    /* we use (BalsaIndex*) instead of BALSA_INDEX because we don't want
       ugly conversion warning when balsa_window_find_current_index
       returns NULL. */
    index = balsa_window_find_current_index(window);

    if (index != NULL)
        balsa_mblist_focus_mailbox(balsa_app.mblist, 
                                   BALSA_INDEX (index)->mailbox_node->mailbox);
}

static gboolean
balsa_close_mailbox_on_timer(GtkWidget * widget, gpointer * data)
{
    GTimeVal current_time;
    GtkWidget *index;
    int i, c, time;

    if (! balsa_app.close_mailbox_auto)
        return TRUE;

    g_get_current_time(&current_time);

    c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

    for (i = 0;
         (index =
          gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
         i++) {
        if (i == c)
            continue;
        time =
            current_time.tv_sec -
            BALSA_INDEX(index)->last_use.tv_sec;
        if (time > (balsa_app.close_mailbox_timeout * 60)) {
            if (balsa_app.debug)
                fprintf(stderr, "Closing Page %d, time: %d\n", i, time);
            gtk_notebook_remove_page(GTK_NOTEBOOK(balsa_app.notebook), i);
            unregister_open_mailbox(BALSA_INDEX(index)->mailbox_node->mailbox);
            gtk_object_destroy(GTK_OBJECT(index));
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

    if(show_all_headers_save != -1)
        balsa_app.shown_headers=show_all_headers_save;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));

    /* don't try to use notebook later in empty_trash */
    balsa_app.notebook = NULL;
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
        /* update the date column, only in the current page */
        balsa_index_refresh_date (GTK_NOTEBOOK(balsa_app.notebook),
                                  NULL, 0, index);
        /* update the size column, only in the current page */
        balsa_index_refresh_size (GTK_NOTEBOOK(balsa_app.notebook),
                                  NULL, 0, index);

    }
    if (balsa_app.alternative_layout)
	paned = GTK_WIDGET(balsa_app.notebook)->parent->parent;
    else    
	paned = GTK_WIDGET(balsa_app.notebook)->parent;
    g_assert(paned != NULL);
    if (balsa_app.previewpane) {
	if(index) balsa_index_redraw_current(BALSA_INDEX(index));
	gtk_paned_set_position(GTK_PANED(paned), balsa_app.notebook_height);
    } else {
	bmsg = BALSA_MESSAGE(BALSA_WINDOW(window)->preview);
	if (bmsg)
	    balsa_message_clear(bmsg);
	/* Set the height to something really big (those new hi-res
	   screens and all :) */
	gtk_paned_set_position(GTK_PANED(paned), G_MAXINT);
    }

    /*
     * set the toolbar style
     */
    item = gnome_app_get_dock_item_by_name(GNOME_APP(window),
                                           GNOME_APP_TOOLBAR_NAME);
    if(item) {
        toolbar = gnome_dock_item_get_child(item);
        gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), balsa_app.toolbar_style);
    }
    /* I don't know if this is a bug of gtk or not but if this is not here
       it doesn't properly resize after a toolbar style change */
    gtk_widget_queue_resize(GTK_WIDGET(window));
}

/* monitored functions for MT-safe manipulation of the open mailbox list
   QUESTION: could they migrate to balsa-app.c?
*/
#ifdef BALSA_USE_THREADS
static pthread_mutex_t open_list_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK_OPEN_LIST pthread_mutex_lock(&open_list_lock)
#define UNLOCK_OPEN_LIST pthread_mutex_unlock(&open_list_lock)
#else
#define LOCK_OPEN_LIST 
#define UNLOCK_OPEN_LIST
#endif
static void register_open_mailbox(LibBalsaMailbox *m)
{
    LOCK_OPEN_LIST;
    balsa_app.open_mailbox_list =
        g_list_prepend(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
}
static void unregister_open_mailbox(LibBalsaMailbox *m)
{
    LOCK_OPEN_LIST;
    balsa_app.open_mailbox_list =
        g_list_remove(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
}
static gboolean is_open_mailbox(LibBalsaMailbox *m)
{
    GList *res;
    LOCK_OPEN_LIST;
    res= g_list_find(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
    return (res != NULL);
}

/*
 * show the about box for Balsa
 */
static void
show_about_box(void)
{
    GtkWidget *about;
    const gchar *authors[] = {
        "Balsa Maintainers <balsa-maintainer@theochem.kth.se>:",
        "Peter Bloomfield <PeterBloomfield@mindspring.com>",
        "Carlos Morgado <chbm@chbm.nu>",
        "Pawel Salek <pawsa@theochem.kth.se>",
        "and many others (see AUTHORS file)",
        NULL
    };


    /* only show one about box at a time */
    if (about_box_visible)
        return;
    else
        about_box_visible = TRUE;

    about = gnome_about_new("Balsa",
                            BALSA_VERSION,
                            _("Copyright (C) 1997-2002"),
                            authors,
                            _
                            ("The Balsa email client is part of the GNOME desktop environment.  Information on Balsa can be found at http://www.balsa.net/\n\nIf you need to report bugs, please do so at: http://bugzilla.gnome.org/"),
                            "balsa/balsa_logo.png");

    gtk_signal_connect(GTK_OBJECT(about),
                       "destroy",
                       (GtkSignalFunc) about_box_destroy_cb, NULL);

    gtk_widget_show(about);
}

/* Check all mailboxes in a list
 *
 */
static void
check_mailbox_list(GList * mailbox_list)
{
    GList *list;
    LibBalsaMailbox *mailbox;

    list = g_list_first(mailbox_list);
    while (list) {
        mailbox = BALSA_MAILBOX_NODE(list->data)->mailbox;

        gdk_threads_enter();
        libbalsa_mailbox_pop3_set_inbox(mailbox, balsa_app.inbox);
        libbalsa_mailbox_check(mailbox);
        gdk_threads_leave();

        list = g_list_next(list);
    }
}

/*Callback to check a mailbox in a balsa-mblist */
static void
mailbox_check_func(GtkCTree * ctree, GtkCTreeNode * node, gpointer data)
{
    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree, node);
    g_return_if_fail(mbnode);
    
    if(mbnode->mailbox) { /* mailbox, not a folder */
        if (!LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox) ||
            imap_check_test(mbnode->dir ? mbnode->dir :
                            LIBBALSA_MAILBOX_IMAP(mbnode->mailbox)->path)) {
            gdk_threads_enter();
            libbalsa_mailbox_check(mbnode->mailbox);
            gdk_threads_leave();
        }
    }
}

/*
 * Callback for testing whether to check an IMAP mailbox
 * Called from mutt_buffy_check
 */
static gboolean
imap_check_test(const gchar * path)
{
    /* path has been parsed, so it's just the folder path */
    if (balsa_app.check_imap && balsa_app.check_imap_inbox)
        return strcmp(path, "INBOX") == 0;
    else
        return balsa_app.check_imap;
}

#if BALSA_USE_THREADS
static void
progress_dialog_destroy_cb(GtkWidget * widget, gpointer data)
{
    progress_dialog = NULL;
    progress_dialog_source = NULL;
    progress_dialog_message = NULL;
    progress_dialog_bar = NULL;
}
/* ensure_check_mail_dialog:
   make sure that mail checking dialog exists.
*/
static void
ensure_check_mail_dialog(void)
{
    if (progress_dialog && GTK_IS_WIDGET(progress_dialog))
	gtk_widget_destroy(GTK_WIDGET(progress_dialog));
    
    progress_dialog =
	gnome_dialog_new(_("Checking Mail..."), _("Hide"), NULL);
    gtk_window_set_wmclass(GTK_WINDOW(progress_dialog), 
			   "progress_dialog", "Balsa");
        
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
#endif

/*
 * Callbacks
 */

gint
check_new_messages_auto_cb(gpointer data)
{
    check_new_messages_real((GtkWidget *) NULL, data, TYPE_BACKGROUND);

    if (balsa_app.debug)
        fprintf(stderr, "Auto-checked for new messages...\n");

    /*  preserver timer */
    return TRUE;
}

/* check_new_messages_cb:
   check new messages the data argument is the BalsaWindow pointer
   or NULL.
*/
void
check_new_messages_real(GtkWidget *widget, gpointer data, int type)
{
#ifdef BALSA_USE_THREADS
    /*  Only Run once -- If already checking mail, return.  */
    pthread_mutex_lock(&mailbox_lock);
    if (checking_mail) {
        pthread_mutex_unlock(&mailbox_lock);
        fprintf(stderr, "Already Checking Mail!\n");
        return;
    }
    checking_mail = 1;

    quiet_check = (type == TYPE_CALLBACK) 
        ? 0 : balsa_app.quiet_background_check;

    pthread_mutex_unlock(&mailbox_lock);

    if (type == TYPE_CALLBACK && 
        (balsa_app.pwindow_option == WHILERETR ||
         (balsa_app.pwindow_option == UNTILCLOSED && progress_dialog)))
	ensure_check_mail_dialog();

    /* initiate threads */
    pthread_create(&get_mail_thread,
                   NULL, (void *) &check_messages_thread, (void *)0);
    
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(get_mail_thread);
#else
    libbalsa_notify_start_check(imap_check_test);
    check_mailbox_list(balsa_app.inbox_input);

    gtk_ctree_post_recursive(GTK_CTREE(balsa_app.mblist), NULL, 
                             mailbox_check_func, NULL);
    balsa_mblist_have_new(balsa_app.mblist);
#endif
}

static void
send_receive_messages_cb(GtkWidget * widget, gpointer data)
{
#if ENABLE_ESMTP
    libbalsa_process_queue(balsa_app.outbox, balsa_app.encoding_style,
                           balsa_app.smtp_server, balsa_app.smtp_authctx,
                           balsa_app.smtp_tls_mode,
                           balsa_app.send_rfc2646_format_flowed);
#else
    libbalsa_process_queue(balsa_app.outbox, balsa_app.encoding_style,
                           balsa_app.send_rfc2646_format_flowed);
#endif
    check_new_messages_real(widget, data, TYPE_CALLBACK);
}

void
check_new_messages_cb(GtkWidget * widget, gpointer data)
{
    check_new_messages_real(widget, data, TYPE_CALLBACK);
}

/* send_outbox_messages_cb:
   tries again to send the messages queued in outbox.
*/

static void
send_outbox_messages_cb(GtkWidget * widget, gpointer data)
{
#if ENABLE_ESMTP
    libbalsa_process_queue(balsa_app.outbox, balsa_app.encoding_style,
                           balsa_app.smtp_server, balsa_app.smtp_authctx,
                           balsa_app.smtp_tls_mode,
                           balsa_app.send_rfc2646_format_flowed);
#else
    libbalsa_process_queue(balsa_app.outbox, balsa_app.encoding_style,
                           balsa_app.send_rfc2646_format_flowed);
#endif
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
    
    /* For recognizing new mail it is assumed, that new mail is */
    /* _always_ unread and that nothing else will increase the */
    /* number of unread messages except for actual new mail arriving */
    /* It should be safe to assume that an upwards change of the */
    /* total of all new messages in all mailboxes will be caused by new */
    /* and nothing else */
    
    int new_msgs_before=0, new_msgs_after=0;
    gtk_ctree_post_recursive(GTK_CTREE(balsa_app.mblist), NULL, 
                             count_unread_msgs_func, 
                             (gpointer)&new_msgs_before);

    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_SOURCE, NULL, "POP3", 0, 0);
        
    check_mailbox_list(balsa_app.inbox_input);

    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_SOURCE, NULL,
                  "Local Mail", 0, 0);
    libbalsa_notify_start_check(imap_check_test);

    gtk_ctree_post_recursive(GTK_CTREE(balsa_app.mblist), NULL, 
                             mailbox_check_func, NULL);

    gtk_ctree_post_recursive(GTK_CTREE(balsa_app.mblist), NULL, 
                             count_unread_msgs_func, 
                             (gpointer)&new_msgs_after);

    new_msgs_after-=new_msgs_before;
    if(new_msgs_after < 0)
        new_msgs_after=0;
    MSGMAILTHREAD(threadmessage, MSGMAILTHREAD_FINISHED, NULL, "Finished",
                  new_msgs_after, 0);
    
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

    if(quiet_check) {
        /* Eat messages */
        while (count) {
            threadmessage = *currentpos;
            if(threadmessage->message_type == MSGMAILTHREAD_FINISHED) {
		gdk_threads_enter();
                balsa_mblist_have_new(balsa_app.mblist);
                display_new_mail_notification(threadmessage->num_bytes);
		gdk_threads_leave();
            }
            g_free(threadmessage);
            currentpos++;
            count -= sizeof(void *);
        }
        g_free(msgbuffer);
        return TRUE;
    }
    
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
            if (progress_dialog) {
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
            if (progress_dialog) {
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
            if (progress_dialog)
                gtk_progress_bar_update(GTK_PROGRESS_BAR(progress_dialog_bar),
					percent);
            else
                gnome_appbar_set_progress(balsa_app.appbar, percent);
            break;
        case MSGMAILTHREAD_FINISHED:

            if (balsa_app.pwindow_option == WHILERETR && progress_dialog) {
                gtk_widget_destroy(progress_dialog);
            } else if (progress_dialog) {
                gtk_label_set_text(GTK_LABEL(progress_dialog_source),
                                   _("Finished Checking."));
                gtk_progress_bar_update(GTK_PROGRESS_BAR
                                        (progress_dialog_bar), 0.0);
            } else {
                gnome_appbar_refresh(balsa_app.appbar);
                gnome_appbar_set_progress(balsa_app.appbar, 0.0);
            }
            balsa_mblist_have_new(balsa_app.mblist);
            display_new_mail_notification(threadmessage->num_bytes);
            break;

        case MSGMAILTHREAD_ERROR:
            errorbox = gnome_message_box_new(threadmessage->message_string,
                                             GNOME_MESSAGE_BOX_ERROR,
                                             GNOME_STOCK_BUTTON_OK, NULL);
            gnome_dialog_run(GNOME_DIALOG(errorbox));
            break;

        default:
            fprintf(stderr, " Unknown check mail message(%d): %s\n",
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

gboolean
send_progress_notify_cb()
{
    SendThreadMessage *threadmessage;
    SendThreadMessage **currentpos;
    void *msgbuffer;
    uint count;
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

            if (percent == 0 && send_dialog) {
                gtk_label_set_text(GTK_LABEL(send_progress_message),
                                   threadmessage->message_string);
                gtk_widget_show_all(send_dialog);
            }

            if (send_dialog)
                gtk_progress_bar_update(GTK_PROGRESS_BAR(send_dialog_bar),
                                        percent);
            else
                gnome_appbar_set_progress(balsa_app.appbar, percent);

            /* display progress x of y, y = of_total */
            break;

        case MSGSENDTHREADFINISHED:
            /* closes progress dialog */
            if (send_dialog)
                gtk_widget_destroy(send_dialog);
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

static void
count_unread_msgs_func(GtkCTree * ctree, GtkCTreeNode * node, gpointer data)
{
    int *val=(int *)data;

    BalsaMailboxNode *mbnode = gtk_ctree_node_get_row_data(ctree, node);
    g_return_if_fail(mbnode);
    
    if(mbnode->mailbox)
        *val+=LIBBALSA_MAILBOX(mbnode->mailbox)->unread_messages;
}

static void
new_mail_dialog_destroy_cb(GtkWidget *widget, gpointer data)
{
    new_mail_dialog_visible = FALSE;
}

/* display_new_mail_notification:
   num_new is the number of the recently arrived messsages.
*/
static void
display_new_mail_notification(int num_new)
{
    GtkDialog *dlg;
    GtkWidget *label, *ok_button, *vbox;
    
    if(num_new == 0)
        return;
    
    if(balsa_app.notify_new_mail_dialog && new_mail_dialog_visible)
        return;
    
    if(balsa_app.notify_new_mail_sound)
        gnome_triggers_do("New mail has arrived", "email",
                          "email", "newmail", NULL);
    
    if(!balsa_app.notify_new_mail_dialog)
        return;
    
    new_mail_dialog_visible = TRUE;
    
    dlg=GTK_DIALOG(gtk_dialog_new());
    gtk_window_set_wmclass(GTK_WINDOW(dlg), "new_mail_dialog", "Balsa");
    
    vbox=gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(dlg->vbox), vbox, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 30);
    
    label=gtk_label_new(_("You have new mail waiting"));
    gtk_box_pack_start(GTK_BOX(vbox), label, TRUE, TRUE, 0);
    
    ok_button=gtk_button_new_with_label(_("OK"));
    GTK_WIDGET_SET_FLAGS (GTK_WIDGET (ok_button), GTK_CAN_DEFAULT);
    gtk_box_pack_start(GTK_BOX(dlg->action_area), ok_button, FALSE, FALSE, 0);
    gtk_widget_grab_default(ok_button);
    
    gtk_signal_connect_object(GTK_OBJECT(ok_button), "clicked",
                       gtk_object_destroy, GTK_OBJECT(dlg));

    gtk_signal_connect(GTK_OBJECT(dlg), "destroy",
                       new_mail_dialog_destroy_cb, NULL);

    gtk_widget_show_all(GTK_WIDGET(dlg));
}
 
#endif

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    GtkWidget *index;

    g_return_val_if_fail(window != NULL, NULL);

    index = gtk_notebook_get_nth_page(GTK_NOTEBOOK(window->notebook),
                                     gtk_notebook_get_current_page
                                     (GTK_NOTEBOOK(window->notebook)));
    if (!index)
        return NULL;

    return GTK_WIDGET(BALSA_INDEX(index));
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
replytogroup_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_replytogroup
        (widget,
         balsa_window_find_current_index(BALSA_WINDOW(data)));
}

static void
forward_message_attached_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_forward_attached(widget,
        balsa_window_find_current_index(BALSA_WINDOW(data)));
}

static void
forward_message_inline_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_forward_inline(widget,
        balsa_window_find_current_index(BALSA_WINDOW(data)));
}

static void
forward_message_default_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_forward_default(widget,
        balsa_window_find_current_index(BALSA_WINDOW(data)));
}


static void
continue_message_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index && BALSA_INDEX(index)->mailbox_node->mailbox == balsa_app.draftbox)
        balsa_message_continue(widget, BALSA_INDEX(index));
    else
        mblist_open_mailbox(balsa_app.draftbox);
}


static void
next_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_select_next(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

static void
next_unread_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_select_next_unread(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

static void
next_flagged_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_select_next_flagged(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

static void
previous_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_select_previous(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
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
view_msg_source_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    bw = BALSA_WINDOW(data);
    if (bw->preview) {
        LibBalsaMessage * msg = BALSA_MESSAGE(bw->preview)->message;
        libbalsa_show_message_source(msg);
    }
}

static void
trash_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_move_to_trash(widget,
                                balsa_window_find_current_index(
                                    BALSA_WINDOW(data)));
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
toggle_new_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_toggle_new(widget,
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
    refresh_preferences_manager();
}

/* show_no_headers_cb:
   this is a callback for the menu item but it is also called
   by the show_all_headers_tool_cb function to reset the menu and 
   internal balsa_app data to HEADERS_SELECTED state.
   These two cases are distinguished by widget parameter.
   when widget != NULL, this callback is triggered by the menu event.
   when widget == NULL, we just reset the state.
*/
static void
show_no_headers_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    reset_show_all_headers();
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
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

    reset_show_all_headers();
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
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
    
    reset_show_all_headers();
    
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
        return;
    
    balsa_app.shown_headers = HEADERS_ALL;
    bw = BALSA_WINDOW(data);
    if (bw->preview)
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            HEADERS_ALL);
}

static void
threading_flat_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;
    GNode *gnode;
    
    if(!GTK_CHECK_MENU_ITEM(widget)->active) return;
    index = balsa_window_find_current_index(balsa_app.main_window);
    g_return_if_fail(index);
    gnode = find_gnode_in_mbox_list(balsa_app.mailbox_nodes, 
                                    BALSA_INDEX(index)->mailbox_node->mailbox);
    g_return_if_fail(gnode);
    BALSA_MAILBOX_NODE(gnode->data)->threading_type = 
        BALSA_INDEX_THREADING_FLAT;
    balsa_index_set_threading_type(BALSA_INDEX(index), 
                                   BALSA_INDEX_THREADING_FLAT);
}

static void
threading_simple_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;
    GNode *gnode;

    if(!GTK_CHECK_MENU_ITEM(widget)->active) return;
    index = balsa_window_find_current_index(balsa_app.main_window);
    g_return_if_fail(index);
    gnode = find_gnode_in_mbox_list(balsa_app.mailbox_nodes, 
                                    BALSA_INDEX(index)->mailbox_node->mailbox);
    g_return_if_fail(gnode);
    BALSA_MAILBOX_NODE(gnode->data)->threading_type = 
        BALSA_INDEX_THREADING_SIMPLE;
   balsa_index_set_threading_type(BALSA_INDEX(index),
                                   BALSA_INDEX_THREADING_SIMPLE);
}

static void
threading_jwz_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;
    GNode *gnode;

    if(!GTK_CHECK_MENU_ITEM(widget)->active) return;
    index = balsa_window_find_current_index(balsa_app.main_window);
    g_return_if_fail(index);
    gnode = find_gnode_in_mbox_list(balsa_app.mailbox_nodes, 
                                    BALSA_INDEX(index)->mailbox_node->mailbox);
    g_return_if_fail(gnode);
    BALSA_MAILBOX_NODE(gnode->data)->threading_type = 
        BALSA_INDEX_THREADING_JWZ;
    balsa_index_set_threading_type(BALSA_INDEX(index),
                                        BALSA_INDEX_THREADING_JWZ);
}

static void
expand_all_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(balsa_app.main_window);
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), TRUE);
}

static void
collapse_all_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(balsa_app.main_window);
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), FALSE);
}


static void
address_book_cb(GtkWindow *widget, gpointer data)
{
    GtkWidget *ab;

    ab = balsa_address_book_new(FALSE);
    gnome_dialog_set_parent(GNOME_DIALOG(ab), GTK_WINDOW(balsa_app.main_window));

    gtk_widget_show(GTK_WIDGET(ab));
}

#ifdef BALSA_SHOW_ALL

static GtkToggleButton*
add_check_button(GtkWidget* table, const gchar* label, gint x, gint y)
{
    GtkWidget* res = gtk_check_button_new_with_label(label);
    gtk_table_attach(GTK_TABLE(table),
                     res,
                     x, x+1, y, y+1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
    return GTK_TOGGLE_BUTTON(res);
}

static void 
find_real(BalsaIndex * bindex,gboolean again)
{
    /* FIXME : later we could do a search based on a complete filter */
    static LibBalsaFilter * f=NULL;
    /* Condition set up for the search, it will be of type
       CONDITION_NONE if nothing has been set up */
    static LibBalsaCondition * cnd=NULL;
    GSList * conditions;
    static gboolean reverse=FALSE;

    if (!cnd) {
	cnd=libbalsa_condition_new();
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_FROM);
        CONDITION_SETMATCH(cnd,CONDITION_MATCH_SUBJECT);
    }


    /* first search, so set up the match rule(s) */
    if (!again || (!f && cnd->type==CONDITION_NONE)) {
	GnomeDialog* dia=
            GNOME_DIALOG(gnome_dialog_new(_("Search a message"),
                                          GNOME_STOCK_BUTTON_OK,
                                          GNOME_STOCK_BUTTON_CANCEL,
                                          NULL));
	GtkWidget *reverse_button, *search_entry, *w, *page, *table;
	GtkToggleButton *matching_body, *matching_from;
        GtkToggleButton *matching_to, *matching_cc, *matching_subject;
	gint ok;
	
	gnome_dialog_close_hides(dia,TRUE);

	/* FIXME : we'll set up this callback later when selecting
	   filters has been enabled
	   gtk_signal_connect(GTK_OBJECT(dia),"clicked",
	   find_dialog_button_cb,&f);
	*/
	reverse_button = gtk_check_button_new_with_label(_("Reverse search"));

	page=gtk_table_new(2, 1, FALSE);
	w = gtk_label_new(_("Search for:"));
	gtk_table_attach(GTK_TABLE(page),w,0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	search_entry = gtk_entry_new_with_max_length(30);
	gtk_table_attach(GTK_TABLE(page),search_entry,1, 2, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	gtk_box_pack_start(GTK_BOX(dia->vbox), page, FALSE, FALSE, 2);

	/* builds the toggle buttons to specify fields concerned by
         * the search. */
	page = gtk_table_new(3, 7, FALSE);
    
	w = gtk_frame_new(_("In:"));
	gtk_frame_set_label_align(GTK_FRAME(w), GTK_POS_LEFT, GTK_POS_TOP);
	gtk_frame_set_shadow_type(GTK_FRAME(w), GTK_SHADOW_ETCHED_IN);
	gtk_table_attach(GTK_TABLE(page),
			 w,
			 0, 3, 0, 2,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 5, 5);
    
	table = gtk_table_new(3, 3, TRUE);
	gtk_container_add(GTK_CONTAINER(w), table);
		
	matching_body    = add_check_button(table, _("Body"),    0, 0);
	matching_to      = add_check_button(table, _("To:"),     1, 0);
	matching_from    = add_check_button(table, _("From:"),   1, 1);
        matching_subject = add_check_button(table, _("Subject"), 2, 0);
	matching_cc      = add_check_button(table, _("Cc:"),     2, 1);
	gtk_box_pack_start(GTK_BOX(dia->vbox), page, FALSE, FALSE, 2);

	gtk_box_pack_start(GTK_BOX(dia->vbox), gtk_hseparator_new(), 
                           FALSE, FALSE, 2);
	gtk_box_pack_start(GTK_BOX(dia->vbox), reverse_button,TRUE,TRUE,0);
	gtk_widget_show_all(dia->vbox);

	if (cnd->match.string)
	    gtk_entry_set_text(GTK_ENTRY(search_entry),cnd->match.string);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_button),reverse);
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
        gnome_dialog_editable_enters(dia, GTK_EDITABLE(search_entry));
        gnome_dialog_set_default(dia, 0);
	do {
	    ok=gnome_dialog_run(dia);
	    if (ok==0) {
		reverse=GTK_TOGGLE_BUTTON(reverse_button)->active;
		g_free(cnd->match.string);
		cnd->match.string =
                    g_strdup(gtk_entry_get_text(GTK_ENTRY(search_entry)));
		cnd->match_fields=CONDITION_EMPTY;

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
		if (cnd->match_fields!=CONDITION_EMPTY && cnd->match.string[0])

		    /* FIXME : We should print error messages, but for
		     * that we should first make find dialog non-modal
		     * balsa_information(LIBBALSA_INFORMATION_ERROR,_("You
		     * must specify at least one field to look in"));
		     * *balsa_information(LIBBALSA_INFORMATION_ERROR,_("You
		     * must provide a non-empty string")); */

		    ok=1;
		else ok=-1;
	    }
	    else ok=-1;
	}
	while (ok==0);
	gtk_widget_destroy(GTK_WIDGET(dia));
	/* Here ok==1 means OK button was pressed, search is valid so let's go
	 * else cancel was pressed return */
	if (ok!=1) return;
	cnd->type=CONDITION_SIMPLE;
    }

    if (f) {
	GSList * lst=g_slist_append(NULL,f);
	if (!filters_prepare_to_run(lst)) return;
	g_slist_free(lst);
	conditions=f->conditions;
    }
    else conditions=g_slist_append(NULL,cnd);

    balsa_index_find(bindex,
                     f ? f->conditions_op : FILTER_OP_OR,
                     conditions, reverse);

    /* FIXME : See if this does not lead to a segfault because of
       balsa_index_scan_info */
    if (!f) g_slist_free(conditions);
}

static void
find_cb(GtkWidget * widget,gpointer data)
{
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(BALSA_WINDOW(data))))
	find_real(BALSA_INDEX(bindex),FALSE);
}

static void
find_again_cb(GtkWidget * widget,gpointer data)
{
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(BALSA_WINDOW(data))))
	find_real(BALSA_INDEX(bindex),TRUE);
}

static void
filter_dlg_cb(GtkWidget * widget, gpointer data)
{
    filters_edit_dialog();
}

static void
filter_export_cb(GtkWidget * widget, gpointer data)
{
    filters_export_dialog();
}
#endif

/* closes the mailbox on the notebook's active page */
static void
mailbox_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
        mblist_close_mailbox(BALSA_INDEX(index)->mailbox_node->mailbox);
}

static void
mailbox_tab_close_cb(GtkWidget * widget, gpointer data)
{
   balsa_window_real_close_mbnode(balsa_app.main_window, (BalsaMailboxNode *)data);
}


static void
mailbox_commit_changes(GtkWidget * widget, gpointer data)
{
    LibBalsaMailbox *current_mailbox;
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    g_return_if_fail(index != NULL);

    current_mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    
    if (!libbalsa_mailbox_commit(current_mailbox))
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Commiting mailbox %s failed."),
                          current_mailbox->name);
}


static gboolean
mailbox_commit_each(GNode *node, gpointer data) 
{
    LibBalsaMailbox *box;
    if ( (box = BALSA_MAILBOX_NODE(node->data)->mailbox) == NULL)
        return FALSE; /* mailbox_node->mailbox == NULL is legal */
    
    g_return_val_if_fail(LIBBALSA_IS_MAILBOX(box), FALSE);

    if(box->open_ref == 0)
	return FALSE;

    if (!libbalsa_mailbox_commit(box))
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Commiting mailbox %s failed."),
                          box->name);
    return FALSE;
}


static void
mailbox_commit_all(GtkWidget * widget, gpointer data)
{
    g_node_traverse(balsa_app.mailbox_nodes, G_IN_ORDER, G_TRAVERSE_ALL,
		    -1, (GNodeTraverseFunc)mailbox_commit_each, 
		    NULL);
}

/* empty_trash:
   empty the trash mailbox.
*/
void
empty_trash(void)
{
    GList *message;

    if(!libbalsa_mailbox_open(balsa_app.trash)) return;

    message = balsa_app.trash->message_list;

    while (message) {
        libbalsa_message_delete(message->data, TRUE);
        message = message->next;
    }
    libbalsa_mailbox_close(balsa_app.trash);
    balsa_mblist_update_mailbox(balsa_app.mblist, balsa_app.trash);
    enable_empty_trash(TRASH_EMPTY);
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

void
balsa_change_window_layout(BalsaWindow *window)
{

    gtk_widget_ref(window->notebook);
    gtk_widget_ref(window->vpaned);
    gtk_widget_ref(window->hpaned);
 
    gtk_container_remove(GTK_CONTAINER(window->notebook->parent), window->notebook);
    gtk_container_remove(GTK_CONTAINER(window->vpaned->parent), window->vpaned);
    gtk_container_remove(GTK_CONTAINER(window->hpaned->parent), window->hpaned);

    if (balsa_app.alternative_layout) {
        gnome_app_set_contents(GNOME_APP(window), window->vpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->notebook, TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->hpaned, TRUE, TRUE);
    } else {
        gnome_app_set_contents(GNOME_APP(window), window->hpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->vpaned, TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->notebook, TRUE, TRUE);
    }

    gtk_widget_unref(window->notebook);
    gtk_widget_unref(window->vpaned);
    gtk_widget_unref(window->hpaned);
 
    gtk_paned_set_position(GTK_PANED(window->hpaned), 
                           balsa_app.show_mblist 
                           ? balsa_app.mblist_width
                           : 0);
    gtk_widget_show(window->vpaned);
    gtk_widget_show(window->hpaned);

}

static void
about_box_destroy_cb(void)
{
    about_box_visible = FALSE;
}

static void
set_icon(GnomeApp * app)
{
    GdkPixbuf *pb = NULL;
    GdkWindow *ic_win, *w;
    GdkWindowAttr att;
    XIconSize *is;
    gint i, count, j;
    char *filename;

    w = GTK_WIDGET(app)->window;

    if ((XGetIconSizes(GDK_DISPLAY(), GDK_ROOT_WINDOW(), &is, &count))
        && (count > 0)) {
        i = 0;                  /* use first icon size - not much point using the others */
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
    att.visual = gdk_rgb_get_visual();
    att.colormap = gdk_rgb_get_cmap();
    ic_win = gdk_window_new(NULL, &att, GDK_WA_VISUAL | GDK_WA_COLORMAP);
    gdk_window_set_icon(w, ic_win, NULL, NULL);

    if( (filename = balsa_pixmap_finder("balsa/balsa_icon.png")) ) {
        pb = gdk_pixbuf_new_from_file(filename);
        gdk_window_clear(ic_win);
        gdk_pixbuf_unref(pb);
        g_free(filename);
    }
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
    GtkWidget *index;
    GtkWidget *window;
    GtkWidget* clist;
    LibBalsaMailbox *mailbox;
    LibBalsaMessage *message;
    gchar *title;

    index = page->child;

    mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    window = BALSA_INDEX(index)->window;
    clist = GTK_WIDGET (BALSA_INDEX (index)->ctree);

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

    balsa_index_update_message(BALSA_INDEX(index));

    if (GTK_CLIST(clist)->selection) {
        message =
            gtk_ctree_node_get_row_data(GTK_CTREE(clist),
                                        (g_list_last
                                         (GTK_CLIST(clist)->selection)->
                                         data));
        enable_message_menus(message);
    } else {
        enable_message_menus(NULL);
    }

    enable_mailbox_menus(BALSA_INDEX(index)->mailbox_node);

    balsa_mblist_focus_mailbox(balsa_app.mblist, mailbox);

    balsa_index_refresh_date (GTK_NOTEBOOK(balsa_app.notebook),
                              NULL, 0, index);
    balsa_index_refresh_size (GTK_NOTEBOOK(balsa_app.notebook),
                              NULL, 0, index);
}

static void
balsa_window_select_message_cb(GtkWidget * widget,
                               LibBalsaMessage * message,
                               GdkEventButton * bevent, gpointer data)
{
    BalsaIndex *index;

    index = 
        BALSA_INDEX(balsa_window_find_current_index(balsa_app.main_window));
    g_return_if_fail(index);
    enable_mailbox_menus(index->mailbox_node);
    enable_message_menus(message);
}

static void
balsa_window_unselect_message_cb(GtkWidget * widget,
                                 LibBalsaMessage * message,
                                 GdkEventButton * bevent, gpointer data)
{
    BalsaIndex *index;

    index = 
        BALSA_INDEX(balsa_window_find_current_index(balsa_app.main_window));
    g_return_if_fail(index);
    enable_mailbox_menus(index->mailbox_node);
/*     enable_message_menus(NULL); */
/*     enable_edit_menus(NULL); */
}


static void
balsa_window_unselect_all_messages_cb (GtkWidget* widget, gpointer data)
{
    enable_message_menus (NULL);
    enable_edit_menus (NULL);
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


/* notebook_find_page
 * 
 * Description: Finds the page from which notebook page tab the
 * coordinates are over.
 **/
static BalsaIndex*
balsa_window_notebook_find_page (GtkNotebook* notebook, gint x, gint y)
{
    GtkWidget* page;
    GtkWidget* label;
    gint page_num = 0;
    gint label_x;
    gint label_y;
    gint label_width;
    gint label_height;
    

    while ((page = gtk_notebook_get_nth_page (notebook, page_num)) != NULL) {
        label = gtk_notebook_get_tab_label (notebook, page);
        
        label_x = label->allocation.x;
        label_width = label->allocation.width;
        
        if (x > label_x && x < label_x + label_width) {
            label_y = label->allocation.y;
            label_height = label->allocation.height;
            
            if (y > label_y && y < label_y + label_height) {
                return BALSA_INDEX (page);
            }
        }
        ++page_num;
    }

    return NULL;
}


/* notebook_drag_received_cb
 * 
 * Description: Signal handler for the drag-data-received signal from
 * the GtkNotebook widget.  Finds the tab the messages were dragged
 * over, then transfers them.
 **/
static void
notebook_drag_received_cb (GtkWidget* widget, GdkDragContext* context, 
                           gint x, gint y, 
                           GtkSelectionData* selection_data, 
                           guint info, guint32 time, gpointer data)
{
    BalsaIndex* index;
    LibBalsaMailbox* mailbox;
    LibBalsaMailbox* orig_mailbox;
    GList* messages = NULL;
    LibBalsaMessage** message_array;
    gint i = 0;


    index = balsa_window_notebook_find_page (GTK_NOTEBOOK (widget), x, y);
    
    if (index == NULL)
        return;
    
    mailbox = index->mailbox_node->mailbox;
    message_array = (LibBalsaMessage**) selection_data->data;

    if (message_array[i] != NULL) {
        while (message_array[i] != NULL) {
            messages = g_list_append (messages, message_array[i]);
            ++i;
        }
    } else {
        return;
    }
        
    orig_mailbox = ((LibBalsaMessage*) messages->data)->mailbox;
    
    if (mailbox != NULL && mailbox != orig_mailbox)
        balsa_index_transfer(messages, orig_mailbox, mailbox,
                             balsa_find_index_by_mailbox(orig_mailbox),
                             context->action != GDK_ACTION_MOVE);

    g_list_free (messages);
}

static gboolean
notebook_drag_motion_cb(GtkWidget * widget, GdkDragContext * context,
                        gint x, gint y, guint time, gpointer user_data)
{
    if (balsa_app.drag_default_is_move)
        gdk_drag_status(context,
                        (context->actions ==
                         GDK_ACTION_COPY) ? GDK_ACTION_COPY :
                        GDK_ACTION_MOVE, time);
    return FALSE;
}

/* balsa_window_progress_timeout
 * 
 * This function is called at a preset interval to cause the progress
 * bar to move in activity mode.  
 * this routine is called from g_timeout_dispatch() and needs to take care 
 * of GDK locking itself using gdk_threads_{enter,leave}
 **/
gint
balsa_window_progress_timeout(gpointer user_data) 
{
    gfloat new_val;
    GtkAdjustment* adj;
    
    gdk_threads_enter();
    /* calculate the new value of the progressbar */
    new_val = gtk_progress_get_value(GTK_PROGRESS(user_data)) + 1;
    adj = GTK_PROGRESS(user_data)->adjustment;
    if (new_val > adj->upper) {
        new_val = adj->lower;
    }
    gtk_progress_set_value(GTK_PROGRESS(user_data), new_val);
    gdk_threads_leave();

    /* return true so it continues to be called */
    return TRUE;
}


/* balsa_window_increase_activity
 * 
 * Calling this causes this to the progress bar of the window to
 * switch into activity mode if it's not already going.  Otherwise it
 * simply increments the counter (so that multiple threads can
 * indicate activity simultaneously).
 **/
void 
balsa_window_increase_activity(BalsaWindow* window)
{
    gint in_use = 0;
    gint activity_handler;
    guint activity_counter = 0;
    GtkProgress* progress_bar;
    GtkAdjustment* adj;
    

    progress_bar = gnome_appbar_get_progress(
        GNOME_APPBAR(GNOME_APP(window)->statusbar));
    in_use = GPOINTER_TO_INT(gtk_object_get_data(GTK_OBJECT(progress_bar), 
                                                  "in_use"));
    
    if (!in_use) {
        gtk_object_set_data(GTK_OBJECT(progress_bar), "in_use", 
                            GINT_TO_POINTER(BALSA_PROGRESS_ACTIVITY));

        gtk_progress_set_activity_mode(progress_bar, 1);
        adj = progress_bar->adjustment;
        adj->lower = 0;
        adj->upper = 100;
        adj->value = 0;

        /* add a timeout to make the activity bar move */
        activity_handler = gtk_timeout_add(100, balsa_window_progress_timeout,
                                           progress_bar);
        gtk_object_set_data(GTK_OBJECT(progress_bar), 
                            "activity_handler", 
                            GINT_TO_POINTER(activity_handler));
    } else if (in_use != BALSA_PROGRESS_ACTIVITY) {
        /* the progress bar is already in use doing something else, so
         * quit */
        return;
    }
    
    /* increment the reference counter */
    activity_counter = GPOINTER_TO_UINT(
        gtk_object_get_data(GTK_OBJECT(progress_bar),
                            "activity_counter"));
    ++activity_counter;
    gtk_object_set_data(GTK_OBJECT(progress_bar), 
                        "activity_counter", 
                        GUINT_TO_POINTER(activity_counter));
}


/* balsa_window_decrease_activity
 * 
 * When called, decreases the reference counter of the progress
 * activity bar, if it goes to zero the progress bar is stopped and
 * cleared.
 **/
void 
balsa_window_decrease_activity(BalsaWindow* window)
{
    gint in_use;
    gint activity_handler;
    guint activity_counter = 0;
    GtkProgress* progress_bar;
    
    progress_bar = gnome_appbar_get_progress(
        GNOME_APPBAR(GNOME_APP(window)->statusbar));
    in_use = GPOINTER_TO_INT(
        gtk_object_get_data(GTK_OBJECT(progress_bar), "in_use"));

    /* make sure the progress bar is being used for activity */
    if (in_use != BALSA_PROGRESS_ACTIVITY)
        return;

    activity_counter = GPOINTER_TO_UINT(
        gtk_object_get_data(GTK_OBJECT(progress_bar),
                            "activity_counter"));
    
    /* decrement the counter if it exists */
    if (activity_counter) {
        --activity_counter;
        
        /* if the reference count is now zero, clear the bar and make
         * it available for others to use */
        if (!activity_counter) {
            activity_handler = GPOINTER_TO_INT(
                gtk_object_get_data(GTK_OBJECT(progress_bar),
                                    "activity_handler"));
            gtk_timeout_remove(activity_handler);
            activity_handler = 0;
            
            gtk_progress_set_activity_mode(progress_bar, 
                                           activity_counter);
            gtk_adjustment_set_value(progress_bar->adjustment, 0);
            gtk_object_set_data(GTK_OBJECT(progress_bar), 
                                "activity_handler",
                                GINT_TO_POINTER(activity_handler));
            gtk_object_set_data(GTK_OBJECT(progress_bar),
                                "in_use", 
                                GINT_TO_POINTER(BALSA_PROGRESS_NONE));
        }
        /* make sure to store the counter value */
        gtk_object_set_data(GTK_OBJECT(progress_bar), 
                            "activity_counter",
                            GUINT_TO_POINTER(activity_counter));
    }
}


/* balsa_window_setup_progress
 * 
 * window: BalsaWindow that contains the progressbar 
 * upper_bound: Defines the top of the range to be incremented along
 * 
 * returns: true if initialization is successful, otherwise returns
 * false.
 * 
 * Initializes the progress bar for incremental operation with a range
 * from 0 to upper_bound.  If the bar is already in operation, either
 * in activity mode or otherwise, the function returns false, if the
 * initialization is successful it returns true.
 **/
gboolean
balsa_window_setup_progress(BalsaWindow* window, gfloat upper_bound)
{
    gint in_use;
    GtkProgress* progress_bar;
    GtkAdjustment* adj;
    

    progress_bar = gnome_appbar_get_progress(
        GNOME_APPBAR(GNOME_APP(window)->statusbar));
    in_use = GPOINTER_TO_INT(
        gtk_object_get_data(GTK_OBJECT(progress_bar), "in_use"));

    /* make sure the progress bar is currently unused */
    if (in_use != BALSA_PROGRESS_NONE) 
        return FALSE;
    
    in_use = BALSA_PROGRESS_INCREMENT;
    gtk_object_set_data(GTK_OBJECT(progress_bar), 
                        "in_use", 
                        GINT_TO_POINTER(in_use));
    
    /* set up the adjustment */
    gtk_progress_set_activity_mode(progress_bar, 0);
    adj = progress_bar->adjustment;
    adj->lower = 0;
    adj->upper = upper_bound;
    adj->value = 0;

    return TRUE;
}


/* balsa_window_clear_progress
 * 
 * Clears the progress bar from incrementing, and makes it availble to
 * be used by another area of the program.
 **/
void 
balsa_window_clear_progress(BalsaWindow* window)
{
    gint in_use = 0;
    GtkProgress* progress_bar;
    GtkAdjustment* adj;

    progress_bar = gnome_appbar_get_progress(
        GNOME_APPBAR(GNOME_APP(window)->statusbar));
    in_use = GPOINTER_TO_INT(
        gtk_object_get_data(GTK_OBJECT(progress_bar), "in_use"));

    /* make sure we're using it before it is cleared */
    if (in_use != BALSA_PROGRESS_INCREMENT)
        return;

    adj = progress_bar->adjustment;
    adj->lower = 0;
    adj->upper = 100;
    gtk_adjustment_set_value(adj, 0);
    
    in_use = BALSA_PROGRESS_NONE;
    gtk_object_set_data(GTK_OBJECT(progress_bar), 
                        "in_use", 
                        GINT_TO_POINTER(in_use));
}


/* balsa_window_increment_progress
 *
 * If the progress bar has been initialized using
 * balsa_window_setup_progress, this function increments the
 * adjustment by one and executes any pending gtk events.  So the
 * progress bar will be shown as updated even if called within a loop.
 **/
void
balsa_window_increment_progress(BalsaWindow* window)
{
    gint in_use;
    gfloat new_val;
    GtkProgress* progress_bar;
    GtkAdjustment* adj;
    
    progress_bar = gnome_appbar_get_progress(
        GNOME_APPBAR(GNOME_APP(window)->statusbar));
    in_use = GPOINTER_TO_INT(
        gtk_object_get_data(GTK_OBJECT(progress_bar), "in_use"));

    /* make sure the progress bar is being incremented */
    if (in_use != BALSA_PROGRESS_INCREMENT)
        return;

    new_val = gtk_progress_get_value(progress_bar) + 1;
    adj = progress_bar->adjustment;
    
    /* check for hitting the upper limit, if there pin it */
    if (new_val > adj->upper) {
        new_val = adj->upper;
    }
    
    gtk_adjustment_set_value(adj, new_val);
#ifdef BALSA_USE_THREADS
    /* run some gui events to make sure the progress bar gets drawn to
     * screen; it's not needed when we compile in MT-enabled mode because
     * the events will be processed by the main thread as soon as it gets
     * the gdk_lock. Or so I believed. */
#else
    while (gtk_events_pending()) {
        gtk_main_iteration_do(FALSE);
    }
#endif
}


static void
ident_manage_dialog_cb(GtkWidget* widget, gpointer user_data)
{
    GtkWidget* dialog;

    /* create dialog  */
    dialog = libbalsa_identity_config_dialog(GTK_WINDOW(balsa_app.main_window),
                                             &balsa_app.identities,
                                             &balsa_app.current_ident);
    if(gnome_dialog_run(GNOME_DIALOG(dialog)) == 1) {
        g_print("Saving identities...\n");
        config_identities_save();
        gnome_config_sync();
    }
}


static void
mark_all_cb(GtkWidget * widget, gpointer data)
{
    GtkCList *clist;
    BalsaIndex *bindex;

    bindex=BALSA_INDEX(balsa_window_find_current_index(balsa_app.main_window));
    g_return_if_fail(bindex != NULL);
    clist = GTK_CLIST(bindex->ctree);

    gtk_clist_select_all(clist);
}

static void
show_all_headers_tool_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *btn;
    BalsaWindow *bw;

    btn=get_tool_widget(GTK_WIDGET(balsa_app.main_window), 0, BALSA_PIXMAP_SHOW_HEADERS);
    if(!btn)
        return;
    if(GTK_TOGGLE_BUTTON(btn)->active) {
        show_all_headers_save=balsa_app.shown_headers;
        balsa_app.shown_headers=HEADERS_ALL;
        bw = BALSA_WINDOW(data);
        if (bw->preview)
            balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                                HEADERS_ALL);
    } else {
        if(show_all_headers_save == -1)
            return;

        switch(show_all_headers_save) {
        case HEADERS_NONE:
            show_no_headers_cb(NULL, data);
            break;
        case HEADERS_ALL:
            show_all_headers_cb(NULL, data);
            break;
        case HEADERS_SELECTED:
        default:
            show_selected_cb(NULL, data);
            break;
        }
        show_all_headers_save=-1;
    }
}

void
reset_show_all_headers(void)
{
    GtkWidget *btn;

    show_all_headers_save=-1;
    btn=get_tool_widget(GTK_WIDGET(balsa_app.main_window), 0,
                        BALSA_PIXMAP_SHOW_HEADERS);
    if(btn)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), FALSE);
}

static void
show_preview_pane_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *btn;

    btn=get_tool_widget(GTK_WIDGET(balsa_app.main_window), TOOLBAR_MAIN,
			BALSA_PIXMAP_SHOW_PREVIEW);
    if(!btn)
	return;

    balsa_app.previewpane = GTK_TOGGLE_BUTTON(btn)->active;
    balsa_window_refresh(balsa_app.main_window);
}

/* browse_wrap can also be changed in the preferences window
 *
 * update_view_menu is called to synchronize the view menu check item
 * */
void
update_view_menu(void)
{
    GtkWidget *w = view_menu[MENU_VIEW_WRAP_POS].widget;
    gtk_signal_handler_block_by_func(GTK_OBJECT(w), wrap_message_cb,
                                     balsa_app.main_window);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w),
                                   balsa_app.browse_wrap);
    gtk_signal_handler_unblock_by_func(GTK_OBJECT(w), wrap_message_cb,
                                       balsa_app.main_window);
    if (balsa_app.main_window->preview)
        balsa_message_set_wrap(BALSA_MESSAGE(balsa_app.main_window->preview),
                               balsa_app.browse_wrap);
}
