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
#include "pref-manager.h"
#include "balsa-app.h"

typedef struct _PreferencesManagerWindow PreferencesManagerWindow;
struct _PreferencesManagerWindow
{
  GtkWidget *window;

  /* identity */
  GtkWidget *user_name;
  GtkWidget *email;
  GtkWidget *organization;

  /* local */
  GtkWidget *mail_directory;

  /* servers */
  GtkWidget *smtp_server;

};

static PreferencesManagerWindow *pmw = NULL;

static gint preferences_manager_destroy ();


void
open_preferences_manager ()
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
