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
#include <errno.h>
#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-icons.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "main-window.h"
#include "misc.h"
#include "balsa-index-page.h"
#include "store-address.h"


enum {
    TARGET_MESSAGES
};

static GtkTargetEntry index_drag_types[] = {
    {"x-application/x-message-list", GTK_TARGET_SAME_APP, TARGET_MESSAGES}
};

#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static void index_drag_cb (GtkWidget* widget,
                           GdkDragContext* drag_context,
                           GtkSelectionData* data,
                           guint info,
                           guint time,
                           gpointer user_data);

/* callbacks */
static void index_select_cb(GtkWidget * widget, LibBalsaMessage * message,
			    GdkEventButton *, gpointer data);
static void index_unselect_cb(GtkWidget * widget,
			      LibBalsaMessage * message, GdkEventButton *,
			      gpointer data);
static GtkWidget *create_menu(BalsaIndex * bindex);
static void index_button_press_cb(GtkWidget * widget,
				  GdkEventButton * event, gpointer data);

/* menu item callbacks */

static gint close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent * event,
				    BalsaIndex * bi);
static void transfer_messages_cb(BalsaMBList *, LibBalsaMailbox *,
				 GtkCTreeNode *, GdkEventButton *,
				 BalsaIndex *);

static void sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data);

static GtkObjectClass *parent_class = NULL;

static void balsa_index_page_class_init(BalsaIndexPageClass * class);
static void balsa_index_page_init(BalsaIndexPage * page);
void balsa_index_page_window_init(BalsaIndexPage * page);
void balsa_index_page_close_and_destroy(GtkObject * obj);

GtkType
balsa_index_page_get_type(void)
{
    static GtkType window_type = 0;

    if (!window_type) {
	static const GtkTypeInfo window_info = {
	    "BalsaIndexPage",
	    sizeof(BalsaIndexPage),
	    sizeof(BalsaIndexPageClass),
	    (GtkClassInitFunc) balsa_index_page_class_init,
	    (GtkObjectInitFunc) balsa_index_page_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	window_type = gtk_type_unique(GTK_TYPE_OBJECT, &window_info);
    }

    return window_type;
}


static void
balsa_index_page_class_init(BalsaIndexPageClass * class)
{
    GtkObjectClass *object_class;

    object_class = (GtkObjectClass *) class;

    parent_class = gtk_type_class(GTK_TYPE_OBJECT);

    /*  object_class->destroy = index_child_destroy; */
     /*PKGW*/ object_class->destroy = balsa_index_page_close_and_destroy;

}

static void
balsa_index_page_init(BalsaIndexPage * page)
{

}

GtkObject *
balsa_index_page_new(BalsaWindow * window)
{
    BalsaIndexPage *bip;

    bip = gtk_type_new(BALSA_TYPE_INDEX_PAGE);
    balsa_index_page_window_init(bip);
    bip->window = GTK_WIDGET(window);

    g_get_current_time(&bip->last_use);
    GTK_OBJECT_UNSET_FLAGS(bip->index, GTK_CAN_FOCUS);

    return GTK_OBJECT(bip);
}

void
balsa_index_page_window_init(BalsaIndexPage * bip)
{
    GtkWidget *sw;
    GtkWidget *index;

    sw = gtk_scrolled_window_new(NULL, NULL);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sw),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    index = balsa_index_new();
    /*  gtk_widget_set_usize (index, -1, 200); */
    gtk_container_add(GTK_CONTAINER(sw), index);

    gtk_widget_show(index);
    gtk_widget_show(sw);

    gtk_signal_connect(GTK_OBJECT(index), "select_message",
		       (GtkSignalFunc) index_select_cb, bip);

    gtk_signal_connect(GTK_OBJECT(index), "unselect_message",
		       (GtkSignalFunc) index_unselect_cb, bip);

    gtk_signal_connect(GTK_OBJECT(index),  "button_press_event",
		       (GtkSignalFunc) index_button_press_cb, bip);

    gtk_drag_source_set (index, 
                         GDK_BUTTON1_MASK | GDK_BUTTON2_MASK |
                         GDK_SHIFT_MASK | GDK_CONTROL_MASK,
                         index_drag_types, ELEMENTS(index_drag_types),
                         GDK_ACTION_DEFAULT | GDK_ACTION_COPY | 
                         GDK_ACTION_MOVE);

    gtk_signal_connect (GTK_OBJECT (index), "drag-data-get",
                        GTK_SIGNAL_FUNC (index_drag_cb), NULL);

    bip->index = index;
    bip->sw = sw;
}

