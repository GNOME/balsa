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
#include "balsa-app.h"
#include "balsa-index.h"
#include "balsa-message.h"
#include "main-window.h"
#include "message-window.h"
#include "misc.h"
#include "balsa-index-page.h"


enum
{
  LAST_SIGNAL
};

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

static void index_child_setup_dnd ( GnomeMDIChild * child );

/* -- end of DND declarations */


/* callbacks */
static void index_select_cb (GtkWidget * widget, Message * message, GdkEventButton *, gpointer data);
static GtkWidget *create_menu (BalsaIndex * bindex);
static void index_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer data);

/* menu item callbacks */

static void message_status_set_new_cb (GtkWidget *, Message *);
static void message_status_set_read_cb (GtkWidget *, Message *);
static void message_status_set_answered_cb (GtkWidget *, Message *);
static void delete_message_cb (GtkWidget *, BalsaIndex *);
static void undelete_message_cb (GtkWidget *, BalsaIndex *);
static void transfer_messages_cb (BalsaMBList *, Mailbox *, GtkCTreeNode *, GdkEventButton *, BalsaIndex *);


static GtkObjectClass *parent_class = NULL;
static guint signals[LAST_SIGNAL] = { 0 };

static void balsa_index_page_class_init(BalsaIndexPageClass *class);
static void balsa_index_page_init(BalsaIndexPage *page);

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

  //  object_class->destroy = index_child_destroy;
  /*PKGW*/
  object_class->destroy = balsa_index_page_close_and_destroy;

  parent_class = gtk_type_class(GTK_TYPE_OBJECT);
}

static void
balsa_index_page_init(BalsaIndexPage *page)
{

}

GtkObject *balsa_index_page_new(BalsaWindow *window)
{
  BalsaIndexPage *bip;
  GtkWidget *sw;
  GtkWidget *index;
  GtkAdjustment *vadj, *hadj;

  bip = gtk_type_new(BALSA_TYPE_INDEX_PAGE);
 
  sw = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (sw),
				 GTK_POLICY_AUTOMATIC,
				 GTK_POLICY_AUTOMATIC);
  index = balsa_index_new ();
  //  gtk_widget_set_usize (index, -1, 200);
  gtk_container_add(GTK_CONTAINER(sw), index);

  gtk_widget_show(index);
  gtk_widget_show(sw);

  gtk_signal_connect(GTK_OBJECT(index), "select_message",
		     (GtkSignalFunc) index_select_cb, bip);
  
  gtk_signal_connect(GTK_OBJECT (index), "button_press_event",
		     (GtkSignalFunc) index_button_press_cb, bip);
  
  /* setup the dnd stuff for the messages */
  gtk_object_set(GTK_OBJECT(index), "use_drag_icons", FALSE, NULL);
  gtk_object_set(GTK_OBJECT(index), "reorderable", FALSE, NULL);
  // XXX
  //  index_child_setup_dnd(child);

  bip->window = window;
  bip->index = index;
  bip->sw = sw;

  return GTK_OBJECT(bip);
}



static void
set_password (GtkWidget * widget, GtkWidget * entry)
{
  Mailbox *mailbox;

  mailbox = gtk_object_get_data(GTK_OBJECT(entry), "mailbox");

  if (!mailbox)
    return;

  if (MAILBOX_IS_IMAP(mailbox))
    MAILBOX_IMAP(mailbox)->server->passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

  if (MAILBOX_IS_POP3(mailbox))
    MAILBOX_POP3(mailbox)->server->passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));

  gtk_object_remove_data (GTK_OBJECT (entry), "mailbox");
}


