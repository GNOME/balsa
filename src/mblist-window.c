/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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

#include "libbalsa.h"

#include "mblist-window.h"

/* mailbox list code */
#include "config.h"

#include <string.h>

#include "balsa-app.h"
#include "balsa-icons.h"
#include "balsa-index.h"
#include "balsa-index-page.h"
#include "balsa-mblist.h"
#include "balsa-message.h"
#include "misc.h"
#include "main.h"
#include "main-window.h"
#include "mailbox-conf.h"
#include "../libbalsa/mailbox.h"

typedef struct _MBListWindow MBListWindow;
struct _MBListWindow
  {
    GtkWidget *window;

    GtkCTree *ctree;
    GtkCTreeNode *parent;
  };
static MBListWindow *mblw = NULL;

enum
  {
    TARGET_MESSAGE,
  };

static GtkTargetEntry dnd_mb_target[] =
{
  {"x-application-gnome/balsa", 0, TARGET_MESSAGE}
};
#define ELEMENTS(x) (sizeof (x) / sizeof (x[0]))



/* callbacks */
void mblist_open_mailbox (Mailbox * mailbox);
void mblist_close_mailbox (Mailbox * mailbox);
static void mailbox_select_cb (BalsaMBList *, Mailbox *, GtkCTreeNode *, GdkEventButton *);
static gboolean mblist_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
/*PKGW*/
static void size_allocate_cb( GtkWidget *widget, GtkAllocation *alloc );

static GtkWidget *mblist_create_context_menu (GtkCTree * ctree, Mailbox * mailbox);

static void mblist_drag_data_received (GtkWidget * widget, GdkDragContext * context, gint x, gint y, GtkSelectionData * selection_data, guint info, guint32 time);
static gboolean mblist_drag_motion (GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time);
static gboolean mblist_drag_leave (GtkWidget *widget, GdkDragContext *context, guint time);


GtkWidget *balsa_mailbox_list_window_new(BalsaWindow *window)
{
  GtkWidget *widget;

  gint height;

  mblw = g_malloc0(sizeof(MBListWindow));
  mblw->window = GTK_WIDGET(window);

  widget = gtk_scrolled_window_new (NULL, NULL);
  mblw->ctree = GTK_CTREE (balsa_mblist_new ());
  balsa_app.mblist = BALSA_MBLIST (mblw->ctree);
  gtk_container_add(GTK_CONTAINER(widget), GTK_WIDGET(mblw->ctree));

  /* PKGW TEST: what happens if we do this?
   *  gtk_widget_set_usize (GTK_WIDGET (mblw->ctree), balsa_app.mblist_width, balsa_app.mblist_height);
   */

/*
   gtk_ctree_show_stub (mblw->ctree, FALSE);
 */
  /* gtk_ctree_set_line_style (mblw->ctree, GTK_CTREE_LINES_DOTTED); */

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(widget),
				  GTK_POLICY_AUTOMATIC,
				  GTK_POLICY_AUTOMATIC);

  gtk_clist_set_row_height (GTK_CLIST (mblw->ctree), 16);

  gtk_widget_show(GTK_WIDGET(mblw->ctree));
#ifdef BALSA_SHOW_INFO
  /* set the "show_content_info" property and redraw the mailbox list */
  gtk_object_set(GTK_OBJECT (mblw->ctree), "show_content_info", balsa_app.mblist_show_mb_content_info, NULL);
#endif

  height = GTK_CLIST (mblw->ctree)->rows * GTK_CLIST (mblw->ctree)->row_height;

  gtk_drag_dest_set (GTK_WIDGET (mblw->ctree), GTK_DEST_DEFAULT_ALL,
		     dnd_mb_target, ELEMENTS (dnd_mb_target),
		     GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK ); 
  
  
  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "select_mailbox",
    GTK_SIGNAL_FUNC (mailbox_select_cb), NULL);
  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "button_press_event",
  GTK_SIGNAL_FUNC (mblist_button_press_cb), NULL);
  /* PKGW: We want to catch size changes for balsa_app.mblist_width */
  gtk_signal_connect( GTK_OBJECT( mblw->ctree ), "size_allocate",
		      GTK_SIGNAL_FUNC( size_allocate_cb ), NULL );

 /* callback when dragged object moves in the mblist window */
  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "drag_motion",
		      GTK_SIGNAL_FUNC (mblist_drag_motion), NULL);
  /* drag leave the window */
  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "drag_leave",
		      GTK_SIGNAL_FUNC (mblist_drag_leave), NULL);


  /* callbacks when object dropped on a mailbox */

  gtk_signal_connect (GTK_OBJECT (mblw->ctree), "drag_data_received",
		      GTK_SIGNAL_FUNC (mblist_drag_data_received), NULL);

  return widget;
}

