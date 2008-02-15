/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2007 Stuart Parmenter and others,
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
 * BalsaWindow: subclass of GtkWindow
 *
 * The only known instance of BalsaWindow is balsa_app.main_window,
 * but the code in this module does not depend on that fact, to make it
 * more self-contained.  pb
 */

#include "config.h"

#include <string.h>
#include <gnome.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#endif

#include "libbalsa.h"
#include "misc.h"
#include "html.h"
#include <glib/gi18n.h>

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

enum {
    OPEN_MAILBOX_NODE,
    CLOSE_MAILBOX_NODE,
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

#ifdef BALSA_USE_THREADS
/* Define thread-related globals, including dialogs */
GtkWidget *progress_dialog = NULL;
GtkWidget *progress_dialog_source = NULL;
GtkWidget *progress_dialog_message = NULL;
GtkWidget *progress_dialog_bar = NULL;
GSList *list = NULL;
static int quiet_check=0;

static void bw_check_messages_thread(GSList * list);

#endif
static void bw_display_new_mail_notification(int num_new, int has_new);

static void balsa_window_class_init(BalsaWindowClass * klass);
static void balsa_window_init(BalsaWindow * window);
static void balsa_window_real_open_mbnode(BalsaWindow *window,
                                          BalsaMailboxNode *mbnode);
static void balsa_window_real_close_mbnode(BalsaWindow *window,
					   BalsaMailboxNode *mbnode);
static void balsa_window_destroy(GtkObject * object);

static gboolean bw_close_mailbox_on_timer(void);

static void bw_index_changed_cb(GtkWidget * widget, gpointer data);
static void bw_idle_replace(BalsaWindow * window, BalsaIndex * bindex);
static void bw_idle_remove(BalsaWindow * window);
static gboolean bw_idle_cb(BalsaWindow * window);


static void bw_check_mailbox_list(GList * list);
static gboolean bw_mailbox_check_func(GtkTreeModel * model,
                                      GtkTreePath * path,
                                      GtkTreeIter * iter,
                                      GSList ** list);
static gboolean bw_imap_check_test(const gchar * path);

static void bw_enable_mailbox_menus(BalsaWindow * window, BalsaIndex * index);
static void bw_enable_message_menus(BalsaWindow * window, guint msgno);
static void bw_enable_edit_menus(BalsaWindow * window, BalsaMessage * bm);
#ifdef HAVE_GTKHTML
static void bw_enable_view_menus(BalsaWindow * window, BalsaMessage * bm);
#endif				/* HAVE_GTKHTML */
static void bw_register_open_mailbox(LibBalsaMailbox *m);
static void bw_unregister_open_mailbox(LibBalsaMailbox *m);
static gboolean bw_is_open_mailbox(LibBalsaMailbox *m);

/* dialogs */
static void bw_show_about_box(BalsaWindow * window);

/* callbacks */
static void bw_send_outbox_messages_cb    (GtkAction *, gpointer data);
static void bw_send_receive_messages_cb   (GtkAction *, gpointer data);
#ifdef HAVE_GTK_PRINT
static void bw_page_setup_cb              (GtkAction * action, gpointer data);
#endif
static void bw_message_print_cb           (GtkAction * action, gpointer data);

static void bw_new_message_cb             (GtkAction * action, gpointer data);
static void bw_replyto_message_cb         (GtkAction * action, gpointer data);
static void bw_replytoall_message_cb      (GtkAction * action, gpointer data);
static void bw_replytogroup_message_cb    (GtkAction * action, gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void bw_forward_message_attached_cb(GtkAction * action, gpointer data);
static void bw_forward_message_inline_cb  (GtkAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */
static void bw_forward_message_default_cb (GtkAction * action, gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void bw_pipe_message_cb            (GtkAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */
static void bw_continue_message_cb        (GtkAction * action, gpointer data);

static void bw_next_message_cb            (GtkAction * action, gpointer data);
static void bw_next_unread_message_cb     (GtkAction * action, gpointer data);
static void bw_next_flagged_message_cb    (GtkAction * action, gpointer data);
static void bw_previous_message_cb        (GtkAction * action, gpointer data);

#if !defined(ENABLE_TOUCH_UI)
static void bw_next_part_cb               (GtkAction * action, gpointer data);
static void bw_previous_part_cb           (GtkAction * action, gpointer data);
#endif
static void bw_save_current_part_cb       (GtkAction * action, gpointer data);
static void bw_view_msg_source_cb         (GtkAction * action, gpointer data);

static void bw_trash_message_cb           (GtkAction * action, gpointer data);
static void bw_toggle_flagged_message_cb  (GtkAction * action, gpointer data);
static void bw_toggle_deleted_message_cb  (GtkAction * action, gpointer data);
static void bw_toggle_new_message_cb      (GtkAction * action, gpointer data);
static void bw_toggle_answered_message_cb (GtkAction * action, gpointer data);
static void bw_store_address_cb           (GtkAction * action, gpointer data);
static void bw_empty_trash_cb             (GtkAction * action, gpointer data);

static void bw_header_activate_cb         (GtkAction * action, gpointer data);
static void bw_expand_all_cb              (GtkAction * action, gpointer data);
static void bw_collapse_all_cb            (GtkAction * action, gpointer data);
#ifdef HAVE_GTKHTML
static void bw_zoom_in_cb                 (GtkAction * action, gpointer data);
static void bw_zoom_out_cb                (GtkAction * action, gpointer data);
static void bw_zoom_100_cb                (GtkAction * action, gpointer data);
#endif				/* HAVE_GTKHTML */

static void bw_copy_cb                    (GtkAction * action, gpointer data);
static void bw_select_all_cb              (GtkAction * action, gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void bw_message_copy_cb            (GtkAction * action, gpointer data);
static void bw_message_select_all_cb      (GtkAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */
static void bw_mark_all_cb                (GtkAction * action, gpointer data);

static void bw_find_cb                    (GtkAction * action, gpointer data);
static void bw_find_again_cb              (GtkAction * action, gpointer data);
static void bw_filter_dlg_cb              (GtkAction * action, gpointer data);
static void bw_filter_export_cb           (GtkAction * action, gpointer data);
static void bw_filter_run_cb              (GtkAction * action, gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void bw_remove_duplicates_cb       (GtkAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */

static void bw_mailbox_close_cb           (GtkAction * action, gpointer data);

static void bw_reset_filter_cb            (GtkAction * action, gpointer data);
static void bw_mailbox_expunge_cb         (GtkAction * action, gpointer data);
static void bw_mailbox_tab_close_cb(GtkWidget * widget, gpointer data);

#if defined(ENABLE_TOUCH_UI)
static void bw_sort_change_cb(GtkRadioAction * action,
                              GtkRadioAction * current, gpointer data);
static void bw_toggle_order_cb(GtkToggleAction * action, gpointer data);
static void bw_set_sort_menu(BalsaWindow *window,
                             LibBalsaMailboxSortFields col,
                             LibBalsaMailboxSortType   order);
#endif /* ENABLE_TOUCH_UI */
static void bw_hide_changed_cb         (GtkToggleAction * action, gpointer data);
static void bw_wrap_message_cb         (GtkToggleAction * action, gpointer data);
static void bw_show_all_headers_tool_cb(GtkToggleAction * action, gpointer data);
static void bw_show_preview_pane_cb    (GtkToggleAction * action, gpointer data);
static void bw_reset_show_all_headers(BalsaWindow * window);

#if !defined(ENABLE_TOUCH_UI)
static void bw_threading_activate_cb(GtkAction * action, gpointer data);
static void bw_set_threading_menu(BalsaWindow * window, int option);
static void bw_show_mbtree(BalsaWindow * window);
#endif /* ENABLE_TOUCH_UI */
static void bw_set_filter_menu(BalsaWindow * window, int gui_filter);
static LibBalsaCondition *bw_get_view_filter(BalsaWindow * window,
                                             gboolean flags_only);
#if defined(ENABLE_TOUCH_UI)
static gboolean bw_open_mailbox_cb(GtkWidget *w, GdkEventKey *e, gpointer data);
static void bw_enable_view_filter_cb(GtkToggleAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */

static void bw_address_book_cb(GtkWindow *widget, gpointer data);

static void bw_select_part_cb(BalsaMessage * bm, gpointer data);

static void bw_find_real(BalsaWindow * window, BalsaIndex * bindex,
                         gboolean again);

#if !defined(ENABLE_TOUCH_UI)
static void bw_show_mbtree_cb(GtkToggleAction * action, gpointer data);
static void bw_show_mbtabs_cb(GtkToggleAction * action, gpointer data);
#endif /* ENABLE_TOUCH_UI */

static void bw_notebook_size_allocate_cb(GtkWidget * notebook,
                                         GtkAllocation * alloc,
                                         BalsaWindow * bw);
static void bw_size_allocate_cb(GtkWidget * window, GtkAllocation * alloc);

static void bw_notebook_switch_page_cb(GtkWidget * notebook,
                                       GtkNotebookPage * page,
                                       guint page_num,
                                       gpointer data);
#if !defined(ENABLE_TOUCH_UI)
static void bw_send_msg_window_destroy_cb(GtkWidget * widget, gpointer data);
#endif /*ENABLE_TOUCH_UI */
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
static void bw_ident_manage_dialog_cb(GtkAction * action, gpointer user_data);

static void bw_contents_cb(void);

#ifdef HAVE_NOTIFY
static void bw_cancel_new_mail_notification(GObject *gobject, GParamSpec *arg1,
					    gpointer user_data);
#endif

static void
bw_quit_nicely(GtkAction * action, gpointer data)
{
    GdkEventAny e = { GDK_DELETE, NULL, 0 };
    e.window = GTK_WIDGET(data)->window;
    libbalsa_information(LIBBALSA_INFORMATION_MESSAGE,
                         _("Balsa closes files and connections. Please wait..."));
    while(gtk_events_pending())
        gtk_main_iteration_do(FALSE);
    gdk_event_put((GdkEvent*)&e);
}

/* ===================================================================
   Balsa menus. Touchpad has some simplified menus which do not
   overlap very much with the default balsa menus. They are here
   because they represent an alternative probably appealing to the all
   proponents of GNOME2 dumbify approach (OK, I am bit unfair here).
*/

/* Actions that are always sensitive: */
static const GtkActionEntry entries[] = {
    /* Menus */
    {"FileMenu", NULL, N_("_File")},
    {"EditMenu", NULL, N_("_Edit")},
    {"ViewMenu", NULL, N_("_View")},
    {"MailboxMenu", NULL, N_("Mail_box")},
    {"MessageMenu", NULL, N_("_Message")},
    {"SettingsMenu", NULL, N_("_Settings")},
    {"HelpMenu", NULL, N_("_Help")},
#if !defined(ENABLE_TOUCH_UI)
    {"FileNewMenu", NULL, N_("_New")},
#else  /* ENABLE_TOUCH_UI */
    {"MailboxesMenu", NULL, N_("Mail_boxes")},
    {"ViewMoreMenu", NULL, N_("_More")},
    {"ShownHeadersMenu", NULL, N_("_Headers")},
    {"SortMenu", NULL, N_("_Sort Mailbox")},
    {"HideMessagesMenu", NULL, N_("H_ide messages")},
    {"MessageMoreMenu", NULL, N_("_More")},
    {"ToolsMenu", NULL, N_("_Tools")},
    {"ToolsFiltersMenu", NULL, N_("_Filters")},
    {"ManageFilters", GTK_STOCK_PROPERTIES, N_("F_ilters"), NULL,
     N_("Manage filters"), G_CALLBACK(bw_filter_dlg_cb)},
#endif /* ENABLE_TOUCH_UI */
    /* File menu items */
    /* Not in the touchpad menu, but still available as a toolbar
     * button: */
    {"Continue", BALSA_PIXMAP_CONTINUE, N_("_Continue"), "C",
     N_("Continue editing current message"),
     G_CALLBACK(bw_continue_message_cb)},
    {"GetNewMail", BALSA_PIXMAP_RECEIVE, N_("_Get New Mail"), "<control>M",
     N_("Fetch new incoming mail"), G_CALLBACK(check_new_messages_cb)},
    {"SendQueuedMail", BALSA_PIXMAP_SEND, N_("_Send Queued Mail"),
     "<control>T", N_("Send messages from the outbox"),
     G_CALLBACK(bw_send_outbox_messages_cb)},
    {"SendAndReceiveMail", BALSA_PIXMAP_SEND_RECEIVE,
     N_("Send and _Receive Mail"), "<control>B",
     N_("Send and Receive messages"),
     G_CALLBACK(bw_send_receive_messages_cb)},
#ifdef HAVE_GTK_PRINT
    {"PageSetup", NULL, N_("Page _Setup"), NULL,
     N_("Set up page for printing"), G_CALLBACK(bw_page_setup_cb)},
#endif                          /* HAVE_GTK_PRINT */
    {"AddressBook", BALSA_PIXMAP_BOOK_RED, N_("_Address Book..."), "B",
     N_("Open the address book"), G_CALLBACK(bw_address_book_cb)},
    {"Quit", GTK_STOCK_QUIT, N_("_Quit"), "<control>Q", N_("Quit Balsa"),
     G_CALLBACK(bw_quit_nicely)},
    /* File:New submenu items */
    {"NewMessage", BALSA_PIXMAP_COMPOSE, N_("_Message..."), "M",
     N_("Compose a new message"), G_CALLBACK(bw_new_message_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"NewMbox", GTK_STOCK_ADD, N_("Local mbox mailbox..."), NULL,
     N_("Add a new mbox style mailbox"),
     G_CALLBACK(mailbox_conf_add_mbox_cb)},
    {"NewMaildir", GTK_STOCK_ADD, N_("Local Maildir mailbox..."), NULL,
     N_("Add a new Maildir style mailbox"),
     G_CALLBACK(mailbox_conf_add_maildir_cb)},
    {"NewMH", GTK_STOCK_ADD, N_("Local MH mailbox..."), NULL,
     N_("Add a new MH style mailbox"), G_CALLBACK(mailbox_conf_add_mh_cb)},
#else  /* ENABLE_TOUCH_UI */
    {"NewMbox", GTK_STOCK_ADD, N_("New mailbox..."), NULL,
     N_("Add a new mbox style mailbox"),
     G_CALLBACK(mailbox_conf_add_mbox_cb)},
    {"NewMaildir", GTK_STOCK_ADD, N_("New \"Maildir\" mailbox..."), NULL,
     N_("Add a new Maildir style mailbox"),
     G_CALLBACK(bw_mailbox_conf_add_maildir_cb)},
    {"NewMH", GTK_STOCK_ADD, N_("New \"MH\" mailbox..."), NULL,
     N_("Add a new MH style mailbox"), G_CALLBACK(mailbox_conf_add_mh_cb)},
#endif /* ENABLE_TOUCH_UI */
    {"NewIMAPBox", GTK_STOCK_ADD, N_("Remote IMAP mailbox..."), NULL,
     N_("Add a new IMAP mailbox"), G_CALLBACK(mailbox_conf_add_imap_cb)},
    {"NewIMAPFolder", GTK_STOCK_ADD, N_("Remote IMAP folder..."), NULL,
     N_("Add a new IMAP folder"), G_CALLBACK(folder_conf_add_imap_cb)},
    {"NewIMAPSubfolder", GTK_STOCK_ADD, N_("Remote IMAP subfolder..."),
     NULL, N_("Add a new IMAP subfolder"),
     G_CALLBACK(folder_conf_add_imap_sub_cb)},
    /* Edit menu items */
    {"Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C", NULL,
     G_CALLBACK(bw_copy_cb)},
    {"Filters", GTK_STOCK_PROPERTIES, N_("F_ilters..."), NULL,
     N_("Manage filters"), G_CALLBACK(bw_filter_dlg_cb)},
    {"ExportFilters", GTK_STOCK_PROPERTIES, N_("_Export Filters"), NULL,
     N_("Export filters as Sieve scripts"), G_CALLBACK(bw_filter_export_cb)},
    {"Preferences", GTK_STOCK_PREFERENCES, N_("Prefere_nces"), NULL, NULL,
     G_CALLBACK(open_preferences_manager)},
    /* View menu items */
    {"ExpandAll", NULL, N_("E_xpand All"), "<control>E",
     N_("Expand all threads"), G_CALLBACK(bw_expand_all_cb)},
    {"CollapseAll", NULL, N_("_Collapse All"), "<control>L",
     N_("Collapse all expanded threads"), G_CALLBACK(bw_collapse_all_cb)},
#ifdef HAVE_GTKHTML
    {"ZoomIn", GTK_STOCK_ZOOM_IN, N_("Zoom _In"), "<control>plus",
     N_("Increase magnification"), G_CALLBACK(bw_zoom_in_cb)},
    {"ZoomOut", GTK_STOCK_ZOOM_OUT, N_("Zoom _Out"), "<control>minus",
     N_("Decrease magnification"), G_CALLBACK(bw_zoom_out_cb)},
    /* To warn msgfmt that the % sign isn't a format specifier: */
    /* xgettext:no-c-format */
    {"Zoom100", GTK_STOCK_ZOOM_100, N_("Zoom _100%"), NULL,
     N_("No magnification"), G_CALLBACK(bw_zoom_100_cb)},
#endif                          /* HAVE_GTKHTML */
    /* Mailbox menu item that does not require a mailbox */
    {"NextUnread", BALSA_PIXMAP_NEXT_UNREAD, N_("Next Unread Message"),
     "<control>N", N_("Next Unread Message"),
     G_CALLBACK(bw_next_unread_message_cb)},
    {"EmptyTrash", GTK_STOCK_REMOVE, N_("Empty _Trash"), NULL,
     N_("Delete messages from the Trash mailbox"),
     G_CALLBACK(bw_empty_trash_cb)},
    /* Settings menu items */
    {"Toolbars", GTK_STOCK_EXECUTE, N_("_Toolbars..."), NULL,
     N_("Customize toolbars"), G_CALLBACK(customize_dialog_cb)},
    {"Identities", BALSA_PIXMAP_IDENTITY, N_("_Identities..."), NULL,
     N_("Create and set current identities"),
     G_CALLBACK(bw_ident_manage_dialog_cb)},
    /* Help menu items */
    {"TableOfContents", GTK_STOCK_HELP, N_("_Contents"), "F1",
     N_("Table of Contents"), G_CALLBACK(bw_contents_cb)},
    {"About", GTK_STOCK_ABOUT, N_("_About"), NULL, N_("About Balsa"),
     G_CALLBACK(bw_show_about_box)}
};

/* Actions that are sensitive only when a mailbox is selected: */
static const GtkActionEntry mailbox_entries[] = { 
    /* Edit menu items */
    {"SelectAll", NULL, N_("Select _All"), "<control>A", NULL,
     G_CALLBACK(bw_select_all_cb)},
    {"Find", GTK_STOCK_FIND, N_("_Find"), "<control>F", NULL,
     G_CALLBACK(bw_find_cb)},
    {"FindNext", GTK_STOCK_FIND, N_("Find Ne_xt"), "<control>G", NULL,
     G_CALLBACK(bw_find_again_cb)},
    /* Mailbox menu items */
    {"NextMessage", BALSA_PIXMAP_NEXT, N_("Next Message"), "N",
     N_("Next Message"), G_CALLBACK(bw_next_message_cb)},
    {"PreviousMessage", BALSA_PIXMAP_PREVIOUS, N_("Previous Message"), "P",
     N_("Previous Message"), G_CALLBACK(bw_previous_message_cb)},
    {"NextFlagged", BALSA_PIXMAP_NEXT_FLAGGED, N_("Next Flagged Message"),
     "<control><alt>F", N_("Next Flagged Message"),
     G_CALLBACK(bw_next_flagged_message_cb)},
    {"MailboxHideMenu", NULL, N_("_Hide Messages")},
    {"ResetFilter", GTK_STOCK_CLEAR, N_("_Reset Filter"), NULL,
     N_("Reset mailbox filter"), G_CALLBACK(bw_reset_filter_cb)},
    {"MailboxSelectAll", BALSA_PIXMAP_MARK_ALL, N_("_Select All"), NULL,
     N_("Select all messages in current mailbox"),
     G_CALLBACK(bw_mark_all_cb)},
    {"MailboxEdit", GTK_STOCK_PREFERENCES, N_("_Edit..."), NULL,
     N_("Edit the selected mailbox"), G_CALLBACK(mailbox_conf_edit_cb)},
    {"MailboxDelete", GTK_STOCK_REMOVE, N_("_Delete..."), NULL,
     N_("Delete the selected mailbox"),
     G_CALLBACK(mailbox_conf_delete_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"Expunge", GTK_STOCK_REMOVE, N_("E_xpunge Deleted Messages"), NULL,
     N_("Expunge messages marked as deleted in the current mailbox"),
#else  /* ENABLE_TOUCH_UI */
    {"Expunge", GTK_STOCK_REMOVE, N_("E_xpunge Deleted Messages"), NULL,
     N_("Expunge messages marked as deleted in the current mailbox"),
#endif /* ENABLE_TOUCH_UI */
     G_CALLBACK(bw_mailbox_expunge_cb)},
    {"Close", GTK_STOCK_CANCEL, N_("_Close"), NULL, N_("Close mailbox"),
     G_CALLBACK(bw_mailbox_close_cb)},
    {"SelectFilters", GTK_STOCK_PROPERTIES, N_("Select _Filters"), NULL,
     N_("Select filters to be applied automatically to current mailbox"),
     G_CALLBACK(bw_filter_run_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"RemoveDuplicates", GTK_STOCK_REMOVE, N_("_Remove Duplicates"), NULL,
     N_("Remove duplicated messages from the current mailbox"),
     G_CALLBACK(bw_remove_duplicates_cb)}
#endif /* ENABLE_TOUCH_UI */
};

/* Actions that are sensitive only when a message is selected: */
static const GtkActionEntry message_entries[] = { 
    /* File menu item */
    {"Print", GTK_STOCK_PRINT, N_("_Print..."), "<control>P",
     N_("Print current message"), G_CALLBACK(bw_message_print_cb)},
    /* Message menu items */
    {"Reply", BALSA_PIXMAP_REPLY, N_("_Reply..."), "R",
     N_("Reply to the current message"), G_CALLBACK(bw_replyto_message_cb)},
    {"ReplyAll", BALSA_PIXMAP_REPLY_ALL, N_("Reply to _All..."), "A",
     N_("Reply to all recipients of the current message"),
     G_CALLBACK(bw_replytoall_message_cb)},
    {"ReplyGroup", BALSA_PIXMAP_REPLY_GROUP, N_("Reply to _Group..."), "G",
     N_("Reply to mailing list"), G_CALLBACK(bw_replytogroup_message_cb)},
    {"StoreAddress", BALSA_PIXMAP_BOOK_RED, N_("_Store Address..."), "S",
     N_("Store address of sender in addressbook"),
     G_CALLBACK(bw_store_address_cb)},
    {"SavePart", GTK_STOCK_SAVE, N_("Save Current Part..."), "<control>S",
     N_("Save currently displayed part of message"),
     G_CALLBACK(bw_save_current_part_cb)},
    {"ViewSource", BALSA_PIXMAP_BOOK_OPEN, N_("_View Source..."),
     "<control>U", N_("View source form of the message"),
     G_CALLBACK(bw_view_msg_source_cb)},
    /* All three "Forward" actions and the "Pipe" action have the same
     * stock_id; the first in this list defines the action tied to the
     * toolbar's Forward button, so "ForwardDefault" must come before
     * the others. */
    {"ForwardDefault", BALSA_PIXMAP_FORWARD, N_("_Forward..."), "F",
     N_("Forward the current message"),
     G_CALLBACK(bw_forward_message_default_cb)},
#if !defined(ENABLE_TOUCH_UI)
    {"ForwardAttached", BALSA_PIXMAP_FORWARD, N_("_Forward attached..."), "F",
     N_("Forward the current message as attachment"),
     G_CALLBACK(bw_forward_message_attached_cb)},
    {"ForwardInline", BALSA_PIXMAP_FORWARD, N_("Forward _inline..."), NULL,
     N_("Forward the current message inline"),
     G_CALLBACK(bw_forward_message_inline_cb)},
    {"Pipe", BALSA_PIXMAP_FORWARD, N_("_Pipe through..."), NULL,
     N_("Pipe the message through another program"),
     G_CALLBACK(bw_pipe_message_cb)},
    {"NextPart", BALSA_PIXMAP_NEXT_PART, N_("_Next Part"), "<control>period",
     N_("Next part in message"), G_CALLBACK(bw_next_part_cb)},
    {"PreviousPart", BALSA_PIXMAP_PREVIOUS_PART, N_("_Previous Part"),
     "<control>comma", N_("Previous part in message"),
     G_CALLBACK(bw_previous_part_cb)},
    {"CopyMessage", GTK_STOCK_COPY, N_("_Copy"), "<control>C",
     N_("Copy message"), G_CALLBACK(bw_message_copy_cb)},
    {"SelectText", NULL, N_("_Select Text"), NULL,
     N_("Select entire mail"), G_CALLBACK(bw_message_select_all_cb)}
#endif /* ENABLE_TOUCH_UI */
};

/* Actions that are sensitive only when a message is selected and
 * can be modified: */
static const GtkActionEntry modify_message_entries[] = { 
    /* Message menu items */
#if !defined(ENABLE_TOUCH_UI)
    {"MoveToTrash", GTK_STOCK_DELETE, N_("_Move to Trash"), "D",
     N_("Move the current message to Trash mailbox"),
     G_CALLBACK(bw_trash_message_cb)},
#else  /* ENABLE_TOUCH_UI */
    {"MoveToTrash", GTK_STOCK_DELETE, N_("_Delete to Trash"), "D",
     N_("Move the current message to Trash mailbox"),
     G_CALLBACK(bw_trash_message_cb)},
    {"ToolbarToggleNew", BALSA_PIXMAP_MARKED_NEW, N_("_New"), NULL,
     N_("Toggle New"), G_CALLBACK(bw_toggle_new_message_cb)},
#endif /* ENABLE_TOUCH_UI */
    {"MessageToggleFlagMenu", NULL, N_("_Toggle Flag")},
    /* Message:toggle-flag submenu items */
    {"ToggleFlagged", BALSA_PIXMAP_INFO_FLAGGED, N_("_Flagged"), "X",
     N_("Toggle flagged"), G_CALLBACK(bw_toggle_flagged_message_cb)},
    {"ToggleDeleted", GTK_STOCK_DELETE, N_("_Deleted"), "<control>D",
     N_("Toggle deleted flag"), G_CALLBACK(bw_toggle_deleted_message_cb)},
    {"ToggleNew", BALSA_PIXMAP_INFO_NEW, N_("_New"), "<control>R",
     N_("Toggle New"), G_CALLBACK(bw_toggle_new_message_cb)},
    {"ToggleAnswered", BALSA_PIXMAP_INFO_REPLIED, N_("_Answered"), NULL,
     N_("Toggle Answered"), G_CALLBACK(bw_toggle_answered_message_cb)},
};

/* Toggle items */
static const GtkToggleActionEntry toggle_entries[] = {
    /* View menu items */
#if !defined(ENABLE_TOUCH_UI)
    {"ShowMailboxTree", NULL, N_("_Show Mailbox Tree"), "F9",
     N_("Toggle display of mailbox and folder tree"),
     G_CALLBACK(bw_show_mbtree_cb), FALSE},
    {"ShowMailboxTabs", NULL, N_("Show Mailbox _Tabs"), NULL,
     N_("Toggle display of mailbox notebook tabs"),
     G_CALLBACK(bw_show_mbtabs_cb), FALSE},
#else  /* ENABLE_TOUCH_UI */
    {"SortDescending", NULL, N_("_Descending"), NULL,
     N_("Sort in a descending order"),
     G_CALLBACK(bw_toggle_order_cb), FALSE},
    {"ViewFilter", NULL, N_("_View filter"), NULL,
     N_("Enable quick message index filter"),
     G_CALLBACK(bw_enable_view_filter_cb), FALSE},
#endif /* ENABLE_TOUCH_UI */
    {"Wrap", NULL, N_("_Wrap"), NULL, N_("Wrap message lines"),
     G_CALLBACK(bw_wrap_message_cb), FALSE},
    /* Hide messages menu items */
    {"HideDeleted", NULL, N_("_Deleted"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideUndeleted", NULL, N_("Un_Deleted"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideRead", NULL, N_("_Read"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideUnread", NULL, N_("Un_read"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideFlagged", NULL, N_("_Flagged"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideUnflagged", NULL, N_("Un_flagged"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideAnswered", NULL, N_("_Answered"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    {"HideUnanswered", NULL, N_("Un_answered"), NULL, NULL,
     G_CALLBACK(bw_hide_changed_cb), FALSE},
    /* Toolbar items not on any menu */
    {"ShowAllHeaders", BALSA_PIXMAP_SHOW_HEADERS, N_("All\nheaders"),
     NULL, N_("Show all headers"),
     G_CALLBACK(bw_show_all_headers_tool_cb), FALSE},
    {"ShowPreviewPane", BALSA_PIXMAP_SHOW_PREVIEW, N_("Msg Preview"),
     NULL, N_("Show preview pane"), G_CALLBACK(bw_show_preview_pane_cb),
     FALSE}
};

/* Radio items */
static const GtkRadioActionEntry shown_hdrs_radio_entries[] = {
    {"NoHeaders", NULL, N_("_No Headers"), NULL,
     N_("Display no headers"), HEADERS_NONE},
    {"SelectedHeaders", NULL, N_("S_elected Headers"), NULL,
     N_("Display selected headers"), HEADERS_SELECTED},
    {"AllHeaders", NULL, N_("All _Headers"), NULL,
     N_("Display all headers"), HEADERS_ALL}
};
#if !defined(ENABLE_TOUCH_UI)
static const GtkRadioActionEntry threading_radio_entries[] = {
    {"FlatIndex", NULL, N_("_Flat index"), NULL,
     N_("No threading at all"), LB_MAILBOX_THREADING_FLAT},
    {"SimpleThreading", NULL, N_("Si_mple threading"), NULL,
     N_("Simple threading algorithm"), LB_MAILBOX_THREADING_SIMPLE},
    {"JWZThreading", NULL, N_("_JWZ threading"), NULL,
     N_("Elaborate JWZ threading"), LB_MAILBOX_THREADING_JWZ}
};
#endif /* ENABLE_TOUCH_UI */
#if defined(ENABLE_TOUCH_UI)
static const GtkRadioActionEntry sort_radio_entries[] = {
    {"ByArrival", NULL, N_("By _Arrival"), NULL,
     N_("Arrival order"), LB_MAILBOX_SORT_NO},
    {"BySender", NULL, N_("By _Sender"), NULL,
     N_("Sender order"), LB_MAILBOX_SORT_SENDER},
    {"BySubject", NULL, N_("By S_ubject"), NULL,
     N_("Subject order"), LB_MAILBOX_SORT_SUBJECT},
    {"BySize", NULL, N_("By Si_ze"), NULL,
     N_("By message size"), LB_MAILBOX_SORT_SIZE},
    {"Threaded", NULL, N_("_Threaded"), NULL,
     N_("Use message threading"), LB_MAILBOX_SORT_THREAD}
};
#endif /* ENABLE_TOUCH_UI */

static const char *ui_description =
#if !defined(ENABLE_TOUCH_UI)
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menu action='FileNewMenu'>"
"        <menuitem action='NewMessage'/>"
"        <separator/>"
"        <menuitem action='NewMbox'/>"
"        <menuitem action='NewMaildir'/>"
"        <menuitem action='NewMH'/>"
"        <menuitem action='NewIMAPBox'/>"
"        <separator/>"
"        <menuitem action='NewIMAPFolder'/>"
"        <menuitem action='NewIMAPSubfolder'/>"
"      </menu>"
"      <menuitem action='Continue'/>"
"      <separator/>"
"      <menuitem action='GetNewMail'/>"
"      <menuitem action='SendQueuedMail'/>"
"      <menuitem action='SendAndReceiveMail'/>"
"      <separator/>"
#ifdef HAVE_GTK_PRINT
"      <menuitem action='PageSetup'/>"
#endif                          /* HAVE_GTK_PRINT */
"      <menuitem action='Print'/>"
"      <separator/>"
"      <menuitem action='AddressBook'/>"
"      <separator/>"
"      <menuitem action='Quit'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Copy'/>"
"      <menuitem action='SelectAll'/>"
"      <separator/>"
"      <menuitem action='Find'/>"
"      <menuitem action='FindNext'/>"
"      <separator/>"
"      <menuitem action='Filters'/>"
"      <menuitem action='ExportFilters'/>"
"      <separator/>"
"      <menuitem action='Preferences'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='ShowMailboxTree'/>"
"      <menuitem action='ShowMailboxTabs'/>"
"      <separator/>"
"      <menuitem action='Wrap'/>"
"      <separator/>"
"      <menuitem action='NoHeaders'/>"
"      <menuitem action='SelectedHeaders'/>"
"      <menuitem action='AllHeaders'/>"
"      <separator/>"
"      <menuitem action='FlatIndex'/>"
"      <menuitem action='SimpleThreading'/>"
"      <menuitem action='JWZThreading'/>"
"      <separator/>"
"      <menuitem action='ExpandAll'/>"
"      <menuitem action='CollapseAll'/>"
#ifdef HAVE_GTKHTML
"      <separator/>"
"      <menuitem action='ZoomIn'/>"
"      <menuitem action='ZoomOut'/>"
"      <menuitem action='Zoom100'/>"
#endif				/* HAVE_GTKHTML */
"    </menu>"
"    <menu action='MailboxMenu'>"
"      <menuitem action='NextMessage'/>"
"      <menuitem action='PreviousMessage'/>"
"      <menuitem action='NextUnread'/>"
"      <menuitem action='NextFlagged'/>"
"      <separator/>"
"      <menu action='MailboxHideMenu'>"
"        <menuitem action='HideDeleted'/>"
"        <menuitem action='HideUndeleted'/>"
"        <menuitem action='HideRead'/>"
"        <menuitem action='HideUnread'/>"
"        <menuitem action='HideFlagged'/>"
"        <menuitem action='HideUnflagged'/>"
"        <menuitem action='HideAnswered'/>"
"        <menuitem action='HideUnanswered'/>"
"      </menu>"
"      <menuitem action='ResetFilter'/>"
"      <separator/>"
"      <menuitem action='MailboxSelectAll'/>"
"      <separator/>"
"      <menuitem action='MailboxEdit'/>"
"      <menuitem action='MailboxDelete'/>"
"      <separator/>"
"      <menuitem action='Expunge'/>"
"      <menuitem action='Close'/>"
"      <separator/>"
"      <menuitem action='EmptyTrash'/>"
"      <separator/>"
"      <menuitem action='SelectFilters'/>"
"      <separator/>"
"      <menuitem action='RemoveDuplicates'/>"
"    </menu>"
"    <menu action='MessageMenu'>"
"      <menuitem action='Reply'/>"
"      <menuitem action='ReplyAll'/>"
"      <menuitem action='ReplyGroup'/>"
"      <menuitem action='ForwardAttached'/>"
"      <menuitem action='ForwardInline'/>"
"      <separator/>"
"      <menuitem action='Pipe'/>"
"      <separator/>"
"      <menuitem action='NextPart'/>"
"      <menuitem action='PreviousPart'/>"
"      <menuitem action='SavePart'/>"
"      <menuitem action='ViewSource'/>"
"      <separator/>"
"      <menuitem action='CopyMessage'/>"
"      <menuitem action='SelectText'/>"
"      <separator/>"
"      <menuitem action='MoveToTrash'/>"
"      <menu action='MessageToggleFlagMenu'>"
"        <menuitem action='ToggleFlagged'/>"
"        <menuitem action='ToggleDeleted'/>"
"        <menuitem action='ToggleNew'/>"
"        <menuitem action='ToggleAnswered'/>"
"      </menu>"
"      <separator/>"
"      <menuitem action='StoreAddress'/>"
"    </menu>"
"    <menu action='SettingsMenu'>"
"      <menuitem action='Toolbars'/>"
"      <menuitem action='Identities'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='TableOfContents'/>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='Toolbar'>"
"  </toolbar>"
"</ui>";
#else  /* ENABLE_TOUCH_UI */
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menuitem action='SendAndReceiveMail'/>"
"      <menuitem action='SendQueuedMail'/>"
"      <menuitem action='GetNewMail'/>"
"      <separator/>"
"      <menu action='MailboxesMenu'>"
"        <menuitem action='NewMbox'/>"
"        <menuitem action='NewMaildir'/>"
"        <menuitem action='NewMH'/>"
"        <menuitem action='NewIMAPBox'/>"
"        <separator/>"
"        <menuitem action='NewIMAPFolder'/>"
"        <menuitem action='NewIMAPSubfolder'/>"
"        <separator/>"
"        <menuitem action='MailboxDelete'/>"
"        <menuitem action='MailboxEdit'/>"
"        <separator/>"
"      </menu>"
#ifdef HAVE_GTK_PRINT
"      <menuitem action='PageSetup'/>"
#endif                          /* HAVE_GTK_PRINT */
"      <menuitem action='Print'/>"
"      <separator/>"
"      <menuitem action='Quit'/>"
"    </menu>"
"    <menu action='EditMenu'>"
"      <menuitem action='Copy'/>"
"      <menuitem action='SelectAll'/>"
"      <separator/>"
"      <menuitem action='Find'/>"
"      <menuitem action='FindNext'/>"
"    </menu>"
"    <menu action='ViewMenu'>"
"      <menuitem action='NextUnread'/>"
"      <menuitem action='NextMessage'/>"
"      <menuitem action='PreviousMessage'/>"
#ifdef HAVE_GTKHTML
"      <separator/>"
"      <menuitem action='ZoomIn'/>"
"      <menuitem action='ZoomOut'/>"
"      <menuitem action='Zoom100'/>"
#endif				/* HAVE_GTKHTML */
"      <separator/>"
"      <menu action='ViewMoreMenu'>"
"        <menuitem action='NextFlagged'/>"
"        <separator/>"
"        <menu action='ShownHeadersMenu'>"
"          <menuitem action='NoHeaders'/>"
"          <menuitem action='SelectedHeaders'/>"
"          <menuitem action='AllHeaders'/>"
"        </menu>"
"        <menuitem action='Wrap'/>"
"        <menu action='SortMenu'>"
"          <menuitem action='SortDescending'/>"
"          <separator/>"
"          <menuitem action='ByArrival'/>"
"          <menuitem action='BySender'/>"
"          <menuitem action='BySubject'/>"
"          <menuitem action='BySize'/>"
"          <menuitem action='Threaded'/>"
"        </menu>"
"        <menu action='HideMessagesMenu'>"
"          <menuitem action='HideDeleted'/>"
"          <menuitem action='HideUndeleted'/>"
"          <menuitem action='HideRead'/>"
"          <menuitem action='HideUnread'/>"
"          <menuitem action='HideFlagged'/>"
"          <menuitem action='HideUnflagged'/>"
"          <menuitem action='HideAnswered'/>"
"          <menuitem action='HideUnanswered'/>"
"        </menu>"
"        <separator/>"
"        <menuitem action='ExpandAll'/>"
"        <menuitem action='CollapseAll'/>"
"        <separator/>"
"        <menuitem action='ViewFilter'/>"
"      </menu>"
"    </menu>"
"    <menu action='MessageMenu'>"
"      <menuitem action='NewMessage'/>"
"      <menuitem action='Reply'/>"
"      <menuitem action='ReplyAll'/>"
"      <menuitem action='ForwardDefault'/>"
"      <separator/>"
"      <menuitem action='SavePart'/>"
"      <menuitem action='MoveToTrash'/>"
"      <separator/>"
"      <menu action='MessageMoreMenu'>"
"        <menuitem action='ViewSource'/>"
"        <menu action='MessageToggleFlagMenu'>"
"          <menuitem action='ToggleFlagged'/>"
"          <menuitem action='ToggleDeleted'/>"
"          <menuitem action='ToggleNew'/>"
"          <menuitem action='ToggleAnswered'/>"
"        </menu>"
"        <menuitem action='StoreAddress'/>"
"      </menu>"
"    </menu>"
"    <menu action='ToolsMenu'>"
"      <menuitem action='AddressBook'/>"
"      <menuitem action='EmptyTrash'/>"
"      <menu action='ToolsFiltersMenu'>"
"        <menuitem action='ManageFilters'/>"
"        <menuitem action='ExportFilters'/>"
"      </menu>"
"      <menuitem action='Identities'/>"
"      <menuitem action='Preferences'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='TableOfContents'/>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"  <toolbar name='Toolbar'>"
"  </toolbar>"
"</ui>";
#endif /* ENABLE_TOUCH_UI */

G_DEFINE_TYPE (BalsaWindow, balsa_window, GTK_TYPE_WINDOW)

static guint window_signals[LAST_SIGNAL] = { 0 };

static void
balsa_window_class_init(BalsaWindowClass * klass)
{
    GtkObjectClass *object_class = (GtkObjectClass *) klass;

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
    window_signals[IDENTITIES_CHANGED] =
        g_signal_new("identities-changed",
                     G_TYPE_FROM_CLASS(object_class),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(BalsaWindowClass, identities_changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

    object_class->destroy = balsa_window_destroy;

    klass->open_mbnode  = balsa_window_real_open_mbnode;
    klass->close_mbnode = balsa_window_real_close_mbnode;

    /* Signals */
    klass->identities_changed = NULL;

    g_timeout_add(30000, (GSourceFunc) bw_close_mailbox_on_timer, NULL);

}

static void
balsa_window_init(BalsaWindow * window)
{
}

static gboolean
bw_delete_cb(GtkWidget* main_window)
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
bw_mblist_size_allocate_cb(GtkWidget * widget, GtkAllocation * alloc,
                           BalsaWindow * bw)
{
    if (balsa_app.show_mblist && !balsa_app.mw_maximized)
        balsa_app.mblist_width =
            gtk_paned_get_position(GTK_PANED(bw->hpaned));
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

    view_filter = bw_get_view_filter(bw, FALSE);
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
    if(str && sscanf(str, "%d", &days) == 1) {
        time_t upperbound = time(NULL)-(days-1)*24*3600;
        return libbalsa_condition_new_date(FALSE, NULL, &upperbound);
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
    { N_("Older than (days):"),             bw_filter_old  }
};
static gboolean view_filters_translated = FALSE;

static GtkWidget*
bw_create_index_widget(BalsaWindow *bw)
{
    GtkWidget *vbox, *button;
    GtkWidget *hbox = gtk_hbox_new(FALSE, 5);
    unsigned i;

    if(!view_filters_translated) {
        for(i=0; i<ELEMENTS(view_filters); i++)
            view_filters[i].str = _(view_filters[i].str);
        view_filters_translated = TRUE;
    }

    bw->filter_choice = gtk_combo_box_new_text();
    gtk_box_pack_start(GTK_BOX(hbox), bw->filter_choice,
                       FALSE, FALSE, 0);
    for(i=0; i<ELEMENTS(view_filters); i++)
        gtk_combo_box_insert_text(GTK_COMBO_BOX(bw->filter_choice),
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
    gtk_box_pack_start(GTK_BOX(hbox), bw->sos_entry, TRUE, TRUE, 0);
    gtk_widget_show(bw->sos_entry);
    gtk_box_pack_start(GTK_BOX(hbox),
                       button = gtk_button_new(),
                       FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GTK_STOCK_OK,
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
    vbox = gtk_vbox_new(FALSE, 0);
#if defined(ENABLE_TOUCH_UI)
    /* Usually we want to show the widget unless we operate in
     * space-constrained conditions. */
    if(balsa_app.enable_view_filter)
#endif
        gtk_widget_show(hbox);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), bw->notebook, TRUE, TRUE, 0);
    gtk_container_set_focus_chain(GTK_CONTAINER(vbox),
                                  g_list_append(NULL, bw->notebook));
    gtk_widget_set_sensitive(button, FALSE);
    gtk_widget_show(vbox);
    return vbox;
}

static void
bw_set_panes(BalsaWindow * window)
{
    GtkWidget *index_widget = bw_create_index_widget(window);
    window->vpaned = gtk_vpaned_new();
    window->hpaned = gtk_hpaned_new();
    gtk_paned_pack1(GTK_PANED(window->hpaned), bw_frame(window->mblist),
                    TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(window->vpaned), bw_frame(window->preview),
                    TRUE, TRUE);
    if (balsa_app.alternative_layout) {
        if (window->content)
            gtk_container_remove(GTK_CONTAINER(window->vbox),
                                 window->content);
        window->content = window->vpaned;
        gtk_box_pack_start(GTK_BOX(window->vbox), window->content,
                           TRUE, TRUE, 0);
        gtk_paned_pack2(GTK_PANED(window->hpaned), bw_frame(index_widget),
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), window->hpaned,
                        TRUE, TRUE);
    } else {
        if (window->content)
            gtk_container_remove(GTK_CONTAINER(window->vbox),
                                 window->content);
        window->content = window->hpaned;
        gtk_box_pack_start(GTK_BOX(window->vbox), window->content,
                           TRUE, TRUE, 0);
        gtk_paned_pack2(GTK_PANED(window->hpaned), window->vpaned,
                        TRUE, TRUE);
        gtk_paned_pack1(GTK_PANED(window->vpaned), bw_frame(index_widget),
                        TRUE, TRUE);
    }
}

/*
 * GtkAction helpers
 */

/* Find a GtkAction by name.
 */
static GtkAction *
bw_get_action(BalsaWindow * window, const gchar * action_name)
{
    GtkAction *action;

    if ((action =
         gtk_action_group_get_action(window->action_group, action_name)))
        return action;

    if ((action =
         gtk_action_group_get_action(window->mailbox_action_group,
                                     action_name)))
        return action;

    return gtk_action_group_get_action(window->message_action_group,
                                       action_name);
}

/* Set the sensitivity of a GtkAction.
 */
static void
bw_set_sensitive(BalsaWindow * window, const gchar * action_name,
                 gboolean sensitive)
{
    GtkAction *action = bw_get_action(window, action_name);
    gtk_action_set_sensitive(action, sensitive);
}

/* Set the state of a GtkToggleAction; if block == TRUE,
 * block the handling of signals emitted on the action.
 * Note: if action_name is a GtkRadioAction, and we are connected to the
 * "toggled" signal, we must block the first action in the group, since
 * that is the only action in the group that is connected to the signal;
 * as of now (2008-02-14), we do not use the "toggled" signal, as it is
 * not emitted when the user clicks on the currently active member of
 * the group; instead, we connect to the "activate" signal for all
 * members of the group, so the correct action to block is the one in
 * the call.
 */
static void
bw_set_active(BalsaWindow * window, const gchar * action_name,
              gboolean active, gboolean block)
{
    GtkAction *action = bw_get_action(window, action_name);

    if (block)
        g_signal_handlers_block_matched(action, G_SIGNAL_MATCH_DATA, 0,
                                        (GQuark) 0, NULL, NULL, window);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), active);
    if (block)
        g_signal_handlers_unblock_matched(action, G_SIGNAL_MATCH_DATA, 0,
                                          (GQuark) 0, NULL, NULL, window);
}

static gboolean
bw_get_active(BalsaWindow * window, const gchar * action_name)
{
    GtkAction *action = bw_get_action(window, action_name);
    return gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action));
}

/*
 * end of GtkAction helpers
 */

static void
bw_enable_next_unread(BalsaWindow * window, gboolean has_unread_mailbox)
{
    bw_set_sensitive(window, "NextUnread", has_unread_mailbox);
}

/* Create the toolbar model for the main window's toolbar.
 */
/* Standard buttons; "" means a separator. */
static const gchar* main_toolbar[] = {
#if defined(ENABLE_TOUCH_UI)
    BALSA_PIXMAP_RECEIVE,
    "",
    BALSA_PIXMAP_COMPOSE,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_FORWARD,
    "",
    GTK_STOCK_DELETE,
    "",
    BALSA_PIXMAP_NEXT_UNREAD,
    BALSA_PIXMAP_MARKED_NEW
#else /* defined(ENABLE_TOUCH_UI) */
    BALSA_PIXMAP_RECEIVE,
    "",
    GTK_STOCK_DELETE,
    "",
    BALSA_PIXMAP_COMPOSE,
    BALSA_PIXMAP_CONTINUE,
    BALSA_PIXMAP_REPLY,
    BALSA_PIXMAP_REPLY_ALL,
    BALSA_PIXMAP_FORWARD,
    "",
    BALSA_PIXMAP_NEXT_UNREAD,
    "",
    GTK_STOCK_PRINT
#endif /* defined(ENABLE_TOUCH_UI) */
};

static BalsaToolbarModel *
bw_get_toolbar_model(void)
{
    static BalsaToolbarModel *model = NULL;
    GSList *standard;
    guint i;

    if (model)
        return model;

    standard = NULL;
    for (i = 0; i < ELEMENTS(main_toolbar); i++)
        standard = g_slist_append(standard, g_strdup(main_toolbar[i]));

    model =
        balsa_toolbar_model_new(BALSA_TOOLBAR_TYPE_MAIN_WINDOW, standard);
    balsa_toolbar_model_add_actions(model, entries,
                                    G_N_ELEMENTS(entries));
    balsa_toolbar_model_add_actions(model, mailbox_entries,
                                    G_N_ELEMENTS(mailbox_entries));
    balsa_toolbar_model_add_actions(model, message_entries,
                                    G_N_ELEMENTS(message_entries));
    balsa_toolbar_model_add_actions(model, modify_message_entries,
                                    G_N_ELEMENTS(modify_message_entries));
    balsa_toolbar_model_add_toggle_actions(model, toggle_entries,
                                           G_N_ELEMENTS(toggle_entries));

    return model;
}

/* Create a GtkUIManager for a main window, with all the actions, but no
 * ui.
 */
static GtkUIManager *
bw_get_ui_manager(BalsaWindow * window)
{
    GtkUIManager *ui_manager;
    GtkActionGroup *action_group;

    ui_manager = gtk_ui_manager_new();

    action_group = gtk_action_group_new("BalsaWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (window)
        window->action_group = action_group;
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), window);
    gtk_action_group_add_toggle_actions(action_group, toggle_entries,
                                        G_N_ELEMENTS(toggle_entries),
                                        window);
    /* Add the header option actions.
     * Note: if we provide a callback, it's connected to the "changed"
     * signal, which is emitted only when the radio list changes state.
     * We want to respond also to a click on the current option, so we
     * connect later to the "activate" signal, and pass a NULL callback
     * here.  */
    gtk_action_group_add_radio_actions(action_group,
                                       shown_hdrs_radio_entries,
                                       G_N_ELEMENTS
                                       (shown_hdrs_radio_entries), 0,
                                       NULL, /* no callback */
                                       NULL);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    action_group = gtk_action_group_new("BalsaWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (window)
        window->mailbox_action_group = action_group;
    gtk_action_group_add_actions(action_group, mailbox_entries,
                                 G_N_ELEMENTS(mailbox_entries),
                                 window);
#if !defined(ENABLE_TOUCH_UI)
    /* Add the threading option actions.
     * Note: if we provide a callback, it's connected to the "changed"
     * signal, which is emitted only when the radio list changes state.
     * We want to respond also to a click on the current option, so we
     * connect later to the "activate" signal, and pass a NULL callback
     * here.  */
    gtk_action_group_add_radio_actions(action_group,
                                       threading_radio_entries,
                                       G_N_ELEMENTS
                                       (threading_radio_entries), 0,
                                       NULL, /* no callback */
                                       NULL);
#endif /* ENABLE_TOUCH_UI */
#if defined(ENABLE_TOUCH_UI)
    gtk_action_group_add_radio_actions(action_group,
                                       sort_radio_entries,
                                       G_N_ELEMENTS
                                       (sort_radio_entries), 0,
                                       G_CALLBACK(bw_sort_change_cb),
                                       window);
#endif /* ENABLE_TOUCH_UI */

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    action_group = gtk_action_group_new("BalsaWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (window)
        window->message_action_group = action_group;
    gtk_action_group_add_actions(action_group, message_entries,
                                 G_N_ELEMENTS(message_entries),
                                 window);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    action_group = gtk_action_group_new("BalsaWindow");
    gtk_action_group_set_translation_domain(action_group, NULL);
    if (window)
        window->modify_message_action_group = action_group;
    gtk_action_group_add_actions(action_group, modify_message_entries,
                                 G_N_ELEMENTS(modify_message_entries),
                                 window);

    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    return ui_manager;
}

static BalsaToolbarModel *
bw_get_toolbar_model_and_ui_manager(BalsaWindow * window,
                                    GtkUIManager ** ui_manager)
{
    BalsaToolbarModel *model = bw_get_toolbar_model();

    if (ui_manager)
        *ui_manager = bw_get_ui_manager(window);

    return model;
}

BalsaToolbarModel *
balsa_window_get_toolbar_model(GtkUIManager ** ui_manager)
{
    return bw_get_toolbar_model_and_ui_manager(NULL, ui_manager);
}

/*
 * "window-state-event" signal handler
 *
 * If the window is maximized, the resize grip is still sensitive but
 * does nothing, so leaving it showing could be confusing.
 */
static gboolean
bw_window_state_event_cb(BalsaWindow * window,
                         GdkEventWindowState * event,
                         GtkStatusbar * statusbar)
{
    balsa_app.mw_maximized =
        event->new_window_state & GDK_WINDOW_STATE_MAXIMIZED;

    gtk_statusbar_set_has_resize_grip(statusbar, !balsa_app.mw_maximized);

    return FALSE;
}

GtkWidget *
balsa_window_new()
{
    BalsaWindow *window;
    BalsaToolbarModel *model;
    GtkUIManager *ui_manager;
    GtkAccelGroup *accel_group;
    GError *error;
    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *hbox;
    static const gchar *const header_options[] =
        { "NoHeaders", "SelectedHeaders", "AllHeaders" };
    static const gchar *const threading_options[] =
        { "FlatIndex", "SimpleThreading", "JWZThreading" };
    guint i;

    /* Call to register custom balsa pixmaps with GNOME_STOCK_PIXMAPS
     * - allows for grey out */
    register_balsa_pixmaps();

    window = g_object_new(BALSA_TYPE_WINDOW, NULL);
    window->vbox = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(window), window->vbox);

    gtk_window_set_title(GTK_WINDOW(window), "Balsa");
    register_balsa_pixbufs(GTK_WIDGET(window));

    model = bw_get_toolbar_model_and_ui_manager(window, &ui_manager);

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
        return NULL;
    }

    menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");
    gtk_box_pack_start(GTK_BOX(window->vbox), menubar, FALSE, FALSE, 0);

    toolbar = balsa_toolbar_new(model, ui_manager);
    gtk_box_pack_start(GTK_BOX(window->vbox), toolbar, FALSE, FALSE, 0);

    /* Now that we have installed the menubar and toolbar, we no longer
     * need the UIManager. */
    g_object_unref(ui_manager);

    hbox = gtk_hbox_new(FALSE, 6);
    gtk_box_pack_end(GTK_BOX(window->vbox), hbox, FALSE, FALSE, 0);

    window->progress_bar = gtk_progress_bar_new();
    /* If we don't request a minimum size, the progress bar expands
     * vertically when we set text in it--ugly!! */
    gtk_widget_set_size_request(window->progress_bar, -1, 1);
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(window->progress_bar),
                                    0.01);
    gtk_box_pack_start(GTK_BOX(hbox), window->progress_bar, FALSE, FALSE,
                       0);

    window->statusbar = gtk_statusbar_new();
    g_signal_connect(window, "window-state-event",
                     G_CALLBACK(bw_window_state_event_cb),
                     window->statusbar);
    gtk_box_pack_start(GTK_BOX(hbox), window->statusbar, TRUE, TRUE, 0);

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
    g_signal_connect(G_OBJECT(window->notebook), "size_allocate",
                     G_CALLBACK(bw_notebook_size_allocate_cb), window);
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

    g_signal_connect(G_OBJECT(window->preview), "select-part",
                     G_CALLBACK(bw_select_part_cb), window);

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
		     G_CALLBACK(bw_mblist_size_allocate_cb), window);
    g_signal_connect_swapped(balsa_app.mblist, "has-unread-mailbox",
		             G_CALLBACK(bw_enable_next_unread), window);
    balsa_mblist_default_signal_bindings(balsa_app.mblist);
    gtk_widget_show_all(window->mblist);

    bw_set_panes(window);

    /*PKGW: do it this way, without the usizes. */
#if !defined(ENABLE_TOUCH_UI)
    bw_set_active(window, "ShowMailboxTree", balsa_app.show_mblist, FALSE);
#endif                          /* !defined(ENABLE_TOUCH_UI) */

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

    /* set the toolbar style */
    balsa_window_refresh(window);

    for (i = 0; i < G_N_ELEMENTS(header_options); i++) {
        GtkAction *action = bw_get_action(window, header_options[i]);
        if (i == balsa_app.shown_headers)
            gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), TRUE);
        g_signal_connect(action, "activate", 
                         G_CALLBACK(bw_header_activate_cb), window);
    }

#if !defined(ENABLE_TOUCH_UI)
    for (i = 0; i < G_N_ELEMENTS(threading_options); i++) {
        GtkAction *action = bw_get_action(window, threading_options[i]);
        g_signal_connect(action, "activate", 
                         G_CALLBACK(bw_threading_activate_cb), window);
    }

    bw_set_active(window, "ShowMailboxTabs", balsa_app.show_notebook_tabs,
                  TRUE);
    bw_set_active(window, "Wrap", balsa_app.browse_wrap, FALSE);
#else
    bw_set_sensitive(window, "ViewFilter", balsa_app.enable_view_filter);
    g_signal_connect_after(G_OBJECT(window), "key_press_event",
                     G_CALLBACK(bw_open_mailbox_cb), NULL);
#endif

    /* Disable menu items at start up */
    balsa_window_update_book_menus(window);
    bw_enable_mailbox_menus(window, NULL);
    bw_enable_message_menus(window, 0);
    bw_enable_edit_menus(window, NULL);
#ifdef HAVE_GTKHTML
    bw_enable_view_menus(window, NULL);
#endif				/* HAVE_GTKHTML */
#if !defined(ENABLE_TOUCH_UI)
    balsa_window_enable_continue(window);
#endif /*ENABLE_TOUCH_UI */

    /* set initial state of toggle preview pane button */
    bw_set_active(window, "ShowPreviewPane", balsa_app.previewpane, TRUE);

    /* set initial state of next-unread controls */
    bw_enable_next_unread(window, FALSE);

    g_signal_connect(G_OBJECT(window), "size_allocate",
                     G_CALLBACK(bw_size_allocate_cb), NULL);
    g_signal_connect(G_OBJECT (window), "destroy",
                     G_CALLBACK (gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(bw_delete_cb), NULL);

#ifdef HAVE_NOTIFY
    /* Cancel new-mail notification when we get the focus. */
    g_signal_connect(G_OBJECT(window), "notify::is-active",
                     G_CALLBACK(bw_cancel_new_mail_notification), NULL);
#endif

    gtk_widget_show_all(GTK_WIDGET(window));
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

    enable = mailbox &&
        libbalsa_mailbox_get_threading_type(mailbox) !=
        LB_MAILBOX_THREADING_FLAT;
    bw_set_sensitive(window, "ExpandAll", enable);
    bw_set_sensitive(window, "CollapseAll", enable);
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
    bw_set_sensitive(window, "Expunge", mailbox && !mailbox->readonly);
#if defined(ENABLE_TOUCH_UI)
    {gboolean can_sort, can_thread; guint i;
    static const gchar * const sort_actions[] = {
        "ByArrival",
        "BySender",
        "BySubject",
        "BySize"
    };

    can_sort = mailbox &&
        libbalsa_mailbox_can_do(mailbox, LIBBALSA_MAILBOX_CAN_SORT);
    can_thread = mailbox &&
        libbalsa_mailbox_can_do(mailbox, LIBBALSA_MAILBOX_CAN_THREAD);
    for (i = 0; i < G_N_ELEMENTS(sort_actions); i++)
        bw_set_sensitive(window, sort_actions[i], can_sort);
    bw_set_sensitive(window, "Threaded", can_thread);
    }
#endif

    gtk_action_group_set_sensitive(window->mailbox_action_group, enable);
    bw_set_sensitive(window, "NextMessage",
                     index && index->next_message);
    bw_set_sensitive(window, "PreviousMessage",
                     index && index->prev_message);

#if !defined(ENABLE_TOUCH_UI)
    bw_set_sensitive(window, "RemoveDuplicates", mailbox
                     && libbalsa_mailbox_can_move_duplicates(mailbox));
#endif

    if (mailbox) {
#if defined(ENABLE_TOUCH_UI)
        bw_set_sort_menu(window,
                                   libbalsa_mailbox_get_sort_field(mailbox),
                                   libbalsa_mailbox_get_sort_type(mailbox));
#else
	bw_set_threading_menu(window,
					libbalsa_mailbox_get_threading_type
					(mailbox));
#endif
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

    bw_set_sensitive(window, "AddressBook", has_books);
    bw_set_sensitive(window, "StoreAddress", has_books
                     && window->current_index
                     && BALSA_INDEX(window->current_index)->current_msgno);
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
    gtk_action_group_set_sensitive(window->message_action_group, enable);

    enable_mod = (enable && !bindex->mailbox_node->mailbox->readonly);
    gtk_action_group_set_sensitive(window->modify_message_action_group,
                                   enable_mod);

    enable_store = (enable && balsa_app.address_book_list != NULL);
    bw_set_sensitive(window, "StoreAddress", enable_store);

#if !defined(ENABLE_TOUCH_UI)
    balsa_window_enable_continue(window);
#endif /*ENABLE_TOUCH_UI */
}

/*
 * Called when the current part has changed: Enable/disable the copy
 * and select all buttons
 */
static void
bw_enable_edit_menus(BalsaWindow * window, BalsaMessage * bm)
{
#if !defined(ENABLE_TOUCH_UI)
    gboolean enable = (bm && balsa_message_can_select(bm));

    bw_set_sensitive(window, "Copy",        bm != NULL);
    bw_set_sensitive(window, "CopyMessage", enable);
    bw_set_sensitive(window, "SelectText",  enable);
#endif /* ENABLE_TOUCH_UI */
#ifdef HAVE_GTKHTML
    bw_enable_view_menus(window, bm);
#endif				/* HAVE_GTKHTML */
}

#ifdef HAVE_GTKHTML
/*
 * Enable/disable the Zoom menu items on the View menu.
 */
static void
bw_enable_view_menus(BalsaWindow * window, BalsaMessage * bm)
{
    gboolean enable = bm && balsa_message_can_zoom(bm);

    bw_set_sensitive(window, "ZoomIn",  enable);
    bw_set_sensitive(window, "ZoomOut", enable);
    bw_set_sensitive(window, "Zoom100", enable);
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
    bw_set_sensitive(window, "EmptyTrash", set);
}

/*
 * Enable/disable the continue buttons
 */
void
balsa_window_enable_continue(BalsaWindow * window)
{
#if !defined(ENABLE_TOUCH_UI)
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

        bw_set_sensitive(window, "Continue", n);

/*      libbalsa_mailbox_close(balsa_app.draftbox); */
    }
#endif /* ENABLE_TOUCH_UI */
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_enable_part_menu_items(BalsaWindow * window)
{
    BalsaMessage *msg = window ? BALSA_MESSAGE(window->preview) : NULL;

    bw_set_sensitive(window, "NextPart",
                     balsa_message_has_next_part(msg));
    bw_set_sensitive(window, "PreviousPart",
                     balsa_message_has_previous_part(msg));
}

static void
bw_set_threading_menu(BalsaWindow * window, int option)
{
    GtkWidget *index;
    BalsaMailboxNode *mbnode;
    LibBalsaMailbox *mailbox;

    switch(option) {
    case LB_MAILBOX_THREADING_FLAT:
    bw_set_active(window, "FlatIndex", TRUE, TRUE); break;
    case LB_MAILBOX_THREADING_SIMPLE:
    bw_set_active(window, "SimpleThreading", TRUE, TRUE); break;
    case LB_MAILBOX_THREADING_JWZ:
    bw_set_active(window, "JWZThreading", TRUE, TRUE); break;
    default: return;
    }

    if ((index = balsa_window_find_current_index(window))
	&& (mbnode = BALSA_INDEX(index)->mailbox_node)
	&& (mailbox = mbnode->mailbox))
	bw_enable_expand_collapse(window, mailbox);
}
#endif /* ENABLE_TOUCH_UI */

/* Really, entire mailbox_hide_menu should be build dynamically from
 * the hide_states array since different mailboxes support different
 * set of flags/keywords. */
static const struct {
    LibBalsaMessageFlag flag;
    unsigned set:1;
    gint states_index;
    const gchar *action_name;
} hide_states[] = {
    { LIBBALSA_MESSAGE_FLAG_DELETED, 1, 0, "HideDeleted"    },
    { LIBBALSA_MESSAGE_FLAG_DELETED, 0, 1, "HideUndeleted"  },
    { LIBBALSA_MESSAGE_FLAG_NEW,     0, 2, "HideRead"       },
    { LIBBALSA_MESSAGE_FLAG_NEW,     1, 3, "HideUnread"     },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 1, 4, "HideFlagged"    },
    { LIBBALSA_MESSAGE_FLAG_FLAGGED, 0, 5, "HideUnflagged"  },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 1, 6, "HideAnswered"   },
    { LIBBALSA_MESSAGE_FLAG_REPLIED, 0, 7, "HideUnanswered" }
};

static void
bw_set_filter_menu(BalsaWindow * window, int mask)
{
    unsigned i;

    for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
        gint states_index = hide_states[i].states_index;
        GtkAction *action =
            gtk_action_group_get_action(window->action_group,
                                        hide_states[i].action_name);
        g_signal_handlers_block_by_func(G_OBJECT(action),
                                        G_CALLBACK(bw_hide_changed_cb),
                                        window);
        gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),
                                     (mask >> states_index) & 1);
        g_signal_handlers_unblock_by_func(G_OBJECT(action),
                                          G_CALLBACK(bw_hide_changed_cb),
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

static void
bw_notebook_label_notify(LibBalsaMailbox * mailbox, GtkLabel * lab)
{
    g_signal_handlers_disconnect_by_func(mailbox, bw_mailbox_changed,
					 lab);
}

static GtkWidget *
bw_notebook_label_new(BalsaMailboxNode * mbnode)
{
    GtkWidget *lab;
    GtkWidget *close_pix;
    GtkWidget *box;
    GtkWidget *but;
#if !GTK_CHECK_VERSION(2, 11, 0)
    GtkWidget *ev;
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
    GtkRcStyle *rcstyle;
    GtkSettings *settings;
    gint w, h;

    box = gtk_hbox_new(FALSE, 4);

    lab = gtk_label_new(mbnode->mailbox->name);
    bw_notebook_label_style(GTK_LABEL(lab),
                            libbalsa_mailbox_get_unread(mbnode->mailbox) > 0);
    g_signal_connect(mbnode->mailbox, "changed",
                     G_CALLBACK(bw_mailbox_changed), lab);
    g_object_weak_ref(G_OBJECT(lab), (GWeakNotify) bw_notebook_label_notify,
                      mbnode->mailbox);
    gtk_box_pack_start(GTK_BOX(box), lab, TRUE, TRUE, 0);

    but = gtk_button_new();
    gtk_button_set_focus_on_click(GTK_BUTTON(but), FALSE);
    gtk_button_set_relief(GTK_BUTTON(but), GTK_RELIEF_NONE);

    rcstyle = gtk_rc_style_new();
    rcstyle->xthickness = rcstyle->ythickness = 0;
    gtk_widget_modify_style(but, rcstyle);
#if GTK_CHECK_VERSION(2, 11, 0)
    g_object_unref(rcstyle);
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_rc_style_unref(rcstyle);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */

    settings = gtk_widget_get_settings(GTK_WIDGET(lab));
    gtk_icon_size_lookup_for_settings(settings, GTK_ICON_SIZE_MENU, &w, &h);
    gtk_widget_set_size_request(but, w + 2, h + 2);

    g_signal_connect(but, "clicked",
                     G_CALLBACK(bw_mailbox_tab_close_cb), mbnode);

    close_pix = gtk_image_new_from_stock(GTK_STOCK_CLOSE,
                                         GTK_ICON_SIZE_MENU);
    gtk_container_add(GTK_CONTAINER(but), close_pix);
    gtk_box_pack_start(GTK_BOX(box), but, FALSE, FALSE, 0);

    gtk_widget_show_all(box);

#if GTK_CHECK_VERSION(2, 11, 0)
    gtk_widget_set_tooltip_text(box, mbnode->mailbox->url);
    return box;
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    ev = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(ev), FALSE);
    gtk_container_add(GTK_CONTAINER(ev), box);
    gtk_tooltips_set_tip(balsa_app.tooltips,
                         ev,
                         mbnode->mailbox->url,
                         mbnode->mailbox->url);
    return ev;
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
}

struct bw_open_mbnode_info {
    BalsaMailboxNode * mbnode;
    BalsaWindow *window;
};

static void
bw_real_open_mbnode(struct bw_open_mbnode_info * info)
{
    BalsaIndex * index;
    GtkWidget *label;
    GtkWidget *scroll;
    gint page_num;
    gboolean failurep;
    GError *err = NULL;
    gchar *message;
    LibBalsaCondition *view_filter;
    LibBalsaMailbox *mailbox;

#ifdef BALSA_USE_THREADS
    static pthread_mutex_t open_lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&open_lock);
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
#endif
    /* FIXME: the check is not needed in non-MT-mode */
    gdk_threads_enter();
    if (!info->window || bw_is_open_mailbox(mailbox = info->mbnode->mailbox)) {
	gdk_threads_leave();
#ifdef BALSA_USE_THREADS
        pthread_mutex_unlock(&open_lock);
#endif
	g_object_unref(info->mbnode);
	g_free(info);
        return;
    }

    index = BALSA_INDEX(balsa_index_new());

    message = g_strdup_printf(_("Opening %s"), mailbox->name);
    balsa_window_increase_activity(info->window, message);

    /* Call balsa_index_load_mailbox_node NOT holding the gdk lock. */
    gdk_threads_leave();
    failurep = balsa_index_load_mailbox_node(BALSA_INDEX (index),
                                             info->mbnode, &err);
    gdk_threads_enter();

    if (info->window) {
	balsa_window_decrease_activity(info->window, message);
	g_object_remove_weak_pointer(G_OBJECT(info->window),
				     (gpointer) &info->window);
    }
    g_free(message);

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
                     G_CALLBACK (bw_index_changed_cb),
                     info->window);

    /* if(config_short_label) label = gtk_label_new(mbnode->mailbox->name);
       else */
    label = bw_notebook_label_new(info->mbnode);

    /* store for easy access */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(index));
    gtk_widget_show(scroll);
    gtk_notebook_append_page(GTK_NOTEBOOK(info->window->notebook),
                             scroll, label);
#if GTK_CHECK_VERSION(2, 10, 0)
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(info->window->notebook),
                                     scroll, TRUE);
#endif                          /* GTK_CHECK_VERSION(2, 10, 0) */

    /* change the page to the newly selected notebook item */
    page_num = gtk_notebook_page_num(GTK_NOTEBOOK
                                     (info->window->notebook),
                                     scroll);
    gtk_notebook_set_current_page(GTK_NOTEBOOK
                                  (info->window->notebook),
                                  page_num);

    /* bw_switch_page_cb has now set the hide-states for this mailbox, so
     * we can set up the view-filter: */
    view_filter = bw_get_view_filter(info->window, TRUE);
    libbalsa_mailbox_set_view_filter(mailbox, view_filter,
                                     FALSE);
    libbalsa_condition_unref(view_filter);
    libbalsa_mailbox_make_view_filter_persistent(mailbox);

    bw_register_open_mailbox(mailbox);
    /* scroll may select the message and GtkTreeView does not like selecting
     * without being shown first. */
    libbalsa_mailbox_set_threading(mailbox,
                                   libbalsa_mailbox_get_threading_type
                                   (mailbox));
    balsa_index_scroll_on_open(index);
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
    pthread_create(&open_thread, NULL, (void*(*)(void*))bw_real_open_mbnode, 
                   info);
    pthread_detach(open_thread);
#else
    bw_real_open_mbnode(info);
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
    if (balsa_app.mblist_tree_store)
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
        bw_unregister_open_mailbox(mbnode->mailbox);

        /* If this is the last notebook page clear the message preview
           and the status bar */
        page =
            gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), 0);

        if (page == NULL) {
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
            bw_enable_edit_menus(window, NULL);
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
    g_return_if_fail(bw != NULL);
    g_return_if_fail(BALSA_IS_WINDOW(bw));

    g_signal_emit(G_OBJECT(bw), window_signals[IDENTITIES_CHANGED], 0);
}

static gboolean
bw_close_mailbox_on_timer(void)
{
    time_t current_time;
    GtkWidget *page;
    int i, c, delta_time;

    if (!balsa_app.notebook)
        return FALSE;
    if (!balsa_app.close_mailbox_auto)
        return TRUE;

    gdk_threads_enter();
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
            bw_unregister_open_mailbox(index->mailbox_node->mailbox);
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
    bw_idle_remove(BALSA_WINDOW(object));

    if (GTK_OBJECT_CLASS(balsa_window_parent_class)->destroy)
        GTK_OBJECT_CLASS(balsa_window_parent_class)->
            destroy(GTK_OBJECT(object));
}


/*
 * refresh data in the main window
 */
void
balsa_window_refresh(BalsaWindow * window)
{
    GtkWidget *index;
    GtkWidget *paned;
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
    paned = gtk_widget_get_ancestor(balsa_app.notebook, GTK_TYPE_VPANED);
    g_assert(paned != NULL);
    if (balsa_app.previewpane) {
        bw_idle_replace(window, bindex);
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
static void
bw_register_open_mailbox(LibBalsaMailbox *m)
{
    LOCK_OPEN_LIST;
    balsa_app.open_mailbox_list =
        g_list_prepend(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
    libbalsa_mailbox_set_open(m, TRUE);
}
static void
bw_unregister_open_mailbox(LibBalsaMailbox *m)
{
    LOCK_OPEN_LIST;
    balsa_app.open_mailbox_list =
        g_list_remove(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
    libbalsa_mailbox_set_open(m, FALSE);
}
static gboolean
bw_is_open_mailbox(LibBalsaMailbox *m)
{
    GList *res;
    LOCK_OPEN_LIST;
    res= g_list_find(balsa_app.open_mailbox_list, m);
    UNLOCK_OPEN_LIST;
    return (res != NULL);
}

static void
bw_contents_cb(void)
{
    GError *err = NULL;

    gnome_help_display("balsa", NULL, &err);
    if (err) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Error displaying help: %s\n"), err->message);
        g_error_free(err);
    }
}

/*
 * show the about box for Balsa
 */
static void
bw_show_about_box(BalsaWindow * window)
{
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

#if GTK_CHECK_VERSION(2, 6, 0)
    gtk_show_about_dialog(GTK_WINDOW(window),
                          "name", "Balsa",
                          "version", BALSA_VERSION,
                          "copyright",
                          "Copyright \xc2\xa9 1997-2005 The Balsa Developers",
                          "comments",
                          _("The Balsa email client is part of "
                            "the GNOME desktop environment.  "
                            "Information on Balsa can be found at "
                            "http://balsa.gnome.org/\n\n"
                            "If you need to report bugs, "
                            "please do so at: "
                            "http://bugzilla.gnome.org/"),
                          "authors", authors,
                          "documenters", documenters,
                          "translator-credits",
                          strcmp(translator_credits, "translator-credits") ?
			  translator_credits : NULL,
			  "logo", balsa_logo,
                          NULL);
    g_object_unref(balsa_logo);
#else /* GTK_CHECK_VERSION(2, 6, 0) */
    static GtkWidget *about = NULL;

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
                            strcmp(translator_credits, "translator-credits") != 0 ? translator_credits : NULL,
                            balsa_logo
                            );

    g_object_add_weak_pointer(G_OBJECT(about), (gpointer) &about);

    gtk_widget_show(about);
#endif /* GTK_CHECK_VERSION(2, 6, 0) */
}

/* Check all mailboxes in a list
 *
 */
static void
bw_check_mailbox_list(GList * mailbox_list)
{
    GList *list;
    LibBalsaMailbox *mailbox;

    list = g_list_first(mailbox_list);
    while (list) {
        mailbox = BALSA_MAILBOX_NODE(list->data)->mailbox;
        libbalsa_mailbox_pop3_set_inbox(mailbox, balsa_app.inbox);
        libbalsa_mailbox_pop3_set_msg_size_limit
            (LIBBALSA_MAILBOX_POP3(mailbox), balsa_app.msg_size_limit*1024);
        libbalsa_mailbox_check(mailbox);
        list = g_list_next(list);
    }
}

/*Callback to check a mailbox in a balsa-mblist */
static gboolean
bw_mailbox_check_func(GtkTreeModel * model, GtkTreePath * path,
		   GtkTreeIter * iter, GSList ** list)
{
    BalsaMailboxNode *mbnode;

    gtk_tree_model_get(model, iter, 0, &mbnode, -1);
    g_return_val_if_fail(mbnode, FALSE);

    if (mbnode->mailbox) {	/* mailbox, not a folder */
	if (!LIBBALSA_IS_MAILBOX_IMAP(mbnode->mailbox) ||
	    bw_imap_check_test(mbnode->dir ? mbnode->dir :
			    libbalsa_mailbox_imap_get_path
			    (LIBBALSA_MAILBOX_IMAP(mbnode->mailbox)))) {
	    g_object_ref(mbnode->mailbox);
	    *list = g_slist_prepend(*list, mbnode->mailbox);
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
bw_imap_check_test(const gchar * path)
{
    /* path has been parsed, so it's just the folder path */
    if (balsa_app.check_imap && balsa_app.check_imap_inbox)
        return strcmp(path, "INBOX") == 0;
    else
        return balsa_app.check_imap;
}

#if BALSA_USE_THREADS
static void
bw_progress_dialog_destroy_cb(GtkWidget * widget, gpointer data)
{
    progress_dialog = NULL;
    progress_dialog_source = NULL;
    progress_dialog_message = NULL;
    progress_dialog_bar = NULL;
}
static void
bw_progress_dialog_response_cb(GtkWidget* dialog, gint response)
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
		     G_CALLBACK(bw_progress_dialog_destroy_cb), NULL);
    g_signal_connect(G_OBJECT(progress_dialog), "response",
		     G_CALLBACK(bw_progress_dialog_response_cb), NULL);
    
    progress_dialog_source = gtk_label_new(_("Checking Mail..."));
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_source, FALSE, FALSE, 0);
    
    progress_dialog_message = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_message, FALSE, FALSE, 0);
    
    progress_dialog_bar = gtk_progress_bar_new();
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(progress_dialog)->vbox),
		       progress_dialog_bar, FALSE, FALSE, 0);
    gtk_window_set_default_size(GTK_WINDOW(progress_dialog), 250, 100);
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
check_new_messages_real(BalsaWindow * window, int type)
{
    GSList *list = NULL;
#ifdef BALSA_USE_THREADS
    /*  Only Run once -- If already checking mail, return.  */
    pthread_mutex_lock(&mailbox_lock);
    if (checking_mail) {
        pthread_mutex_unlock(&mailbox_lock);
        fprintf(stderr, "Already Checking Mail!\n");
	if (progress_dialog)
	    gdk_window_raise(progress_dialog->window);
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

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) bw_mailbox_check_func,
			   &list);

    /* initiate threads */
    pthread_create(&get_mail_thread,
                   NULL, (void *) &bw_check_messages_thread, (void *) list);
    
    /* Detach so we don't need to pthread_join
     * This means that all resources will be
     * reclaimed as soon as the thread exits
     */
    pthread_detach(get_mail_thread);
#else

    /* NOT USED: libbalsa_notify_start_check(bw_imap_check_test); */
    bw_check_mailbox_list(balsa_app.inbox_input);

    gtk_tree_model_foreach(GTK_TREE_MODEL(balsa_app.mblist_tree_store),
			   (GtkTreeModelForeachFunc) bw_mailbox_check_func,
			   &list);
    g_slist_foreach(list, (GFunc) libbalsa_mailbox_check, NULL);
    g_slist_foreach(list, (GFunc) g_object_unref, NULL);
    g_slist_free(list);
#endif
}

/* bw_send_receive_messages_cb:
   check messages first to satisfy those that use smtp-after-pop.
*/
static void
bw_send_receive_messages_cb(GtkAction * action, gpointer data)
{
    check_new_messages_real(data, TYPE_CALLBACK);
    libbalsa_process_queue(balsa_app.outbox, balsa_find_sentbox_by_url,
#if ENABLE_ESMTP
                           balsa_app.smtp_servers,
#endif /* ENABLE_ESMTP */
			   balsa_app.debug);
}

void
check_new_messages_cb(GtkAction * action, gpointer data)
{
    check_new_messages_real(data, TYPE_CALLBACK);
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

/* bw_send_outbox_messages_cb:
   tries again to send the messages queued in outbox.
*/

static void
bw_send_outbox_messages_cb(GtkAction * action, gpointer data)
{
    libbalsa_process_queue(balsa_app.outbox, balsa_find_sentbox_by_url,
#if ENABLE_ESMTP
                           balsa_app.smtp_servers,
#endif /* ENABLE_ESMTP */
			   balsa_app.debug);
}

#ifdef HAVE_GTK_PRINT
/* Callback for `Page setup' item on the `File' menu */
static void
bw_page_setup_cb(GtkAction * action, gpointer data)
{
    message_print_page_setup(GTK_WINDOW(data));
}
#endif

/* Callback for `Print current message' item on the `File' menu, 
 * and the toolbar button. */
static void
bw_message_print_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;
    BalsaIndex *bindex;

    g_return_if_fail(data);

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    if (!index)
        return;

    bindex = BALSA_INDEX(index);
    if (bindex->current_msgno) {
        LibBalsaMessage *message =
            libbalsa_mailbox_get_message(bindex->mailbox_node->mailbox,
                                         bindex->current_msgno);
        if (!message)
            return;
        message_print(message, GTK_WINDOW(data));
        g_object_unref(message);
    }
}

/* this one is called only in the threaded code */
#ifdef BALSA_USE_THREADS
static void
bw_mailbox_check(LibBalsaMailbox * mailbox)
{
    MailThreadMessage *threadmessage;
    gchar *string = NULL;

    if (libbalsa_mailbox_get_subscribe(mailbox) == LB_MAILBOX_SUBSCRIBE_NO)
        return;

    if (LIBBALSA_IS_MAILBOX_IMAP(mailbox)) {
	string = g_strdup_printf(_("IMAP mailbox: %s"), mailbox->url);
        if (balsa_app.debug)
            fprintf(stderr, "%s\n", string);
    } else if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox))
	string = g_strdup_printf(_("Local mailbox: %s"), mailbox->name);
    MSGMAILTHREAD(threadmessage, LIBBALSA_NTFY_SOURCE, NULL, string, 0, 0);
    g_free(string);

    libbalsa_mailbox_check(mailbox);
}

static void
bw_check_messages_thread(GSList * list)
{
    /*  
     *  It is assumed that this will always be called as a pthread,
     *  and that the calling procedure will check for an existing lock
     *  and set checking_mail to true before calling.
     */
    MailThreadMessage *threadmessage;
    
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    MSGMAILTHREAD(threadmessage, LIBBALSA_NTFY_SOURCE, NULL, "POP3", 0, 0);
    bw_check_mailbox_list(balsa_app.inbox_input);

    g_slist_foreach(list, (GFunc) bw_mailbox_check, NULL);
    g_slist_foreach(list, (GFunc) g_object_unref, NULL);
    g_slist_free(list);

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
mail_progress_notify_cb(GIOChannel * source, GIOCondition condition,
                        BalsaWindow ** window)
{
    const int MSG_BUFFER_SIZE = 512 * sizeof(MailThreadMessage *);
    MailThreadMessage *threadmessage;
    MailThreadMessage **currentpos;
    void *msgbuffer;
    ssize_t count;
    gfloat fraction;
    GtkStatusbar *statusbar;
    guint context_id;

    msgbuffer = g_malloc(MSG_BUFFER_SIZE);
    count = read(mail_thread_pipes[0], msgbuffer, MSG_BUFFER_SIZE);

    /* FIXME: imagine reading just half of the pointer. The sync is gone.. */
    if (count % sizeof(MailThreadMessage *)) {
        g_free(msgbuffer);
        return TRUE;
    }

    currentpos = (MailThreadMessage **) msgbuffer;

    if (quiet_check || !*window) {
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

    statusbar = GTK_STATUSBAR((*window)->statusbar);
    context_id = gtk_statusbar_get_context_id(statusbar, "BalsaWindow mail progress");

    while (count) {
        threadmessage = *currentpos;

        if (balsa_app.debug)
            fprintf(stderr, "Message: %lu, %d, %s\n",
                    (unsigned long) threadmessage,
                    threadmessage->message_type,
                    threadmessage->message_string);

        if (!progress_dialog)
            gtk_statusbar_pop(statusbar, context_id);

        switch (threadmessage->message_type) {
        case LIBBALSA_NTFY_SOURCE:
            if (progress_dialog) {
                gtk_label_set_text(GTK_LABEL(progress_dialog_source),
                                   threadmessage->message_string);
                gtk_label_set_text(GTK_LABEL(progress_dialog_message), "");
                gtk_widget_show_all(progress_dialog);
            } else
                gtk_statusbar_push(statusbar, context_id,
                                   threadmessage->message_string);
            break;
        case LIBBALSA_NTFY_MSGINFO:
            if (progress_dialog) {
                gtk_label_set_text(GTK_LABEL(progress_dialog_message),
                                   threadmessage->message_string);
                gtk_widget_show_all(progress_dialog);
            } else
                gtk_statusbar_push(statusbar, context_id,
                                   threadmessage->message_string);
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
                gtk_label_set_text(GTK_LABEL(progress_dialog_message),
                                   threadmessage->message_string);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              (progress_dialog_bar),
					      fraction);
            } else {
                gtk_statusbar_push(statusbar, context_id,
                                   threadmessage->message_string);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              ((*window)->progress_bar),
                                              fraction);
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
                gtk_statusbar_pop(statusbar, context_id);
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              ((*window)->progress_bar), 0.0);
            }
            break;

        case LIBBALSA_NTFY_ERROR:
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              "%s",
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
send_progress_notify_cb(GIOChannel * source, GIOCondition condition,
                        BalsaWindow ** window)
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
                gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR
                                              ((*window)->progress_bar),
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
 
#endif

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
#ifdef HAVE_NOTIFY
static NotifyNotification *new_mail_note = NULL;
#endif

#if GTK_CHECK_VERSION(2, 10, 0)
static void
hide_sys_tray_icon(GObject *gobject, GParamSpec *arg1, gpointer user_data)
{
    if (gtk_window_is_active(GTK_WINDOW(gobject)))
	gtk_status_icon_set_visible(GTK_STATUS_ICON(user_data), FALSE);
}
#endif

static void
bw_display_new_mail_notification(int num_new, int has_new)
{
    static GtkWidget *dlg = NULL;
    static gint num_total = 0;
    gchar *msg = NULL;
#if GTK_CHECK_VERSION(2, 10, 0)
    static GtkStatusIcon *new_mail_tray = NULL;
#endif

    if (num_new <= 0 && has_new <= 0)
        return;

    if (balsa_app.notify_new_mail_sound)
        gnome_triggers_do("New mail has arrived", "email",
                          "balsa", "newmail", NULL);

#if GTK_CHECK_VERSION(2, 10, 0)
    /* set up the sys tray icon when it is not yet present */
    if (!new_mail_tray) {
	new_mail_tray = gtk_status_icon_new_from_icon_name("stock_mail-compose");
	g_signal_connect_swapped(G_OBJECT(new_mail_tray), "activate",
				 G_CALLBACK(gtk_window_present),
				 balsa_app.main_window);
	/* hide tray icon when the main window gets the focus. */
	g_signal_connect(G_OBJECT(balsa_app.main_window), "notify::is-active",
			 G_CALLBACK(hide_sys_tray_icon), new_mail_tray);
    }

    /* show sys tray icon if we don't have the focus */
    if (!gtk_window_is_active(GTK_WINDOW(balsa_app.main_window))) {
	if (num_new > 0)
	    msg = g_strdup_printf(ngettext("Balsa: you have received %d new message.",
					   "Balsa: you have received %d new messages.",
					   num_new + num_total), num_new + num_total);
	else
	    msg = g_strdup(_("Balsa: you have new mail."));
	gtk_status_icon_set_tooltip(new_mail_tray, msg);
	gtk_status_icon_set_visible(new_mail_tray, TRUE);
	g_free(msg);
    }
#endif

    if (!balsa_app.notify_new_mail_dialog)
        return;

#ifdef HAVE_NOTIFY
    /* Before attemtping to use the notifications check whether they
       are actually available - perhaps the underlying connection to
       dbus could not be created? In any case, we must not continue or
       ugly things will happen, at least with libnotify-0.4.2. */
    if (notify_is_initted()) {
        if (gtk_window_is_active(GTK_WINDOW(balsa_app.main_window)))
            return;

        if (new_mail_note) {
            /* the user didn't acknowledge the last info, so we'll
             * accumulate the count */
            num_total += num_new;
        } else {
            num_total = num_new;
            new_mail_note =
                notify_notification_new("Balsa", NULL, NULL, NULL);
            g_object_add_weak_pointer(G_OBJECT(new_mail_note),
                                      (gpointer) & new_mail_note);
            g_signal_connect(new_mail_note, "closed",
                             G_CALLBACK(g_object_unref), NULL);
        }
    } else {
#endif
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
#ifdef HAVE_NOTIFY
    }

    msg = bw_get_new_message_notification_string(num_new, num_total);
    if (new_mail_note) {
        notify_notification_update(new_mail_note, "Balsa", msg, 
                                   GTK_STOCK_DIALOG_INFO);
        notify_notification_set_timeout(new_mail_note, 30000); /* 30 seconds */
        notify_notification_show(new_mail_note, NULL);
    } else
        gtk_label_set_text(GTK_LABEL(GTK_MESSAGE_DIALOG(dlg)->label), msg);
#else

    msg = bw_get_new_message_notification_string(num_new, num_total);
    gtk_label_set_text(GTK_LABEL(GTK_MESSAGE_DIALOG(dlg)->label), msg);
#endif
    g_free(msg);
}

#ifdef HAVE_NOTIFY
static void
bw_cancel_new_mail_notification(GObject *gobject, GParamSpec *arg1,
				gpointer user_data)
{
    if (new_mail_note
        && gtk_window_is_active(GTK_WINDOW(balsa_app.main_window))) {
        /* Setting 0 would mean never timeout! */
        notify_notification_set_timeout(new_mail_note, 1);
        notify_notification_show(new_mail_note, NULL);
    }
}
#endif

GtkWidget *
balsa_window_find_current_index(BalsaWindow * window)
{
    g_return_val_if_fail(window != NULL, NULL);

    return window->current_index;
}


static void
bw_new_message_cb(GtkAction * action, gpointer data)
{
    BalsaSendmsg *smwindow;

    smwindow = sendmsg_window_compose();

#if !defined(ENABLE_TOUCH_UI)
    g_signal_connect(G_OBJECT(smwindow->window), "destroy",
                     G_CALLBACK(bw_send_msg_window_destroy_cb), data);
#endif /*ENABLE_TOUCH_UI */
}


static void
bw_replyto_message_cb(GtkAction * action, gpointer data)
{
    balsa_message_reply(balsa_window_find_current_index
                        (BALSA_WINDOW(data)));
}

static void
bw_replytoall_message_cb(GtkAction * action, gpointer data)
{
    balsa_message_replytoall(balsa_window_find_current_index
                             (BALSA_WINDOW(data)));
}

static void
bw_replytogroup_message_cb(GtkAction * action, gpointer data)
{
    balsa_message_replytogroup(balsa_window_find_current_index
                               (BALSA_WINDOW(data)));
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_forward_message_attached_cb(GtkAction * action, gpointer data)
{
    balsa_message_forward_attached(balsa_window_find_current_index
                                   (BALSA_WINDOW(data)));
}

static void
bw_forward_message_inline_cb(GtkAction * action, gpointer data)
{
    balsa_message_forward_inline(balsa_window_find_current_index
                                 (BALSA_WINDOW(data)));
}
#endif /* ENABLE_TOUCH_UI */

static void
bw_forward_message_default_cb(GtkAction * action, gpointer data)
{
    balsa_message_forward_default(balsa_window_find_current_index
                                  (BALSA_WINDOW(data)));
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_pipe_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_pipe(BALSA_INDEX
                     (balsa_window_find_current_index
                      (BALSA_WINDOW(data))));
}
#endif /* ENABLE_TOUCH_UI */

static void
bw_continue_message_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index && BALSA_INDEX(index)->mailbox_node->mailbox == balsa_app.draftbox)
        balsa_message_continue(BALSA_INDEX(index));
    else
        balsa_mblist_open_mailbox(balsa_app.draftbox);
}


static void
bw_next_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_select_next(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

/* Select next unread message, changing mailboxes if necessary; 
 * returns TRUE if mailbox was changed. */
gboolean
balsa_window_next_unread(BalsaWindow * window)
{
    BalsaIndex *index =
        BALSA_INDEX(balsa_window_find_current_index(window));
    LibBalsaMailbox *mailbox = index ? index->mailbox_node->mailbox : NULL;
#if WE_REALLY_WANT_TO_GET_IN_THE_USERS_FACE
    GtkWidget *dialog;
    gint response;
#endif                          /* WE_REALLY_WANT_TO_GET_IN_THE_USERS_FACE */

    if (libbalsa_mailbox_get_unread(mailbox) > 0) {
        if (!balsa_index_select_next_unread(index)) {
            /* All unread messages must be hidden; we assume that the
             * user wants to see them, and try again. */
            bw_reset_filter_cb(NULL, window);
            balsa_index_select_next_unread(index);
        }
        return FALSE;
    }

    mailbox = bw_next_unread_mailbox(mailbox);
    if (!mailbox || libbalsa_mailbox_get_unread(mailbox) == 0)
        return FALSE;

#if WE_REALLY_WANT_TO_GET_IN_THE_USERS_FACE
    dialog =
        gtk_message_dialog_new(GTK_WINDOW(window), 0,
                               GTK_MESSAGE_QUESTION,
                               GTK_BUTTONS_YES_NO,
                               _("The next unread message is in %s"),
                               mailbox->name);
    gtk_message_dialog_format_secondary_text
        (GTK_MESSAGE_DIALOG(dialog),
         _("Do you want to switch to %s?"), mailbox->name);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_YES);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES)
        return FALSE;
#endif                          /* WE_REALLY_WANT_TO_GET_IN_THE_USERS_FACE */

    balsa_mblist_open_mailbox(mailbox);
    index = balsa_find_index_by_mailbox(mailbox);
    if (index)
        balsa_index_select_next_unread(index);
    else
        g_object_set_data(G_OBJECT(mailbox),
                          BALSA_INDEX_VIEW_ON_OPEN, GINT_TO_POINTER(TRUE));
    return TRUE;
}

static void
bw_next_unread_message_cb(GtkAction * action, gpointer data)
{
    balsa_window_next_unread(BALSA_WINDOW(data));
}

static void
bw_next_flagged_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_select_next_flagged(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

static void
bw_previous_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_select_previous(
        BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))));
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_next_part_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    balsa_message_next_part(BALSA_MESSAGE(bw->preview));
    bw_enable_edit_menus(bw, BALSA_MESSAGE(bw->preview));
    bw_enable_part_menu_items(bw);
}

