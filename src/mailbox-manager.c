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
#include "mailbox-manager.h"
#include "balsa-app.h"
#include "index.h"
#include "mailbox.h"

typedef struct _MailboxManagerWindow MailboxManagerWindow;
struct _MailboxManagerWindow
{
  GtkWidget *window;
  GtkWidget *list;
};
static MailboxManagerWindow *mmw = NULL;


static gint mailbox_manager_destroy ();
static void update_mailbox_list ();
static void select_row_cb (GtkWidget * widget,
			   gint row,
			   gint column,
			   GdkEventButton * bevent);
static void edit_cb ();
static void new_cb ();
static void duplicate_cb ();
static void delete_cb (GtkWidget * widget, gpointer something);


static void edit_mailbox_pop3 (Mailbox * mailbox);
static gint edit_mailbox_destroy (GtkWidget * widget);
static void ok_edit_mailbox_cb (GtkWidget * widget,
				gpointer * data);


void
open_mailbox_manager ()
{
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *vboxm;
  GtkWidget *table;
  GtkWidget *hbox;
  GtkWidget *button;

  static gchar *titles[] =
  {
    "Account",
    "Type"
  };


  /* only one mailbox manager window */
  if (mmw)
    return;

  mmw = g_malloc (sizeof (MailboxManagerWindow));

  mmw->window = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_container_border_width (GTK_CONTAINER (mmw->window), 3);
  gtk_window_set_title (GTK_WINDOW (mmw->window), "Mailbox Manager");
  gtk_window_set_wmclass (GTK_WINDOW (mmw->window), "mailbox_manager", "Balsa");
  gtk_widget_set_usize (mmw->window, 400, 300);
  gtk_window_position (GTK_WINDOW (mmw->window), GTK_WIN_POS_CENTER);

  gtk_signal_connect (GTK_OBJECT (mmw->window), 
		      "delete_event",
		      GTK_SIGNAL_FUNC (mailbox_manager_destroy),
		      NULL);


  vboxm = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (mmw->window), vboxm);
  gtk_widget_show (vboxm);


  hbox = gtk_hbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (hbox), 10);
  gtk_box_pack_start (GTK_BOX (vboxm), hbox, TRUE, TRUE, 10);
  gtk_widget_show (hbox);


  /* accounts clist */
  mmw->list = gtk_clist_new_with_titles (2, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (mmw->list));
  gtk_clist_set_selection_mode (GTK_CLIST (mmw->list), GTK_SELECTION_BROWSE);

  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 0, 100);
  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 1, 50);

  gtk_clist_set_policy (GTK_CLIST (mmw->list),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (hbox), mmw->list, TRUE, TRUE, 10);

  gtk_signal_connect (GTK_OBJECT (mmw->list),
		      "select_row",
		      (GtkSignalFunc) select_row_cb,
		      NULL);

  update_mailbox_list ();
  gtk_widget_show (mmw->list);


  /* one vbox to hold them all... */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 10);
  gtk_widget_show (vbox);


  /* edit account button */
  button = gtk_button_new_with_label ("Edit...");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) edit_cb,
		      NULL);

  gtk_widget_show (button);


  /* new account button */
  button = gtk_button_new_with_label ("New...");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) new_cb,
		      NULL);
  
  gtk_widget_show (button);


  /* duplicate account button */
  button = gtk_button_new_with_label ("Duplicate");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);
 
  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) duplicate_cb,
		      NULL);

  gtk_widget_show (button);


  /* delete account button */
  button = gtk_button_new_with_label ("Delete");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) delete_cb,
		      NULL);
  
  gtk_widget_show (button);


  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vboxm), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);


  /* close button */
  button = gtk_button_new_with_label ("Close");
  GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
  gtk_widget_grab_default (button);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      GTK_SIGNAL_FUNC (mailbox_manager_destroy),
		      NULL);

  gtk_signal_connect_object (GTK_OBJECT (button), 
			     "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (mmw->window));

  gtk_widget_show (button);


  /* show the whole thing */
  gtk_widget_show (mmw->window);
}

static gint
mailbox_manager_destroy ()
{
  g_free (mmw);
  mmw = NULL;

  return FALSE;
}

/* sets the list of mailboxes in the mailbox manager window */
static void
update_mailbox_list ()
{
  GList *list;
  Mailbox *mailbox;
  gchar *list_items[3];

  gtk_clist_freeze (GTK_CLIST (mmw->list));
  gtk_clist_clear (GTK_CLIST (mmw->list));

  list = balsa_app.mailbox_list;
  while (list)
    {
      mailbox = list->data;
      list = list->next;
      
      list_items[0] = mailbox->name;
      list_items[1] = mailbox_type_description (mailbox->type);
      
      gtk_clist_set_row_data (GTK_CLIST (mmw->list),
			      gtk_clist_append (GTK_CLIST (mmw->list), list_items),
			      mailbox);
    }

  gtk_clist_thaw (GTK_CLIST (mmw->list));
}

static void
select_row_cb (GtkWidget * widget,
	       gint row,
	       gint column,
	       GdkEventButton * bevent)
{
}

static void
edit_cb ()
{
  Mailbox *mailbox;

  mailbox = GTK_CLIST (mmw->list)->selection->data;
}

static void
new_cb ()
{
  Mailbox *mailbox;

  mailbox = GTK_CLIST (mmw->list)->selection->data;
  edit_mailbox_pop3 (NULL);
}

static void
duplicate_cb ()
{
  Mailbox *mailbox;

  mailbox = GTK_CLIST (mmw->list)->selection->data;

}

static void
delete_cb (GtkWidget * widget, gpointer something)
{
  Mailbox *mailbox;

  mailbox = GTK_CLIST (mmw->list)->selection->data;

}


/*
 * Mailbox Editing
 */
static void
edit_mailbox_pop3 (Mailbox * mailbox)
{
  GtkWidget *window;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;


  window = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_wmclass (GTK_WINDOW (window), "edit_mailbox_pop3", "Balsa");
  gtk_window_position (GTK_WINDOW (window), GTK_WIN_POS_CENTER);
  gtk_window_set_title (GTK_WINDOW (window), "Edit POP3 Account");
  gtk_container_border_width (GTK_CONTAINER (window), 3);

  gtk_signal_connect (GTK_OBJECT (window),
		      "delete_event",
		      GTK_SIGNAL_FUNC (edit_mailbox_destroy),
		      NULL);


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_widget_show (vbox);


  table = gtk_table_new (4, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 10);
  gtk_widget_show (table);


  /* name of the mailbox */
  label = gtk_label_new ("Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, 
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 0, 1, 
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (entry);


  /* POP server name */
  label = gtk_label_new ("POP3 server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (entry);


  /* username on POP3 server */
  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (entry);


  /* password on POP3 server */
  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, 
		    GTK_EXPAND | GTK_FILL, 
		    0, 0);
  gtk_widget_show (entry);


  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);


  /* okay button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_signal_connect (GTK_OBJECT (button), 
		      "clicked",
		       (GtkSignalFunc) ok_edit_mailbox_cb,
		      NULL);

  gtk_widget_show (button);


  /* cancel button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);

  gtk_signal_connect_object (GTK_OBJECT (button), 
			     "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));

  gtk_widget_show (button);


  gtk_widget_show (window);
}


static gint
edit_mailbox_destroy (GtkWidget * widget)
{
  return FALSE;
}


static void
ok_edit_mailbox_cb (GtkWidget * widget,
		    gpointer * data)
{
}