void
balsa_index_page_reset(BalsaIndexPage * page)
{
    GtkWidget *current_page, *window;
    BalsaMailboxNode *mbnode;
    gint i;

    if (!page)
	return;

    mbnode = page->mailbox_node;
    window = page->window;

    i = gtk_notebook_current_page(GTK_NOTEBOOK(balsa_app.notebook));
    current_page =
	gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i);
    current_page =
	gtk_object_get_data(GTK_OBJECT(current_page), "indexpage");

    balsa_window_close_mbnode(BALSA_WINDOW(window), mbnode);
    balsa_window_open_mbnode(BALSA_WINDOW(window),  mbnode);

    gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook),
			  balsa_find_notebook_page_num(mbnode->mailbox));

}

gint
balsa_find_notebook_page_num(LibBalsaMailbox * mailbox)
{
    GtkWidget *cur_page;
    guint i;

    for (i = 0;
	 (cur_page =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {
	cur_page = gtk_object_get_data(GTK_OBJECT(cur_page), "indexpage");
	if (BALSA_INDEX_PAGE(cur_page)->mailbox_node->mailbox == mailbox)
	    return i;
    }

    /* didn't find a matching mailbox */
    return -1;
}

BalsaIndexPage *
balsa_find_notebook_page(LibBalsaMailbox * mailbox)
{
    GtkWidget *page;
    guint i;

    for (i = 0;
	 (page =
	  gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook), i));
	 i++) {
	page = gtk_object_get_data(GTK_OBJECT(page), "indexpage");
	if (BALSA_INDEX_PAGE(page)->mailbox_node->mailbox == mailbox)
	    return BALSA_INDEX_PAGE(page);
    }

    /* didn't find a matching mailbox */
    return NULL;
}

gboolean
balsa_index_page_load_mailbox_node(BalsaIndexPage * page,
				   BalsaMailboxNode * mbnode)
{
    GtkWidget *messagebox;

    g_return_val_if_fail(mbnode->mailbox, FALSE);
    page->mailbox_node = mbnode;
    libbalsa_mailbox_open(mbnode->mailbox, FALSE);

    if (mbnode->mailbox->open_ref == 0) {
	messagebox =
	    gnome_message_box_new(_
				  ("Unable to Open Mailbox!\nPlease check the mailbox settings."),
				  GNOME_MESSAGE_BOX_ERROR,
				  GNOME_STOCK_BUTTON_OK, NULL);
	gtk_widget_set_usize(messagebox, MESSAGEBOX_WIDTH,
			     MESSAGEBOX_HEIGHT);
	gtk_window_set_position(GTK_WINDOW(messagebox),
				GTK_WIN_POS_CENTER);
	gtk_widget_show(messagebox);
	page->mailbox_node = NULL;
	return TRUE;
    }

    balsa_index_set_mailbox(BALSA_INDEX(page->index), mbnode->mailbox);
    return FALSE;
}

/* balsa_index_page_close_and_destroy:
 * destroy method - closes the page/associated mailbox and destroys it.
 * We assume that we've been detached from the notebook.
 * NOTE: there are some extreme situations (like index was created but 
 * opening mailbox failed) when the index does not have  associated 
 * mailbox mailbox and this is why we need to test before disconnecting 
 * the signal.
 * [FIXME] pawels wonders if it is possible that mailbox is destroyed first
 * and the page is closed later: this would call this function with bogus
 * pointer to the destroyed mailbox.
 */
void
balsa_index_page_close_and_destroy(GtkObject * obj)
{
    BalsaIndexPage *page;

    g_return_if_fail(obj);
    page = BALSA_INDEX_PAGE(obj);

    /*    printf( "Close and destroy!\n" ); */

    if (page->index) {
	if(BALSA_INDEX(page->index)->mailbox)
	    gtk_signal_disconnect_by_data((GTK_OBJECT(
		BALSA_INDEX(page->index)->mailbox)), BALSA_INDEX(page->index));

	gtk_widget_destroy(GTK_WIDGET(page->index));
	page->index = NULL;
    }

    if (page->sw) {
	gtk_widget_destroy(GTK_WIDGET(page->sw));
	page->sw = NULL;
    }

    /*page->window references our owner */

    if (page->mailbox_node && page->mailbox_node->mailbox) {
	libbalsa_mailbox_close(page->mailbox_node->mailbox);

	page->mailbox_node = NULL;
    }

    if (parent_class->destroy)
	(*parent_class->destroy) (obj);
}