static void
bw_previous_part_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    balsa_message_previous_part(BALSA_MESSAGE(bw->preview));
    bw_enable_edit_menus(bw, BALSA_MESSAGE(bw->preview));
    bw_enable_part_menu_items(bw);
}
#endif /* ENABLE_TOUCH_UI */

/* Edit menu callbacks. */
static void
bw_copy_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
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
bw_select_all_cb(GtkAction * action, gpointer data)
{
    balsa_window_select_all(data);
}

/* Message menu callbacks. */
#if !defined(ENABLE_TOUCH_UI)
static void
bw_message_copy_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    if (balsa_message_grab_focus(BALSA_MESSAGE(bw->preview)))
        bw_copy_cb(action, data);
}

static void
bw_message_select_all_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    if (balsa_message_grab_focus(BALSA_MESSAGE(bw->preview)))
	balsa_window_select_all(data);
}
#endif /* ENABLE_TOUCH_UI */

static void
bw_save_current_part_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    balsa_message_save_current_part(BALSA_MESSAGE(bw->preview));
}

static void
bw_view_msg_source_cb(GtkAction * action, gpointer data)
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
				     &balsa_app.source_escape_specials,
                                     &balsa_app.source_width,
                                     &balsa_app.source_height);
    }

    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
}

static void
bw_trash_message_cb(GtkAction * action, gpointer data)
{
    balsa_message_move_to_trash(balsa_window_find_current_index
                                (BALSA_WINDOW(data)));
}