/* mblist_open_mailbox
 * 
 * Description: This checks to see if the mailbox is already open and
 * just on a different mailbox page, or if a new page needs to be
 * created and the mailbox parsed.
 * */
void
mblist_open_mailbox (Mailbox * mailbox)
{
  GtkWidget *page = NULL;
  int i, c;

  if (!mblw)
    return;

  c = gtk_notebook_get_current_page(GTK_NOTEBOOK(balsa_app.notebook));

  /* If we currently have a page open, update the time last visited */
  if (c != -1) { 
    page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),c); 
    page = gtk_object_get_data(GTK_OBJECT(page),"indexpage"); 
    g_get_current_time(&BALSA_INDEX_PAGE(page)->last_use); 
  } 
  
  if( mailbox->open_ref ) {
    i = balsa_find_notebook_page_num( mailbox );
    if( i != -1 )
      {
	gtk_notebook_set_page(GTK_NOTEBOOK(balsa_app.notebook),i);
	page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(balsa_app.notebook),i);
	page = gtk_object_get_data(GTK_OBJECT(page),"indexpage");
	g_get_current_time(&BALSA_INDEX_PAGE(page)->last_use);

/* This nasty looking piece of code is for the column resizing patch, it needs
   to change the size of the columns before they get displayed.  To do this we
   need to get the reference to the index page, which gives us a reference to
   the index, which gives us the clist, which we then reference.  Looks ugly
   but works well. */
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 0, balsa_app.index_num_width);
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 1, balsa_app.index_status_width);
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 2, balsa_app.index_attachment_width);
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 3, balsa_app.index_from_width);
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 4, balsa_app.index_subject_width);
	gtk_clist_set_column_width (GTK_CLIST(&(BALSA_INDEX(BALSA_INDEX_PAGE(page)->index)->clist)), 5, balsa_app.index_date_width);
        
        balsa_mblist_have_new (BALSA_MBLIST(mblw->ctree));
	return;
      }
  }

  balsa_window_open_mailbox(BALSA_WINDOW(mblw->window), mailbox);

  balsa_window_set_cursor(BALSA_WINDOW(mblw->window), NULL);
    
#ifdef BALSA_SHOW_INFO
  if (balsa_app.mblist->display_content_info){
    balsa_mblist_update_mailbox (balsa_app.mblist, mailbox);
  }
#endif

  balsa_mblist_have_new (BALSA_MBLIST(mblw->ctree));

  /* I don't know what is the purpose of that code, so I put
     it in comment until somebody tells me waht it  is useful 
     for.      -Bertrand 
  
  if (!strcmp (mailbox->name, _("Inbox")) ||
      !strcmp (mailbox->name, _("Outbox")) ||
      !strcmp (mailbox->name, _("Trash")))
    return ;
  

  gtk_ctree_set_node_info (GTK_CTREE (mblw->ctree),
    row,
    mailbox->name, 5,
    NULL, NULL,
    balsa_icon_get_pixmap (BALSA_ICON_TRAY_EMPTY),
    balsa_icon_get_bitmap (BALSA_ICON_TRAY_EMPTY),
    FALSE, TRUE); 
    
    gtk_ctree_node_set_row_style (GTK_CTREE (mblw->ctree), row, NULL); */
}


void
mblist_close_mailbox (Mailbox * mailbox)
{
  if (!mblw)
    return;
  balsa_window_close_mailbox(BALSA_WINDOW(mblw->window), mailbox);
  balsa_mblist_have_new (BALSA_MBLIST(mblw->ctree));
}
 

