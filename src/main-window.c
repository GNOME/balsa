/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
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

/*
 * BalsaWindow: subclass of GnomeApp
 *
 * The only known instance of BalsaWindow is balsa_app.main_window,
 * but the code in this module does not depend on that fact, to make it
 * more self-contained.  pb
 */

#include "config.h"

#include <string.h>
#include <gnome.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "libbalsa.h"
#include "misc.h"
#include "html.h"

#include "ab-window.h"
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
#include "send.h"
#include "store-address.h"
#include "save-restore.h"
#include "toolbar-prefs.h"
#include "toolbar-factory.h"

#ifdef BALSA_USE_THREADS
#include "threads.h"
#endif

#include "filter.h"
#include "filter-funcs.h"

#include "libinit_balsa/init_balsa.h"

#define MAILBOX_DATA "mailbox_data"

#define APPBAR_KEY "balsa_appbar"

enum {
    OPEN_MAILBOX_NODE,
    CLOSE_MAILBOX_NODE,
    LAST_SIGNAL
};

enum {
    BALSA_PROGRESS_NONE = 0,
    BALSA_PROGRESS_ACTIVITY,
    BALSA_PROGRESS_INCREMENT
};

enum {
    TARGET_MESSAGES
};

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
static int quiet_check=0;

static void check_messages_thread(gpointer data);
static void display_new_mail_notification(int num_new, int has_new);

#endif

static void balsa_window_class_init(BalsaWindowClass * klass);
static void balsa_window_init(BalsaWindow * window);
static void balsa_window_real_open_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_real_close_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_destroy(GtkObject * object);

GtkWidget *balsa_window_find_current_index(BalsaWindow * window);
static gboolean balsa_close_commit_mailbox_on_timer(GtkWidget * widget, 
					     gpointer * data);

static void balsa_window_index_changed_cb(GtkWidget * widget,
                                          gpointer data);
static void balsa_window_idle_replace(BalsaWindow * window,
                                      LibBalsaMessage * message);
static void balsa_window_idle_remove(BalsaWindow * window);
static gboolean balsa_window_idle_cb(BalsaWindow * window);


static void check_mailbox_list(GList * list);
static gboolean mailbox_check_func(GtkTreeModel * model,
				   GtkTreePath * path, GtkTreeIter * iter);
static gboolean imap_check_test(const gchar * path);

static void enable_message_menus(BalsaWindow * window,
				 LibBalsaMessage * message);
static void enable_edit_menus(BalsaMessage * bm);
#ifdef HAVE_GTKHTML
static void enable_view_menus(BalsaMessage * bm);
#endif				/* HAVE_GTKHTML */
static void register_open_mailbox(LibBalsaMailbox *m);
static void unregister_open_mailbox(LibBalsaMailbox *m);
static gboolean is_open_mailbox(LibBalsaMailbox *m);

/* dialogs */
static void show_about_box(void);

/* callbacks */
static void send_outbox_messages_cb(GtkWidget *, gpointer data);
static void send_receive_messages_cb(GtkWidget *, gpointer data);
static void message_print_cb(GtkWidget * widget, gpointer data);

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
static void toggle_flagged_message_cb(GtkWidget * widget, gpointer data);
static void toggle_deleted_message_cb(GtkWidget * widget, gpointer data);
static void toggle_new_message_cb(GtkWidget * widget, gpointer data);
static void toggle_answered_message_cb(GtkWidget * widget, gpointer data);
static void store_address_cb(GtkWidget * widget, gpointer data);
static void wrap_message_cb(GtkWidget * widget, gpointer data);
static void show_no_headers_cb(GtkWidget * widget, gpointer data);
static void show_selected_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_cb(GtkWidget * widget, gpointer data);
static void show_all_headers_tool_cb(GtkWidget * widget, gpointer data);
static void empty_trash_cb(GtkWidget * widget, gpointer data);
static void reset_show_all_headers(BalsaWindow * window);
static void show_preview_pane_cb(GtkWidget * widget, gpointer data);

static void threading_change_cb(GtkWidget * widget, gpointer data);
static void balsa_window_set_threading_menu(BalsaWindow * window,
					    int option);
static void balsa_window_set_filter_menu(BalsaWindow * window,
					 int gui_filter);
static void expand_all_cb(GtkWidget * widget, gpointer data);
static void collapse_all_cb(GtkWidget * widget, gpointer data);
#ifdef HAVE_GTKHTML
static void zoom_cb(GtkWidget * widget, gpointer data);
#endif				/* HAVE_GTKHTML */
#if defined(ENABLE_TOUCH_UI)
static void open_mailbox_cb(GtkWidget *widget, gpointer data);
#endif /* ENABLE_TOUCH_UI */

static void address_book_cb(GtkWindow *widget, gpointer data);

static void copy_cb(GtkWidget * widget, BalsaWindow *bw);
static void select_all_cb(GtkWidget * widget, gpointer);
static void message_copy_cb(GtkWidget * widget, gpointer data);
static void message_select_all_cb(GtkWidget * widget, gpointer data);
static void mark_all_cb(GtkWidget * widget, gpointer);

static void select_part_cb(BalsaMessage * bm, gpointer data);

static void find_real(BalsaWindow * window, BalsaIndex * bindex,
		      gboolean again);
static void find_cb(GtkWidget * widget, gpointer data);
static void find_again_cb(GtkWidget * widget, gpointer data);
static void filter_dlg_cb(GtkWidget * widget, gpointer data);
static void filter_export_cb(GtkWidget * widget, gpointer data);
static void filter_run_cb(GtkWidget * widget, gpointer data);
static void remove_duplicates_cb(GtkWidget * widget, gpointer data);

static void mailbox_close_cb(GtkWidget * widget, gpointer data);
static void mailbox_tab_close_cb(GtkWidget * widget, gpointer data);

static void hide_changed_cb(GtkWidget * widget, gpointer data);
#ifdef DEBUG
static void show_name_cb(GtkWidget * widget, gpointer data);
static void show_patch_cb(GtkWidget * widget, gpointer data);
#endif
static void reset_filter_cb(GtkWidget * widget, gpointer data);
static void mailbox_commit_changes(GtkWidget * widget, gpointer data);
static void mailbox_commit_all(GtkWidget * widget, gpointer data);

static void show_mbtree_cb(GtkWidget * widget, gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void show_mbtabs_cb(GtkWidget * widget, gpointer data);
#endif

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
balsa_quit_nicely(GtkWidget * widget, gpointer data)
{
    GdkEventAny e = { GDK_DELETE, NULL, 0 };
    e.window = GTK_WIDGET(data)->window;
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
                           GTK_STOCK_ADD),
#define MENU_FILE_NEW_MAILDIR_POS 3
    GNOMEUIINFO_ITEM_STOCK(N_("Local Maildir mailbox..."), 
                           N_("Add a new Maildir style mailbox"),
                           mailbox_conf_add_maildir_cb, 
                           GTK_STOCK_ADD),
#define MENU_FILE_NEW_MH_POS 4
    GNOMEUIINFO_ITEM_STOCK(N_("Local MH mailbox..."), 
                           N_("Add a new MH style mailbox"),
                           mailbox_conf_add_mh_cb, 
                           GTK_STOCK_ADD),
#define MENU_FILE_NEW_IMAP_POS 5
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP mailbox..."), 
                           N_("Add a new IMAP mailbox"),
                           mailbox_conf_add_imap_cb, 
                           GTK_STOCK_ADD),
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_NEW_IMAP_FOLDER_POS 7
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP folder..."), 
                           N_("Add a new IMAP folder"),
                           folder_conf_add_imap_cb, 
                           GTK_STOCK_ADD),
#define MENU_FILE_NEW_IMAP_SUBFOLDER_POS 8
    GNOMEUIINFO_ITEM_STOCK(N_("Remote IMAP subfolder..."), 
                           N_("Add a new IMAP subfolder"),
                           folder_conf_add_imap_sub_cb, 
                           GTK_STOCK_ADD),
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
      N_("Print current message"),
      message_print_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      BALSA_PIXMAP_MENU_PRINT, 'P', GDK_CONTROL_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_FILE_ADDRESS_POS 9
    {
	GNOME_APP_UI_ITEM, N_("_Address Book..."),
	N_("Open the address book"),
	address_book_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
	GNOME_STOCK_BOOK_RED, 'B', 0, NULL
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
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_MENU_FIND_ITEM(find_cb, NULL),
    GNOMEUIINFO_MENU_FIND_AGAIN_ITEM(find_again_cb, NULL),
/*     GNOMEUIINFO_MENU_REPLACE_ITEM(NULL, NULL); */
/*     GNOMEUIINFO_SEPARATOR, */
/* #define MENU_EDIT_PREFERENCES_POS 7 */
/*     GNOMEUIINFO_MENU_PREFERENCES_ITEM(open_preferences_manager, NULL), */
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_ITEM_STOCK(N_("F_ilters..."), N_("Manage filters"),
                           filter_dlg_cb, GTK_STOCK_PROPERTIES),
    GNOMEUIINFO_ITEM_STOCK(N_("_Export Filters"), N_("Export filters as Sieve scripts"),
			   filter_export_cb, GTK_STOCK_PROPERTIES),
    GNOMEUIINFO_END
};