static void
bw_toggle_deleted_message_cb(GtkAction * action, gpointer data)
{
    LibBalsaMessageFlag f = LIBBALSA_MESSAGE_FLAG_DELETED; 
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         f);
}


static void
bw_toggle_flagged_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_FLAGGED);
}

static void
bw_toggle_new_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_NEW);
}

static void
bw_toggle_answered_message_cb(GtkAction * action, gpointer data)
{
    balsa_index_toggle_flag
        (BALSA_INDEX(balsa_window_find_current_index(BALSA_WINDOW(data))),
         LIBBALSA_MESSAGE_FLAG_REPLIED);
}

static void
bw_store_address_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));
    GList *messages;

    g_assert(index != NULL);

    messages = balsa_index_selected_list(BALSA_INDEX(index));
    balsa_store_address_from_messages(messages);
    g_list_foreach(messages, (GFunc)g_object_unref, NULL);
    g_list_free(messages);
}

#if defined(ENABLE_TOUCH_UI)
static void
bw_set_sort_menu(BalsaWindow *window,
                 LibBalsaMailboxSortFields col,
                 LibBalsaMailboxSortType   order)
{
    const gchar *action_name;
    GtkAction *action;

    switch(col) {
    case LB_MAILBOX_SORT_DATE:
    case LB_MAILBOX_SORT_NO:      action_name = "ByArrival"; break;
    case LB_MAILBOX_SORT_SENDER:  action_name = "BySender";  break;
    case LB_MAILBOX_SORT_SUBJECT: action_name = "BySubject"; break;
    case LB_MAILBOX_SORT_SIZE:    action_name = "BySize";    break;
    case LB_MAILBOX_SORT_THREAD:  action_name = "Threaded";  break;
    default: return;
    }

    action =
        gtk_action_group_get_action(window->mailbox_action_group,
                                    action_name);
    g_signal_handlers_block_by_func(G_OBJECT(action),
                                    G_CALLBACK(bw_sort_change_cb),
                                    window);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), TRUE);
    g_signal_handlers_unblock_by_func(G_OBJECT(action),
                                      G_CALLBACK(bw_sort_change_cb),
                                      window);

    action =
        gtk_action_group_get_action(window->action_group,
                                    "SortDescending");
    g_signal_handlers_block_by_func(G_OBJECT(action),
                                    G_CALLBACK(bw_sort_change_cb),
                                    window);
    gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action),
                                 order == LB_MAILBOX_SORT_TYPE_DESC);
    g_signal_handlers_unblock_by_func(G_OBJECT(action),
                                      G_CALLBACK(bw_sort_change_cb),
                                      window);

    gtk_action_set_sensitive(action, strcmp(action_name, "Threaded") != 0);
}

