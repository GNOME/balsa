/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1998-1999 Stuart Parmenter
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
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "misc.h"
#include "balsa-index-page.h"
#include "store-address.h"

/* #define DND_USED */
#ifdef DND_USED
/* DND declarations */
enum
{
  TARGET_MESSAGE,
};

static GtkTargetEntry drag_types[] =
{
  {"x-application-gnome/balsa", 0, TARGET_MESSAGE}
};
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static void 
index_child_setup_dnd ( GnomeMDIChild * child );
#endif

/* -- end of DND declarations */


/* callbacks */
static void index_select_cb (GtkWidget * widget, LibBalsaMessage * message, GdkEventButton *, gpointer data);
static void index_unselect_cb (GtkWidget * widget, LibBalsaMessage * message, GdkEventButton *, gpointer data);
static GtkWidget *create_menu (BalsaIndex * bindex);
static void index_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer data);

/* menu item callbacks */

static gint
close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent *event, BalsaIndex * bi);
static void transfer_messages_cb (BalsaMBList *, LibBalsaMailbox *, GtkCTreeNode *, GdkEventButton *, BalsaIndex *);

static GtkObjectClass *parent_class = NULL;

static void balsa_index_page_class_init(BalsaIndexPageClass *class);
static void balsa_index_page_init(BalsaIndexPage *page);
void balsa_index_page_window_init(BalsaIndexPage *page);
void balsa_index_page_close_and_destroy( GtkObject *obj );

GtkType
balsa_index_page_get_type (void)
{
  static GtkType window_type = 0;

  if (!window_type)
    {
      static const GtkTypeInfo window_info =
      {
	"BalsaIndexPage",
	sizeof (BalsaIndexPage),
	sizeof (BalsaIndexPageClass),
	(GtkClassInitFunc) balsa_index_page_class_init,
	(GtkObjectInitFunc) balsa_index_page_init,
        /* reserved_1 */ NULL,
	/* reserved_2 */ NULL,
	(GtkClassInitFunc) NULL,
      };

      window_type = gtk_type_unique (GTK_TYPE_OBJECT, &window_info);
    }

  return window_type;
}


static void
balsa_index_page_class_init(BalsaIndexPageClass *class)
{
  GtkObjectClass *object_class;

  object_class = (GtkObjectClass *) class;

  parent_class = gtk_type_class(GTK_TYPE_OBJECT);

  /*  object_class->destroy = index_child_destroy; */
  /*PKGW*/
  object_class->destroy = balsa_index_page_close_and_destroy;

}

static void
balsa_index_page_init(BalsaIndexPage *page)
{

}

GtkObject *balsa_index_page_new(BalsaWindow *window)
{
  BalsaIndexPage *bip;

  bip = gtk_type_new(BALSA_TYPE_INDEX_PAGE);
  balsa_index_page_window_init( bip );
  bip->window = GTK_WIDGET( window );

  g_get_current_time(&bip->last_use);
  GTK_OBJECT_UNSET_FLAGS(bip->index, GTK_CAN_FOCUS);

  return GTK_OBJECT(bip);
}

void
balsa_index_page_window_init(BalsaIndexPage *bip)
{
  GtkWidget *sw;
  GtkWidget *index;

  sw = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (sw),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);
  index = balsa_index_new ();
  /*  gtk_widget_set_usize (index, -1, 200); */
  gtk_container_add(GTK_CONTAINER(sw), index);

  gtk_widget_show(index);
  gtk_widget_show(sw);

  gtk_signal_connect (GTK_OBJECT(index), "select_message",
		     (GtkSignalFunc) index_select_cb, bip);
  
  gtk_signal_connect (GTK_OBJECT(index), "unselect_message",
		     (GtkSignalFunc) index_unselect_cb, bip);
  
  gtk_signal_connect (GTK_OBJECT (index), "button_press_event",
		     (GtkSignalFunc) index_button_press_cb, bip);
  
  /* setup the dnd stuff for the messages */