void balsa_index_page_load_mailbox(BalsaIndexPage *page, Mailbox * mailbox)
{
  GtkWidget *messagebox;
  GdkCursor *cursor;

  page->mailbox = mailbox;

#if 0
  // XXX
  cursor = gdk_cursor_new(GDK_WATCH);
  balsa_window_set_cursor(page, cursor);
  gdk_cursor_destroy(cursor);
#endif

  if ((mailbox->type == MAILBOX_IMAP && !MAILBOX_IMAP(mailbox)->server->passwd) ||
      (mailbox->type == MAILBOX_POP3 && !MAILBOX_POP3(mailbox)->server->passwd))
  {
    GtkWidget *hbox;
    GtkWidget *label;
    GtkWidget *dialog;
    GtkWidget *entry;

    dialog = gnome_dialog_new(_("Mailbox password:"),
			       GNOME_STOCK_BUTTON_OK, GNOME_STOCK_BUTTON_CANCEL, NULL);

    hbox = gtk_hbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX(GNOME_DIALOG(dialog)->vbox), hbox, FALSE, FALSE, 10);

    label = gtk_label_new(_("Password:"));
    gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, FALSE, 10);

    entry = gtk_entry_new ();
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_object_set_data(GTK_OBJECT(entry), "mailbox", mailbox);
    gtk_box_pack_start(GTK_BOX(hbox), entry, FALSE, FALSE, 10);

    gtk_widget_show_all(dialog);
    gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, set_password, entry);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gnome_dialog_run(GNOME_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    dialog = NULL;
  }

  /* check to see if its still null */
  if ((mailbox->type == MAILBOX_IMAP && !MAILBOX_IMAP(mailbox)->server->passwd) ||
      (mailbox->type == MAILBOX_POP3 && !MAILBOX_POP3(mailbox)->server->passwd))
  {
    return;
  }

  if (!mailbox_open_ref(mailbox))
  {
    messagebox = gnome_message_box_new(_("Unable to Open Mailbox!"),
					GNOME_MESSAGE_BOX_ERROR,
					GNOME_STOCK_BUTTON_OK,
					NULL);
    gtk_widget_set_usize (messagebox, MESSAGEBOX_WIDTH, MESSAGEBOX_HEIGHT);
    gtk_window_set_position (GTK_WINDOW (messagebox), GTK_WIN_POS_CENTER);
    gtk_widget_show (messagebox);
    return;
  }

  balsa_index_set_mailbox(BALSA_INDEX(page->index), mailbox);
}

/* PKGW: you'd think this function would be a good idea. 
We assume that we've been detached from the notebook.
*/
void balsa_index_page_close_and_destroy( BalsaIndexPage *page )
{
    g_return_if_fail( page );

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
	mailbox_open_unref( page->mailbox );
	page->mailbox = NULL;
    }
}

static gint handler = 0;


static gboolean
idle_handler_cb(GtkWidget * widget)
{
  GdkEventButton *bevent;
  BalsaMessage *bmsg;
  Message *message;
  gpointer data;

  bevent = gtk_object_get_data(GTK_OBJECT (widget), "bevent");
  message = gtk_object_get_data(GTK_OBJECT (widget), "message");
  data = gtk_object_get_data(GTK_OBJECT (widget), "data");

  /* get the preview pane from the index page's BalsaWindow parent */
  bmsg = BALSA_MESSAGE(BALSA_WINDOW(BALSA_INDEX_PAGE(data)->window)->preview);

  if (bevent && bevent->button == 3)
  {
    gtk_menu_popup (GTK_MENU(create_menu(BALSA_INDEX(widget))),
		    NULL, NULL, NULL, NULL,
		    bevent->button, bevent->time);
  } else if (bmsg) {
      if (BALSA_MESSAGE(bmsg)) {
	  balsa_message_set(BALSA_MESSAGE(bmsg), message);
	  message_read( message );
      }
  }

  handler = 0;

  gtk_object_remove_data (GTK_OBJECT (widget), "bevent");
  gtk_object_remove_data (GTK_OBJECT (widget), "message");
  gtk_object_remove_data (GTK_OBJECT (widget), "data");
  return FALSE;
}

static void
index_select_cb (GtkWidget * widget,
		 Message * message,
		 GdkEventButton * bevent,
		 gpointer data)
{
    g_return_if_fail (widget != NULL);
    g_return_if_fail (BALSA_IS_INDEX (widget));
    g_return_if_fail (message != NULL);
    
    set_imap_username (message->mailbox);

    gtk_object_set_data (GTK_OBJECT (widget), "message", message);
    gtk_object_set_data (GTK_OBJECT (widget), "bevent", bevent);
    gtk_object_set_data (GTK_OBJECT (widget), "data", data);

    /* this way we only display one message, not lots and lots */
    if (!handler) {
	handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);
    }
}