static gint handler = 0;


/*
 * This is an idle handler, be sure to call use gdk_threads_{enter/leave}
 */
static gboolean
idle_handler_cb(GtkWidget * widget)
{
    BalsaMessage *bmsg;
    LibBalsaMessage *message;
    gpointer data;

    if (handler == 0)
        return FALSE;

    gdk_threads_enter();

    message = gtk_object_get_data(GTK_OBJECT(widget), "message");
    data = gtk_object_get_data(GTK_OBJECT(widget), "data");

    if (!data) {
	handler = 0;
	gdk_threads_leave();
	return FALSE;
    }

    /* get the preview pane from the index page's BalsaWindow parent */
    bmsg =
	BALSA_MESSAGE(BALSA_WINDOW
		      (BALSA_INDEX_PAGE(data)->window)->preview);

    if (bmsg && BALSA_MESSAGE(bmsg)) {
	if (message)
	    balsa_message_set(BALSA_MESSAGE(bmsg), message);
	else
	    balsa_message_clear(BALSA_MESSAGE(bmsg));
    }

    handler = 0;

    if (message)
	gtk_object_unref(GTK_OBJECT(message));

    gtk_object_unref(GTK_OBJECT(data));
    gtk_object_remove_data(GTK_OBJECT(widget), "message");
    gtk_object_remove_data(GTK_OBJECT(widget), "data");

    /* Update the style and message counts in the mailbox list */
    /* ijc: Are both of these needed now */
    /* MBG: I don't think so */
    balsa_mblist_update_mailbox(balsa_app.mblist,
				BALSA_INDEX(widget)->mailbox);

    gdk_threads_leave();

    return FALSE;
}

/* replace_attached_data: 
   ref messages so the don't get destroyed in meantime.  
   QUESTION: is it possible that the idle is scheduled but
   then entire balsa-index object is destroyed before the idle
   function is executed? One can first try to handle all pending
   messages before closing...
*/

static void
replace_attached_data(GtkObject * obj, const gchar * key, GtkObject * data)
{
    GtkObject *old;
    if ((old = gtk_object_get_data(obj, key)))
	gtk_object_unref(old);

    gtk_object_set_data(obj, key, data);

    if (data)
	gtk_object_ref(data);
}

void
balsa_index_update_message(BalsaIndexPage * index_page)
{
    GtkObject *message;
    BalsaIndex *index;
    GtkCList *list;

    index = BALSA_INDEX(index_page->index);
    list = GTK_CLIST(index);
    if (g_list_find(list->selection, (gpointer) list->focus_row) == NULL)
	message = NULL;
    else
	message =
	    GTK_OBJECT(gtk_clist_get_row_data(list, list->focus_row));

    replace_attached_data(GTK_OBJECT(index), "message", message);
    replace_attached_data(GTK_OBJECT(index), "data",
			  GTK_OBJECT(index_page));

    if(handler) gtk_idle_remove(handler);
    handler = gtk_idle_add((GtkFunction) idle_handler_cb, index);
}

static void
index_select_cb(GtkWidget * widget, LibBalsaMessage * message,
		GdkEventButton * bevent, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(BALSA_IS_INDEX(widget));
    g_return_if_fail(message != NULL);

    if (bevent && bevent->button == 3) {
	gtk_idle_remove(handler);
	gtk_menu_popup(GTK_MENU(create_menu(BALSA_INDEX(widget))),
		       NULL, NULL, NULL, NULL,
		       bevent->button, bevent->time);
    } else {
	replace_attached_data(GTK_OBJECT(widget), "message",
			      GTK_OBJECT(message));
	replace_attached_data(GTK_OBJECT(widget), "data",
			      GTK_OBJECT(data));
	handler = gtk_idle_add((GtkFunction) idle_handler_cb, widget);
    }
}

