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

/*#define SIGNALS_USED*/
#ifdef SIGNALS_USED
enum
{
  LAST_SIGNAL
};
#endif


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
static void index_select_cb (GtkWidget * widget, Message * message, GdkEventButton *, gpointer data);
static void index_unselect_cb (GtkWidget * widget, Message * message, GdkEventButton *, gpointer data);
static GtkWidget *create_menu (BalsaIndex * bindex);
static void index_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer data);

/* menu item callbacks */

/*#define MSG_STATUS_USED*/
#ifdef MSG_STATUS_USED
static void message_status_set_new_cb (GtkWidget *, Message *);
static void message_status_set_read_cb (GtkWidget *, Message *);
static void message_status_set_answered_cb (GtkWidget *, Message *);
#endif

static void transfer_messages_cb (BalsaMBList *, Mailbox *, GtkCTreeNode *, GdkEventButton *, BalsaIndex *);

static void
store_address_dialog_button_clicked_cb(GtkWidget *widget, gint which, GtkWidget **entries);

static GtkObjectClass *parent_class = NULL;
#ifdef SIGNALS_USED
static guint signals[LAST_SIGNAL] = { 0 };
#endif

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

  //  object_class->destroy = index_child_destroy;
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
  //  gtk_widget_set_usize (index, -1, 200);
  gtk_container_add(GTK_CONTAINER(sw), index);

  gtk_widget_show(index);
  gtk_widget_show(sw);

  gtk_signal_connect(GTK_OBJECT(index), "select_message",
		     (GtkSignalFunc) index_select_cb, bip);
  
  gtk_signal_connect(GTK_OBJECT(index), "unselect_message",
		     (GtkSignalFunc) index_unselect_cb, bip);
  
  gtk_signal_connect(GTK_OBJECT (index), "button_press_event",
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
  Mailbox *mailbox;
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

  gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook), 
       balsa_find_notebook_page_num(BALSA_INDEX_PAGE(current_page)->mailbox));

}

gint
balsa_find_notebook_page_num(Mailbox *mailbox)
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
balsa_find_notebook_page(Mailbox *mailbox)
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


gboolean balsa_index_page_load_mailbox(BalsaIndexPage *page, Mailbox * mailbox)
{
  GtkWidget *messagebox;
  /*GdkCursor *cursor;*/

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

    gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
			     GTK_WINDOW (balsa_app.main_window));
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
    return TRUE;
  }

  if (!mailbox_open_ref(mailbox))
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
    mailbox_open_unref( page->mailbox );
    page->mailbox = NULL;
  }

  if (parent_class->destroy)
    (*parent_class->destroy) (obj);

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
  }

  if (bmsg && BALSA_MESSAGE(bmsg))
    if (message)
      balsa_message_set(BALSA_MESSAGE(bmsg), message);
    else
      balsa_message_clear (BALSA_MESSAGE (bmsg));
  
  handler = 0;

  /* Update the style and message counts in the mailbox list */
  balsa_mblist_update_mailbox (balsa_app.mblist, 
			       BALSA_INDEX(widget)->mailbox);

  gtk_object_remove_data (GTK_OBJECT (widget), "bevent");
  gtk_object_remove_data (GTK_OBJECT (widget), "message");
  gtk_object_remove_data (GTK_OBJECT (widget), "data");
  return FALSE;
}

void
balsa_index_update_message (BalsaIndexPage *index_page)
{
    Message *message;
    BalsaIndex *index;
    GtkCList *list;
    
    index = BALSA_INDEX (index_page->index);
    list = GTK_CLIST (index);
    if (g_list_find (list->selection, (gpointer) list->focus_row) == NULL)
        message = NULL;
    else
        message = (Message *) gtk_clist_get_row_data (list, list->focus_row);
    gtk_object_set_data (GTK_OBJECT (index), "message", message);
    gtk_object_set_data (GTK_OBJECT (index), "bevent", NULL);
    gtk_object_set_data (GTK_OBJECT (index), "data", index_page);

    /* this way we only display one message, not lots and lots, and
       we also avoid flicker due to consecutive unselect/select */
    if (!handler)
	handler = gtk_idle_add ((GtkFunction) idle_handler_cb, index);
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

    /* this way we only display one message, not lots and lots, and
       we also avoid flicker due to consecutive unselect/select */
    if (!handler)
	handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);
}