/*   gtk_object_set(GTK_OBJECT(index), "use_drag_icons", FALSE, NULL); */
/*   gtk_object_set(GTK_OBJECT(index), "reorderable", FALSE, NULL); */
  
  /* FIXME: DND support is broken */
  /* index_child_setup_dnd(child); */

  bip->index = index;
  bip->sw = sw;
}

void
balsa_index_page_reset(BalsaIndexPage *page)
{
  GtkWidget *current_page, *window;
  LibBalsaMailbox *mailbox;
  gint i;

  if( !page )
    return;

  mailbox = page->mailbox;
  window = page->window;

  i = gtk_notebook_current_page( GTK_NOTEBOOK(balsa_app.notebook));
  current_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),i);
  current_page = gtk_object_get_data(GTK_OBJECT(current_page),"indexpage");

  balsa_window_close_mailbox(BALSA_WINDOW(window), mailbox);
  balsa_window_open_mailbox(BALSA_WINDOW(window), mailbox);

  gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), balsa_find_notebook_page_num(BALSA_INDEX_PAGE(current_page)->mailbox));

}

gint
balsa_find_notebook_page_num(LibBalsaMailbox *mailbox)
{
  GtkWidget *cur_page;
  guint i;

  for (i=0;(cur_page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),i));i++)
    {
      cur_page = gtk_object_get_data(GTK_OBJECT(cur_page),"indexpage");
      if( BALSA_INDEX_PAGE(cur_page)->mailbox == mailbox)
	return i;
    }

  /* didn't find a matching mailbox */
  return -1;
}

BalsaIndexPage *
balsa_find_notebook_page(LibBalsaMailbox *mailbox)
{
    GtkWidget *page;
    guint i;

    for(i=0;(page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),i));i++)
      {
	page = gtk_object_get_data(GTK_OBJECT(page),"indexpage");
	if( BALSA_INDEX_PAGE(page)->mailbox == mailbox)
	  return BALSA_INDEX_PAGE(page);
      }

    /* didn't find a matching mailbox */
    return NULL;
}

gboolean balsa_index_page_load_mailbox(BalsaIndexPage *page, LibBalsaMailbox * mailbox)
{
  GtkWidget *messagebox;

  page->mailbox = mailbox;
  libbalsa_mailbox_open(mailbox, FALSE);
  
  if ( mailbox->open_ref == 0 )
  {
      messagebox = gnome_message_box_new(
	  _("Unable to Open Mailbox!\nPlease check the mailbox settings."),
	  GNOME_MESSAGE_BOX_ERROR,
	  GNOME_STOCK_BUTTON_OK,
	  NULL);
      gtk_widget_set_usize (messagebox, MESSAGEBOX_WIDTH, MESSAGEBOX_HEIGHT);
      gtk_window_set_position (GTK_WINDOW (messagebox), GTK_WIN_POS_CENTER);
      gtk_widget_show (messagebox);
      page->mailbox = NULL;
      return TRUE;
  }

  balsa_index_set_mailbox(BALSA_INDEX(page->index), mailbox);
  return FALSE;
}

/* PKGW: you'd think this function would be a good idea. 
 * We assume that we've been detached from the notebook.
 */