static void
index_unselect_cb(GtkWidget * widget, LibBalsaMessage * message,
		  GdkEventButton * bevent, gpointer data)
{
    g_return_if_fail(widget != NULL);
    g_return_if_fail(BALSA_IS_INDEX(widget));
    g_return_if_fail(message != NULL);

    if (g_list_find(GTK_CLIST(widget)->selection,
		    (gpointer) GTK_CLIST(widget)->focus_row) != NULL)
	return;

    if (bevent && bevent->button == 3) {
	gtk_idle_remove(handler);
	gtk_menu_popup(GTK_MENU(create_menu(BALSA_INDEX(widget))),
		       NULL, NULL, NULL, NULL,
		       bevent->button, bevent->time);
    } else {
	replace_attached_data(GTK_OBJECT(widget), "message", NULL);
	replace_attached_data(GTK_OBJECT(widget), "data",
			      GTK_OBJECT(data));
	handler = gtk_idle_add((GtkFunction) idle_handler_cb, widget);
    }
}


static void
index_button_press_cb(GtkWidget * widget, GdkEventButton * event,
		      gpointer data)
{
    gint on_message;
    guint row, column;
    LibBalsaMessage *current_message;
    GtkCList *clist;
    /* LibBalsaMailbox *mailbox; */

    clist = GTK_CLIST(widget);
    on_message =
	gtk_clist_get_selection_info(clist, event->x, event->y, &row,
				     &column);

    if (on_message) {
	/* mailbox = gtk_clist_get_row_data(clist, row); */
	current_message =
	    LIBBALSA_MESSAGE(gtk_clist_get_row_data(clist, row));

	if (event && event->button == 1
	    && event->type == GDK_2BUTTON_PRESS) {
	    message_window_new(current_message);
	    return;
	}
    }
}

static void
create_stock_menu_item(GtkWidget * menu, const gchar * type,
		       const gchar * label, GtkSignalFunc cb,
		       gpointer data, gboolean sensitive)
{
    GtkWidget *menuitem = gnome_stock_menu_item(type, label);
    gtk_widget_set_sensitive(menuitem, sensitive);

    gtk_signal_connect(GTK_OBJECT(menuitem),
		       "activate", (GtkSignalFunc) cb, data);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);
}

static GtkWidget *
create_menu(BalsaIndex * bindex)
{
    GtkWidget *menu, *menuitem, *submenu, *smenuitem;
    GtkWidget *bmbl, *scroll;
    GtkRequisition req;

    BALSA_DEBUG();

    menu = gtk_menu_new();

    create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply..."),
			   balsa_message_reply, bindex, TRUE);

    create_stock_menu_item(menu, BALSA_PIXMAP_MAIL_RPL_ALL_MENU,
			   _("Reply to All..."), balsa_message_replytoall,
			   bindex, TRUE);

    create_stock_menu_item(menu, BALSA_PIXMAP_MAIL_RPL_ALL_MENU,
			   _("Reply to Group..."), balsa_message_replytogroup,
			   bindex, TRUE);

    create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_FWD,
			   _("Forward..."), balsa_message_forward, bindex,
			   TRUE);

    if (bindex->mailbox == balsa_app.trash) {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE,
			       _("Undelete"), balsa_message_undelete,
			       bindex, !bindex->mailbox->readonly);
	create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE,
			       _("Delete"), balsa_message_delete, bindex,
			       !bindex->mailbox->readonly);
    } else {
	create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH,
			       _("Move to Trash"), balsa_message_delete,
			       bindex, !bindex->mailbox->readonly);
    }

    create_stock_menu_item(menu, GNOME_STOCK_MENU_BOOK_RED,
			   _("Store Address..."),
			   balsa_store_address, bindex, TRUE);

    menuitem = gtk_menu_item_new_with_label(_("Toggle"));
    submenu = gtk_menu_new();
    
    create_stock_menu_item( submenu, BALSA_PIXMAP_FLAGGED, _("Flagged"),
			    balsa_message_toggle_flagged, bindex, TRUE);
     
    create_stock_menu_item( submenu, BALSA_PIXMAP_ENVELOPE, _("Unread"),
		    balsa_message_toggle_new, bindex, TRUE);

    gtk_widget_show(submenu);

    gtk_widget_show(menuitem);
    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);

    gtk_menu_append(GTK_MENU(menu), menuitem);
    
    menuitem = gtk_menu_item_new_with_label(_("Transfer"));
    gtk_widget_set_sensitive(menuitem, !bindex->mailbox->readonly);
    submenu = gtk_menu_new();

    smenuitem = gtk_menu_item_new();
    gtk_signal_connect (GTK_OBJECT(smenuitem), "button_release_event",
                        (GtkSignalFunc) close_if_transferred_cb,
                        (gpointer) bindex);

    scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll), 
                                    GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

    bmbl = balsa_mblist_new();
    gtk_signal_connect(GTK_OBJECT(bmbl), "select_mailbox",
		       (GtkSignalFunc) transfer_messages_cb,
		       (gpointer) bindex);
    
    /* Force the mailbox list to be a reasonable size. */
    gtk_widget_size_request(bmbl, &req);
    if ( req.height > balsa_app.mw_height )
	req.height = balsa_app.mw_height;
    if ( req.width > gdk_screen_width() ) 
	req.width = gdk_screen_width() - 2*GTK_CONTAINER(scroll)->border_width; 
    gtk_widget_set_usize(GTK_WIDGET(bmbl), req.width, req.height);

    gtk_container_add(GTK_CONTAINER(scroll), bmbl);
    gtk_container_add(GTK_CONTAINER(smenuitem), scroll);
    gtk_menu_append(GTK_MENU(submenu), smenuitem);

    gtk_widget_show(bmbl);
    gtk_widget_show(scroll);
    gtk_widget_show(smenuitem);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menuitem), submenu);
    gtk_menu_append(GTK_MENU(menu), menuitem);
    gtk_widget_show(menuitem);

    return menu;
}

