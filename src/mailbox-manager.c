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

static void new_cb (GtkWidget *, GtkWidget *);
static void edit_cb (GtkWidget *, GtkWidget *);
static void duplicate_cb (GtkWidget *, GtkWidget *);
static void delete_cb (GtkWidget *, GtkWidget *);

static GtkWidget *notebook_create (void);


static GtkWidget *new_mailbox (void);
static GtkWidget *edit_local_mailbox (void);
static GtkWidget *edit_pop3_mailbox (void);
static GtkWidget *delete_yesno (void);


void
open_mailbox_manager ()
{
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *vboxm;
  GtkWidget *table;
  GtkWidget *hbox;
  GtkWidget *button;
  GtkWidget *vpane;
  GtkWidget *bcontents;
  GtkWidget *notebook = notebook_create ();

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

  vpane = gtk_vpaned_new ();
  gtk_container_add (GTK_CONTAINER (mmw->window), vpane);
  gtk_widget_show (vpane);


  vboxm = gtk_vbox_new (FALSE, 0);
  gtk_paned_add1 (GTK_PANED (vpane), vboxm);
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


  /* new account button */
  button = gtk_button_new_with_label ("New");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) new_cb,
		      notebook);

  gtk_widget_show (button);


  /* edit account button */
  button = gtk_button_new_with_label ("Edit");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);
  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) edit_cb,
		      notebook);

  gtk_widget_show (button);


  /* duplicate account button */
  button = gtk_button_new_with_label ("Duplicate");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) duplicate_cb,
		      notebook);

  gtk_widget_show (button);


  /* delete account button */
  button = gtk_button_new_with_label ("Delete");
  gtk_widget_set_usize (button, 70, 30);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, FALSE, 10);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) delete_cb,
		      notebook);

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

  gtk_paned_add2 (GTK_PANED (vpane), notebook);

  /* show the whole thing */
  gtk_widget_show (mmw->window);
}

static GtkWidget *
notebook_create ()
{
  GtkWidget *notebook;

  notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
  gtk_widget_show (notebook);
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), new_mailbox (), NULL, NULL);         /* 0 */
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), edit_local_mailbox (), NULL, NULL);  /* 1 */
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), edit_pop3_mailbox (), NULL, NULL);   /* 2 */
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), edit_pop3_mailbox (), NULL, NULL);   /* 3 */
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), edit_pop3_mailbox (), NULL, NULL);   /* 4 */
  gtk_notebook_append_page_menu (GTK_NOTEBOOK (notebook), delete_yesno (), NULL, NULL);        /* 5 */
  return notebook;
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

  list = g_list_first(balsa_app.mailbox_list);
  while (list)
    {
      mailbox = list->data;

      list_items[0] = mailbox->name;
      list_items[1] = mailbox_type_description (mailbox->type);

      gtk_clist_set_row_data (GTK_CLIST (mmw->list),
		       gtk_clist_append (GTK_CLIST (mmw->list), list_items),
			      mailbox);
      list = list->next;
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
new_cb (GtkWidget * widget, GtkWidget * notebook)
{
  Mailbox *mailbox;
  mailbox = GTK_CLIST (mmw->list)->selection->data;
  gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 0);
}

static void
edit_cb (GtkWidget * widget, GtkWidget * notebook)
{
  Mailbox *mailbox;
  mailbox = gtk_clist_get_row_data(GTK_CLIST(mmw->list),((gint) GTK_CLIST (mmw->list)->selection->data));
  switch (mailbox->type)
    {
    case MAILBOX_MBX:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_MTX:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_TENEX:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_MBOX:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_MMDF:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_UNIX:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_MH:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 1);
      break;
    case MAILBOX_POP3:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 2);
      break;
    case MAILBOX_IMAP:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 3);
      break;
    case MAILBOX_NNTP:
      gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 4);
      break;
    }
}

static void
duplicate_cb (GtkWidget * widget, GtkWidget * notebook)
{
  Mailbox *mailbox;
  mailbox = GTK_CLIST (mmw->list)->selection->data;
  gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 0);
}

static void
delete_cb (GtkWidget * widget, GtkWidget * notebook)
{
  Mailbox *mailbox;
  mailbox = GTK_CLIST (mmw->list)->selection->data;
  gtk_notebook_set_page (GTK_NOTEBOOK (notebook), 5);
}


/* АБВлллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллВБА */
/* New mailbox */

static GtkWidget *
new_mailbox (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (4, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 1);
  gtk_widget_show (table);

  label = gtk_label_new ("Name:");
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

  label = gtk_label_new ("Path:");
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

  button = gtk_button_new_with_label ("Update");
  gtk_table_attach (GTK_TABLE (table), button, 0, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (button);

  return vbox;
}


/* АБВлллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллВБА */
/* Edit - Local mailboxes */

static GtkWidget *
edit_local_mailbox (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (3, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 1);
  gtk_widget_show (table);


  label = gtk_label_new ("Name:");
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

  label = gtk_label_new ("Path:");
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

  button = gtk_button_new_with_label ("Update");
  gtk_table_attach (GTK_TABLE (table), button, 0, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (button);

  return vbox;
}


/* АБВлллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллВБА */
/* Edit - POP3 */

static GtkWidget *
edit_pop3_mailbox (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (4, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 1);
  gtk_widget_show (table);


  /* name of the mailbox */
  label = gtk_label_new ("Mailbox Name:");
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

  return vbox;
}


/* АБВлллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллллВБА */
/* Confirm delete */
static GtkWidget *
delete_yesno (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_widget_show (vbox);

  label = gtk_label_new ("Are you sure you wish to delete this mailbox?");
  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  button = gtk_button_new_with_label ("Delete");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Cancel");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox), button, FALSE, FALSE, 0);
  gtk_widget_show (button);

  return vbox;
}