static GnomeUIInfo shown_hdrs_menu[] = {
    GNOMEUIINFO_RADIOITEM(N_("_No Headers"), N_("Display no headers"),
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
    GNOMEUIINFO_RADIOITEM_DATA(N_("_Flat index"),
                               N_("No threading at all"),
                               threading_change_cb,
                               GINT_TO_POINTER(LB_MAILBOX_THREADING_FLAT),
                               NULL),
#define MENU_THREADING_SIMPLE_POS 1
    GNOMEUIINFO_RADIOITEM_DATA(N_("Si_mple threading"),
                               N_("Simple threading algorithm"),
                               threading_change_cb,
                               GINT_TO_POINTER(LB_MAILBOX_THREADING_SIMPLE),
                               NULL),
#define MENU_THREADING_JWZ_POS 2
    GNOMEUIINFO_RADIOITEM_DATA(N_("_JWZ threading"),
                               N_("Elaborate JWZ threading"),
                               threading_change_cb,
                               GINT_TO_POINTER(LB_MAILBOX_THREADING_JWZ),
                               NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo view_menu[] = {
#if defined(ENABLE_TOUCH_UI)
    { GNOME_APP_UI_ITEM, N_("_In"),  N_("Show Incoming Folder"),
     open_mailbox_cb, GINT_TO_POINTER('I'), NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'I', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL},
    { GNOME_APP_UI_ITEM, N_("_Drafts"),  N_("Show Drafts"),
     open_mailbox_cb, GINT_TO_POINTER('D'), NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'D', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL},
    { GNOME_APP_UI_ITEM, N_("_Out"),  N_("Show Outgoing Folder"),
     open_mailbox_cb, GINT_TO_POINTER('O'), NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'O', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL},
    { GNOME_APP_UI_ITEM, N_("_Sent"),  N_("Show Sent Messages"),
     open_mailbox_cb, GINT_TO_POINTER('S'), NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'I', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL},
    { GNOME_APP_UI_ITEM, N_("_Trash"),  N_("Show Trashed Messages"),
     open_mailbox_cb, GINT_TO_POINTER('T'), NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'T', GDK_CONTROL_MASK|GDK_SHIFT_MASK, NULL},
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_MAILBOX_LIST_POS 6
#else
#define MENU_VIEW_MAILBOX_LIST_POS  0
#endif
    GNOMEUIINFO_TOGGLEITEM(N_("_Show Mailbox Tree (F9)"),
                           N_("Toggle display of mailbox and folder tree"),
                           show_mbtree_cb, NULL),
#if !defined(ENABLE_TOUCH_UI)
#define MENU_VIEW_MAILBOX_TABS_POS 1
    GNOMEUIINFO_TOGGLEITEM(N_("Show Mailbox _Tabs"),
                           N_("Toggle display of mailbox notebook tabs"),
                           show_mbtabs_cb, NULL),
#define SHOW_MENU_CNT 2
#else
#define SHOW_MENU_CNT 1
#endif /* ENABLE_TOUCH_UI */
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_WRAP_POS (MENU_VIEW_MAILBOX_LIST_POS+SHOW_MENU_CNT+1)
    GNOMEUIINFO_TOGGLEITEM(N_("_Wrap"), N_("Wrap message lines"),
                           wrap_message_cb, NULL),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(shown_hdrs_menu),
    GNOMEUIINFO_SEPARATOR,
    GNOMEUIINFO_RADIOLIST(threading_menu),
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_EXPAND_ALL_POS (MENU_VIEW_WRAP_POS+6)
    { GNOME_APP_UI_ITEM, N_("E_xpand All"),
     N_("Expand all threads"),
     expand_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'E', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_COLLAPSE_ALL_POS (MENU_VIEW_EXPAND_ALL_POS+1)
    { GNOME_APP_UI_ITEM, N_("_Collapse All"),
     N_("Collapse all expanded threads"),
     collapse_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
     NULL, 'L', GDK_CONTROL_MASK, NULL},
#ifdef HAVE_GTKHTML
    GNOMEUIINFO_SEPARATOR,
#define MENU_VIEW_ZOOM_IN MENU_VIEW_COLLAPSE_ALL_POS + 2
    { GNOME_APP_UI_ITEM, N_("Zoom _In"), N_("Increase magnification"),
      zoom_cb, GINT_TO_POINTER(1), NULL, GNOME_APP_PIXMAP_STOCK,
      GTK_STOCK_ZOOM_IN, '+', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_ZOOM_OUT MENU_VIEW_ZOOM_IN + 1
    { GNOME_APP_UI_ITEM, N_("Zoom _Out"), N_("Decrease magnification"),
      zoom_cb, GINT_TO_POINTER(-1), NULL, GNOME_APP_PIXMAP_STOCK,
      GTK_STOCK_ZOOM_OUT, '-', GDK_CONTROL_MASK, NULL},
#define MENU_VIEW_ZOOM_100 MENU_VIEW_ZOOM_OUT + 1
      /* To warn msgfmt that the % sign isn't a format specifier: */
      /* xgettext:no-c-format */
    { GNOME_APP_UI_ITEM, N_("Zoom _100%"), N_("No magnification"),
      zoom_cb, GINT_TO_POINTER(0), NULL, GNOME_APP_PIXMAP_STOCK,
      GTK_STOCK_ZOOM_100, 0, 0, NULL},
#endif				/* HAVE_GTKHTML */
    GNOMEUIINFO_END
};

static GnomeUIInfo message_toggle_menu[] = {
#define MENU_MESSAGE_TOGGLE_FLAGGED_POS 0
    /* ! */
    {
        GNOME_APP_UI_ITEM, N_("_Flagged"), N_("Toggle flagged"),
        toggle_flagged_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_FLAGGED, 'X', 0, NULL
    },
#define MENU_MESSAGE_TOGGLE_DELETED_POS 1
    { GNOME_APP_UI_ITEM, N_("_Deleted"), 
      N_("Toggle deleted flag"),
      toggle_deleted_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
      GNOME_STOCK_TRASH, 'D', GDK_CONTROL_MASK, NULL },
#define MENU_MESSAGE_TOGGLE_NEW_POS 2
    {
        GNOME_APP_UI_ITEM, N_("_New"), N_("Toggle New"),
        toggle_new_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEW, 'R', GDK_CONTROL_MASK, NULL
    },
#define MENU_MESSAGE_TOGGLE_ANSWERED_POS 3
    {
        GNOME_APP_UI_ITEM, N_("_Answered"), N_("Toggle Answered"),
        toggle_answered_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
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
        GNOME_APP_UI_ITEM, N_("Forward _inline..."),
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
        GNOME_STOCK_BOOK_OPEN, 'v', GDK_CONTROL_MASK, NULL
    },
	GNOMEUIINFO_SEPARATOR,   
#define MENU_MESSAGE_COPY_POS 11
	GNOMEUIINFO_MENU_COPY_ITEM(message_copy_cb, NULL),
#define MENU_MESSAGE_SELECT_ALL_POS 12
	{
		GNOME_APP_UI_ITEM, N_("_Select Text"),
		N_("Select entire mail"),
		message_select_all_cb, NULL, NULL, GNOME_APP_PIXMAP_NONE,
		NULL, 0, (GdkModifierType) 0, NULL
    },  
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_TRASH_POS 14
    /* D */
    {
        GNOME_APP_UI_ITEM, N_("_Move to Trash"), 
        N_("Move the current message to Trash mailbox"),
        trash_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_TRASH, 'D', 0, NULL
    },
#define MENU_MESSAGE_TOGGLE_POS 15
    /* ! */
    GNOMEUIINFO_SUBTREE(N_("_Toggle flag"), message_toggle_menu),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MESSAGE_STORE_ADDRESS_POS 17
    /* S */
    {
        GNOME_APP_UI_ITEM, N_("_Store Address..."),
        N_("Store address of sender in addressbook"),
        store_address_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        GNOME_STOCK_BOOK_RED, 'S', 0, NULL
    },
    GNOMEUIINFO_END
};

/* Really, entire mailbox_hide_menu should be build dynamically from
 * the hide_states array since different mailboxes support different
 * set of flags/keywords. */
static const struct {
    LibBalsaMessageFlag flag;
    unsigned set:1;
} hide_states[] = {
    { LIBBALSA_MESSAGE_FLAG_DELETED, 1 },
    { LIBBALSA_MESSAGE_FLAG_DELETED, 0 },
    { LIBBALSA_MESSAGE_FLAG_NEW,     0 },
    { LIBBALSA_MESSAGE_FLAG_NEW,     1 },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 1 },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 0 },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 1 },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 0 }
};

static GnomeUIInfo mailbox_hide_menu[] = {
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("_Deleted"),  "",
     hide_changed_cb, GINT_TO_POINTER(0), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("Un_Deleted"),  "",
     hide_changed_cb, GINT_TO_POINTER(1), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("_Read"),     "",
     hide_changed_cb, GINT_TO_POINTER(2), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("Un_read"),     "",
     hide_changed_cb, GINT_TO_POINTER(3), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("_Flagged"),  "",
     hide_changed_cb, GINT_TO_POINTER(4), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("Un_flagged"),  "",
     hide_changed_cb, GINT_TO_POINTER(5), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("_Answered"), "",
     hide_changed_cb, GINT_TO_POINTER(6), NULL),
    GNOMEUIINFO_TOGGLEITEM_DATA
    (N_("Un_answered"), "",
     hide_changed_cb, GINT_TO_POINTER(7), NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo mailbox_menu[] = {
#define MENU_MAILBOX_NEXT_POS 0
    {
        GNOME_APP_UI_ITEM, N_("Next Message"), N_("Next Message"),
        next_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT, 'N', 0, NULL
    },
#define MENU_MAILBOX_PREV_POS (MENU_MAILBOX_NEXT_POS+1)
    {
        GNOME_APP_UI_ITEM, N_("Previous Message"), N_("Previous Message"),
        previous_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_PREVIOUS, 'P', 0, NULL
    },
#define MENU_MAILBOX_NEXT_UNREAD_POS (MENU_MAILBOX_PREV_POS+1)
    {
        GNOME_APP_UI_ITEM, N_("Next Unread Message"),
        N_("Next Unread Message"),
        next_unread_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT_UNREAD, 'N', GDK_CONTROL_MASK, NULL
    },
#define MENU_MAILBOX_NEXT_FLAGGED_POS (MENU_MAILBOX_NEXT_UNREAD_POS+1)
    {
        GNOME_APP_UI_ITEM, N_("Next Flagged Message"),
        N_("Next Flagged Message"),
        next_flagged_message_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_NEXT_FLAGGED, 'F',GDK_MOD1_MASK|GDK_CONTROL_MASK,
        NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_HIDE_POS (MENU_MAILBOX_NEXT_FLAGGED_POS+2)
    GNOMEUIINFO_SUBTREE(N_("_Hide messages"), mailbox_hide_menu),
    /* the next one is for testing only */
#ifdef DEBUG
    GNOMEUIINFO_ITEM_STOCK(N_("Show from _name"),  "",
                           show_name_cb, GTK_STOCK_FIND),
    GNOMEUIINFO_ITEM_STOCK(N_("Show _Patches"),  "",
                           show_patch_cb, GTK_STOCK_FIND),
#define FILTER_ENTRIES_CNT 5
#else
#define FILTER_ENTRIES_CNT 3
#endif
    GNOMEUIINFO_ITEM_STOCK(N_("Reset _Filter"),  "",
                           reset_filter_cb, GTK_STOCK_FIND),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_MARK_ALL_POS (MENU_MAILBOX_HIDE_POS+FILTER_ENTRIES_CNT)
    {
        GNOME_APP_UI_ITEM, N_("Select all"),
        N_("Select all messages in current mailbox"),
        mark_all_cb, NULL, NULL, GNOME_APP_PIXMAP_STOCK,
        BALSA_PIXMAP_MENU_MARK_ALL, 0, (GdkModifierType) 0, NULL
    },
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EDIT_POS (MENU_MAILBOX_MARK_ALL_POS+2)
    GNOMEUIINFO_ITEM_STOCK(N_("_Edit..."), N_("Edit the selected mailbox"),
                           mailbox_conf_edit_cb,
                           GTK_STOCK_PREFERENCES),
#define MENU_MAILBOX_DELETE_POS (MENU_MAILBOX_EDIT_POS+1)
    GNOMEUIINFO_ITEM_STOCK(N_("_Delete..."),
                           N_("Delete the selected mailbox"),
                           mailbox_conf_delete_cb,
                           GTK_STOCK_REMOVE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_COMMIT_POS (MENU_MAILBOX_DELETE_POS+2)
    GNOMEUIINFO_ITEM_STOCK(
        N_("Co_mmit Current"),
        N_("Commit the changes in the currently opened mailbox"),
        mailbox_commit_changes,
        GTK_STOCK_REFRESH),
    GNOMEUIINFO_ITEM_STOCK(
        N_("Commit _All"),
        N_("Commit the changes in all mailboxes"),
        mailbox_commit_all,
        GTK_STOCK_REFRESH),
#define MENU_MAILBOX_CLOSE_POS (MENU_MAILBOX_COMMIT_POS+2)
    GNOMEUIINFO_ITEM_STOCK(N_("_Close"), N_("Close mailbox"),
                           mailbox_close_cb, GTK_STOCK_CLOSE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_EMPTY_TRASH_POS (MENU_MAILBOX_CLOSE_POS+2)
    GNOMEUIINFO_ITEM_STOCK(N_("Empty _Trash"),
                           N_("Delete messages from the Trash mailbox"),
                           empty_trash_cb, GTK_STOCK_REMOVE),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_APPLY_FILTERS (MENU_MAILBOX_EMPTY_TRASH_POS+2)
    GNOMEUIINFO_ITEM_STOCK(N_("Edit/Apply _Filters"),
                           N_("Filter the content of the selected mailbox"),
                           filter_run_cb, GTK_STOCK_PROPERTIES),
    GNOMEUIINFO_SEPARATOR,
#define MENU_MAILBOX_REMOVE_DUPLICATES (MENU_MAILBOX_APPLY_FILTERS+2)
    GNOMEUIINFO_ITEM_STOCK(N_("_Remove Duplicates"),
                           N_("Remove duplicated messages "
                              "from the selected mailbox"),
                           remove_duplicates_cb, GTK_STOCK_REMOVE),
    GNOMEUIINFO_END
};

static GnomeUIInfo settings_menu[] = {
#define MENU_SETTINGS_PREFERENCES_POS 0
    GNOMEUIINFO_MENU_PREFERENCES_ITEM (open_preferences_manager, NULL),
    GNOMEUIINFO_ITEM_STOCK(N_("_Toolbars..."),
                           N_("Customize toolbars"),
                           customize_dialog_cb,
                           GTK_STOCK_EXECUTE),
    GNOMEUIINFO_ITEM_STOCK(N_("_Identities..."), 
                           N_("Create and set current identities"), 
                           ident_manage_dialog_cb, 
                           BALSA_PIXMAP_MENU_IDENTITY),
    GNOMEUIINFO_END
};

static GnomeUIInfo help_menu[] = {
    GNOMEUIINFO_HELP("balsa"),
    GNOMEUIINFO_MENU_ABOUT_ITEM(show_about_box, NULL),
    GNOMEUIINFO_END
};

static GnomeUIInfo main_menu[] = {
    GNOMEUIINFO_MENU_FILE_TREE(file_menu),
    GNOMEUIINFO_MENU_EDIT_TREE(edit_menu),
    GNOMEUIINFO_MENU_VIEW_TREE(view_menu),
    GNOMEUIINFO_SUBTREE(N_("Mail_box"), mailbox_menu),
    GNOMEUIINFO_SUBTREE(N_("_Message"), message_menu),
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
        static const GTypeInfo window_info = {
            sizeof(BalsaWindowClass),
            NULL,               /* base_init */
            NULL,               /* base_finalize */
            (GClassInitFunc) balsa_window_class_init,
            NULL,               /* class_finalize */
            NULL,               /* class_data */
            sizeof(BalsaWindow),
            0,                  /* n_preallocs */
            (GInstanceInitFunc) balsa_window_init
        };

        window_type =
            g_type_register_static(GNOME_TYPE_APP, "BalsaWindow",
                                   &window_info, 0);
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
        g_signal_new("open_mailbox_node",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(BalsaWindowClass, open_mbnode),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE, 1, G_TYPE_OBJECT);

    window_signals[CLOSE_MAILBOX_NODE] =
        g_signal_new("close_mailbox_node",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(BalsaWindowClass, close_mbnode),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE, 1, G_TYPE_OBJECT);

    object_class->destroy = balsa_window_destroy;

    klass->open_mbnode = balsa_window_real_open_mbnode;
    klass->close_mbnode = balsa_window_real_close_mbnode;

    g_timeout_add(30000, (GSourceFunc) balsa_close_commit_mailbox_on_timer, NULL);

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
#endif
    return FALSE; /* allow delete */
}
static void
size_allocate_cb(GtkWidget * widget, GtkAllocation * alloc)
{
    if (balsa_app.show_mblist) {
	GtkWidget *paned = gtk_widget_get_ancestor(widget, GTK_TYPE_PANED);
	balsa_app.mblist_width = gtk_paned_get_position(GTK_PANED(paned));
    }
}

/* Toolbar buttons and their callbacks. */
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
    { BALSA_PIXMAP_TRASH_EMPTY,      empty_trash_cb },
    { BALSA_PIXMAP_CLOSE_MBOX,       mailbox_close_cb },
    { BALSA_PIXMAP_SHOW_PREVIEW,     show_preview_pane_cb }
};

/* Standard buttons; "" means a separator. */
static const gchar* main_toolbar[] = {
    BALSA_PIXMAP_RECEIVE,
    "",
    BALSA_PIXMAP_TRASH,
    "",
    BALSA_PIXMAP_NEW,
    BALSA_PIXMAP_CONTINUE,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    BALSA_PIXMAP_PRINT
};

/* Create the toolbar model for the main window's toolbar.
 */
BalsaToolbarModel *
balsa_window_get_toolbar_model(void)
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
    for (i = 0; i < ELEMENTS(main_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(main_toolbar[i]));

    current = &balsa_app.main_window_toolbar_current;

    model = balsa_toolbar_model_new(legal, standard, current);

    return model;
}

static GtkWidget *
bw_frame(GtkWidget * widget)
{
    GtkWidget *frame = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame), widget);
    gtk_widget_show(frame);
    return frame;
}

static void
bw_set_panes(BalsaWindow * window)
{
    window->vpaned = gtk_vpaned_new();
    window->hpaned = gtk_hpaned_new();
    gtk_paned_pack1(GTK_PANED(window->hpaned), bw_frame(window->mblist),
		    TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(window->vpaned), bw_frame(window->preview),
		    TRUE, TRUE);
    if  (balsa_app.alternative_layout){
        gnome_app_set_contents(GNOME_APP(window), window->vpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), bw_frame(window->notebook),
			TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->hpaned,  TRUE,TRUE);
    } else {
        gnome_app_set_contents(GNOME_APP(window), window->hpaned);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->vpaned,  TRUE,TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), bw_frame(window->notebook),
			TRUE, TRUE);
    }
}