static void
bw_sort_change_cb(GtkRadioAction *action, GtkRadioAction *current, gpointer data) 
{
    BalsaWindow *window = BALSA_WINDOW(data);
    LibBalsaMailboxSortFields key;
    LibBalsaMailboxSortType   order;
    GtkWidget       *bindex;
    LibBalsaMailbox *mailbox;
    gint             col;

    bindex = balsa_window_find_current_index(window);
    if(!bindex)
        return;

    key = gtk_radio_action_get_current_value(action);
    mailbox = BALSA_INDEX(bindex)->mailbox_node->mailbox;

    switch(key) {
    case LB_MAILBOX_SORT_NO:      col = LB_MBOX_MSGNO_COL;   break;
    case LB_MAILBOX_SORT_SENDER:  col = LB_MBOX_FROM_COL;    break;
    case LB_MAILBOX_SORT_SUBJECT: col = LB_MBOX_SUBJECT_COL; break;
    case LB_MAILBOX_SORT_DATE:    col = LB_MBOX_DATE_COL;    break;
    case LB_MAILBOX_SORT_SIZE:    col = LB_MBOX_SIZE_COL;    break;
    case LB_MAILBOX_SORT_THREAD:
        libbalsa_mailbox_set_sort_field(mailbox, key);
        balsa_index_set_threading_type(BALSA_INDEX(bindex),
                                       LB_MAILBOX_THREADING_JWZ);
        bw_set_sensitive(window, "SortDescending", FALSE);
        return;
    default: return;
    }
    bw_set_sensitive(window, "SortDescending", TRUE);
    if(libbalsa_mailbox_get_threading_type(mailbox)
       != LB_MAILBOX_THREADING_FLAT)
        balsa_index_set_threading_type(BALSA_INDEX(bindex),
                                       LB_MAILBOX_THREADING_FLAT);
    order = libbalsa_mailbox_get_sort_type(mailbox);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(mailbox),
                                         col, 
                                         order == LB_MAILBOX_SORT_TYPE_ASC
                                         ? GTK_SORT_ASCENDING
                                         : GTK_SORT_DESCENDING);
}