static void
index_unselect_cb (GtkWidget * widget,
                   Message * message,
                   GdkEventButton * bevent,
                   gpointer data)
{
    g_return_if_fail (widget != NULL);
    g_return_if_fail (BALSA_IS_INDEX (widget));
    g_return_if_fail (message != NULL);
    
    if (g_list_find (GTK_CLIST (widget)->selection,
                     (gpointer) GTK_CLIST (widget)->focus_row) != NULL)
        return;

    gtk_object_set_data (GTK_OBJECT (widget), "message", NULL);
    gtk_object_set_data (GTK_OBJECT (widget), "bevent", bevent);
    gtk_object_set_data (GTK_OBJECT (widget), "data", data);

    /* this way we only display one message, not lots and lots, and
       we also avoid flicker due to consecutive unselect/select */
    if (!handler)
	handler = gtk_idle_add ((GtkFunction) idle_handler_cb, widget);
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


#ifdef MSG_STATUS_USED
/*
 * CLIST Callbacks
 */
static void
message_status_set_new_cb (GtkWidget * widget, BalsaIndex *bindex)
{
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);
 
  list = GTK_CLIST (bindex)->selection;

  while (list)
    {
      message = gtk_clist_get_row_data (GTK_CLIST (bindex), 
      					GPOINTER_TO_INT (list->data));
      message_unread (message);
      list = list->next;
    }    	   
}
#endif

#ifdef MSG_STATUS_USED
static void
message_status_set_read_cb (GtkWidget * widget, BalsaIndex *bindex)
{
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);
 
  list = GTK_CLIST (bindex)->selection;

  while (list)
    {
      message = gtk_clist_get_row_data (GTK_CLIST (bindex), 
      					GPOINTER_TO_INT (list->data));
      
      if(message) /* FIXME: some crashes were reported with gnome-libs 1.2.0
		   * if this wasn't checked. How come? */
	  message_read (message);
      list = list->next;
    }
}
#endif

#ifdef MSG_STATUS_USED
static void
message_status_set_answered_cb (GtkWidget * widget, BalsaIndex *bindex)
{
  GList *list;
  Message *message;

  g_return_if_fail (widget != NULL);
 
  list = GTK_CLIST (bindex)->selection;

  while (list)
    {
      message = gtk_clist_get_row_data (GTK_CLIST (bindex), 
      					GPOINTER_TO_INT (list->data));
      message_reply (message);
      list = list->next;
    }
}
#endif

static void
create_stock_menu_item(GtkWidget *menu, const gchar* type, const gchar* label,
		       GtkSignalFunc cb, gpointer data)
{
    GtkWidget * menuitem = gnome_stock_menu_item (type, label); 
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
  
  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_NEW, _("New"),
			 balsa_message_new, NULL);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply"),
			 balsa_message_reply, bindex);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_RPL, _("Reply to all"),
			 balsa_message_replytoall, bindex);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_MAIL_FWD, _("Forward"),
			 balsa_message_forward, bindex);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_TRASH, _("Delete"),
			 balsa_message_delete, bindex);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_UNDELETE, _("Undelete"),
			 balsa_message_undelete, bindex);

  create_stock_menu_item(menu, GNOME_STOCK_MENU_BOOK_RED, 
			 _("Store Address"),
			 balsa_message_store_address, bindex);

	menuitem = gtk_menu_item_new_with_label(_("Toggle flagged"));
	gtk_signal_connect(GTK_OBJECT(menuitem),
			"activate",
			(GtkSignalFunc) balsa_message_toggle_flagged,
			bindex);
	gtk_menu_append (GTK_MENU (menu), menuitem);
	gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_("Transfer"));
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

/* FIXME: Still does not work? Find out why. -Knut <knut.neumann@uni-duesseldorf.de> */
/*    menuitem = gtk_menu_item_new_with_label (_ ("Change Status")); */
/*    submenu = gtk_menu_new (); */
  
/*    smenuitem = gtk_menu_item_new_with_label (_ ("Unread")); */
/*    gtk_signal_connect (GTK_OBJECT (smenuitem), "activate", */
/*  		      (GtkSignalFunc) message_status_set_new_cb,  */
/*  		      (gpointer) bindex); */
/*    gtk_menu_append (GTK_MENU (submenu), smenuitem); */
/*    gtk_widget_show (smenuitem); */