GtkWidget *
balsa_window_new()
{

    BalsaWindow *window;
    BalsaToolbarModel *model;
    GtkWidget *toolbar;
    GnomeAppBar *appbar;
    unsigned i;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS
     * - allows for grey out */
    register_balsa_pixmaps();

    window = g_object_new(BALSA_TYPE_WINDOW, NULL);

    gnome_app_construct(GNOME_APP(window), "balsa", "Balsa");
    register_balsa_pixbufs(GTK_WIDGET(window));

    gnome_app_create_menus_with_data(GNOME_APP(window), main_menu, window);

#ifdef HAVE_GTKHTML
    /* Use Ctrl+= as an alternative accelerator for zoom-in, because
     * Ctrl++ is a 3-key combination. */
    gtk_widget_add_accelerator(view_menu[MENU_VIEW_ZOOM_IN].widget,
			       "activate", GNOME_APP(window)->accel_group,
			       '=', GDK_CONTROL_MASK, (GtkAccelFlags) 0);
#endif				/* HAVE_GTKHTML */

    /* Use Del as an alternative accelerator for Ctrl+D
     * (toggle deleted). */
    gtk_widget_add_accelerator(message_toggle_menu
			       [MENU_MESSAGE_TOGGLE_DELETED_POS].widget,
			       "activate", GNOME_APP(window)->accel_group,
			       GDK_Delete, (GdkModifierType) 0,
			       (GtkAccelFlags) 0);

    /* Use F9 as an accelerator for `Show mailbox tree' */
    gtk_widget_add_accelerator(view_menu
			       [MENU_VIEW_MAILBOX_LIST_POS].widget,
			       "activate", GNOME_APP(window)->accel_group,
			       GDK_F9, (GdkModifierType) 0,
			       (GtkAccelFlags) 0);

    model = balsa_window_get_toolbar_model();
    toolbar = balsa_toolbar_new(model);
    for(i=0; i < ELEMENTS(callback_table); i++)
        balsa_toolbar_set_callback(toolbar, callback_table[i].icon_id,
                                   G_CALLBACK(callback_table[i].callback),
                                   window);

    gnome_app_set_toolbar(GNOME_APP(window), GTK_TOOLBAR(toolbar));
    
    appbar =
        GNOME_APPBAR(gnome_appbar_new(TRUE, TRUE, GNOME_PREFERENCES_USER));
    gnome_app_set_statusbar(GNOME_APP(window), GTK_WIDGET(appbar));
    gtk_progress_bar_set_pulse_step(gnome_appbar_get_progress(appbar), 0.01);
    g_object_set_data(G_OBJECT(window), APPBAR_KEY, appbar);
    balsa_app.appbar = appbar;
    gnome_app_install_appbar_menu_hints(GNOME_APPBAR(balsa_app.appbar),
                                        main_menu);

    gtk_window_set_resizable(GTK_WINDOW(window), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(window), balsa_app.mw_width,
                                balsa_app.mw_height);

    window->notebook = gtk_notebook_new();
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(window->notebook),
                               balsa_app.show_notebook_tabs);
    gtk_notebook_set_show_border (GTK_NOTEBOOK(window->notebook), FALSE);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (window->notebook), TRUE);
    g_signal_connect(G_OBJECT(window->notebook), "size_allocate",
                     G_CALLBACK(notebook_size_alloc_cb), NULL);
    g_signal_connect(G_OBJECT(window->notebook), "switch_page",
                     G_CALLBACK(notebook_switch_page_cb), NULL);
    gtk_drag_dest_set (GTK_WIDGET (window->notebook), GTK_DEST_DEFAULT_ALL,
                       notebook_drop_types, NUM_DROP_TYPES,
                       GDK_ACTION_DEFAULT | GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(G_OBJECT (window->notebook), "drag-data-received",
                     G_CALLBACK (notebook_drag_received_cb), NULL);
    g_signal_connect(G_OBJECT (window->notebook), "drag-motion",
                     G_CALLBACK (notebook_drag_motion_cb), NULL);
    balsa_app.notebook = window->notebook;
    g_object_add_weak_pointer(G_OBJECT(window->notebook),
			      (gpointer) &balsa_app.notebook);

    window->preview = balsa_message_new();

    g_signal_connect(G_OBJECT(window->preview), "select-part",
                     G_CALLBACK(select_part_cb), window);

    /* XXX */
    balsa_app.mblist =  BALSA_MBLIST(balsa_mblist_new());
    window->mblist =
        gtk_scrolled_window_new(gtk_tree_view_get_hadjustment
                                (GTK_TREE_VIEW(balsa_app.mblist)),
                                gtk_tree_view_get_vadjustment
                                (GTK_TREE_VIEW(balsa_app.mblist)));
    gtk_container_add(GTK_CONTAINER(window->mblist), 
                      GTK_WIDGET(balsa_app.mblist));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->mblist),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    g_signal_connect(G_OBJECT(balsa_app.mblist), "size_allocate",
		     G_CALLBACK(size_allocate_cb), NULL);
    balsa_mblist_default_signal_bindings(balsa_app.mblist);
    gtk_widget_show_all(window->mblist);

    bw_set_panes(window);

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

#if !defined(ENABLE_TOUCH_UI)
    if (balsa_app.show_notebook_tabs)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM
                                       (view_menu[MENU_VIEW_MAILBOX_TABS_POS].widget),
                                       TRUE);
#endif

    /* Disable menu items at start up */
    balsa_window_enable_mailbox_menus(window, NULL);
    enable_message_menus(window, NULL);
    enable_edit_menus(NULL);
#ifdef HAVE_GTKHTML
    enable_view_menus(NULL);
#endif				/* HAVE_GTKHTML */
    balsa_window_enable_continue(window);

    /* set initial state of toggle preview pane button */
    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_SHOW_PREVIEW,
                                    balsa_app.previewpane);

    g_signal_connect(G_OBJECT(window), "size_allocate",
                     G_CALLBACK(mw_size_alloc_cb), NULL);
    g_signal_connect(G_OBJECT (window), "destroy",
                     G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(delete_cb), NULL);

    return GTK_WIDGET(window);
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a mailbox open. 
 */
void
balsa_window_enable_mailbox_menus(BalsaWindow * window, BalsaIndex * index)
{
    const static int mailbox_menu_entries[] = {
        MENU_MAILBOX_NEXT_POS,        MENU_MAILBOX_PREV_POS,
        MENU_MAILBOX_NEXT_UNREAD_POS, MENU_MAILBOX_NEXT_FLAGGED_POS,
        MENU_MAILBOX_MARK_ALL_POS,    MENU_MAILBOX_DELETE_POS,
        MENU_MAILBOX_EDIT_POS,     /* MENU_MAILBOX_COMMIT_POS, */
	MENU_MAILBOX_CLOSE_POS,       MENU_MAILBOX_APPLY_FILTERS,
	MENU_MAILBOX_REMOVE_DUPLICATES
    };

    const static int threading_menu_entries[] = {
        MENU_THREADING_FLAT_POS,      MENU_THREADING_SIMPLE_POS,
        MENU_THREADING_JWZ_POS
    };

    const static int view_menu_entries[] = {
        MENU_VIEW_EXPAND_ALL_POS,     MENU_VIEW_COLLAPSE_ALL_POS
    };

    LibBalsaMailbox *mailbox = NULL;
    BalsaMailboxNode *mbnode = NULL;
    gboolean enable;
    GtkWidget *toolbar;
    unsigned i;

    toolbar = balsa_toolbar_get_from_gnome_app(GNOME_APP(window));
    enable = (index != NULL);
    if (enable) {
        mbnode = index->mailbox_node;
        mailbox = mbnode->mailbox;
    }
    if (mailbox && mailbox->readonly) {
        gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget,
                                 FALSE);
    } else {
        gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_COMMIT_POS].widget,
                                 enable);
    }

    /* Toolbar */
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_PREVIOUS, 
                                       index && index->prev_message);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT, 
                                       index && index->next_message);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT_UNREAD, 
                                       mailbox 
                                       && mailbox->unread_messages > 0);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_NEXT_FLAGGED, 
                                       mailbox
                                       && libbalsa_mailbox_total_messages
				       (mailbox) > 0);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_CLOSE_MBOX,
                                       enable);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_MARKED_ALL,
                                       enable);

    /* Menu entries */
    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_SELECT_ALL_POS].widget,
                             enable);
    for(i=0; i < ELEMENTS(mailbox_menu_entries); i++)
        gtk_widget_set_sensitive(mailbox_menu[mailbox_menu_entries[i]].widget, enable);
    for(i=0; i < ELEMENTS(threading_menu_entries); i++)
        gtk_widget_set_sensitive(threading_menu[threading_menu_entries[i]].widget, enable);
    for(i=0; i < ELEMENTS(view_menu_entries); i++)
        gtk_widget_set_sensitive(view_menu[view_menu_entries[i]].widget, enable);

    if (mailbox) {
	balsa_window_set_threading_menu(window,
					libbalsa_mailbox_get_threading_type
					(mailbox));
	balsa_window_set_filter_menu(window,
				     libbalsa_mailbox_get_filter(mailbox));
    }
}

/*
 * Enable or disable menu items/toolbar buttons which depend 
 * on if there is a message selected. 
 */
static void
enable_message_menus(BalsaWindow * window, LibBalsaMessage * message)
{
    const static gchar* tools[] = { /* toolbar items */
        BALSA_PIXMAP_REPLY,       BALSA_PIXMAP_REPLY_ALL,  
        BALSA_PIXMAP_REPLY_GROUP, BALSA_PIXMAP_FORWARD, 
        BALSA_PIXMAP_MARKED_NEW,  BALSA_PIXMAP_PRINT
    };
    const static GnomeUIInfo* mods[] = { /* menu items to modify message */
        &message_menu[MENU_MESSAGE_TRASH_POS],
        &message_menu[MENU_MESSAGE_TOGGLE_POS],
        &message_toggle_menu[MENU_MESSAGE_TOGGLE_DELETED_POS],
        &message_toggle_menu[MENU_MESSAGE_TOGGLE_FLAGGED_POS],
        &message_toggle_menu[MENU_MESSAGE_TOGGLE_NEW_POS],
        &message_toggle_menu[MENU_MESSAGE_TOGGLE_ANSWERED_POS]
    };
    /* menu items requiring a message */
    const static GnomeUIInfo* std_menu[] = { 
        &file_menu[MENU_FILE_PRINT_POS], 
        &message_menu[MENU_MESSAGE_SAVE_PART_POS],
        &message_menu[MENU_MESSAGE_SOURCE_POS],
        &message_menu[MENU_MESSAGE_REPLY_POS],
        &message_menu[MENU_MESSAGE_REPLY_ALL_POS],
        &message_menu[MENU_MESSAGE_REPLY_GROUP_POS],
        &message_menu[MENU_MESSAGE_FORWARD_ATTACH_POS],
        &message_menu[MENU_MESSAGE_FORWARD_INLINE_POS],
        &message_menu[MENU_MESSAGE_STORE_ADDRESS_POS]
    };
    gboolean enable, enable_mod, enable_multi;
    guint i;
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(window));

    enable       = (message != NULL && message->mailbox != NULL);
    enable_mod   = (enable && !message->mailbox->readonly);
    enable_multi = (enable && libbalsa_message_is_multipart(message));

    /* Handle menu items which require write access to mailbox */
    for(i=0; i<ELEMENTS(mods); i++)
        gtk_widget_set_sensitive(mods[i]->widget, enable_mod);
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_TRASH,
                                       enable_mod);

    /* Handle items which require multiple parts to the mail */
    gtk_widget_set_sensitive(message_menu
                             [MENU_MESSAGE_NEXT_PART_POS].widget, 
                             enable_multi);
    gtk_widget_set_sensitive(message_menu
                             [MENU_MESSAGE_PREVIOUS_PART_POS].widget, 
                             enable_multi);

    for(i=0; i<ELEMENTS(std_menu); i++)
        gtk_widget_set_sensitive(std_menu[i]->widget, enable);

    /* Toolbar */
    for(i=0; i<ELEMENTS(tools); i++)
        balsa_toolbar_set_button_sensitive(toolbar, tools[i], enable);

    balsa_window_enable_continue(window);
}