static gboolean
mblist_button_press_cb (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
  BalsaMBList * bmbl;
  GtkCList * clist;
  GtkCTree * ctree;

  gint row, column;
  gint on_mailbox;
  MailboxNode *mbnode;
  Mailbox *mailbox;
  GtkCTreeNode *node;

  bmbl = BALSA_MBLIST (widget);
  clist = GTK_CLIST (widget);
  ctree = GTK_CTREE (widget);

  on_mailbox = gtk_clist_get_selection_info (clist, event->x, event->y, &row, &column);
  
  if (on_mailbox)
    {
      node = gtk_ctree_node_nth( ctree, row );
      mbnode = gtk_ctree_node_get_row_data(ctree, node);
      mailbox = mbnode->mailbox;
      
      /* FIXME: The BALSA_IS_MAILBOX below causes segfaults, when
       * clicking on non-mailboxes, but this works until the problem
       * is fixed */
      if (mbnode->IsDir)
        return FALSE;
      
      g_return_val_if_fail (BALSA_IS_MAILBOX(mailbox), FALSE);
            
      if (event->button == 1) // && event->type == GDK_2BUTTON_PRESS)
	
	{
	  /* double click with button left */
	  
	  mblist_open_mailbox (mailbox);  
	  return TRUE;
	}
     
      if (event && event->button == 3)
	{
	  if (node) gtk_ctree_select(ctree, node);
	  gtk_menu_popup (GTK_MENU (mblist_create_context_menu (GTK_CTREE (bmbl), mailbox)), NULL, NULL, NULL, NULL, event->button, event->time);
	}

      return FALSE;
    } 
  else /* not on_mailbox */
    { 
      
      if (event->type == GDK_BUTTON_PRESS && event->button == 3)
	{
	  /* simple click on right button */
	  gtk_menu_popup (GTK_MENU (mblist_create_context_menu (GTK_CTREE (bmbl), NULL)), 
			  NULL, NULL, NULL, NULL, event->button, event->time);
	  return TRUE;
	}
      
      return FALSE;
    }

  return FALSE; /* never reached but this avoid compiler warning */
}

/*PKGW*/
static void size_allocate_cb( GtkWidget *widget, GtkAllocation *alloc )
{
/*    printf( "size_allocate_cb()\n" ); */
  if (balsa_app.show_mblist)
    balsa_app.mblist_width = alloc->width + 18;
}

static void
mailbox_select_cb (BalsaMBList * bmbl, Mailbox * mailbox, GtkCTreeNode * row, GdkEventButton * event)
{
  if (!mblw)
    return;
}


static void
mb_open_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mblist_open_mailbox (mailbox);
}

static void
mb_close_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mblist_close_mailbox (mailbox);
}

static void
mb_conf_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox, FALSE, MAILBOX_UNKNOWN);
}

static void
mb_add_cb (GtkWidget * widget, Mailbox * mailbox)
{
  mailbox_conf_new (mailbox, TRUE, MAILBOX_UNKNOWN);
}

static void
mb_del_cb (GtkWidget * widget, Mailbox * mailbox)
{
  if (mailbox->type == MAILBOX_UNKNOWN)
    return;
  mailbox_conf_delete (mailbox);
}

/* FIXME make these use gnome_popup_menu stuff
static GnomeUIInfo mailbox_menu[] =
  GNOMEUIINFO_ITEM_STOCK (_ ("_Open Mailbox"), N_("Open the selected mailbox"),
                          mb_open_cb, GNOME_STOCK_MENU_OPEN),
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Close"), N_("Close the selected mailbox"),
                          mblist_menu_close_cb, GNOME_STOCK_MENU_CLOSE),
  GNOMEUIINFO_SEPARATOR,
  GNOMEUIINFO_ITEM_STOCK (N_ ("_Add"), N_("Add a new mailbox"),
                          mblist_menu_add_cb, GNOME_STOCK_PIXMAP_ADD),
  GNOMEUIINFO_END
};
*/

