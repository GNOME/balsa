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
#include "balsa-mblist.h"
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

/* DND declarations */
enum
{
  TARGET_MESSAGE,
};

/*#define DND_USED*/
#ifdef DND_USED

static GtkTargetEntry drag_types[] =
{
  {"x-application-gnome/balsa", 0, TARGET_MESSAGE}
};
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))

static void index_child_setup_dnd ( GnomeMDIChild * child );
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
  gtk_object_set(GTK_OBJECT(index), "use_drag_icons", FALSE, NULL);
  gtk_object_set(GTK_OBJECT(index), "reorderable", FALSE, NULL);
  // XXX
  //  index_child_setup_dnd(child);

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

    gnome_dialog_set_parent (GNOME_DIALOG (dialog), GTK_WINDOW (GTK_WIDGET (page)->parent->parent));
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
  if (bmsg) {
    if (BALSA_MESSAGE(bmsg)) {
      if(balsa_app.previewpane) {
        if (message) 
          balsa_message_set(BALSA_MESSAGE(bmsg), message);
        else
          balsa_message_clear (BALSA_MESSAGE (bmsg));
      }
    }
  }
  
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

static GtkWidget *
create_menu (BalsaIndex * bindex)
{
  GtkWidget *menu, *menuitem, *submenu, *smenuitem;
  GtkWidget *bmbl;

  BALSA_DEBUG ();
  
  menu = gtk_menu_new ();
  
  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_NEW, _ ("New"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_new,
		      NULL);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RPL, _ ("Reply"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_reply,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_RPL, _ ("Reply to all"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_replytoall,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_MAIL_FWD, _ ("Forward"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_forward,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_TRASH, _ ("Delete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_delete,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gnome_stock_menu_item (GNOME_STOCK_MENU_UNDELETE, _ ("Undelete"));
  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) balsa_message_undelete,
		      bindex);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_label (_ ("Transfer"));
  submenu = gtk_menu_new ();
  smenuitem = gtk_menu_item_new ();
  bmbl = balsa_mblist_new ();
  gtk_signal_connect (GTK_OBJECT (bmbl), "select_mailbox",
		      (GtkSignalFunc) transfer_messages_cb,
		      (gpointer) bindex); 
 
  /* XXX get a good approximation here FIXME */
  /* gtk_widget_set_usize (GTK_WIDGET (bmbl), balsa_app.mblist_width, -1); */

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
/* DND features                                              */

#ifdef DND_USED

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