/*
 * Enable/disable the copy and select all buttons
 */
static void
enable_edit_menus(BalsaMessage * bm)
{
    gboolean enable;
    enable = (bm && balsa_message_can_select(bm));

    gtk_widget_set_sensitive(edit_menu[MENU_EDIT_COPY_POS].widget, bm !=
                             NULL);

    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_COPY_POS].widget,
                             enable);
    gtk_widget_set_sensitive(message_menu[MENU_MESSAGE_SELECT_ALL_POS].
                             widget, enable);
}

#ifdef HAVE_GTKHTML
/*
 * Enable/disable the Zoom menu items on the View menu.
 */
static void
enable_view_menus(BalsaMessage * bm)
{
    gboolean enable = bm && balsa_message_can_zoom(bm);
    gtk_widget_set_sensitive(view_menu[MENU_VIEW_ZOOM_IN].widget, enable);
    gtk_widget_set_sensitive(view_menu[MENU_VIEW_ZOOM_OUT].widget, enable);
    gtk_widget_set_sensitive(view_menu[MENU_VIEW_ZOOM_100].widget, enable);
}
#endif				/* HAVE_GTKHTML */

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
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(window));
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
    balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_TRASH_EMPTY,
                                       set);
    gtk_widget_set_sensitive(mailbox_menu[MENU_MAILBOX_EMPTY_TRASH_POS].widget,
                             set);
}

/*
 * Enable/disable the continue buttons
 */
void
balsa_window_enable_continue(BalsaWindow * window)
{
    GtkWidget *toolbar;

    if (!window)
	return;
    toolbar = balsa_toolbar_get_from_gnome_app(GNOME_APP(window));

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

        balsa_toolbar_set_button_sensitive(toolbar, BALSA_PIXMAP_CONTINUE, n);
        gtk_widget_set_sensitive(file_menu[MENU_FILE_CONTINUE_POS].widget, n);

/*      libbalsa_mailbox_close(balsa_app.draftbox); */
    }
}

static void
balsa_window_set_threading_menu(BalsaWindow * window, int option)
{
    int pos;
    switch(option) {
    case LB_MAILBOX_THREADING_FLAT:
    pos = MENU_THREADING_FLAT_POS; break;
    case LB_MAILBOX_THREADING_SIMPLE:
    pos = MENU_THREADING_SIMPLE_POS; break;
    case LB_MAILBOX_THREADING_JWZ:
    pos = MENU_THREADING_JWZ_POS; break;
    default: return;
    }
    g_signal_handlers_block_by_func(G_OBJECT(threading_menu[pos].widget),
                                    threading_menu[pos].moreinfo, 
                                    window);
    gtk_check_menu_item_set_active
        (GTK_CHECK_MENU_ITEM(threading_menu[pos].widget), TRUE);
    g_signal_handlers_unblock_by_func(G_OBJECT(threading_menu[pos].widget),
                                      threading_menu[pos].moreinfo,
                                      window);
}

static void
balsa_window_set_filter_menu(BalsaWindow * window, int mask)
{
    unsigned i;

    for(i=0; i<ELEMENTS(mailbox_hide_menu); i++) {
        GtkWidget *item;
        int states_index =
            GPOINTER_TO_INT(mailbox_hide_menu[i].user_data);
        if(mailbox_hide_menu[i].type != GNOME_APP_UI_TOGGLEITEM)
            continue;
        item = mailbox_hide_menu[i].widget;
        g_signal_handlers_block_by_func(G_OBJECT(item),
                                        mailbox_hide_menu[i].moreinfo, 
                                        window);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), 
                                       mask & (1<<states_index));
        g_signal_handlers_unblock_by_func(G_OBJECT(item),
                                          mailbox_hide_menu[i].moreinfo, 
                                          window);
    }
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

    g_signal_emit(G_OBJECT(window), window_signals[OPEN_MAILBOX_NODE],
                  0, mbnode);
}

void
balsa_window_close_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
    g_return_if_fail(window != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(window));

    g_signal_emit(G_OBJECT(window), window_signals[CLOSE_MAILBOX_NODE],
                  0, mbnode);
}

static void
mailbox_tab_size_request(GtkWidget * widget, GtkRequisition * requisition,
                         gpointer user_data)
{
    gint border_width = GTK_CONTAINER(widget)->border_width;
    GtkRequisition child_requisition;
    
    requisition->width = border_width * 2;
    requisition->height = border_width * 2;
    gtk_widget_size_request(GTK_BIN(widget)->child, &child_requisition);
    requisition->width += child_requisition.width;
    requisition->height += child_requisition.height;
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
bw_notebook_label_notify(LibBalsaMailbox * mailbox, GtkLabel * lab)
{
    g_signal_handlers_disconnect_by_func(mailbox, bw_notebook_label_style,
					 lab);
}

typedef struct {
    GtkLabel *lab;
    gboolean has_unread_messages;
} BalsaWindowMailboxChangedInfo;

static gboolean
bw_mailbox_changed_idle(BalsaWindowMailboxChangedInfo *bwmci)
{
    gdk_threads_enter();
    if (bwmci->lab) {
	g_object_remove_weak_pointer(G_OBJECT(bwmci->lab),
				     (gpointer) &bwmci->lab);
	bw_notebook_label_style(bwmci->lab, bwmci->has_unread_messages);
    }
    g_free(bwmci);
    gdk_threads_leave();
    return FALSE;
}

static void
bw_mailbox_changed(LibBalsaMailbox * mailbox, GtkLabel * lab)
{
    BalsaWindowMailboxChangedInfo *bwmci =
	g_new(BalsaWindowMailboxChangedInfo, 1);

    bwmci->lab = lab;
    g_object_add_weak_pointer(G_OBJECT(bwmci->lab), (gpointer) &bwmci->lab);
    bwmci->has_unread_messages = mailbox->has_unread_messages;
    g_idle_add((GSourceFunc) bw_mailbox_changed_idle, bwmci);
}

static GtkWidget *
balsa_notebook_label_new (BalsaMailboxNode* mbnode)
{
       GtkWidget *close_pix;
       GtkWidget *box = gtk_hbox_new(FALSE, 4);
       GtkWidget *lab = gtk_label_new(mbnode->mailbox->name);
       GtkWidget *but = gtk_button_new();
       GtkWidget *ev = gtk_event_box_new();

#if GTK_CHECK_VERSION(2, 4, 0)
       gtk_event_box_set_visible_window(GTK_EVENT_BOX(ev), FALSE);
#endif

    bw_notebook_label_style(GTK_LABEL(lab),
			    mbnode->mailbox->has_unread_messages);
    g_signal_connect(mbnode->mailbox, "changed",
		     G_CALLBACK(bw_mailbox_changed), lab);
    g_object_weak_ref(G_OBJECT(lab), (GWeakNotify) bw_notebook_label_notify,
		      mbnode->mailbox);

       close_pix = gtk_image_new_from_stock(GTK_STOCK_CLOSE,
                                            GTK_ICON_SIZE_MENU);
       g_signal_connect(G_OBJECT(but), "size-request",
                        G_CALLBACK(mailbox_tab_size_request), NULL);

       gtk_button_set_relief(GTK_BUTTON (but), GTK_RELIEF_NONE);
       gtk_container_add(GTK_CONTAINER (but), close_pix);

       gtk_box_pack_start(GTK_BOX (box), lab, TRUE, TRUE, 0);
       gtk_box_pack_start(GTK_BOX (box), but, FALSE, FALSE, 0);
       gtk_widget_show_all(box);
       gtk_container_add(GTK_CONTAINER(ev), box);

       g_signal_connect(G_OBJECT (but), "clicked", 
                        G_CALLBACK(mailbox_tab_close_cb), mbnode);

       gtk_tooltips_set_tip(balsa_app.tooltips, 
			    ev,
			    mbnode->mailbox->url,
			    mbnode->mailbox->url);       
       return ev;
}

struct bw_open_mbnode_info {
    BalsaMailboxNode * mbnode;
    BalsaWindow *window;
};

static void
real_open_mbnode(struct bw_open_mbnode_info * info)
{
    BalsaIndex * index;
    GtkWidget *label;
    GtkWidget *scroll;
    gint page_num;
    gboolean failurep;
    GError *err = NULL;

#ifdef BALSA_USE_THREADS
    static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&open_lock);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#endif
    /* FIXME: the check is not needed in non-MT-mode */
    gdk_threads_enter();
    if (!info->window || is_open_mailbox(info->mbnode->mailbox)) {
	gdk_threads_leave();
#ifdef BALSA_USE_THREADS
        pthread_mutex_unlock(&open_lock);
#endif
	g_object_unref(info->mbnode);
	g_free(info);
        return;
    }

    index = BALSA_INDEX(balsa_index_new());
    index->window = GTK_WIDGET(info->window);

    balsa_window_increase_activity(info->window);
    failurep = balsa_index_load_mailbox_node(BALSA_INDEX (index),
                                             info->mbnode, &err);
    if (info->window) {
	balsa_window_decrease_activity(info->window);
	g_object_remove_weak_pointer(G_OBJECT(info->window),
				     (gpointer) &info->window);
    }

    if (!info->window || failurep) {
        libbalsa_information(
            LIBBALSA_INFORMATION_ERROR,
            _("Unable to Open Mailbox!\n%s."), 
	    err ? err->message : _("Unknown error"));
	g_clear_error(&err);
        gtk_object_destroy(GTK_OBJECT(index));
        gdk_threads_leave();
#ifdef BALSA_USE_THREADS
        pthread_mutex_unlock(&open_lock);
#endif
	g_object_unref(info->mbnode);
	g_free(info);
        return;
    }
    g_assert(index->mailbox_node);
    g_signal_connect(G_OBJECT (index), "index-changed",
                     G_CALLBACK (balsa_window_index_changed_cb),
                     info->window);

    /* if(config_short_label) label = gtk_label_new(mbnode->mailbox->name);
       else */
    label = balsa_notebook_label_new(info->mbnode);

    /* for updating date when settings change */
    index->line_length = balsa_app.line_length;

    /* store for easy access */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(index));
    gtk_widget_show(scroll);
    gtk_notebook_append_page(GTK_NOTEBOOK(info->window->notebook),
                             scroll, label);

    /* change the page to the newly selected notebook item */
    page_num = gtk_notebook_page_num(GTK_NOTEBOOK
                                     (info->window->notebook),
                                     scroll);
    gtk_notebook_set_current_page(GTK_NOTEBOOK
                                  (info->window->notebook),
                                  page_num);
    /* Enable relavent menu items... */
    register_open_mailbox(info->mbnode->mailbox);
    /* scroll may select the message and GtkTreeView does not like selecting
     * without being shown first. */
    balsa_index_scroll_on_open(index);
    gdk_flush();
    gdk_threads_leave();    
#ifdef BALSA_USE_THREADS
    pthread_mutex_unlock(&open_lock);
#endif
    g_object_unref(info->mbnode);
    g_free(info);
}

static void
balsa_window_real_open_mbnode(BalsaWindow * window, BalsaMailboxNode * mbnode)
{
    struct bw_open_mbnode_info *info;
#ifdef BALSA_USE_THREADS
    pthread_t open_thread;

#endif
    info = g_new(struct bw_open_mbnode_info, 1);
    info->window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer) &info->window);
    info->mbnode = mbnode;
    g_object_ref(mbnode);
#ifdef BALSA_USE_THREADS
    pthread_create(&open_thread, NULL, (void*(*)(void*))real_open_mbnode, 
                   info);
    pthread_detach(open_thread);
#else
    real_open_mbnode(info);
#endif
}