static GtkWidget *
mblist_create_context_menu (GtkCTree * ctree, Mailbox * mailbox)
{
  GtkWidget *menu, *menuitem;

  menu = gtk_menu_new ();

  if (mailbox)
    {
      menuitem = gtk_menu_item_new_with_label (_ ("Open Mailbox"));
      gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			  GTK_SIGNAL_FUNC (mb_open_cb), mailbox);
      gtk_menu_append (GTK_MENU (menu), menuitem);
      gtk_widget_show (menuitem);
     
      menuitem = gtk_menu_item_new_with_label (_ ("Close Mailbox"));
      gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			  GTK_SIGNAL_FUNC (mb_close_cb), mailbox);
      gtk_menu_append (GTK_MENU (menu), menuitem);
      gtk_widget_show (menuitem);
    }
  
  menuitem = gtk_menu_item_new_with_label (_ ("Add New Mailbox"));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      GTK_SIGNAL_FUNC (mb_add_cb), mailbox);
  gtk_menu_append (GTK_MENU (menu), menuitem);
  gtk_widget_show (menuitem);
  
  if (mailbox)
    {
      menuitem = gtk_menu_item_new_with_label (_ ("Edit Mailbox Properties"));
      gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			  GTK_SIGNAL_FUNC (mb_conf_cb), mailbox);
      gtk_menu_append (GTK_MENU (menu), menuitem);
      gtk_widget_show (menuitem);
      
      menuitem = gtk_menu_item_new_with_label (_ ("Delete Mailbox"));
      gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			  GTK_SIGNAL_FUNC (mb_del_cb), mailbox);
      gtk_menu_append (GTK_MENU (menu), menuitem);
      gtk_widget_show (menuitem);
    }

  return menu;
}


void
mblist_menu_add_cb (GtkWidget * widget, gpointer data)
{
  Mailbox *mailbox = mblist_get_selected_mailbox ();
  
  mailbox_conf_new (mailbox, TRUE, MAILBOX_UNKNOWN);
}


void
mblist_menu_edit_cb (GtkWidget * widget, gpointer data)
{
  Mailbox *mailbox = mblist_get_selected_mailbox ();

  if (mailbox == NULL)
    {
      GtkWidget *err_dialog = gnome_error_dialog (_ ("No mailbox selected."));
      gnome_dialog_run (GNOME_DIALOG (err_dialog));
      return;
    }

  mailbox_conf_new (mailbox, FALSE, MAILBOX_UNKNOWN);
}


void
mblist_menu_delete_cb (GtkWidget * widget, gpointer data)
{
  Mailbox *mailbox = mblist_get_selected_mailbox ();

  if (mailbox == NULL)
    {
      GtkWidget *err_dialog = gnome_error_dialog (_ ("No mailbox selected."));
      gnome_dialog_run (GNOME_DIALOG (err_dialog));
      return;
    }

  if (mailbox->type == MAILBOX_UNKNOWN)
    return;
  mailbox_conf_delete (mailbox);
}


void
mblist_menu_open_cb (GtkWidget * widget, gpointer data)
{
  Mailbox *mailbox = mblist_get_selected_mailbox ();

  if (mailbox == NULL)
    {
      GtkWidget *err_dialog = gnome_error_dialog (_ ("No mailbox selected."));
      gnome_dialog_run (GNOME_DIALOG (err_dialog));
      return;
    }
  mblist_open_mailbox (mailbox);
}


void
mblist_menu_close_cb (GtkWidget * widget, gpointer data)
{
  Mailbox *mailbox = mblist_get_selected_mailbox ();

  if (mailbox == NULL)
    {
      GtkWidget *err_dialog = gnome_error_dialog (_ ("No mailbox selected."));
      gnome_dialog_run (GNOME_DIALOG (err_dialog));
      return;
    }
  mblist_close_mailbox (mailbox);
}


Mailbox *
mblist_get_selected_mailbox (void)
{
  GtkCTreeNode *node;
  MailboxNode *mbnode;
  
  g_assert (mblw != NULL);
  g_assert (mblw->ctree != NULL);

  if (!GTK_CLIST (mblw->ctree)->selection)
    return NULL;

  node = GTK_CTREE_NODE (GTK_CLIST (mblw->ctree)->selection->data);

  mbnode = gtk_ctree_node_get_row_data (GTK_CTREE (mblw->ctree), node);
  return mbnode->mailbox;
}

static gboolean
mbox_by_name (gconstpointer a, gconstpointer b)
{
  MailboxNode *mbnode = (MailboxNode *) a;
  const gchar *name = (const gchar *) b;
  g_assert(mbnode != NULL);
  g_assert(mbnode->mailbox != NULL);

  return strcmp(mbnode->mailbox->name, name) != 0;
}