/*    smenuitem = gtk_menu_item_new_with_label (_ ("Read")); */
/*    gtk_signal_connect (GTK_OBJECT (smenuitem), "activate", */
/*  		      (GtkSignalFunc) message_status_set_read_cb,  */
/*  		      (gpointer) bindex); */
/*    gtk_menu_append (GTK_MENU (submenu), smenuitem); */
/*    gtk_widget_show (smenuitem); */

/*    smenuitem = gtk_menu_item_new_with_label (_ ("Replied")); */
/*    gtk_signal_connect (GTK_OBJECT (smenuitem), "activate", */
/*  		      (GtkSignalFunc) message_status_set_answered_cb,  */
/*  		      (gpointer) bindex); */
/*    gtk_menu_append (GTK_MENU (submenu), smenuitem); */
/*    gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), submenu); */
/*    gtk_menu_append (GTK_MENU (menu), menuitem); */
/*    gtk_widget_show (smenuitem); */

/*    gtk_widget_show (menuitem); */

  return menu;
}


static void
transfer_messages_cb (BalsaMBList * bmbl, Mailbox * mailbox, GtkCTreeNode * row, GdkEventButton * event, BalsaIndex * bindex)
{
	GtkCList *clist;
	BalsaIndexPage *page=NULL;
	GList *list;
	Message *message;

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
		message_move (message, mailbox);
		list = list->next;
	}
	mailbox_commit_flagged_changes(bindex->mailbox);
	
	if((page=balsa_find_notebook_page(mailbox)))
    balsa_index_page_reset(page);
	
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
#endif /*DND_USED*/

static void
store_address_dialog_button_clicked_cb(GtkWidget *widget, gint which, GtkWidget **entries)
{
    if(which == 0)
    {
        Contact *card = NULL;
        gint error_check = 0;
        GtkWidget *box = NULL;
        gchar * msg = NULL;
        gint cnt = 0;
        gint cnt2 = 0;
        gchar *entry_str = NULL;
        gint entry_str_len = 0;
            /* semicolons mess up how GnomeCard processes the fields, so disallow them */
        for(cnt = 0; cnt < NUM_FIELDS; cnt++)
        {
            entry_str = gtk_editable_get_chars(GTK_EDITABLE(entries[cnt]), 0, -1);
            entry_str_len = strlen(entry_str);
            
            for(cnt2 = 0; cnt2 < entry_str_len; cnt2++)
            {
                if(entry_str[cnt2] == ';')
                {
                    msg = _("Sorry, no semicolons are allowed in the name!\n");
                    gtk_editable_select_region(GTK_EDITABLE(entries[cnt]), 0, -1);
                    gtk_widget_grab_focus(GTK_WIDGET(entries[cnt]));
                    box = gnome_message_box_new(msg,
                                                GNOME_MESSAGE_BOX_ERROR,
                                                GNOME_STOCK_BUTTON_OK, NULL );
                    gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
                    gnome_dialog_run_and_close( GNOME_DIALOG( box ) );
                    g_free(entry_str);
                    return;
                }
            }
            g_free(entry_str);
        }
        
        card = contact_new();
        card->card_name = g_strstrip(gtk_editable_get_chars(
	    GTK_EDITABLE(entries[CARD_NAME]), 0, -1));
        card->first_name = g_strstrip(gtk_editable_get_chars(
	    GTK_EDITABLE(entries[FIRST_NAME]), 0, -1));
        card->last_name = g_strstrip(gtk_editable_get_chars(
	    GTK_EDITABLE(entries[LAST_NAME]), 0, -1));
        card->organization = g_strstrip(gtk_editable_get_chars(
	    GTK_EDITABLE(entries[ORGANIZATION]), 0, -1));
        card->email_address = g_strstrip(gtk_editable_get_chars(
	    GTK_EDITABLE(entries[EMAIL_ADDRESS]), 0, -1));

        error_check = contact_store(card);

        if(error_check == CONTACT_UNABLE_TO_OPEN_GNOMECARD_FILE)
        {
            msg  = g_strdup_printf(
                _("Unable to open ~/.gnome/GnomeCard.gcrd for read.\n - %s\n"),
                g_unix_error_string(errno)); 
        }
        else if(error_check == CONTACT_CARD_NAME_FIELD_EMPTY)
        {
            msg = g_strdup( _("The Card Name field must be non-empty.\n"));
            gtk_widget_grab_focus(GTK_WIDGET(entries[CARD_NAME]));
        }
        else if(error_check == CONTACT_CARD_NAME_EXISTS)
        {
            msg = g_strdup_printf(
                _("There is already an address book entry for %s.\nRun GnomeCard if you would like to edit your address book entries.\n"),
                card->card_name);
            gtk_editable_select_region(GTK_EDITABLE(entries[CARD_NAME]), 0, -1);
            gtk_widget_grab_focus(GTK_WIDGET(entries[CARD_NAME]));
        }
        else
        {
                /* storing was successful */
            contact_free(card);
            gnome_dialog_close(GNOME_DIALOG(widget));
            return;
        }
            
        box = gnome_message_box_new(msg,
                                    GNOME_MESSAGE_BOX_ERROR,
                                    GNOME_STOCK_BUTTON_OK, NULL );
        gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
        gnome_dialog_run_and_close( GNOME_DIALOG( box ) );
        contact_free(card);
        g_free(msg);
        return;
    }
    else
    {
        gnome_dialog_close(GNOME_DIALOG(widget));
        return;
    }
}