/* balsa_window_real_close_mbnode:
   this function overloads libbalsa_mailbox_close_mailbox.

*/
static gboolean
bw_focus_idle(LibBalsaMailbox ** mailbox)
{
    gdk_threads_enter();
    if (*mailbox)
	g_object_remove_weak_pointer(G_OBJECT(*mailbox), (gpointer) mailbox);
    balsa_mblist_focus_mailbox(balsa_app.mblist, *mailbox);
    g_free(mailbox);
    gdk_threads_leave();
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
        GtkWidget *page =
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);

        gtk_notebook_remove_page(GTK_NOTEBOOK(window->notebook), i);
        unregister_open_mailbox(mbnode->mailbox);

        /* If this is the last notebook page clear the message preview
           and the status bar */
        page =
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), 0);

        if (page == NULL) {
            gtk_window_set_title(GTK_WINDOW(window), "Balsa");
            balsa_window_idle_replace(window, NULL);
            gnome_appbar_set_default(balsa_app.appbar, "Mailbox closed");

            /* Disable menus */
            balsa_window_enable_mailbox_menus(window, NULL);
            enable_message_menus(window, NULL);
            enable_edit_menus(NULL);
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

static gboolean
balsa_close_commit_mailbox_on_timer(GtkWidget * widget, gpointer * data)
{
    GTimeVal current_time;
    GtkWidget *page;
    int i, c, time;

    if (!balsa_app.notebook)
        return FALSE;
    if (! (balsa_app.close_mailbox_auto || balsa_app.commit_mailbox_auto) )
        return TRUE;

    gdk_threads_enter();
    g_get_current_time(&current_time);

    c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

    for (i = 0;
         (page =
          gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
         i++) {
	BalsaIndex *index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));
        time = current_time.tv_sec - index->last_use.tv_sec;
        if (balsa_app.commit_mailbox_auto &&
            time < 31+balsa_app.commit_mailbox_timeout &&
            /* only do this once */
            time > balsa_app.commit_mailbox_timeout &&
	    !BALSA_INDEX(index)->mailbox_node->mailbox->readonly) {
            if (balsa_app.debug)
                fprintf(stderr, "Commiting %s, time: %d\n",
                        BALSA_INDEX(index)->mailbox_node->mailbox->url ,
                        time);
            libbalsa_mailbox_sync_storage
                (BALSA_INDEX(index)->mailbox_node->mailbox, FALSE);
        }
	if (i == c)
            continue;
        if (balsa_app.close_mailbox_auto &&
	    time > balsa_app.close_mailbox_timeout) {
            if (balsa_app.debug)
                fprintf(stderr, "Closing Page %d, time: %d\n", i, time);
            unregister_open_mailbox(index->mailbox_node->mailbox);
            gtk_notebook_remove_page(GTK_NOTEBOOK(balsa_app.notebook), i);
            if (i < c)
                c--;
            i--;
        }
    }
    gdk_threads_leave();
    return TRUE;
}

static void
balsa_window_destroy(GtkObject * object)
{
    BalsaWindow *window;

    window = BALSA_WINDOW(object);

    balsa_window_idle_remove(window);
    if (window->current_message) {
	g_object_remove_weak_pointer(G_OBJECT(window->current_message),
				     (gpointer) &window->current_message);
	window->current_message = NULL;
    }

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
        (*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}


/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow * window)
{
    GtkWidget *index;
    GtkWidget *paned;

    g_return_if_fail(window);

    index = balsa_window_find_current_index(window);
    if (index) {
        /* update the date column, only in the current page */
        balsa_index_refresh_date(BALSA_INDEX(index));
        /* update the size column, only in the current page */
        balsa_index_refresh_size(BALSA_INDEX(index));

    }
    paned = gtk_widget_get_ancestor(balsa_app.notebook, GTK_TYPE_VPANED);
    g_assert(paned != NULL);
    if (balsa_app.previewpane) {
        LibBalsaMessage *message = window->current_message;
        window->current_message = NULL;
        balsa_window_idle_replace(window, message);
	gtk_paned_set_position(GTK_PANED(paned), balsa_app.notebook_height);
    } else {
	/* Set the height to something really big (those new hi-res
	   screens and all :) */
	gtk_paned_set_position(GTK_PANED(paned), G_MAXINT);
    }
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
    libbalsa_mailbox_set_open(m, TRUE);
}
static void unregister_open_mailbox(LibBalsaMailbox *m)
{
    LOCK_OPEN_LIST;
    balsa_app.open_mailbox_list =
        g_list_remove(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
    libbalsa_mailbox_set_open(m, FALSE);
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
    static GtkWidget *about = NULL;
    const gchar *authors[] = {
        "Balsa Maintainers <balsa-maintainer@theochem.kth.se>:",
        "Peter Bloomfield <PeterBloomfield@bellsouth.net>",
	"Bart Visscher <magick@linux-fan.com>",
        "Emmanuel Allaud <e.allaud@wanadoo.fr>",
        "Carlos Morgado <chbm@chbm.nu>",
        "Pawel Salek <pawsa@theochem.kth.se>",
        "and many others (see AUTHORS file)",
        NULL
    };
    const gchar *documenters[] = {
        NULL
    };

    const gchar *translator_credits = _("translator_credits");
    /* FIXME: do we need error handling for this? */
    GdkPixbuf *balsa_logo = 
        gdk_pixbuf_new_from_file(BALSA_DATA_PREFIX
                                 "/pixmaps/balsa_logo.png", NULL);


    /* only show one about box at a time */
    if (about) {
        gdk_window_raise(about->window);
        return;
    }

    about = gnome_about_new("Balsa",
                            BALSA_VERSION,
                            "Copyright \xc2\xa9 1997-2003 The Balsa Developers",
                            _("The Balsa email client is part of "
                              "the GNOME desktop environment.  "
                              "Information on Balsa can be found at "
                              "http://balsa.gnome.org/\n\n"
                              "If you need to report bugs, "
                              "please do so at: "
                              "http://bugzilla.gnome.org/"),
                            authors,
                            documenters,
                            strcmp(translator_credits, "translator_credits") != 0 ? translator_credits : NULL,
                            balsa_logo
                            );

    g_object_add_weak_pointer(G_OBJECT(about), (gpointer) &about);

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
        libbalsa_mailbox_pop3_set_inbox(mailbox, balsa_app.inbox);
        libbalsa_mailbox_check(mailbox);
        list = g_list_next(list);
    }
}

/*Callback to check a mailbox in a balsa-mblist */
static gboolean
mailbox_check_func(GtkTreeModel * model, GtkTreePath * path,
		   GtkTreeIter * iter)
{
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    g_return_val_if_fail(mbnode, FALSE);

    if (mbnode->mailbox) {	/* mailbox, not a folder */
	if (!LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox) ||
	    imap_check_test(mbnode->dir ? mbnode->dir :
			    libbalsa_mailbox_imap_get_path
			    (LIBBALSA_MAILBOX_IMAP(mbnode->mailbox)))) {
	    libbalsa_mailbox_check(mbnode->mailbox);
	}
    }
    g_object_unref(mbnode);

    return FALSE;
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
static void
progress_dialog_response_cb(GtkWidget* dialog, gint response)
{
    if(response == GTK_RESPONSE_CLOSE)
        /* this should never be done in response handler, but... */
        gtk_widget_destroy(dialog);
}

/* ensure_check_mail_dialog:
   make sure that mail checking dialog exists.
*/
static void
ensure_check_mail_dialog(BalsaWindow * window)
{
    if (progress_dialog && GTK_IS_WIDGET(progress_dialog))
	gtk_widget_destroy(GTK_WIDGET(progress_dialog));
    
    progress_dialog =
	gtk_dialog_new_with_buttons(_("Checking Mail..."),
                                    GTK_WINDOW(window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    _("_Hide"), GTK_RESPONSE_CLOSE,
                                    NULL);
    gtk_window_set_wmclass(GTK_WINDOW(progress_dialog), 
			   "progress_dialog", "Balsa");
        
    g_signal_connect(G_OBJECT(progress_dialog), "destroy",
		     G_CALLBACK(progress_dialog_destroy_cb), NULL);
    g_signal_connect(G_OBJECT(progress_dialog), "response",
		     G_CALLBACK(progress_dialog_response_cb), NULL);
    
    progress_dialog_source = gtk_label_new("Checking Mail....");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_source, FALSE, FALSE, 0);
    
    progress_dialog_message = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_message, FALSE, FALSE, 0);
    
    progress_dialog_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_bar, FALSE, FALSE, 0);
    gtk_widget_show_all(progress_dialog);
}
#endif

/*
 * Callbacks
 */

/* check_new_messages_cb:
   check new messages the data argument is the BalsaWindow pointer
   or NULL.
*/
void
check_new_messages_real(GtkWidget *widget, gpointer data, int type)
{
#ifdef BALSA_USE_THREADS
    BalsaWindow * window = data;

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
	ensure_check_mail_dialog(window);

    /* initiate threads */
    pthread_create(&get_mail_thread,
                   NULL, (void *) &check_messages_thread, (void *)0);
    
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(get_mail_thread);
#else

    /* NOT USED: libbalsa_notify_start_check(imap_check_test); */
    check_mailbox_list(balsa_app.inbox_input);

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) mailbox_check_func,
			   NULL);
#endif
}

/* send_receive_messages_cb:
   check messages first to satisfy those that use smtp-after-pop.
*/
static void
send_receive_messages_cb(GtkWidget * widget, gpointer data)
{
    check_new_messages_real(widget, data, TYPE_CALLBACK);
#if ENABLE_ESMTP
    libbalsa_process_queue(balsa_app.outbox, balsa_app.smtp_server,
                           balsa_app.smtp_authctx,
                           balsa_app.smtp_tls_mode, balsa_app.debug);
#else
    libbalsa_process_queue(balsa_app.outbox, balsa_app.debug);
#endif
}

void
check_new_messages_cb(GtkWidget * widget, gpointer data)
{
    check_new_messages_real(widget, data, TYPE_CALLBACK);
}

void
check_new_messages_count(LibBalsaMailbox * mailbox)
{
    struct count_info {
        gint unread_messages;
        gint has_unread_messages;
    } *info;
    static const gchar count_info_key[] = "balsa-window-count-info";
    gint num_new, has_new;

    info = g_object_get_data(G_OBJECT(mailbox), count_info_key);
    if (!info) {
        info = g_new0(struct count_info, 1);
        g_object_set_data_full(G_OBJECT(mailbox), count_info_key, info,
                               g_free);
    }

    num_new = mailbox->unread_messages - info->unread_messages;
    if (num_new < 0)
        num_new = 0;
    has_new = mailbox->has_unread_messages - info->has_unread_messages;
    if (has_new < 0)
        has_new = 0;

    if (num_new || has_new)
        display_new_mail_notification(num_new, has_new);

    info->unread_messages = mailbox->unread_messages;
    info->has_unread_messages = mailbox->has_unread_messages;
}

/* send_outbox_messages_cb:
   tries again to send the messages queued in outbox.
*/

static void
send_outbox_messages_cb(GtkWidget * widget, gpointer data)
{
#if ENABLE_ESMTP
    libbalsa_process_queue(balsa_app.outbox, balsa_app.smtp_server,
                           balsa_app.smtp_authctx,
                           balsa_app.smtp_tls_mode, balsa_app.debug);
#else
    libbalsa_process_queue(balsa_app.outbox, balsa_app.debug);
#endif
}

/* Callback for `Print current message' item on the `File' menu, 
 * and the toolbar button. */
static void
message_print_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;
    LibBalsaMessage *msg;

    g_return_if_fail(data);

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    if (!index)
        return;

    msg = BALSA_INDEX(index)->current_message;
    if (msg)
        message_print(msg, GTK_WINDOW(data));
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
    
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    MSGMAILTHREAD(threadmessage, LIBBALSA_NTFY_SOURCE, NULL, "POP3", 0, 0);
    check_mailbox_list(balsa_app.inbox_input);
    MSGMAILTHREAD(threadmessage, LIBBALSA_NTFY_SOURCE, NULL,
                  "Local Mail", 0, 0);

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) mailbox_check_func,
			   NULL);

    MSGMAILTHREAD(threadmessage, LIBBALSA_NTFY_FINISHED, NULL, "Finished",
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
    ssize_t count;
    gfloat fraction;

    msgbuffer = g_malloc(MSG_BUFFER_SIZE);
    count = read(mail_thread_pipes[0], msgbuffer, MSG_BUFFER_SIZE);

    /* FIXME: imagine reading just half of the pointer. The sync is gone.. */
    if (count % sizeof(MailThreadMessage *)) {
        g_free(msgbuffer);
        return TRUE;
    }

    currentpos = (MailThreadMessage **) msgbuffer;

    if(quiet_check) {
        /* Eat messages */
        while (count) {
            threadmessage = *currentpos;
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
        case LIBBALSA_NTFY_SOURCE:
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
        case LIBBALSA_NTFY_MSGINFO:
            if (progress_dialog) {
                gtk_label_set_text(GTK_LABEL(progress_dialog_message),
                                   threadmessage->message_string);
                gtk_widget_show_all(progress_dialog);
            } else {
                gnome_appbar_set_status(balsa_app.appbar,
                                        threadmessage->message_string);
            }
            break;
        case LIBBALSA_NTFY_UPDATECONFIG:
            config_mailbox_update(threadmessage->mailbox);
            break;

        case LIBBALSA_NTFY_PROGRESS:
            fraction = (gfloat) threadmessage->num_bytes /
                (gfloat) threadmessage->tot_bytes;
            if (fraction > 1.0 || fraction < 0.0) {
                if (balsa_app.debug)
                    fprintf(stderr,
                            "progress bar fraction out of range %f\n",
                            fraction);
                fraction = 1.0;
            }
            if (progress_dialog) {
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              (progress_dialog_bar),
					      fraction);
                gtk_label_set_text(GTK_LABEL(progress_dialog_message),
                                   threadmessage->message_string);
            } else {
                gnome_appbar_set_progress_percentage(balsa_app.appbar, 
                                                     fraction);
                gnome_appbar_set_status(balsa_app.appbar,
                                        threadmessage->message_string);
            }
            break;
        case LIBBALSA_NTFY_FINISHED:

            if (balsa_app.pwindow_option == WHILERETR && progress_dialog) {
                gtk_widget_destroy(progress_dialog);
            } else if (progress_dialog) {
                gtk_label_set_text(GTK_LABEL(progress_dialog_source),
                                   _("Finished Checking."));
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              (progress_dialog_bar), 0.0);
            } else {
                gnome_appbar_refresh(balsa_app.appbar);
                gnome_appbar_set_progress_percentage(balsa_app.appbar, 0.0);
            }
            break;

        case LIBBALSA_NTFY_ERROR:
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              threadmessage->message_string);
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
    ssize_t count;
    float fraction;

    msgbuffer = malloc(2049);
    count = read(send_thread_pipes[0], msgbuffer, 2048);

    if (count < (ssize_t) sizeof(void *)) {
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
            fraction = threadmessage->of_total;

            if (fraction == 0 && send_dialog) {
                gtk_label_set_text(GTK_LABEL(send_progress_message),
                                   threadmessage->message_string);
                gtk_widget_show_all(send_dialog);
            }

            if (send_dialog)
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              (send_dialog_bar),
                                              fraction);
            else
                gnome_appbar_set_progress_percentage(balsa_app.appbar,
                                                     fraction);

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