Mailbox *
mblist_find_mbox_by_name (const gchar *name) {
  GtkCTreeNode *node;

  g_assert (mblw != NULL);
  g_assert (mblw->ctree != NULL);
  
  node = gtk_ctree_find_by_row_data_custom (GTK_CTREE (mblw->ctree), NULL,
                                          (gchar*)name, mbox_by_name);
  if(node) {
     MailboxNode * mbnode = 
      gtk_ctree_node_get_row_data(GTK_CTREE (mblw->ctree),node);
     return mbnode->mailbox;
  } else return NULL;
}


/* mbox_is_unread: 
   NOTE mbnode->mailbox == NULL for directories */
static gboolean
mbox_is_unread (gconstpointer a, gconstpointer b)
{
  MailboxNode *mbnode = (MailboxNode *) a;
  g_assert(mbnode != NULL);
  return !(mbnode->mailbox && mbnode->mailbox->has_unread_messages);
}

/* mblist_find_all_unread_mboxes:
   find all nodes and translate them to mailbox list 
*/
GList *
mblist_find_all_unread_mboxes (void)
{
   GList * res = NULL, *r, *i;
   g_assert (mblw != NULL);
   g_assert (mblw->ctree != NULL);
   
   r = gtk_ctree_find_all_by_row_data_custom (GTK_CTREE (mblw->ctree), NULL,
					      NULL, mbox_is_unread);
   
   for(i=g_list_first(r); i; i=g_list_next(i) ) {
      MailboxNode * mbnode = 
	 gtk_ctree_node_get_row_data(GTK_CTREE (mblw->ctree),i->data);

      res = g_list_append(res,  mbnode->mailbox);
   }
   g_list_free(r);
   return res;
}

/* DND stuff */


/* receive the data from the source */
static void
mblist_drag_data_received (GtkWidget * widget,
			   GdkDragContext * context,
			   gint x,
			   gint y,
			   GtkSelectionData * selection_data,
			   guint info,
			   guint32 time)
{
  /*--*/
  GtkCList *clist = GTK_CLIST (widget);
  
  Mailbox *target_mailbox;
  
  Message **received_message_list;
  guint nb_received_messages;
  guint received_message_count;
  Message *current_message;

  gint row;
  /*--*/
  
  /* find the mailbox on which the messages have been dropped */
  if (!gtk_clist_get_selection_info (clist, x, y, &row, NULL))
    return;
  
  target_mailbox = gtk_clist_get_row_data (GTK_CLIST(widget), row);
  if (!target_mailbox) 
    return;

  if ((selection_data->length >= 0) )
    {
      nb_received_messages = (guint)(selection_data->length)/sizeof( Message *);
      received_message_list =  (Message **)(selection_data->data);
      
      for (received_message_count=0; received_message_count<nb_received_messages; received_message_count++)
	{
	  current_message = received_message_list[received_message_count];
	  if (current_message->mailbox != target_mailbox)
	    message_move (current_message, target_mailbox);
	}
      gtk_drag_finish (context, TRUE, FALSE, time);
      return;
    }
  
  gtk_drag_finish (context, FALSE, FALSE, time);


}


/* callback : data moved in the window */
static gboolean
mblist_drag_motion         (GtkWidget          *widget,
                            GdkDragContext     *context,
                            gint                x,
                            gint                y,
                            guint               time)
{
  GtkCList *clist = GTK_CLIST (widget);
  GtkCTree *ctree = GTK_CTREE (widget);
  gint row;
  gint mb_selected;
  GtkCTreeNode *node;


  mb_selected = gtk_clist_get_selection_info (clist, x, y, &row, NULL);
  if (mb_selected<1)
    {
      gtk_clist_unselect_all(clist);
      return FALSE;
    }
  node = gtk_ctree_node_nth( ctree, row );
  if (node) 
    {
      gtk_ctree_select(ctree, node);
      return TRUE;
    }
  else
    {
      gtk_clist_unselect_all(clist);
      return FALSE;
    }
  
}

/* callback : drag leave the window */
static gboolean
mblist_drag_leave (GtkWidget          *widget,
		   GdkDragContext     *context,
		   guint               time)
{
  GtkCList *clist = GTK_CLIST (widget);

  gtk_clist_unselect_all(clist);
  return TRUE;
}