static void
bw_toggle_order_cb(GtkToggleAction * action, gpointer data)
{
    LibBalsaMailboxSortType   order;
    GtkWidget       *bindex;
    LibBalsaMailbox *mailbox;
    gint             col;

    bindex = balsa_window_find_current_index(BALSA_WINDOW(data));
    if(!bindex)
        return;
    mailbox = BALSA_INDEX(bindex)->mailbox_node->mailbox;
    order = gtk_toggle_action_get_active(action)
        ? LB_MAILBOX_SORT_TYPE_DESC :  LB_MAILBOX_SORT_TYPE_ASC;

    switch(libbalsa_mailbox_get_sort_field(mailbox)) {
    case LB_MAILBOX_SORT_NO:      col = LB_MBOX_MSGNO_COL;   break;
    case LB_MAILBOX_SORT_SENDER:  col = LB_MBOX_FROM_COL;    break;
    case LB_MAILBOX_SORT_SUBJECT: col = LB_MBOX_SUBJECT_COL; break;
    case LB_MAILBOX_SORT_DATE:    col = LB_MBOX_DATE_COL;    break;
    case LB_MAILBOX_SORT_SIZE:    col = LB_MBOX_SIZE_COL;    break;
    default:
    case LB_MAILBOX_SORT_THREAD:
        g_warning("This should not be possible"); return;
    }
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(mailbox),
                                         col, 
                                         order == LB_MAILBOX_SORT_TYPE_ASC
                                         ? GTK_SORT_ASCENDING
                                         : GTK_SORT_DESCENDING);
}