/* display_new_mail_notification:
   num_new is the number of the recently arrived messsages.
*/
static void
display_new_mail_notification(int num_new, int has_new)
{
    static GtkWidget *dlg = NULL;
    static gint num_total = 0;
    gchar *msg = NULL;

    if (num_new <= 0 && has_new <= 0)
        return;

    if (balsa_app.notify_new_mail_sound)
        gnome_triggers_do("New mail has arrived", "email",
                          "email", "newmail", NULL);

    if (!balsa_app.notify_new_mail_dialog)
        return;

    if (dlg) {
        /* the user didn't acknowledge the last info, so we'll
         * accumulate the count */
        num_total += num_new;
        gdk_window_raise(dlg->window);
    } else {
        num_total = num_new;
        dlg = gtk_message_dialog_new(NULL, /* NOT transient for
                                            * Balsa's main window */
                                     (GtkDialogFlags) 0,
                                     GTK_MESSAGE_INFO,
                                     GTK_BUTTONS_OK, "%s", msg);
        gtk_window_set_title(GTK_WINDOW(dlg), _("Balsa: New mail"));
        gtk_window_set_wmclass(GTK_WINDOW(dlg), "new_mail_dialog",
                               "Balsa");
        gtk_window_set_type_hint(GTK_WINDOW(dlg),
                                 GDK_WINDOW_TYPE_HINT_NORMAL);
        g_signal_connect(G_OBJECT(dlg), "response",
                         G_CALLBACK(gtk_widget_destroy), NULL);
        g_object_add_weak_pointer(G_OBJECT(dlg), (gpointer) & dlg);
        gtk_widget_show_all(GTK_WIDGET(dlg));
    }

    msg = num_new > 0 ?
	g_strdup_printf(ngettext("You have received %d new message.",
				 "You have received %d new messages.",
				 num_total), num_total) :
	g_strdup(_("You have new mail."));
    gtk_label_set_text(GTK_LABEL(GTK_MESSAGE_DIALOG(dlg)->label), msg);
    g_free(msg);
}
 
#endif

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    g_return_val_if_fail(window != NULL, NULL);

    return window->current_index;
}


static void
new_message_cb(GtkWidget * widget, gpointer data)
{
    BalsaSendmsg *smwindow;

    smwindow = sendmsg_window_new(widget, NULL, SEND_NORMAL);

    g_signal_connect(G_OBJECT(smwindow->window), "destroy",
                     G_CALLBACK(send_msg_window_destroy_cb), data);
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
        balsa_mblist_open_mailbox(balsa_app.draftbox);
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

/* Edit menu callbacks. */
static void
copy_cb(GtkWidget * widget, BalsaWindow * bw)
{
    guint signal_id;
    GtkWidget *focus_widget = gtk_window_get_focus(GTK_WINDOW(bw));

    signal_id = g_signal_lookup("copy-clipboard",
                                G_TYPE_FROM_INSTANCE(focus_widget));
    if (signal_id)
        g_signal_emit(focus_widget, signal_id, (GQuark) 0);
#ifdef HAVE_GTKHTML
    else if (libbalsa_html_can_select(focus_widget))
	libbalsa_html_copy(focus_widget);
#endif /* HAVE_GTKHTML */
}

static void
select_all_cb(GtkWidget * widget, gpointer data)
{
    libbalsa_window_select_all(data);
}

/* Message menu callbacks. */
static void
message_copy_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);

    if (bw->preview
        && balsa_message_grab_focus(BALSA_MESSAGE(bw->preview)))
        copy_cb(widget, data);
}

static void
message_select_all_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);

    if (bw->preview
        && balsa_message_grab_focus(BALSA_MESSAGE(bw->preview)))
	libbalsa_window_select_all(data);
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
    GtkWidget *bindex;
    GList *messages, *list;
    bw = BALSA_WINDOW(data);

    bindex = balsa_window_find_current_index(bw);
    g_return_if_fail(bindex);
    messages = balsa_index_selected_list(BALSA_INDEX(bindex));
    for (list = messages; list; list = list->next) {
	LibBalsaMessage *message = list->data;

	libbalsa_show_message_source(message, balsa_app.message_font,
				     &balsa_app.source_escape_specials);
    }

    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
}

static void
trash_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_message_move_to_trash(widget,
                                balsa_window_find_current_index(
                                    BALSA_WINDOW(data)));
}

static void
toggle_deleted_message_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaMessageFlag f = LIBBALSA_MESSAGE_FLAG_DELETED; 
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         f);
}


static void
toggle_flagged_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

static void
toggle_new_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_NEW);
}

static void
toggle_answered_message_cb(GtkWidget * widget, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_REPLIED);
}

static void
store_address_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));
    GList *messages;

    g_assert(index != NULL);

    messages = balsa_index_selected_list(BALSA_INDEX(index));
    balsa_store_address(messages);
    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
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

    bw = BALSA_WINDOW(data);
    reset_show_all_headers(bw);
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
        return;

    balsa_app.shown_headers = HEADERS_NONE;

    if (bw->preview)
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            HEADERS_NONE);
}

static void
show_selected_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;

    bw = BALSA_WINDOW(data);
    reset_show_all_headers(bw);
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
        return;

    balsa_app.shown_headers = HEADERS_SELECTED;

    if (bw->preview)
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            HEADERS_SELECTED);
}

static void
show_all_headers_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    
    bw = BALSA_WINDOW(data);
    reset_show_all_headers(bw);
    
    if(widget && !GTK_CHECK_MENU_ITEM(widget)->active)
        return;
    
    balsa_app.shown_headers = HEADERS_ALL;
    if (bw->preview)
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            HEADERS_ALL);
}

static void
threading_change_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw;
    LibBalsaMailboxThreadingType type;
    GtkWidget *index;

    if(!GTK_CHECK_MENU_ITEM(widget)->active)
        return;

    bw = BALSA_WINDOW(data);
    index = balsa_window_find_current_index(bw);
    g_return_if_fail(index != NULL);

    type = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget),
                                             GNOMEUIINFO_KEY_UIDATA));
    balsa_index_set_threading_type(BALSA_INDEX(index), type);
    balsa_window_set_threading_menu(bw, type);
}

static void
expand_all_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), TRUE);
}

static void
collapse_all_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), FALSE);
}

#ifdef HAVE_GTKHTML
static void
zoom_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *bm = BALSA_WINDOW(data)->preview;
    gint in_out =
	GPOINTER_TO_INT(g_object_get_data
			(G_OBJECT(widget), GNOMEUIINFO_KEY_UIDATA));
    balsa_message_zoom(BALSA_MESSAGE(bm), in_out);
}
#endif				/* HAVE_GTKHTML */

#if defined(ENABLE_TOUCH_UI)
static void
open_mailbox_cb(GtkWidget *widget, gpointer data)
{
    LibBalsaMailbox *mailbox;
    switch(GPOINTER_TO_INT(data)) {
    default:
    case 'I': mailbox = balsa_app.inbox;    break;
    case 'D': mailbox = balsa_app.draftbox; break;
    case 'O': mailbox = balsa_app.outbox;   break;
    case 'S': mailbox = balsa_app.sentbox;  break;
    case 'T': mailbox = balsa_app.trash;    break;
    }
    balsa_mblist_open_mailbox(mailbox);
}

#endif /* ENABLE_TOUCH_UI */

static void
address_book_cb(GtkWindow *widget, gpointer data)
{
    GtkWidget *ab;

    ab = balsa_ab_window_new(FALSE, GTK_WINDOW(data));
    gtk_widget_show(GTK_WIDGET(ab));
}

static GtkToggleButton*
add_check_button(GtkWidget* table, const gchar* label, gint x, gint y)
{
    GtkWidget* res = gtk_check_button_new_with_mnemonic(label);
    gtk_table_attach(GTK_TABLE(table),
                     res,
                     x, x+1, y, y+1,
                     GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
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
find_real(BalsaWindow * window, BalsaIndex * bindex, gboolean again)
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
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
					GTK_STOCK_HELP,   GTK_RESPONSE_HELP,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        NULL);
	GtkWidget *reverse_button, *wrap_button;
	GtkWidget *search_entry, *w, *page, *table;
	GtkWidget *frame, *box, *button;
	GtkToggleButton *matching_body, *matching_from;
        GtkToggleButton *matching_to, *matching_cc, *matching_subject;
	gint ok;
	
        vbox = GTK_DIALOG(dia)->vbox;

	page=gtk_table_new(2, 1, FALSE);
	gtk_container_set_border_width(GTK_CONTAINER(page), 6);
	w = gtk_label_new_with_mnemonic(_("_Search for:"));
	gtk_table_attach(GTK_TABLE(page),w,0, 1, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
	search_entry = gtk_entry_new();
        gtk_entry_set_max_length(GTK_ENTRY(search_entry), 30);
	gtk_table_attach(GTK_TABLE(page),search_entry,1, 2, 0, 1,
			 GTK_FILL | GTK_SHRINK | GTK_EXPAND, GTK_SHRINK, 2, 2);
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
    
	table = gtk_table_new(2, 3, TRUE);
	matching_body    = add_check_button(table, _("_Body"),    0, 0);
	matching_to      = add_check_button(table, _("_To:"),     1, 0);
	matching_from    = add_check_button(table, _("_From:"),   1, 1);
        matching_subject = add_check_button(table, _("S_ubject"), 2, 0);
	matching_cc      = add_check_button(table, _("_Cc:"),     2, 1);
	gtk_container_add(GTK_CONTAINER(frame), table);

	/* Frame with Apply and Clear buttons */
	frame = gtk_frame_new(_("Show only matching messages"));
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_ETCHED_IN);
	gtk_container_set_border_width(GTK_CONTAINER(frame), 6);
	gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 2);

	/* Button box */
	box = gtk_hbutton_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(box), 6);
	button = gtk_button_new_from_stock(GTK_STOCK_APPLY);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(bw_find_button_clicked), 
			 GINT_TO_POINTER(FIND_RESPONSE_FILTER));
	gtk_container_add(GTK_CONTAINER(box), button);
	button = gtk_button_new_from_stock(GTK_STOCK_CLEAR);
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
	box = gtk_hbox_new(FALSE, 6);
	gtk_container_add(GTK_CONTAINER(frame), box);
	w = gtk_vbox_new(TRUE, 2);
	gtk_container_set_border_width(GTK_CONTAINER(w), 6);
	reverse_button = 
            gtk_check_button_new_with_mnemonic(_("_Reverse search"));
	gtk_box_pack_start_defaults(GTK_BOX(w), reverse_button);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse_button),
                                     reverse);
	wrap_button = 
            gtk_check_button_new_with_mnemonic(_("_Wrap around"));
	gtk_box_pack_start_defaults(GTK_BOX(w), wrap_button);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wrap_button),
                                     wrap);
	gtk_box_pack_start(GTK_BOX(box), w, TRUE, TRUE, 0);

	/* Button box */
	w = gtk_hbutton_box_new();
	gtk_container_set_border_width(GTK_CONTAINER(w), 6);
	button = gtk_button_new_from_stock(GTK_STOCK_OK);
	g_signal_connect(G_OBJECT(button), "clicked",
			 G_CALLBACK(bw_find_button_clicked), 
			 GINT_TO_POINTER(GTK_RESPONSE_OK));
	gtk_container_add(GTK_CONTAINER(w), button);
	gtk_box_pack_start(GTK_BOX(box), w, TRUE, TRUE, 0);

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
		reverse = GTK_TOGGLE_BUTTON(reverse_button)->active;
		wrap    = GTK_TOGGLE_BUTTON(wrap_button)->active;
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
		gnome_help_display("balsa", "win-search", &err);
		if (err) {
		    balsa_information(LIBBALSA_INFORMATION_WARNING,
				      _("Error displaying help: %s\n"),
				      err->message);
		    g_error_free(err);
		}
		break;
            case FIND_RESPONSE_RESET:
		reset_filter_cb(NULL, window);
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

	libbalsa_mailbox_search_iter_free(search_iter);
	search_iter = NULL;

        if(ok == FIND_RESPONSE_FILTER) {
            LibBalsaMailbox *mailbox = 
                BALSA_INDEX(bindex)->mailbox_node->mailbox;
            LibBalsaCondition *filter;
            filter = balsa_window_get_view_filter(window);
            /* steal cnd */
            if(filter)
                filter = libbalsa_condition_new_bool_ptr
                    (FALSE, CONDITION_AND, cnd, filter);
            else 
                filter = cnd;
            libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE);
            cnd = NULL;
            return;
        }
    }

    if (!search_iter)
	search_iter = libbalsa_mailbox_search_iter_new(cnd);
    balsa_index_find(bindex, search_iter, reverse, wrap);
}