static gint
store_address_dialog_close(GtkWidget *widget, GtkWidget **entries)
{
    g_free(entries);
    return FALSE;
}

/* New Stuff */

void
balsa_message_new (GtkWidget * widget)
{
  g_return_if_fail (widget != NULL);
  sendmsg_window_new (widget, NULL, SEND_NORMAL);
}


void
balsa_message_reply (GtkWidget * widget, gpointer index)
{
  GList     *list;
  Message   *message;

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
  Message   *message;

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
  Message   *message;

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
  Message *message;

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
	Message *message;
	int      is_all_flagged = TRUE;
 
	g_return_if_fail (widget != NULL);
	g_return_if_fail(index != NULL);

	list = GTK_CLIST(index)->selection;

	/* First see if we should unselect or select */
	while (list)
	{
		message = gtk_clist_get_row_data(GTK_CLIST(index), 
			GPOINTER_TO_INT(list->data));

		if (!(message->flags & MESSAGE_FLAG_FLAGGED))
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
			message_unflag(message);
		} else {
			message_flag(message);
		}
 
		list = list->next;
	}
	mailbox_commit_flagged_changes(BALSA_INDEX(index)->mailbox);
}

void
balsa_message_delete (GtkWidget * widget, gpointer index)
{
  GList   *list;
  BalsaIndexPage *page = NULL;
  Message *message;
  gboolean to_trash;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

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
      message_move(message, balsa_app.trash);
    else
      message_delete(message);

    list = list->next;
  }
  balsa_index_select_next (index);

  mailbox_commit_flagged_changes(BALSA_INDEX(index)->mailbox);

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
  Message *message;

  g_return_if_fail (widget != NULL);
  g_return_if_fail(index != NULL);

  list = GTK_CLIST(index)->selection;
  while (list)
  {
    message = gtk_clist_get_row_data(GTK_CLIST(index), GPOINTER_TO_INT(list->data));
    message_undelete(message);
    list = list->next;
  }
  balsa_index_select_next (index);
}

