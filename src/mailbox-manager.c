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
#include "mailbox.h"
#include "mailbox-manager.h"
#include "new-mailbox.h"



typedef struct _MailboxManagerWindow MailboxManagerWindow;
struct _MailboxManagerWindow
  {
    GtkWidget *window;
    GtkWidget *list;
  };

static MailboxManagerWindow *mmw = NULL;


/* callbacks */
static void destroy_mailbox_manager ();

static void close_mailbox_manager ();

static void select_row_cb (GtkWidget * widget, gint row, gint column, GdkEventButton * bevent);
static void new_cb ();
static void edit_cb ();
static void delete_cb ();




void
open_mailbox_manager ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *bbox;
  GtkWidget *button;

  static gchar *titles[] =
  {
    "Account",
    "Type",
    "Server"
  };


  /* only one mailbox manager window */
  if (mmw)
    {
      gdk_window_raise (mmw->window->window);
      return;
    }


  mmw = g_malloc (sizeof (MailboxManagerWindow));


  /* dialog window */
  mmw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mmw->window), "Mailbox Manager");
  gtk_window_set_wmclass (GTK_WINDOW (mmw->window), "mailbox_manager", "Balsa");
  gtk_window_position (GTK_WINDOW (mmw->window), GTK_WIN_POS_CENTER);
  gtk_container_border_width (GTK_CONTAINER (mmw->window), 0);

  gtk_signal_connect (GTK_OBJECT (mmw->window),
		      "destroy",
		      (GtkSignalFunc) destroy_mailbox_manager,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mmw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);



  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mmw->window)->vbox), hbox, TRUE, TRUE, 5);
  gtk_widget_show (hbox);


  /* accounts clist */
  mmw->list = gtk_clist_new_with_titles (3, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (mmw->list));
  gtk_clist_set_selection_mode (GTK_CLIST (mmw->list), GTK_SELECTION_BROWSE);

  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 0, 100);
  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 1, 50);
  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 2, 100);

  gtk_widget_set_usize (mmw->list, 280, 200);

  gtk_clist_set_policy (GTK_CLIST (mmw->list),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (hbox), mmw->list, TRUE, TRUE, 5);

  gtk_signal_connect (GTK_OBJECT (mmw->list),
		      "select_row",
		      (GtkSignalFunc) select_row_cb,
		      NULL);

  gtk_widget_show (mmw->list);


  /* one button vbox */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 5);
  gtk_widget_show (vbox);


  /* edit account button */
  button = gtk_button_new_with_label ("Edit...");
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) edit_cb,
		      NULL);

  gtk_widget_show (button);


  /* edit account button */
  button = gtk_button_new_with_label ("New...");
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) new_cb,
		      NULL);

  gtk_widget_show (button);


  /* delete account button */
  button = gtk_button_new_with_label ("Delete...");
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, FALSE, FALSE, 0);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) delete_cb,
		      NULL);

  gtk_widget_show (button);



  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  button = gnome_stock_button (GNOME_STOCK_BUTTON_CLOSE);
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) close_mailbox_manager,
		      NULL);

  gtk_widget_show (button);



  /* show the whole thing */
  refresh_mailbox_manager ();
  gtk_widget_show (mmw->window);
}


static void
destroy_mailbox_manager ()
{
  g_free (mmw);
  mmw = NULL;
}



void
close_mailbox_manager ()
{
  if (!mmw)
    return;

  gtk_widget_destroy (mmw->window);
}

static gboolean 
mmw_add_mb_to_clist_traverse_nodes (GNode * node, gpointer data)
{
  Mailbox *mailbox;
  gchar *list_items[3];

  if (node->data)
    mailbox = node->data;
  list_items[0] = mailbox->name;
  list_items[1] = mailbox_type_description (mailbox->type);

  switch (mailbox->type)
    {
    case MAILBOX_POP3:
      list_items[2] = ((MailboxPOP3 *) mailbox)->server;
      break;

    case MAILBOX_IMAP:
      list_items[2] = ((MailboxIMAP *) mailbox)->server;
      break;

    default:
      list_items[2] = NULL;
      break;
    }

  gtk_clist_set_row_data (GTK_CLIST (mmw->list),
		       gtk_clist_append (GTK_CLIST (mmw->list), list_items),
			  mailbox);
  return TRUE;
}

/* sets the list of mailboxes in the mailbox manager window */
void
refresh_mailbox_manager ()
{
  GList *list;
  Mailbox *mailbox;

  gtk_clist_freeze (GTK_CLIST (mmw->list));
  gtk_clist_clear (GTK_CLIST (mmw->list));

  g_node_traverse (balsa_app.mailbox_nodes,
		   G_LEVEL_ORDER,
		   G_TRAVERSE_ALL,
		   10,
		   mmw_add_mb_to_clist_traverse_nodes,
		   NULL);


  gtk_clist_thaw (GTK_CLIST (mmw->list));
}



/*
 * callbacks
 */
static void
select_row_cb (GtkWidget * widget, gint row, gint column, GdkEventButton * bevent)
{
  if (bevent)
    if (bevent->type == GDK_2BUTTON_PRESS)
      open_new_mailbox ((Mailbox *) gtk_clist_get_row_data (GTK_CLIST (mmw->list), row));
}


static void
new_cb ()
{
  open_new_mailbox (NULL);
}


static void
edit_cb ()
{
  gint row;
  Mailbox *mailbox;

  row = (gint) GTK_CLIST (mmw->list)->selection->data;
  mailbox = (Mailbox *) gtk_clist_get_row_data (GTK_CLIST (mmw->list), row);

  open_new_mailbox (mailbox);
}


static void
delete_cb ()
{
  gint row;
  Mailbox *mailbox;

  row = (gint) GTK_CLIST (mmw->list)->selection->data;
  mailbox = (Mailbox *) gtk_clist_get_row_data (GTK_CLIST (mmw->list), row);
}