static void
find_cb(GtkWidget * widget,gpointer data)
{
    BalsaWindow *window = data;
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(window)))
	find_real(window, BALSA_INDEX(bindex),FALSE);
}

static void
find_again_cb(GtkWidget * widget,gpointer data)
{
    BalsaWindow *window = data;
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(window)))
	find_real(window, BALSA_INDEX(bindex), TRUE);
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

static void
filter_run_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
        filters_run_dialog(BALSA_INDEX(index)->mailbox_node->mailbox);
    else
	/* FIXME : Perhaps should we be able to apply filters on folders (ie recurse on all mailboxes in it),
	   but there are problems of infinite recursion (when one mailbox being filtered is also the destination
	   of the filter action (eg a copy)). So let's see that later :) */
	balsa_information(LIBBALSA_INFORMATION_WARNING, 
                          _("You can apply filters only on mailbox\n"));
}

static void
remove_duplicates_cb(GtkWidget * widget, gpointer data)
{                          
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
        balsa_index_remove_duplicates(BALSA_INDEX(index));
}

static void
empty_trash_cb(GtkWidget * widget, gpointer data)
{
    empty_trash(BALSA_WINDOW(data));
}

/* closes the mailbox on the notebook's active page */
static void
mailbox_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
        balsa_mblist_close_mailbox(BALSA_INDEX(index)->mailbox_node->
                                   mailbox);
}

static void
mailbox_tab_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget * window = gtk_widget_get_toplevel(widget);
    balsa_window_real_close_mbnode(BALSA_WINDOW(window),
				   BALSA_MAILBOX_NODE(data));
}


LibBalsaCondition*
balsa_window_get_view_filter(BalsaWindow *window)
{
    static struct {
        LibBalsaMessageFlag flag;
        short setby;
        unsigned state:1;
    } match_flags[] = {
        { LIBBALSA_MESSAGE_FLAG_DELETED, -1, 0 },
        { LIBBALSA_MESSAGE_FLAG_NEW,     -1, 0 },
        { LIBBALSA_MESSAGE_FLAG_FLAGGED, -1, 0 },
        { LIBBALSA_MESSAGE_FLAG_REPLIED, -1, 0 }
    };
    unsigned i, j;
    LibBalsaCondition *filter;
    
    for(i=0; i<ELEMENTS(match_flags); i++)
        match_flags[i].setby = -1;

    for(i=0; i<ELEMENTS(mailbox_hide_menu); i++) {
        LibBalsaMessageFlag flag;
        gboolean set;
        int states_index =
            GPOINTER_TO_INT(mailbox_hide_menu[i].user_data);
        if(mailbox_hide_menu[i].type != GNOME_APP_UI_TOGGLEITEM)
            continue;
        if(!GTK_CHECK_MENU_ITEM(mailbox_hide_menu[i].widget)->active)
            continue;
        flag = hide_states[states_index].flag;
        set  = hide_states[states_index].set;
        for(j=0; j<ELEMENTS(match_flags); j++)
            if(match_flags[j].flag == flag) {
                match_flags[j].setby = i;
                match_flags[j].state = set;
                break;
            }
    }
    
    /* match_flags contains collected information, time to create a
     * LibBalsaCondition data structure.
     */
    filter = NULL;
    for(j=0; j<ELEMENTS(match_flags); j++) {
        LibBalsaCondition *lbc;
        if(match_flags[j].setby < 0) continue;
        lbc = libbalsa_condition_new_flag_enum(match_flags[j].state,
                                               match_flags[j].flag);
        if(filter)
            filter = 
                libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                                filter, lbc);
        else
            filter = lbc;
    }
    return filter;
}

/**balsa_window_filter_to_int() returns an integer representing the
   view filter.
*/
static int
balsa_window_filter_to_int(void)
{
    unsigned i;
    int res = 0;
    for(i=0; i<ELEMENTS(mailbox_hide_menu); i++) {
        int states_index =
            GPOINTER_TO_INT(mailbox_hide_menu[i].user_data);
        if(mailbox_hide_menu[i].type != GNOME_APP_UI_TOGGLEITEM)
            continue;
        if(!GTK_CHECK_MENU_ITEM(mailbox_hide_menu[i].widget)->active)
            continue;
        res |= 1<<states_index;
    }
    return res;
}

static void
hide_changed_cb(GtkWidget * widget, gpointer data)
{
    LibBalsaMailbox *mailbox;
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaCondition *filter;
    
    /* PART 1: assure menu consistency */
    if(GTK_CHECK_MENU_ITEM(widget)->active) {
        /* we may need to deactivate coupled negated flag. */
        unsigned curr_idx, i;
        for(i=0;
            i<ELEMENTS(mailbox_hide_menu) &&
                mailbox_hide_menu[i].widget != widget;
            i++)
            ;
        g_assert(i<ELEMENTS(mailbox_hide_menu));
        curr_idx = GPOINTER_TO_INT(mailbox_hide_menu[i].user_data);

        for(i=0; i<ELEMENTS(mailbox_hide_menu); i++) {
            int states_idx =
                GPOINTER_TO_INT(mailbox_hide_menu[i].user_data);
            if(mailbox_hide_menu[i].type != GNOME_APP_UI_TOGGLEITEM)
                continue;
            if(!GTK_CHECK_MENU_ITEM(mailbox_hide_menu[i].widget)->active)
                continue;
            if(hide_states[states_idx].flag == hide_states[curr_idx].flag
               && hide_states[states_idx].set != hide_states[curr_idx].set) {
                gtk_check_menu_item_set_active
                    (GTK_CHECK_MENU_ITEM(mailbox_hide_menu[i].widget), FALSE);
                return; /* triggered menu change will do the job */
            }
        }
    }

    if(!index)
        return;

    /* PART 2: do the job. */
    mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    /* Store the new filter mask in the mailbox view before we set the
     * view filter; rethreading triggers balsa_window_set_filter_menu,
     * which retrieves the mask from the mailbox view, and we want it to
     * be the new one. */
    libbalsa_mailbox_set_filter(mailbox, balsa_window_filter_to_int());

    filter = balsa_window_get_view_filter(bw);
    /* libbalsa_mailbox_set_view_filter() will take the ownership of
     * filter.  We need also to rethread to take into account that
     * some messages might have been removed or added to the view.  We
     * just steal old view filter for the time being to avoid copying
     * it - but we could just as well clone it. */
    libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE);
}

#ifdef DEBUG
static void
show_name_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaMailbox *mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    LibBalsaCondition *filter, *name;
    time_t t1 = time(NULL)-3*30*24*3600;
    filter = balsa_window_get_view_filter(bw);
    /* (((not body "dreß" and (subject "rfc" or body "rfc") and flagged)
     * or (from "helgaker" and not subject "huddinge") and 
     * sent in last three months.
     * This probably can be moved to a special "debug" menu. */
    name = libbalsa_condition_new_bool_ptr
        (FALSE, CONDITION_AND,
         libbalsa_condition_new_bool_ptr
        (FALSE, CONDITION_OR,
         libbalsa_condition_new_bool_ptr
         (FALSE, CONDITION_AND,
          libbalsa_condition_new_bool_ptr
          (TRUE, CONDITION_AND,
           libbalsa_condition_new_string
           (TRUE, CONDITION_MATCH_FROM, g_strdup("Dreß"),NULL),
           libbalsa_condition_new_string
           (FALSE, CONDITION_MATCH_SUBJECT|CONDITION_MATCH_BODY,
            g_strdup("rfc"),NULL)),
          libbalsa_condition_new_flag_enum
          (FALSE, LIBBALSA_MESSAGE_FLAG_FLAGGED)),
         libbalsa_condition_new_bool_ptr
         (FALSE, CONDITION_AND,
          libbalsa_condition_new_string
          (FALSE, CONDITION_MATCH_FROM, g_strdup("helgaker"),NULL),
          libbalsa_condition_new_string
          (TRUE, CONDITION_MATCH_SUBJECT, g_strdup("huddinge"),NULL))),
         libbalsa_condition_new_date(FALSE, &t1, NULL));

    if(filter)
        filter = libbalsa_condition_new_bool_ptr
            (FALSE, CONDITION_AND, name, filter);
    else 
        filter = name;
    libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE);
}

static void
show_patch_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaMailbox *mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    LibBalsaCondition *filter, *name;
    filter = balsa_window_get_view_filter(bw);
    /* (((not body "dreß" and (subject "rfc" or body "rfc") and flagged)
     * or (from "helgaker" and not subject "huddinge") and 
     * sent in last three months.
     * This probably can be moved to a special "debug" menu. */
    name = libbalsa_condition_new_string
        (FALSE, CONDITION_MATCH_SUBJECT, g_strdup("PATCH"),NULL);

    if(filter)
        filter = libbalsa_condition_new_bool_ptr
            (FALSE, CONDITION_AND, name, filter);
    else 
        filter = name;
    libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE);
}
#endif /* def DEBUG */
static void
reset_filter_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaMailbox *mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    LibBalsaCondition *filter;
    filter = balsa_window_get_view_filter(bw);
    libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE);
}

static void
mailbox_commit_changes(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    balsa_index_expunge(BALSA_INDEX(index));
}

static void
mailbox_commit_each(GtkWidget * page, gpointer data) 
{
    BalsaIndex *index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));
    balsa_index_expunge(index);
}

static void
mailbox_commit_all(GtkWidget * widget, gpointer data)
{
    gtk_container_foreach(GTK_CONTAINER(BALSA_WINDOW(data)->notebook),
			  mailbox_commit_each, NULL);
}

/* empty_trash:
   empty the trash mailbox.
*/
void
empty_trash(BalsaWindow * window)
{
    guint msgno;
    GList *msg_list = NULL;

    g_return_if_fail(LIBBALSA_IS_MAILBOX_LOCAL(balsa_app.trash));
    if (!libbalsa_mailbox_open(balsa_app.trash, NULL))
	return;

    for (msgno = libbalsa_mailbox_total_messages(balsa_app.trash);
	 msgno > 0; msgno--)
	msg_list =
	    g_list_prepend(msg_list,
			   libbalsa_mailbox_get_message(balsa_app.trash,
							msgno));
    libbalsa_messages_change_flag(msg_list, LIBBALSA_MESSAGE_FLAG_DELETED,
				  TRUE);
    g_list_foreach(msg_list, (GFunc)g_object_unref, NULL);
    g_list_free(msg_list);
    libbalsa_mailbox_close(balsa_app.trash);
    balsa_mblist_update_mailbox(balsa_app.mblist_tree_store,
				balsa_app.trash);
    enable_empty_trash(window, TRASH_EMPTY);
}

static void
show_mbtree_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *parent;
    parent = gtk_widget_get_ancestor(bw->mblist, GTK_TYPE_HPANED);
    g_assert(parent != NULL);

    balsa_app.show_mblist = GTK_CHECK_MENU_ITEM(widget)->active;
    if (balsa_app.show_mblist) {
        gtk_widget_show(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), balsa_app.mblist_width);
    } else {
        gtk_widget_hide(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), 0);
    }
}

#if !defined(ENABLE_TOUCH_UI)
static void
show_mbtabs_cb(GtkWidget * widget, gpointer data)
{
    balsa_app.show_notebook_tabs = GTK_CHECK_MENU_ITEM(widget)->active;
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(balsa_app.notebook),
                               balsa_app.show_notebook_tabs);
}
#endif