void balsa_index_page_close_and_destroy( GtkObject *obj )
{
  BalsaIndexPage *page;

  g_return_if_fail( obj );
  page = BALSA_INDEX_PAGE( obj );

/*    printf( "Close and destroy!\n" );*/

  if( page->index ) {
    gtk_widget_destroy( GTK_WIDGET( page->index ) );
    page->index = NULL;
  }

  if( page->sw ) {
    gtk_widget_destroy( GTK_WIDGET( page->sw ) );
    page->sw = NULL;
  }
	
  /*page->window references our owner*/
	
  if( page->mailbox ) {
    libbalsa_mailbox_close (page->mailbox);

    page->mailbox = NULL;
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

  gdk_threads_enter();

  message = gtk_object_get_data(GTK_OBJECT (widget), "message");
  data = gtk_object_get_data(GTK_OBJECT (widget), "data");
  
  if (!data) {
    handler = 0;
    gdk_threads_leave();
    return FALSE;
  }
  
  /* get the preview pane from the index page's BalsaWindow parent */
  bmsg = BALSA_MESSAGE(BALSA_WINDOW(BALSA_INDEX_PAGE(data)->window)->preview);
  
  if (bmsg && BALSA_MESSAGE(bmsg)) {
      if (message)
        balsa_message_set (BALSA_MESSAGE(bmsg), message);
      else
        balsa_message_clear (BALSA_MESSAGE (bmsg));
  }
  
  handler = 0;

  gnome_appbar_pop (balsa_app.appbar);
  if(message)
    gtk_object_unref(GTK_OBJECT(message));
  gtk_object_unref(GTK_OBJECT(data));
  /* Update the style and message counts in the mailbox list */
  /* ijc: Are both of these needed now */
  balsa_mblist_update_mailbox (balsa_app.mblist,
			       BALSA_INDEX (widget)->mailbox);
  balsa_mblist_have_new (balsa_app.mblist);

  gtk_object_remove_data (GTK_OBJECT (widget), "message");
  gtk_object_remove_data (GTK_OBJECT (widget), "data");

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
replace_attached_data(GtkObject *obj,const gchar *key, GtkObject* data)
{
  GtkObject *old;
  if( (old=gtk_object_get_data (obj, key)) )
    gtk_object_unref(old);
  gtk_object_set_data (obj, key, data);
  if(data)
    gtk_object_ref(data);
}

void
balsa_index_update_message (BalsaIndexPage *index_page)
{
  GtkObject *message;
  BalsaIndex *index;
  GtkCList *list;
  
  index = BALSA_INDEX (index_page->index);
  list = GTK_CLIST (index);
  if (g_list_find (list->selection, (gpointer) list->focus_row) == NULL)
    message = NULL;
  else
    message = GTK_OBJECT (gtk_clist_get_row_data (list, list->focus_row));
  
  replace_attached_data (GTK_OBJECT (index), "message", message);
  replace_attached_data (GTK_OBJECT (index), "data", GTK_OBJECT(index_page));
  
  handler = gtk_idle_add ((GtkFunction) idle_handler_cb, index);
}

static void
index_select_cb (GtkWidget * widget,
		 LibBalsaMessage * message,
		 GdkEventButton * bevent,
		 gpointer data)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);
  
  if (bevent && bevent->button == 3) {
    gtk_idle_remove(handler);
    gtk_menu_popup (GTK_MENU(create_menu(BALSA_INDEX(widget))),
		    NULL, NULL, NULL, NULL,
		    bevent->button, bevent->time);
  } else {
    replace_attached_data (GTK_OBJECT (widget), "message",GTK_OBJECT(message));
    replace_attached_data (GTK_OBJECT (widget), "data", GTK_OBJECT(data));
    handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);
  }
}

static void
index_unselect_cb (GtkWidget * widget,
                   LibBalsaMessage * message,
                   GdkEventButton * bevent,
                   gpointer data)
{
  g_return_if_fail (widget != NULL);
  g_return_if_fail (BALSA_IS_INDEX (widget));
  g_return_if_fail (message != NULL);
  
  if (g_list_find (GTK_CLIST (widget)->selection,
		   (gpointer) GTK_CLIST (widget)->focus_row) != NULL)
    return;
  
  if (bevent && bevent->button == 3) {
    gtk_idle_remove(handler);
    gtk_menu_popup (GTK_MENU(create_menu(BALSA_INDEX(widget))),
                    NULL, NULL, NULL, NULL,
                    bevent->button, bevent->time);
  } else {
    replace_attached_data (GTK_OBJECT (widget), "message", NULL);
    replace_attached_data (GTK_OBJECT (widget), "data",  GTK_OBJECT(data));
    handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);
  }
}