void
balsa_message_store_address (GtkWidget * widget, gpointer index)
{
  GList   *list = NULL;
  Message *message = NULL;
  GtkWidget *dialog = NULL;
  GtkWidget *frame = NULL;
  GtkWidget *table = NULL;
  GtkWidget *label = NULL;
  GtkWidget **entries = NULL;
  gint cnt = 0;
  gchar *labels[NUM_FIELDS] = { N_("Card Name:"), N_("First Name:"), 
				N_("Last Name:"), N_("Organization:"), 
				N_("Email Address:") };
  gchar **names;
  
  gchar *new_name = NULL;
  gchar *new_email = NULL;
  gchar *first_name = NULL;
  gchar *last_name = NULL;

  g_return_if_fail(widget != NULL);
  g_return_if_fail(index != NULL);
  
  list = GTK_CLIST(index)->selection;
  
  if(list == NULL) {
      GtkWidget *box = NULL;
      char * msg  = _("In order to store an address, you must select a message.\n");
      box = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR, 
				  GNOME_STOCK_BUTTON_OK, NULL );
      gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
      gnome_dialog_run_and_close( GNOME_DIALOG( box ) );
      return;
  }
  
  if(list->next) {
      GtkWidget *box = NULL;
      char * msg  = _("You may only store one address at a time.\n");
      box = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR, 
				  GNOME_STOCK_BUTTON_OK, NULL );
      gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
      gnome_dialog_run_and_close( GNOME_DIALOG( box ) );
      return;
  }

  message = gtk_clist_get_row_data(GTK_CLIST(index), 
      GPOINTER_TO_INT(list->data));

  if(message->from->mailbox == NULL) {
      GtkWidget *box = NULL;
      char * msg  = _("This message doesn't contain an e-mail address.\n");
      box = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR, 
	  GNOME_STOCK_BUTTON_OK, NULL );
      gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
      gnome_dialog_run_and_close( GNOME_DIALOG( box ) );
      return;
  }

  new_email = g_strdup( message->from->mailbox );

  if(message->from->personal == NULL)
  {
          /* if the message only contains an e-mail address */
     new_name = g_strdup( new_email );
  }
  else
  {
          /* make sure message->from->personal is not all whitespace */
     new_name = g_strstrip( g_strdup(message->from->personal) );

     if(strlen(new_name) == 0)
     {
        first_name = g_strdup("");
        last_name = g_strdup("");
     }
     else
     {
             /* guess the first name and last name */
        names = g_strsplit(new_name, " ", 0);
        first_name = g_strdup(names[0]);
            /* get last name */
        cnt = 0;
        while(names[cnt]) cnt++;

        if(cnt == 1)
            last_name = g_strdup("");
        else
            last_name = g_strdup(names[cnt - 1]);
        
        g_strfreev(names);
     }
  }

  if(!first_name) first_name = g_strdup("");
  if(!last_name)  last_name  = g_strdup("");

  entries = g_new(GtkWidget *, NUM_FIELDS);
  
  dialog = gnome_dialog_new( _("Store Address"), GNOME_STOCK_BUTTON_OK, 
      GNOME_STOCK_BUTTON_CANCEL, NULL);

  frame = gtk_frame_new( _("Contact Information") );
  gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), frame, TRUE, TRUE, 0);
  
  table = gtk_table_new(5, 2, FALSE);
  gtk_container_add(GTK_CONTAINER(frame), table);
  gtk_container_set_border_width( GTK_CONTAINER(table), 3 );
 
  for(cnt = 0; cnt < NUM_FIELDS; cnt++)
  {
      label = gtk_label_new( _(labels[cnt]) );
      entries[cnt] = gtk_entry_new();
      
      gtk_table_attach(GTK_TABLE(table), label, 0, 1, cnt, cnt + 1, GTK_FILL, 
		   GTK_FILL, 4, 4);
      
      gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
      
      gtk_table_attach(GTK_TABLE(table), entries[cnt], 1, 2, cnt, cnt + 1,
		   GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 2, 2);
}

  gtk_entry_set_text( GTK_ENTRY(entries[CARD_NAME]), new_name );
  gtk_entry_set_text( GTK_ENTRY(entries[FIRST_NAME]), first_name);
  gtk_entry_set_text( GTK_ENTRY(entries[LAST_NAME]), last_name);
  gtk_entry_set_text( GTK_ENTRY(entries[EMAIL_ADDRESS]), new_email );

  gtk_editable_select_region (GTK_EDITABLE(entries[CARD_NAME]), 0, -1);
  
  gnome_dialog_set_default(GNOME_DIALOG(dialog), 0);

  gtk_signal_connect( GTK_OBJECT(dialog), "clicked", 
		      GTK_SIGNAL_FUNC(store_address_dialog_button_clicked_cb),
                      entries);
  gtk_signal_connect( GTK_OBJECT(dialog), "close", 
		      GTK_SIGNAL_FUNC(store_address_dialog_close), entries);

  gtk_widget_show_all(dialog);

  g_free(new_name);
  g_free(first_name);
  g_free(last_name);
  g_free(new_email);
}