static void
index_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
  gint on_message;
  guint row, column;
  Message *current_message;
  GtkCList *clist;
  Mailbox *mailbox;

  clist = GTK_CLIST(widget);
  on_message=gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  
  if (on_message)
  {
    mailbox = gtk_clist_get_row_data (clist, row);
    current_message = (Message *) gtk_clist_get_row_data (clist, row);
    if (event && event->type == GDK_2BUTTON_PRESS)
    {
      message_window_new (current_message);
      return;
    } 
  }
}

/*
 * CLIST Callbacks
 */
static GtkWidget *
create_menu (BalsaIndex * bindex)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  GtkWidget *bmbl;

  menu = gtk_menu_new ();
  menuitem = gtk_menu_item_new_with_label (_ ("Transfer"));

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new ();
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

#if 0				/* FIXME */
  menuitem = gtk_menu_item_new_with_label (_ ("Change Status"));

  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new_with_label (_ ("Unread"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		      (GtkSignalFunc) message_status_set_new_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Read"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		      (GtkSignalFunc) message_status_set_read_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Replied"));
  gtk_signal_connect (GTK_OBJECT (smenuitem), "activate",
		   (GtkSignalFunc) message_status_set_answered_cb, message);
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  smenuitem = gtk_menu_item_new_with_label (_ ("Forwarded"));
  gtk_menu_append (GTK_MENU (submenu), smenuitem);
  gtk_widget_show (smenuitem);

  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

#endif
  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) delete_message_cb,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_UNDELETE, _ ("Undelete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) undelete_message_cb,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  return menu;
}


static void
message_status_set_new_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_unread (message);
  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_read_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_read (message);

  /* balsa_index_select_next (BALSA_INDEX (mainwindow->index)); */
}

static void
message_status_set_answered_cb (GtkWidget * widget, Message * message)
{
  g_return_if_fail (widget != NULL);

  message_reply (message);
}

static void
transfer_messages_cb (BalsaMBList * bmbl, Mailbox * mailbox, GtkCTreeNode * row, GdkEventButton * event, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;

  g_return_if_fail (bmbl != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      message_move (message, mailbox);
      list = list->next;
    }
}


static void
delete_message_cb (GtkWidget * widget, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;
  gint i = 0;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      message_delete (message);
      i++;
      list = list->next;
    }

  if (i == 1)
    balsa_index_select_next (bindex);
}

static void
undelete_message_cb (GtkWidget * widget, BalsaIndex * bindex)
{
  GtkCList *clist;
  GList *list;
  Message *message;
  gint i = 0;

  g_return_if_fail (widget != NULL);
  g_return_if_fail (bindex != NULL);

  clist = GTK_CLIST (bindex);
  list = clist->selection;
  while (list)
    {
      message = gtk_clist_get_row_data (clist, GPOINTER_TO_INT (list->data));
      message_undelete (message);
      list = list->next;
    }

  if (i == 1)
    balsa_index_select_next (bindex);
}


/* DND features                                              */

/*--*/
/* forward declaration of the dnd callbacks */
static void index_child_drag_data_get (GtkWidget *widget, GdkDragContext *context,
			  GtkSelectionData *selection_data, guint info, guint32 time);
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
#if 0
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

#endif
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

  Message **message_list;
  Message *current_message;
  guint message_count;
  /*--*/
  
  clist = GTK_CLIST (widget);
  bindex = BALSA_INDEX (widget);
  
  /* retrieve the selected rows */
  balsa_index_get_selected_rows (bindex, &selected_rows, &nb_selected_rows);
  
  /* retrieve the corresponding messages */
  message_list = (Message **) g_malloc (nb_selected_rows * sizeof (Message *));
  for (message_count=0; message_count<nb_selected_rows; message_count++)
    {
      current_message = (Message *) gtk_clist_get_row_data (clist, selected_rows[message_count]);
      message_list[message_count] = current_message;
   }
  
  /* pass the message list to the selection mechanism */
  gtk_selection_data_set (selection_data,
			  selection_data->target,
			  8 * sizeof(Message *), (gchar *)message_list, nb_selected_rows * sizeof(Message *));
  g_free( message_list );
  
}