static void
index_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint on_message;
  guint row, column;
  LibBalsaMessage *current_message;
  GtkCList *clist;
  LibBalsaMailbox *mailbox;

  clist = GTK_CLIST(widget);
  on_message = gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  
  if (on_message)
  {
    mailbox = gtk_clist_get_row_data (clist, row);
    current_message = LIBBALSA_MESSAGE(gtk_clist_get_row_data (clist, row));

    if (event && event->button == 1 && event->type == GDK_2BUTTON_PRESS)
    {
      message_window_new (current_message);
      return;
    } 
  }
}

static void
create_stock_menu_item(GtkWidget *menu, const gchar* type, const gchar* label,
		       GtkSignalFunc cb, gpointer data, gboolean sensitive)
{
    GtkWidget * menuitem = gnome_stock_menu_item (type, label); 
    gtk_widget_set_sensitive(menuitem, sensitive);
    gtk_signal_connect (GTK_OBJECT (menuitem),
			"activate",
			(GtkSignalFunc) cb, data);
    gtk_menu_append (GTK_MENU (menu), menuitem);
    gtk_widget_show (menuitem);
}

static GtkWidget *
create_menu (BalsaIndex * bindex)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  GtkWidget *bmbl;

  BALSA_DEBUG ();
  
  menu = gtk_menu_new ();
  
  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply"),
			 balsa_message_reply, bindex, TRUE);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply to all"),
			 balsa_message_replytoall, bindex, TRUE);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_FWD, _("Forward"),
			 balsa_message_forward, bindex, TRUE);

  if ( bindex->mailbox == balsa_app.trash ) {
    create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE, _("Undelete"),
			   balsa_message_undelete, bindex, !bindex->mailbox->readonly);
    create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE, _("Delete"),
			   balsa_message_undelete, bindex, !bindex->mailbox->readonly);
  } else {
    create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH, _("Move to Trash"),
			   balsa_message_delete, bindex, !bindex->mailbox->readonly);
  }

  create_stock_menu_item(menu, GNOME_STOCK_MENU_BOOK_RED, 
			 _("Store Address"),
			 balsa_store_address, bindex, TRUE);

  menuitem = gtk_menu_item_new_with_label(_("Toggle flagged"));
  gtk_widget_set_sensitive(menuitem, !bindex->mailbox->readonly);
  gtk_signal_connect(GTK_OBJECT(menuitem),
		     "activate",
		     (GtkSignalFunc) balsa_message_toggle_flagged,
		     bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);
  
  menuitem = gtk_menu_item_new_with_label (_("Transfer"));
  gtk_widget_set_sensitive(menuitem, !bindex->mailbox->readonly);
  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new ();
  gtk_signal_connect (GTK_OBJECT (smenuitem), "button_release_event",
		      (GtkSignalFunc) close_if_transferred_cb,
		      (gpointer) bindex); 
  bmbl = balsa_mblist_new ();
  gtk_signal_connect (GTK_OBJECT (bmbl), "select_mailbox",
		      (GtkSignalFunc) transfer_messages_cb,
		      (gpointer) bindex); 
  
  gtk_widget_set_usize (GTK_WIDGET (bmbl), balsa_app.mblist_width, -1);
  gtk_container_add (GTK_CONTAINER (smenuitem), bmbl);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (bmbl);
  gtk_widget_show (smenuitem);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);
    
  return menu;
}

static gint
close_if_transferred_cb(BalsaMBList * bmbl, GdkEvent *event, BalsaIndex * bi)
{
  return gtk_object_get_data(GTK_OBJECT(bi), "transferredp") == NULL;
}

