/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
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
#include <gnome.h>
#include "balsa-app.h"
#include "new-mailbox.h"
#include "misc.h"




typedef struct _NewMailboxWindow NewMailboxWindow;
struct _NewMailboxWindow
{
  Mailbox *mailbox;

  GtkWidget *window;
  GtkWidget *notebook;
  GtkWidget *back;
  GtkWidget *forward;

  /* first page */
  GtkWidget *mailbox_type_menu;
};

static GList *open_mailbox_list = NULL;




/* notebook pages */
static GtkWidget * create_first_page (NewMailboxWindow * nmw);
static GtkWidget * create_second_page ();


/* callbacks */
static void destroy_new_mailbox (GtkWidget * widget);
static void close_new_mailbox (GtkWidget * widget);
static void refresh_new_mailbox (NewMailboxWindow * nmw);
static void refresh_button_state (NewMailboxWindow * nmw);


static void back_cb (GtkWidget * widget);
static void forward_cb (GtkWidget * widget);

static void menu_item_cb (GtkWidget * widget);


/* misc */
static void set_new_mailbox_data (GtkObject * object, NewMailboxWindow *nmw);
static NewMailboxWindow * get_new_mailbox_data (GtkObject * object);




void
open_new_mailbox (Mailbox * mailbox)
{
  NewMailboxWindow *nmw;
  GList *list;
  GtkWidget *button;
  GtkWidget *bbox;


  /* keep a list of mailboxes which are being edited so
   * we don't do a double-edit thing, kus, like, that would
   * suck bevis */
  if (mailbox)
    {
      list = open_mailbox_list;

      while (list)
	{
	  nmw = (NewMailboxWindow *) list->data;
	  list = list->next;

	  if (mailbox == nmw->mailbox)
	    {
	      gdk_window_raise (nmw->window->window);
	      return;
	    }
	}
    }


  nmw = g_malloc (sizeof (NewMailboxWindow));
  nmw->mailbox = mailbox;
  open_mailbox_list = g_list_append (open_mailbox_list, nmw);


  nmw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (nmw->window), "New Mailbox");
  gtk_container_border_width (GTK_CONTAINER (nmw->window), 0);

  set_new_mailbox_data (GTK_OBJECT (nmw->window), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->window),
                      "destroy",
                      (GtkSignalFunc) destroy_new_mailbox,
                      NULL);

  gtk_signal_connect (GTK_OBJECT (nmw->window),
                      "delete_event",
                      (GtkSignalFunc) gtk_false,
                      NULL);



  /* notbook for action area of dialog */
  nmw->notebook = gtk_notebook_new ();
  gtk_container_border_width (GTK_CONTAINER (nmw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->vbox), nmw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (nmw->notebook), FALSE);
  gtk_notebook_set_show_border ( GTK_NOTEBOOK(nmw->notebook), FALSE);
  gtk_widget_show (nmw->notebook);


  /* notebook pages */
  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_first_page (nmw),
			    NULL);

  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_second_page (nmw),
			    NULL);



  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  /* back button */
  nmw->back = gtk_button_new_with_label ("Back");
  gtk_container_add (GTK_CONTAINER (bbox), nmw->back);
  set_new_mailbox_data (GTK_OBJECT (nmw->back), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->back),
		      "clicked",
		      (GtkSignalFunc) back_cb,
		      NULL);

  gtk_widget_show (nmw->back);


  /* forward button */
  nmw->forward = gtk_button_new_with_label ("Forward");
  gtk_container_add (GTK_CONTAINER (bbox), nmw->forward);

  set_new_mailbox_data (GTK_OBJECT (nmw->forward), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->forward),
		      "clicked",
		      (GtkSignalFunc) forward_cb,
		      NULL);

  gtk_widget_show (nmw->forward);


  /* cancel button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL); 
  gtk_container_add (GTK_CONTAINER (bbox), button);
  
  set_new_mailbox_data (GTK_OBJECT (button), nmw);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) close_new_mailbox,
		      NULL);

  gtk_widget_show (button);




  refresh_new_mailbox (nmw);
  refresh_button_state (nmw);
  gtk_widget_show (nmw->window);
}


static void 
destroy_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  /* remove the mailbox from the open mailbox list */
  open_mailbox_list = g_list_remove (open_mailbox_list, nmw);

  g_free (nmw);
}


static void
close_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  gtk_widget_destroy (nmw->window);
}