#endif /* ENABLE_TOUCH_UI */
static void
bw_wrap_message_cb(GtkToggleAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);

    balsa_app.browse_wrap = gtk_toggle_action_get_active(action);

    balsa_message_set_wrap(BALSA_MESSAGE(bw->preview),
                           balsa_app.browse_wrap);
    refresh_preferences_manager();
}

/*
 * Callback for the "activate" signal of the View menu's header options.
 * We use this instead of the GtkRadioAction's "changed" signal so that
 * we can respond to a click on the current choice.
 */
static void
bw_header_activate_cb(GtkAction * action, gpointer data)
{
    if (gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action))) {
        ShownHeaders sh =
            gtk_radio_action_get_current_value(GTK_RADIO_ACTION(action));
        BalsaWindow *bw = BALSA_WINDOW(data);

        balsa_app.shown_headers = sh;
        bw_reset_show_all_headers(bw);
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            sh);
    }
}

#if !defined(ENABLE_TOUCH_UI)
/*
 * Callback for the "activate" signal of the View menu's threading options.
 * We use this instead of the GtkRadioAction's "changed" signal so that
 * we can respond to a click on the current choice.
 */
static void
bw_threading_activate_cb(GtkAction * action, gpointer data)
{
    if (gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action))) {
        BalsaWindow *bw = BALSA_WINDOW(data);
        GtkWidget *index;
        LibBalsaMailboxThreadingType type;
        BalsaMailboxNode *mbnode;
        LibBalsaMailbox *mailbox;

        index = balsa_window_find_current_index(bw);
        g_return_if_fail(index != NULL);

        type =
            gtk_radio_action_get_current_value(GTK_RADIO_ACTION(action));
        balsa_index_set_threading_type(BALSA_INDEX(index), type);

        /* bw->current_index may have been destroyed and cleared during
         * set-threading: */
        index = balsa_window_find_current_index(bw);
        if (index && (mbnode = BALSA_INDEX(index)->mailbox_node)
            && (mailbox = mbnode->mailbox))
            bw_enable_expand_collapse(bw, mailbox);
    }
}
#endif /* ENABLE_TOUCH_UI */