void
balsa_change_window_layout(BalsaWindow *window)
{

    gtk_widget_ref(window->notebook);
    gtk_widget_ref(window->mblist);
    gtk_widget_ref(window->preview);
 
    gtk_container_remove(GTK_CONTAINER(window->notebook->parent), window->notebook);
    gtk_container_remove(GTK_CONTAINER(window->mblist->parent),
			 window->mblist);
    gtk_container_remove(GTK_CONTAINER(window->preview->parent),
			 window->preview);

    bw_set_panes(window);

    gtk_widget_unref(window->notebook);
    gtk_widget_unref(window->mblist);
    gtk_widget_unref(window->preview);
 
    gtk_paned_set_position(GTK_PANED(window->hpaned), 
                           balsa_app.show_mblist 
                           ? balsa_app.mblist_width
                           : 0);
    gtk_widget_show(window->vpaned);
    gtk_widget_show(window->hpaned);

}

/* PKGW: remember when they change the position of the vpaned. */
static void
notebook_size_alloc_cb(GtkWidget * notebook, GtkAllocation * alloc)
{
    if (balsa_app.previewpane) {
	GtkWidget *paned = gtk_widget_get_ancestor(notebook, GTK_TYPE_PANED);
        balsa_app.notebook_height = gtk_paned_get_position(GTK_PANED(paned));
    }
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
                        GtkNotebookPage * notebookpage, guint page_num)
{
    GtkWidget *page;
    BalsaIndex *index;
    BalsaWindow *window;
    LibBalsaMailbox *mailbox;
    gchar *title;

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page_num);
    index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));

    mailbox = index->mailbox_node->mailbox;
    window = BALSA_WINDOW(index->window);
    if (window->current_index)
	g_object_remove_weak_pointer(G_OBJECT(window->current_index),
				     (gpointer) &window->current_index);
    window->current_index = GTK_WIDGET(index);
    g_object_add_weak_pointer(G_OBJECT(window->current_index),
			      (gpointer) &window->current_index);

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

    g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, index);
    balsa_window_idle_replace(window, index->current_message);
    enable_message_menus(window, index->current_message);
    balsa_window_enable_mailbox_menus(window, index);

    balsa_mblist_focus_mailbox(balsa_app.mblist, mailbox);
    balsa_mblist_set_status_bar(mailbox);

    balsa_index_refresh_date(index);
    balsa_index_refresh_size(index);
}

static void
balsa_window_index_changed_cb(GtkWidget * widget, gpointer data)
{
    BalsaWindow *window = data;
    BalsaIndex *index;

    if (widget != window->current_index)
        return;

    index = BALSA_INDEX(widget);
    balsa_window_enable_mailbox_menus(window, index);
    enable_message_menus(window, index->current_message);
    if(index->current_message == NULL) {
        enable_edit_menus(NULL);
    }

    balsa_window_idle_replace(window, index->current_message);
}

#define BALSA_SET_MESSAGE_ID "balsa-set-message-id"
static void
balsa_window_idle_replace(BalsaWindow * window, LibBalsaMessage * message)
{
    if (!message || window->current_message != message) {
	if (window->current_message)
	    g_object_remove_weak_pointer(G_OBJECT(window->current_message),
					 (gpointer) &window->current_message);
        window->current_message = message;
	if (message)
	    g_object_add_weak_pointer(G_OBJECT(message),
				      (gpointer) &window->current_message);
        if (balsa_app.previewpane) {
            guint set_message_id;

            balsa_window_idle_remove(window);
            set_message_id =
                g_idle_add((GSourceFunc) balsa_window_idle_cb, window);
            g_object_set_data(G_OBJECT(window), BALSA_SET_MESSAGE_ID,
                              GUINT_TO_POINTER(set_message_id));
        }
    }
}

static void
balsa_window_idle_remove(BalsaWindow * window)
{
    guint set_message_id =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                           BALSA_SET_MESSAGE_ID));

    if (set_message_id) {
        g_source_remove(set_message_id);
        g_object_set_data(G_OBJECT(window), BALSA_SET_MESSAGE_ID, 
                          GUINT_TO_POINTER(0));
    }
}


static volatile gboolean balsa_window_idle_cb_active = FALSE;

static gboolean
balsa_window_idle_cb(BalsaWindow * window)
{
    guint set_message_id =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),
                                           BALSA_SET_MESSAGE_ID));
    BalsaIndex *index;

    if (set_message_id == 0)
        return FALSE;
    if (balsa_window_idle_cb_active)
	return TRUE;
    balsa_window_idle_cb_active = TRUE;

    g_object_set_data(G_OBJECT(window), BALSA_SET_MESSAGE_ID,
                      GUINT_TO_POINTER(0));

    gdk_threads_enter();

    if (!balsa_message_set(BALSA_MESSAGE(window->preview),
                           window->current_message))
        balsa_information(LIBBALSA_INFORMATION_ERROR,
                          _("Cannot access the message's body\n"));

    index = g_object_get_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS);
    if (index) {
        gtk_widget_grab_focus(GTK_WIDGET(index));
        g_object_set_data(G_OBJECT(window), BALSA_INDEX_GRAB_FOCUS, NULL);
    }

    gdk_threads_leave();
    balsa_window_idle_cb_active = FALSE;

    return FALSE;
}

static void
select_part_cb(BalsaMessage * bm, gpointer data)
{
    enable_edit_menus(bm);
#ifdef HAVE_GTKHTML
    enable_view_menus(bm);
#endif				/* HAVE_GTKHTML */
}

static void
send_msg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue(BALSA_WINDOW(data));
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
                return BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));
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
    LibBalsaMessage **message;


    index = balsa_window_notebook_find_page (GTK_NOTEBOOK (widget), x, y);
    
    if (index == NULL)
        return;
    
    mailbox = index->mailbox_node->mailbox;

    for (message = (LibBalsaMessage **) selection_data->data; *message;
         message++)
        messages = g_list_append (messages, *message);
    g_return_if_fail(messages != NULL);
        
    orig_mailbox = ((LibBalsaMessage*) messages->data)->mailbox;
    
    if (mailbox != NULL && mailbox != orig_mailbox)
        balsa_index_transfer(balsa_find_index_by_mailbox(orig_mailbox),
                             messages, mailbox,
                             context->action != GDK_ACTION_MOVE);

    g_list_free (messages);
}

static gboolean
notebook_drag_motion_cb(GtkWidget * widget, GdkDragContext * context,
                        gint x, gint y, guint time, gpointer user_data)
{
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
static gint
balsa_window_progress_timeout(gpointer user_data) 
{
    gdk_threads_enter();
    gtk_progress_bar_pulse(GTK_PROGRESS_BAR(user_data));
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
    GtkProgressBar *progress_bar;

    progress_bar =
        GTK_PROGRESS_BAR(gnome_appbar_get_progress
                         (GNOME_APPBAR(GNOME_APP(window)->statusbar)));
    in_use = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(progress_bar), 
                                               "in_use"));
    
    if (!in_use) {
        g_object_set_data(G_OBJECT(progress_bar), "in_use", 
                          GINT_TO_POINTER(BALSA_PROGRESS_ACTIVITY));

        /* add a timeout to make the activity bar move */
        activity_handler = g_timeout_add(100, balsa_window_progress_timeout,
                                         progress_bar);
        g_object_set_data(G_OBJECT(progress_bar), "activity_handler", 
                          GINT_TO_POINTER(activity_handler));
    } else if (in_use != BALSA_PROGRESS_ACTIVITY) {
        /* the progress bar is already in use doing something else, so
         * quit */
        return;
    }
    
    /* increment the reference counter */
    activity_counter =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(progress_bar),
                                           "activity_counter"));
    ++activity_counter;
    g_object_set_data(G_OBJECT(progress_bar), "activity_counter", 
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
    GtkProgressBar *progress_bar;
    
    progress_bar =
        GTK_PROGRESS_BAR(gnome_appbar_get_progress
                         (GNOME_APPBAR(GNOME_APP(window)->statusbar)));
    in_use = GPOINTER_TO_INT(g_object_get_data
                             (G_OBJECT(progress_bar), "in_use"));

    /* make sure the progress bar is being used for activity */
    if (in_use != BALSA_PROGRESS_ACTIVITY)
        return;

    activity_counter =
        GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(progress_bar),
                                           "activity_counter"));
    
    /* decrement the counter if it exists */
    if (activity_counter) {
        --activity_counter;
        
        /* if the reference count is now zero, clear the bar and make
         * it available for others to use */
        if (!activity_counter) {
            activity_handler =
                GPOINTER_TO_INT(g_object_get_data
                                (G_OBJECT(progress_bar),
                                 "activity_handler"));
            g_source_remove(activity_handler);
            activity_handler = 0;
            
            g_object_set_data(G_OBJECT(progress_bar), "activity_handler",
                              GINT_TO_POINTER(activity_handler));
            g_object_set_data(G_OBJECT(progress_bar), "in_use",
                              GINT_TO_POINTER(BALSA_PROGRESS_NONE));
            gtk_progress_bar_set_fraction(progress_bar, 0);
        }
        /* make sure to store the counter value */
        g_object_set_data(G_OBJECT(progress_bar), "activity_counter",
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
    GtkProgressBar *progress_bar;

    progress_bar =
        GTK_PROGRESS_BAR(gnome_appbar_get_progress
                         (GNOME_APPBAR(GNOME_APP(window)->statusbar)));
    in_use = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(progress_bar),
                                               "in_use"));

    /* make sure the progress bar is currently unused */
    if (in_use != BALSA_PROGRESS_NONE) 
        return FALSE;
    
    in_use = BALSA_PROGRESS_INCREMENT;
    g_object_set_data(G_OBJECT(progress_bar), "in_use",
                      GINT_TO_POINTER(in_use));
    
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
    GtkProgressBar *progress_bar;

    progress_bar =
        GTK_PROGRESS_BAR(gnome_appbar_get_progress
                         (GNOME_APPBAR(GNOME_APP(window)->statusbar)));
    in_use =
        GPOINTER_TO_INT(g_object_get_data(G_OBJECT(progress_bar), "in_use"));

    /* make sure we're using it before it is cleared */
    if (in_use != BALSA_PROGRESS_INCREMENT)
        return;

    gtk_progress_bar_set_fraction(progress_bar, 0);

    in_use = BALSA_PROGRESS_NONE;
    g_object_set_data(G_OBJECT(progress_bar), "in_use",
                      GINT_TO_POINTER(in_use));
}


#ifndef BALSA_USE_THREADS
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
balsa_window_increment_progress(BalsaWindow* window)
{
    gint in_use;
    GtkProgressBar *progress_bar;
    
    progress_bar =
        GTK_PROGRESS_BAR(gnome_appbar_get_progress
                         (GNOME_APPBAR(GNOME_APP(window)->statusbar)));
    in_use = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(progress_bar),
                                               "in_use"));

    /* make sure the progress bar is being incremented */
    if (in_use != BALSA_PROGRESS_INCREMENT)
        return;

    gtk_progress_bar_pulse(progress_bar);
}
#endif

static void
ident_manage_dialog_cb(GtkWidget * widget, gpointer user_data)
{
    libbalsa_identity_config_dialog(GTK_WINDOW(user_data),
                                    &balsa_app.identities,
                                    &balsa_app.current_ident);
}


static void
mark_all_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index != NULL);

    gtk_widget_grab_focus(index);
    libbalsa_window_select_all(data);
}

static void
show_all_headers_tool_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *toolbar = balsa_toolbar_get_from_gnome_app(GNOME_APP(data));
    BalsaWindow *bw;

    bw = BALSA_WINDOW(data);
    if (balsa_toolbar_get_button_active(toolbar,
                                        BALSA_PIXMAP_SHOW_HEADERS)) {
        balsa_app.show_all_headers = TRUE;
        if (bw->preview)
            balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                                HEADERS_ALL);
    } else {
        balsa_app.show_all_headers = FALSE;
        if (bw->preview)
            balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                                balsa_app.shown_headers);
    }
}

void
reset_show_all_headers(BalsaWindow * window)
{
    GtkWidget *toolbar =
        balsa_toolbar_get_from_gnome_app(GNOME_APP(window));

    balsa_app.show_all_headers = FALSE;
    balsa_toolbar_set_button_active(toolbar, BALSA_PIXMAP_SHOW_HEADERS,
                                    FALSE);
}

static void
show_preview_pane_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget *toolbar = balsa_toolbar_get_from_gnome_app(GNOME_APP(data));

    balsa_app.previewpane =
        balsa_toolbar_get_button_active(toolbar, BALSA_PIXMAP_SHOW_PREVIEW);
    balsa_window_refresh(BALSA_WINDOW(data));
}

/* browse_wrap can also be changed in the preferences window
 *
 * update_view_menu is called to synchronize the view menu check item
 * */
void
update_view_menu(BalsaWindow * window)
{
    GtkWidget *w = view_menu[MENU_VIEW_WRAP_POS].widget;
    g_signal_handlers_block_by_func(G_OBJECT(w), 
                                    G_CALLBACK(wrap_message_cb), window);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w),
                                   balsa_app.browse_wrap);
    g_signal_handlers_unblock_by_func(G_OBJECT(w), 
                                      G_CALLBACK(wrap_message_cb),
                                      window);
    if (window->preview)
        balsa_message_set_wrap(BALSA_MESSAGE(window->preview),
                               balsa_app.browse_wrap);
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
				   balsa_notebook_label_new(mbnode));
    }
}