static gint
close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent * event,
			BalsaIndex * bi)
{
    if (gtk_object_get_data(GTK_OBJECT(bi), "transferredp") == NULL) {
        return TRUE;
    } else {
        gtk_object_remove_data (GTK_OBJECT (bi), "transferredp");
        return FALSE;
    }
}

static void
transfer_messages_cb(BalsaMBList * bmbl, LibBalsaMailbox * mailbox,
		     GtkCTreeNode * row, GdkEventButton * event,
		     BalsaIndex * bindex)
{
    GtkCList *clist;
    BalsaIndexPage *page = NULL;
    GList *list;
    LibBalsaMessage *message;

    g_return_if_fail(bmbl != NULL);
    g_return_if_fail(bindex != NULL);

    clist = GTK_CLIST(bindex);

    /*FIXME: This is a bit messy :-) */
    page =
	BALSA_INDEX_PAGE(gtk_object_get_data
			 (GTK_OBJECT
			  (gtk_notebook_get_nth_page
			   (GTK_NOTEBOOK(balsa_app.notebook),
			    gtk_notebook_get_current_page(GTK_NOTEBOOK
							  (balsa_app.notebook)))),
			  "indexpage"));
    if (page->mailbox_node->mailbox == mailbox)	
        /*Transferring to same mailbox? */
	return;

    {
	GList *messages=NULL;
	list = clist->selection;
	while (list) {
	    message =
		gtk_ctree_node_get_row_data(GTK_CTREE(bindex), list->data);
	    messages=g_list_append(messages, message);
	    list = list->next;
	}
	if(messages!=NULL){
 	  libbalsa_messages_move(messages, mailbox);
	  g_list_free(messages);
	}
    }

    libbalsa_mailbox_commit_changes(bindex->mailbox);

    if ((page = balsa_find_notebook_page(mailbox)))
	balsa_index_page_reset(page);

    gtk_object_set_data(GTK_OBJECT(bindex), "transferredp", (gpointer) 1);
}


void
balsa_message_reply(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}

void
balsa_message_replytoall(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY_ALL);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}

void
balsa_message_replytogroup(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	sm = sendmsg_window_new(widget, message, SEND_REPLY_GROUP);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}

void
balsa_message_forward(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	sm = sendmsg_window_new(widget, message, SEND_FORWARD);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_continue(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    BalsaSendmsg *sm;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	sm = sendmsg_window_new(widget, message, SEND_CONTINUE);
	gtk_signal_connect(GTK_OBJECT(sm->window), "destroy",
			   GTK_SIGNAL_FUNC(sendmsg_window_destroy_cb),
			   NULL);
	list = list->next;
    }
}


void
balsa_message_next(GtkWidget * widget, gpointer index)
{
    g_return_if_fail(index != NULL);
    balsa_index_select_next(index);
}

void
balsa_message_next_unread(GtkWidget * widget, gpointer index)
{
    g_return_if_fail(index != NULL);
    balsa_index_select_next_unread(index);
}

void
balsa_message_previous(GtkWidget * widget, gpointer index)
{
    g_return_if_fail(index != NULL);
    balsa_index_select_previous(index);
}


/* This function toggles the FLAGGED attribute of a list of messages
 */