static void
transfer_messages_cb (BalsaMBList * bmbl, LibBalsaMailbox * mailbox, 
		      GtkCTreeNode * row, GdkEventButton * event, 
		      BalsaIndex * bindex)
{
	GtkCList *clist;
	BalsaIndexPage *page=NULL;
	GList *list;
	LibBalsaMessage *message;

	g_return_if_fail (bmbl != NULL);
	g_return_if_fail (bindex != NULL);

	clist = GTK_CLIST (bindex);
	
	/*This is a bit messy :-)*/
	page = BALSA_INDEX_PAGE( 
		gtk_object_get_data( GTK_OBJECT( 
			gtk_notebook_get_nth_page( GTK_NOTEBOOK( balsa_app.notebook ),
						   gtk_notebook_get_current_page( GTK_NOTEBOOK( balsa_app.notebook ) )  
						   )
				), "indexpage" ) );
	if( page->mailbox == mailbox ) /*Transferring to same mailbox?*/
		return;

	list = clist->selection;
	while (list)
	{
		message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
		libbalsa_message_move (message, mailbox);
		list = list->next;
	}
	libbalsa_mailbox_commit_changes(bindex->mailbox);
	
	if((page=balsa_find_notebook_page(mailbox)))
	  balsa_index_page_reset(page);
	gtk_object_set_data(GTK_OBJECT(bindex), "transferredp", (gpointer)1);
}

#ifdef DND_USED
/* DND features */
/*--*/
/* forward declaration of the dnd callbacks */
static void index_child_drag_data_get (GtkWidget *widget, 
                                       GdkDragContext *context, 
                                       GtkSelectionData *selection_data, 
                                       guint info, guint32 time);

/*--*/

/* 
 * index_child_setup_dnd : 
 *
 * set the drag'n drop features up
 *
 * @child: the message index window to set the dnd ability up
 */

static void 
index_child_setup_dnd ( GnomeMDIChild * child )
{
  IndexChild *ic;
  GdkPixmap *drag_pixmap;
  GdkPixmap *drag_mask;
  GdkColormap *cmap;

  ic = INDEX_CHILD(child);

  cmap = gtk_widget_get_colormap (GTK_WIDGET (ic->index));
  gnome_stock_pixmap_gdk ("Mail", "regular", &drag_pixmap, &drag_mask);
  
  gtk_drag_source_set (GTK_WIDGET (ic->index), GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
		       drag_types, ELEMENTS (drag_types),
		       GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
  gtk_drag_source_set_icon(GTK_WIDGET (ic->index), cmap, drag_pixmap, drag_mask);

  gtk_signal_connect (GTK_OBJECT (ic->index), "drag_data_get",
      GTK_SIGNAL_FUNC (index_child_drag_data_get), NULL);
  
  gdk_pixmap_unref (drag_pixmap);
  gdk_pixmap_unref (drag_mask);
}


/**
 * index_child_drag_data_get:
 *
 * Invoked when the message list is required to provide the dragged messages
 * Finds the selected row in the index clist and create a list of selected message
 * This list is then passed to the X selection system to be retrievd by the drop
 * site.
 */
static void
index_child_drag_data_get (GtkWidget *widget, GdkDragContext *context,
			  GtkSelectionData *selection_data, guint info,
			  guint32 time)
{
  /*--*/
  guint *selected_rows;
  guint nb_selected_rows;

  GtkCList *clist;
  BalsaIndex *bindex;

  LibBalsaMessage **message_list;
  LibBalsaMessage *current_message;
  guint message_count;
  /*--*/
  
  clist = GTK_CLIST (widget);
  bindex = BALSA_INDEX (widget);
  
  /* retrieve the selected rows */
  balsa_index_get_selected_rows (bindex, &selected_rows, &nb_selected_rows);
  
  /* retrieve the corresponding messages */
  message_list = (Message **) g_new (LibBalsaMessage, nb_selected_rows);
  for (message_count=0; message_count<nb_selected_rows; message_count++)
    {
      current_message = LIBBALSA_MESSAGE(gtk_clist_get_row_data (clist, selected_rows[message_count]));
      message_list[message_count] = current_message;
   }
  
  /* pass the message list to the selection mechanism */
  gtk_selection_data_set (selection_data,
			  selection_data->target,
			  8 * sizeof(LibBalsaMessage *), (gchar *)message_list, nb_selected_rows * sizeof(LibBalsaMessage *));
  g_free( message_list );
  
}
#endif /*DND_USED*/

void
balsa_message_reply (GtkWidget * widget, gpointer index)
{
  GList     *list;
  LibBalsaMessage   *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST (index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST (index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_REPLY);
    list = list->next;
  }
}

void
balsa_message_replytoall (GtkWidget * widget, gpointer index)
{
  GList     *list;
  LibBalsaMessage   *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST (index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST (index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_REPLY_ALL);
    list = list->next;
  }
}

void
balsa_message_forward (GtkWidget * widget, gpointer index)
{
  GList     *list;
  LibBalsaMessage   *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST (index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST (index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_FORWARD);
    list = list->next;
  }
}


void
balsa_message_continue (GtkWidget * widget, gpointer index)
{
  GList   *list;
  LibBalsaMessage *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    sendmsg_window_new (widget, message, SEND_CONTINUE);
    list = list->next;
  } 
}