static void
refresh_new_mailbox (NewMailboxWindow * nmw)
{
  GString *str = g_string_new (NULL);
  GList *children;
  GtkWidget *menu;
  GtkWidget *menuitem;
  

  /* set the window title */
  if (nmw->mailbox == NULL)
    gtk_window_set_title (GTK_WINDOW (nmw->window), "Mailbox: New");
  else
    {
      g_string_assign (str, "Mailbox: ");
      g_string_append (str, nmw->mailbox->name);

      gtk_window_set_title (GTK_WINDOW (nmw->window), str->str);
    }


  /* set the mailbox type */
  if (nmw->mailbox)
    {
      MailboxType type;

      menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (nmw->mailbox_type_menu));
      children = GTK_MENU_SHELL (menu)->children;
      while (children)
	{
	  menuitem = children->data;
	  children = children->next;

	  type = (MailboxType) gtk_object_get_user_data (GTK_OBJECT (menuitem));
	  
	  if (type == nmw->mailbox->type)
	      {
		gtk_option_menu_set_history (GTK_OPTION_MENU (nmw->mailbox_type_menu),
					     g_list_index (GTK_MENU_SHELL (menu)->children, menuitem));
		gtk_menu_item_activate (GTK_MENU_ITEM (menuitem));
		break;
	      }
	}
    }



  /* cleanup */
  g_string_free (str, TRUE);
}


static void
refresh_button_state (NewMailboxWindow * nmw)
{
  switch (gtk_notebook_current_page (GTK_NOTEBOOK (nmw->notebook)))
    {
    case 0:
      gtk_widget_set_sensitive (nmw->back, FALSE);
      gtk_widget_set_sensitive (nmw->forward, TRUE);
      break;

    case 1:
      gtk_widget_set_sensitive (nmw->back, TRUE);
      gtk_widget_set_sensitive (nmw->forward, FALSE);
      break;

    default:
      break;
    }
}


/*
 * create notebook pages
 */
static GtkWidget *
create_first_page (NewMailboxWindow * nmw)
{
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *menuitem;
  GtkWidget *menu;

  table = gtk_table_new (5, 2, FALSE);
  gtk_widget_show (table);


  /* your name */
  label = gtk_label_new ("Mailbox Type");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);



  menu = gtk_menu_new ();

  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MBX),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_MBX);

    menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MTX),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_MTX);

  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_TENEX),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_TENEX);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MBOX),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_MBOX);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MMDF),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_MMDF);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_UNIX),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_UNIX);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_MH),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_MH);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_POP3),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_POP3);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_IMAP),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_IMAP);
  menuitem = 
    append_menuitem_connect (GTK_MENU (menu), 
			     mailbox_type_description (MAILBOX_NNTP),
			     (GtkSignalFunc) menu_item_cb,
			     NULL,
			     (gpointer) MAILBOX_NNTP);


  nmw->mailbox_type_menu = gtk_option_menu_new ();
  gtk_widget_set_usize (nmw->mailbox_type_menu, 0, BALSA_BUTTON_HEIGHT);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (nmw->mailbox_type_menu), menu);
  gtk_table_attach (GTK_TABLE (table), nmw->mailbox_type_menu, 1, 2, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL,
		    0, 10);
  gtk_widget_show (nmw->mailbox_type_menu);


  return table;
}



static GtkWidget *
create_second_page (NewMailboxWindow * nmw)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;



  table = gtk_table_new (3, 2, FALSE);
  gtk_widget_show (table);


  /* mailbox name */
  label = gtk_label_new ("Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (entry);


  /* mailbox path */
  label = gtk_label_new ("Path:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_widget_show (entry);


  return table;
}


/*
 * callbacks
 */
static void
back_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  if (gtk_notebook_current_page (GTK_NOTEBOOK (nmw->notebook)) == 0)
    return;

  gtk_notebook_prev_page (GTK_NOTEBOOK (nmw->notebook));
  refresh_button_state (nmw);
}



static void
forward_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  if (gtk_notebook_current_page (GTK_NOTEBOOK (nmw->notebook)) == 1)
    return;

  gtk_notebook_next_page (GTK_NOTEBOOK (nmw->notebook));
  refresh_button_state (nmw);
}


static void
menu_item_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  MailboxType type = (MailboxType) gtk_object_get_user_data (GTK_OBJECT (widget));


  switch (type)
    {
    case MAILBOX_MBX:
      break;

    case MAILBOX_MTX:
      break;

    case MAILBOX_TENEX:
      break;

    case MAILBOX_MBOX:
      break;

    case MAILBOX_MMDF:
      break;

    case MAILBOX_UNIX:
      break;

    case MAILBOX_MH:
      break;

    case MAILBOX_POP3:
      break;

    case MAILBOX_IMAP:
      break;

    case MAILBOX_NNTP:
      break;
    }
}


/*
 * set/get data convience functions used for attaching the
 * NewMailboxWindow structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_new_mailbox_data (GtkObject * object, NewMailboxWindow *nmw)
{
  gtk_object_set_data (object, "new_mailbox_data", (gpointer) nmw);
}


static NewMailboxWindow * 
get_new_mailbox_data (GtkObject * object)
{
  return gtk_object_get_data (object, "new_mailbox_data");
}