void
balsa_message_toggle_flagged(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    int is_all_flagged = TRUE;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;

    /* First see if we should unselect or select */
    while (list) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(index),
					      list->data);

	if (!(message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED)) {
	    is_all_flagged = FALSE;
	    break;
	}
	list = list->next;
    }

    /* If they are all flagged, then unflag them. Otherwise, flag them all */
    list = GTK_CLIST(index)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(index),
					      list->data);

	if (is_all_flagged) {
	    libbalsa_message_unflag(message);
	} else {
	    libbalsa_message_flag(message);
	}

	list = list->next;
    }
    libbalsa_mailbox_commit_changes(BALSA_INDEX(index)->mailbox);
}

/* This function toggles the NEW attribute of a list of messages
 */
void
balsa_message_toggle_new(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;
    int is_all_read = TRUE;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;

    /* First see if we should mark as read or unread */
    while (list) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(index),
					      list->data);

	if (message->flags & LIBBALSA_MESSAGE_FLAG_NEW) {
	    is_all_read = FALSE;
	    break;
	}
	list = list->next;
    }

    /* if all read mark as new, otherwise mark as read */
    list = GTK_CLIST(index)->selection;

    while (list) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(index),
					      list->data);

	if (is_all_read) {
	    libbalsa_message_unread(message);
	} else {
	    libbalsa_message_read(message);
	}

	list = list->next;
    }
    libbalsa_mailbox_commit_changes(BALSA_INDEX(index)->mailbox);
}

void
balsa_message_delete(GtkWidget * widget, gpointer index)
{
    GList *list;
    BalsaIndexPage *page = NULL;
    LibBalsaMessage *message;
    gboolean to_trash;
    GList *messages=NULL;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;

    if (BALSA_INDEX(index)->mailbox == balsa_app.trash)
	to_trash = FALSE;
    else
	to_trash = TRUE;

    while (list) {
	message = gtk_ctree_node_get_row_data(GTK_CTREE(index),
					      list->data);
	messages=g_list_append(messages, message);
	list = list->next;
    }
    if(messages){
	if (to_trash)
	    libbalsa_messages_move(messages, balsa_app.trash);
	else
	    libbalsa_messages_delete(messages);
	g_list_free(messages);
    }
    balsa_index_select_next(index);

    libbalsa_mailbox_commit_changes(BALSA_INDEX(index)->mailbox);

    /*
     * If messages moved to trash mailbox and it's open in the
     * notebook, reset the contents.
     */
    if (to_trash == TRUE)
	if ((page = balsa_find_notebook_page(balsa_app.trash)))
	    balsa_index_page_reset(page);

    balsa_index_redraw_current(BALSA_INDEX(index));
}


void
balsa_message_undelete(GtkWidget * widget, gpointer index)
{
    GList *list;
    LibBalsaMessage *message;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;
    while (list) {
	message =
	    gtk_ctree_node_get_row_data(GTK_CTREE(index),
					list->data);
	libbalsa_message_undelete(message);
	list = list->next;
    }
    balsa_index_select_next(index);
}

static void
sendmsg_window_destroy_cb(GtkWidget * widget, gpointer data)
{
    balsa_window_enable_continue();
}


/* index_drag_cb 
 * 
 * This is the drag_data_get callback for the index widgets.  It
 * copies the list of selected messages to a pointer array, then sets
 * them as the DND data. Currently only supports DND within the
 * application.
 *  */
static void 
index_drag_cb (GtkWidget* widget, GdkDragContext* drag_context, 
               GtkSelectionData* data, guint info, guint time, 
               gpointer user_data)
{ 
    LibBalsaMessage* message;
    GPtrArray* message_array = NULL;
    GList* list = NULL;
    BalsaIndex* index;
    GtkCList* clist;
    
    
    index = BALSA_INDEX (widget);
    clist = GTK_CLIST (index);
    list = clist->selection;
    message_array = g_ptr_array_new ();
    
    while (list) {
        message = gtk_ctree_node_get_row_data (GTK_CTREE (index), list->data);
        g_ptr_array_add (message_array, message);
        list = list->next;
    }
    
    if (message_array) {
        g_ptr_array_add (message_array, NULL);
        gtk_selection_data_set (data, data->target, 8, 
                                (guchar*) message_array->pdata, 
                                (message_array->len)*sizeof (gpointer));
        /* the selection data makes a copy of the data, we 
         * can free it now. */
        g_ptr_array_free (message_array, FALSE);
    }
}