static void
bw_expand_all_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), TRUE);
}

static void
bw_collapse_all_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index);
    balsa_index_update_tree(BALSA_INDEX(index), FALSE);
}

#ifdef HAVE_GTKHTML
static void
bw_zoom_in_cb(GtkAction * action, gpointer data)
{
    GtkWidget *bm = BALSA_WINDOW(data)->preview;
    balsa_message_zoom(BALSA_MESSAGE(bm), 1);
}

static void
bw_zoom_out_cb(GtkAction * action, gpointer data)
{
    GtkWidget *bm = BALSA_WINDOW(data)->preview;
    balsa_message_zoom(BALSA_MESSAGE(bm), -1);
}

static void
bw_zoom_100_cb(GtkAction * action, gpointer data)
{
    GtkWidget *bm = BALSA_WINDOW(data)->preview;
    balsa_message_zoom(BALSA_MESSAGE(bm), 0);
}
#endif				/* HAVE_GTKHTML */

#if defined(ENABLE_TOUCH_UI)
static gboolean
bw_open_mailbox_cb(GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    LibBalsaMailbox *mailbox;

    if( (event->state & (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) !=
        (GDK_CONTROL_MASK|GDK_SHIFT_MASK)) return FALSE;
    switch(event->keyval) {
    case 'I': mailbox = balsa_app.inbox;    break;
    case 'D': mailbox = balsa_app.draftbox; break;
    case 'O': mailbox = balsa_app.outbox;   break;
    case 'S': mailbox = balsa_app.sentbox;  break;
    case 'T': mailbox = balsa_app.trash;    break;
    default: return FALSE;
    }
    balsa_mblist_open_mailbox(mailbox);
    return TRUE;
}

static void
bw_enable_view_filter_cb(GtkToggleAction * action, gpointer data)
{
    BalsaWindow *mw       = BALSA_WINDOW(data);
    GtkWidget *parent_box = gtk_widget_get_parent(mw->sos_entry);
    balsa_app.enable_view_filter = gtk_toggle_action_get_active(action);
    if(balsa_app.enable_view_filter)
        gtk_widget_show(parent_box);
    else
        gtk_widget_hide(parent_box);
}

#endif /* ENABLE_TOUCH_UI */

static void
bw_address_book_cb(GtkWindow *widget, gpointer data)
{
    GtkWidget *ab;

    ab = balsa_ab_window_new(FALSE, GTK_WINDOW(data));
    gtk_widget_show(GTK_WIDGET(ab));
}

static GtkToggleButton*
bw_add_check_button(GtkWidget* table, const gchar* label, gint x, gint y)
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
	matching_body    = bw_add_check_button(table, _("_Body"),    0, 0);
	matching_to      = bw_add_check_button(table, _("_To:"),     1, 0);
	matching_from    = bw_add_check_button(table, _("_From:"),   1, 1);
        matching_subject = bw_add_check_button(table, _("S_ubject"), 2, 0);
	matching_cc      = bw_add_check_button(table, _("_Cc:"),     2, 1);
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
		bw_reset_filter_cb(NULL, window);
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
            LibBalsaCondition *filter, *res;
            filter = bw_get_view_filter(window, FALSE);
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
bw_find_cb(GtkAction * action,gpointer data)
{
    BalsaWindow *window = data;
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(window)))
	bw_find_real(window, BALSA_INDEX(bindex),FALSE);
}

static void
bw_find_again_cb(GtkAction * action,gpointer data)
{
    BalsaWindow *window = data;
    GtkWidget * bindex;
    if ((bindex=balsa_window_find_current_index(window)))
	bw_find_real(window, BALSA_INDEX(bindex), TRUE);
}

static void
bw_filter_dlg_cb(GtkAction * action, gpointer data)
{
    filters_edit_dialog();
}

static void
bw_filter_export_cb(GtkAction * action, gpointer data)
{
    filters_export_dialog();
}

static void
bw_filter_run_cb(GtkAction * action, gpointer data)
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

#if !defined(ENABLE_TOUCH_UI)
static void
bw_remove_duplicates_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));
    if (index) {
        LibBalsaMailbox *mailbox =
            BALSA_INDEX(index)->mailbox_node->mailbox;
        GError *err = NULL;
        libbalsa_mailbox_move_duplicates(mailbox, balsa_app.trash, &err);
        if (err) {
            balsa_information(LIBBALSA_INFORMATION_WARNING,
                              _("Removing duplicates failed: %s"),
                              err->message);
            g_error_free(err);
        } else
            balsa_index_ensure_visible(BALSA_INDEX(index));
    }
}
#endif /* ENABLE_TOUCH_UI */

static void
bw_empty_trash_cb(GtkAction * action, gpointer data)
{
    empty_trash(BALSA_WINDOW(data));
}

/* closes the mailbox on the notebook's active page */
static void
bw_mailbox_close_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index = balsa_window_find_current_index(BALSA_WINDOW(data));

    if (index)
        balsa_mblist_close_mailbox(BALSA_INDEX(index)->mailbox_node->
                                   mailbox);
}