void
balsa_message_next (GtkWidget * widget, gpointer index)
{
  g_return_if_fail(index != NULL);
  balsa_index_select_next (index);
}

void
balsa_message_next_unread (GtkWidget* widget, gpointer index)
{
  g_return_if_fail (index != NULL);
  balsa_index_select_next_unread (index);
}

void
balsa_message_previous (GtkWidget * widget, gpointer index)
{
  g_return_if_fail(index != NULL);
  balsa_index_select_previous (index);
}

 
/* This function toggles the FLAGGED attribute of a message
 */
void
balsa_message_toggle_flagged (GtkWidget * widget, gpointer index)
{
	GList   *list;
	LibBalsaMessage *message;
	int      is_all_flagged = TRUE;
 
	g_return_if_fail (widget != NULL);
	g_return_if_fail(index != NULL);

	list = GTK_CLIST(index)->selection;

	/* First see if we should unselect or select */
	while (list)
	{
		message = gtk_clist_get_row_data(GTK_CLIST(index), 
			GPOINTER_TO_INT(list->data));

		if (!(message->flags & LIBBALSA_MESSAGE_FLAG_FLAGGED))
		{
			is_all_flagged = FALSE;
			break;
		}
		list = list->next;
	}

	/* If they are all flagged, then unflag them. Otherwise, flag them all */
	list = GTK_CLIST(index)->selection;
 
	while (list)
	{
		message = gtk_clist_get_row_data(GTK_CLIST(index), 
			GPOINTER_TO_INT(list->data));

		if (is_all_flagged)
		{
			libbalsa_message_unflag(message);
		} else {
			libbalsa_message_flag(message);
		}
 
		list = list->next;
	}
	libbalsa_mailbox_commit_changes(BALSA_INDEX(index)->mailbox);
}

void
balsa_message_delete (GtkWidget * widget, gpointer index)
{
  GList   *list;
  BalsaIndexPage *page = NULL;
  LibBalsaMessage *message;
  gboolean to_trash;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (index != NULL);

  list = GTK_CLIST(index)->selection;

  if(BALSA_INDEX(index)->mailbox == balsa_app.trash)
    to_trash = FALSE;
  else
    to_trash = TRUE;

  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), 
				     GPOINTER_TO_INT(list->data));
    if(to_trash)
      libbalsa_message_move(message, balsa_app.trash);
    else
      libbalsa_message_delete(message);

    list = list->next;
  }
  balsa_index_select_next (index);

  libbalsa_mailbox_commit_changes(BALSA_INDEX(index)->mailbox);

  /*
   * If messages moved to trash mailbox and it's open in the
   * notebook, reset the contents.
   */
  if(to_trash == TRUE)
    if((page=balsa_find_notebook_page(balsa_app.trash)))
      balsa_index_page_reset( page );

}


void
balsa_message_undelete (GtkWidget * widget, gpointer index)
{
  GList   *list;
  LibBalsaMessage *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    libbalsa_message_undelete(message);
    list = list->next;
  }
  balsa_index_select_next (index);
}