static void
bw_mailbox_tab_close_cb(GtkWidget * widget, gpointer data)
{
    GtkWidget * window = gtk_widget_get_toplevel(widget);
    balsa_window_real_close_mbnode(BALSA_WINDOW(window),
				   BALSA_MAILBOX_NODE(data));
}


static LibBalsaCondition*
bw_get_view_filter(BalsaWindow *window, gboolean flags_only)
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
    LibBalsaCondition *filter, *flag_filter;
    
    for(i=0; i<ELEMENTS(match_flags); i++)
        match_flags[i].setby = -1;

    for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
        LibBalsaMessageFlag flag;
        gboolean set;
        gint states_index = hide_states[i].states_index;

        if (!bw_get_active(window, hide_states[i].action_name))
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
    flag_filter = NULL;
    for(j=0; j<ELEMENTS(match_flags); j++) {
        LibBalsaCondition *lbc, *res;
        if(match_flags[j].setby < 0) continue;
        lbc = libbalsa_condition_new_flag_enum(match_flags[j].state,
                                               match_flags[j].flag);
        res = libbalsa_condition_new_bool_ptr(FALSE, CONDITION_AND,
                                              lbc, flag_filter);
        libbalsa_condition_unref(lbc);
        libbalsa_condition_unref(flag_filter);
        flag_filter = res;
    }

    /* add string filter on top of that */

    i = flags_only
        ? -1 : gtk_combo_box_get_active(GTK_COMBO_BOX(window->filter_choice));
    if(i>=0 && i<(signed)ELEMENTS(view_filters)) {
        const gchar *str = gtk_entry_get_text(GTK_ENTRY(window->sos_entry));
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

/**bw_filter_to_int() returns an integer representing the
   view filter.
*/
static int
bw_filter_to_int(BalsaWindow * window)
{
    unsigned i;
    int res = 0;
    for (i = 0; i < G_N_ELEMENTS(hide_states); i++)
        if (bw_get_active(window, hide_states[i].action_name))
            res |= 1 << hide_states[i].states_index;
    return res;
}

static void
bw_hide_changed_cb(GtkToggleAction * toggle_action, gpointer data)
{
    LibBalsaMailbox *mailbox;
    BalsaWindow *bw = BALSA_WINDOW(data);
    GtkWidget *index = balsa_window_find_current_index(bw);
    LibBalsaCondition *filter;
    
    /* PART 1: assure menu consistency */
    if (gtk_toggle_action_get_active(toggle_action)) {
        /* we may need to deactivate coupled negated flag. */
        const gchar *action_name =
            gtk_action_get_name(GTK_ACTION(toggle_action));
        unsigned curr_idx, i;

        for (i = 0; i < G_N_ELEMENTS(hide_states); i++)
            if (strcmp(action_name, hide_states[i].action_name) == 0)
                break;
        g_assert(i < G_N_ELEMENTS(hide_states));
        curr_idx = hide_states[i].states_index;

        for (i = 0; i < G_N_ELEMENTS(hide_states); i++) {
            int states_idx = hide_states[i].states_index;

            if (!bw_get_active(bw, hide_states[i].action_name))
                continue;

            if (hide_states[states_idx].flag == hide_states[curr_idx].flag
                && hide_states[states_idx].set !=
                hide_states[curr_idx].set) {
                bw_set_active(bw, hide_states[i].action_name, FALSE, FALSE);
                return; /* triggered menu change will do the job */
            }
        }
    }

    if(!index)
        return;

    /* PART 2: do the job. */
    mailbox = BALSA_INDEX(index)->mailbox_node->mailbox;
    /* Store the new filter mask in the mailbox view before we set the
     * view filter; rethreading triggers bw_set_filter_menu,
     * which retrieves the mask from the mailbox view, and we want it to
     * be the new one. */
    libbalsa_mailbox_set_filter(mailbox, bw_filter_to_int(bw));

    /* Set the flags part of this filter as persistent: */
    filter = bw_get_view_filter(bw, TRUE);
    libbalsa_mailbox_set_view_filter(mailbox, filter, FALSE);
    libbalsa_condition_unref(filter);
    libbalsa_mailbox_make_view_filter_persistent(mailbox);

    filter = bw_get_view_filter(bw, FALSE);
    /* libbalsa_mailbox_set_view_filter() will ref the
     * filter.  We need also to rethread to take into account that
     * some messages might have been removed or added to the view. */
    if (libbalsa_mailbox_set_view_filter(mailbox, filter, TRUE))
        balsa_index_ensure_visible(BALSA_INDEX(index));
    libbalsa_condition_unref(filter);
}

static void
bw_reset_filter_cb(GtkAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);
    BalsaIndex *bindex = BALSA_INDEX(balsa_window_find_current_index(bw));

    /* do it by resetting the sos filder */
    gtk_entry_set_text(GTK_ENTRY(bw->sos_entry), "");
    bw_set_view_filter(bw, bindex->filter_no, bw->sos_entry);
}

static void
bw_mailbox_expunge_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    balsa_index_expunge(BALSA_INDEX(index));
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
	balsa_information_parented(GTK_WINDOW(window),
				   LIBBALSA_INFORMATION_WARNING,
				   _("Could not open trash: %s"),
				   err ? err->message : _("Unknown error"));
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
    enable_empty_trash(window, TRASH_EMPTY);
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_show_mbtree(BalsaWindow * bw)
{
    GtkWidget *parent;
    parent = gtk_widget_get_ancestor(bw->mblist, GTK_TYPE_HPANED);
    g_assert(parent != NULL);

    if (balsa_app.show_mblist) {
        gtk_widget_show(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), balsa_app.mblist_width);
    } else {
        gtk_widget_hide(bw->mblist);
        gtk_paned_set_position(GTK_PANED(parent), 0);
    }
}

static void
bw_show_mbtree_cb(GtkToggleAction * action, gpointer data)
{
    balsa_app.show_mblist = gtk_toggle_action_get_active(action);
    bw_show_mbtree(BALSA_WINDOW(data));
}

static void
bw_show_mbtabs_cb(GtkToggleAction * action, gpointer data)
{
    balsa_app.show_notebook_tabs = gtk_toggle_action_get_active(action);
    gtk_notebook_set_show_tabs(GTK_NOTEBOOK(balsa_app.notebook),
                               balsa_app.show_notebook_tabs);
}
#endif /* ENABLE_TOUCH_UI */

void
balsa_change_window_layout(BalsaWindow *window)
{

#if GTK_CHECK_VERSION(2, 11, 0)
    g_object_ref(window->notebook);
    g_object_ref(window->mblist);
    g_object_ref(window->preview);
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_widget_ref(window->notebook);
    gtk_widget_ref(window->mblist);
    gtk_widget_ref(window->preview);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
 
    gtk_container_remove(GTK_CONTAINER(window->notebook->parent), window->notebook);
    gtk_container_remove(GTK_CONTAINER(window->mblist->parent),
			 window->mblist);
    gtk_container_remove(GTK_CONTAINER(window->preview->parent),
			 window->preview);

    bw_set_panes(window);

#if GTK_CHECK_VERSION(2, 11, 0)
    g_object_unref(window->notebook);
    g_object_unref(window->mblist);
    g_object_unref(window->preview);
#else                           /* GTK_CHECK_VERSION(2, 11, 0) */
    gtk_widget_unref(window->notebook);
    gtk_widget_unref(window->mblist);
    gtk_widget_unref(window->preview);
#endif                          /* GTK_CHECK_VERSION(2, 11, 0) */
 
    gtk_paned_set_position(GTK_PANED(window->hpaned), 
                           balsa_app.show_mblist 
                           ? balsa_app.mblist_width
                           : 0);
    gtk_widget_show(window->vpaned);
    gtk_widget_show(window->hpaned);

}

/* PKGW: remember when they change the position of the vpaned. */
static void
bw_notebook_size_allocate_cb(GtkWidget * notebook, GtkAllocation * alloc,
                             BalsaWindow * bw)
{
    if (balsa_app.previewpane && !balsa_app.mw_maximized)
        balsa_app.notebook_height =
            gtk_paned_get_position(GTK_PANED(bw->vpaned));
}

static void
bw_size_allocate_cb(GtkWidget * window, GtkAllocation * alloc)
{
    if (!balsa_app.mw_maximized) {
        balsa_app.mw_height = alloc->height;
        balsa_app.mw_width  = alloc->width;
    }
}

/* When page is switched we change the preview window and the selected 
   mailbox in the mailbox tree.
 */
static void
bw_notebook_switch_page_cb(GtkWidget * notebook,
                        GtkNotebookPage * notebookpage, guint page_num,
                        gpointer data)
{
    BalsaWindow *window = BALSA_WINDOW(data);
    GtkWidget *page;
    BalsaIndex *index;
    LibBalsaMailbox *mailbox;
    gchar *title;

    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), page_num);
    index = BALSA_INDEX(gtk_bin_get_child(GTK_BIN(page)));

    mailbox = index->mailbox_node->mailbox;
    if (window->current_index) {
	g_object_remove_weak_pointer(G_OBJECT(window->current_index),
				     (gpointer) &window->current_index);
	/* Note when this mailbox was hidden, for use in auto-closing. */
	time(&BALSA_INDEX(window->current_index)->mailbox_node->last_use);
    }
    window->current_index = GTK_WIDGET(index);
    g_object_add_weak_pointer(G_OBJECT(window->current_index),
			      (gpointer) &window->current_index);
    /* Note when this mailbox was exposed, for use in auto-expunge. */
    time(&BALSA_INDEX(window->current_index)->mailbox_node->last_use);

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
    bw_idle_replace(window, index);
    bw_enable_message_menus(window, index->current_msgno);
    bw_enable_mailbox_menus(window, index);

    gtk_entry_set_text(GTK_ENTRY(window->sos_entry),
                       index->filter_string ? index->filter_string : "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(window->filter_choice),
                             index->filter_no);
    balsa_mblist_focus_mailbox(balsa_app.mblist, mailbox);
    balsa_mblist_set_status_bar(mailbox);

    balsa_index_refresh_date(index);
    balsa_index_refresh_size(index);
    balsa_index_ensure_visible(index);

#if !defined(ENABLE_TOUCH_UI)
    bw_enable_edit_menus(window, NULL);
    bw_enable_part_menu_items(window);
#endif /*ENABLE_TOUCH_UI */
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
    if (index->current_msgno == 0) {
        bw_enable_edit_menus(window, NULL);
    }

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
        window->set_message_id =
            g_idle_add((GSourceFunc) bw_idle_cb, window);
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

    gdk_threads_enter();

    if (window->set_message_id == 0) {
        gdk_threads_leave();
        return FALSE;
    }
    if (bw_idle_cb_active) {
        gdk_threads_leave();
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

    gdk_threads_leave();
    bw_idle_cb_active = FALSE;

    return FALSE;
}

static void
bw_select_part_cb(BalsaMessage * bm, gpointer data)
{
    bw_enable_edit_menus(BALSA_WINDOW(data), bm);
#if !defined(ENABLE_TOUCH_UI)
    bw_enable_part_menu_items(BALSA_WINDOW(data));
#endif /*ENABLE_TOUCH_UI */
}

#if !defined(ENABLE_TOUCH_UI)
static void
bw_send_msg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue(BALSA_WINDOW(data));
}
#endif /*ENABLE_TOUCH_UI */


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
    
    /* x and y are relative to the notebook, but the label allocations
     * are relative to the main window. */
    x += GTK_WIDGET(notebook)->allocation.x;
    y += GTK_WIDGET(notebook)->allocation.y;

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

    orig_index = *(BalsaIndex **) selection_data->data;
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
                             context->action != GDK_ACTION_MOVE);
    balsa_index_selected_msgnos_free(orig_index, selected);
}

static gboolean bw_notebook_drag_motion_cb(GtkWidget * widget,
                                           GdkDragContext * context,
                                           gint x, gint y, guint time,
                                           gpointer user_data)
{
    gdk_drag_status(context,
                    (context->actions ==
                     GDK_ACTION_COPY) ? GDK_ACTION_COPY :
                    GDK_ACTION_MOVE, time);

    return FALSE;
}

/* bw_progress_timeout
 * 
 * This function is called at a preset interval to cause the progress
 * bar to move in activity mode.  
 * this routine is called from g_timeout_dispatch() and needs to take care 
 * of GDK locking itself using gdk_threads_{enter,leave}
 *
 * Use of the progress bar to show a fraction of a task takes priority.
 **/
static gint
bw_progress_timeout(BalsaWindow ** window)
{
    gdk_threads_enter();

    if (*window && (*window)->progress_type == BALSA_PROGRESS_ACTIVITY)
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR((*window)->progress_bar));

    gdk_threads_leave();

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
gboolean
balsa_window_setup_progress(BalsaWindow * window, const gchar * text)
{
    GtkProgressBar *progress_bar;
    
    if (text) {
        /* make sure the progress bar is currently unused */
        if (window->progress_type == BALSA_PROGRESS_INCREMENT) 
            return FALSE;
        window->progress_type = BALSA_PROGRESS_INCREMENT;
    } else
        window->progress_type = BALSA_PROGRESS_NONE;

    progress_bar = GTK_PROGRESS_BAR(window->progress_bar);
    gtk_progress_bar_set_text(progress_bar, text);
    gtk_progress_bar_set_fraction(progress_bar, 0);

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

static void
bw_ident_manage_dialog_cb(GtkAction * action, gpointer user_data)
{
    libbalsa_identity_config_dialog(GTK_WINDOW(user_data),
                                    &balsa_app.identities,
                                    &balsa_app.current_ident,
#if ENABLE_ESMTP
				    balsa_app.smtp_servers,
#endif /* ENABLE_ESMTP */
                                    (void(*)(gpointer))
                                    balsa_identities_changed);
}


static void
bw_mark_all_cb(GtkAction * action, gpointer data)
{
    GtkWidget *index;

    index = balsa_window_find_current_index(BALSA_WINDOW(data));
    g_return_if_fail(index != NULL);

    gtk_widget_grab_focus(index);
    balsa_window_select_all(data);
}

static void
bw_show_all_headers_tool_cb(GtkToggleAction * action, gpointer data)
{
    BalsaWindow *bw = BALSA_WINDOW(data);

    if (gtk_toggle_action_get_active(action)) {
        balsa_app.show_all_headers = TRUE;
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            HEADERS_ALL);
    } else {
        balsa_app.show_all_headers = FALSE;
        balsa_message_set_displayed_headers(BALSA_MESSAGE(bw->preview),
                                            balsa_app.shown_headers);
    }
}

static void
bw_reset_show_all_headers(BalsaWindow * window)
{
    if (balsa_app.show_all_headers) {
        bw_set_active(window, "ShowAllHeaders", FALSE, TRUE);
        balsa_app.show_all_headers = FALSE;
    }
}

static void
bw_show_preview_pane_cb(GtkToggleAction * action, gpointer data)
{
    balsa_app.previewpane = gtk_toggle_action_get_active(action);
    balsa_window_refresh(BALSA_WINDOW(data));
}

/* browse_wrap can also be changed in the preferences window
 *
 * update_view_menu is called to synchronize the view menu check item
 * */
void
update_view_menu(BalsaWindow * window)
{
#if !defined(ENABLE_TOUCH_UI)
    bw_set_active(window, "Wrap", balsa_app.browse_wrap, TRUE);
    balsa_message_set_wrap(BALSA_MESSAGE(window->preview),
                           balsa_app.browse_wrap);
#endif /* ENABLE_TOUCH_UI */
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
		balsa_index_update_tree((BalsaIndex *) focus_widget, TRUE);
	    else
		gtk_tree_view_expand_all((GtkTreeView *) focus_widget);
            gtk_tree_selection_select_all(selection);
	}
#ifdef    HAVE_GTKHTML
    } else if (libbalsa_html_can_select(focus_widget)) {
	libbalsa_html_select_all(focus_widget);
#endif /* HAVE_GTKHTML */
    }
}
