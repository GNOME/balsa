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



/* widget panels */
static GtkWidget * create_mailbox_list_frame ();
static GtkWidget * create_mailbox_edit_frame ();

static GtkWidget *nb_main_new_mailbox (void);
static GtkWidget *nb_main_edit_local (void);
static GtkWidget *nb_main_edit_pop3 (void);
static GtkWidget *nb_main_edit_imap (void);
static GtkWidget *nb_main_edit_nntp (void);
static GtkWidget *nb_main_delete_yesno (void);


static GtkWidget *nb_add_create (void);
static GtkWidget *nb_add_new_local (void);
static GtkWidget *nb_add_new_pop3 (void);
static GtkWidget *nb_add_new_imap (void);
static GtkWidget *nb_add_new_nntp (void);


/* callbacks */
static void close_mailbox_manager ();

static void select_row_cb (GtkWidget * widget, gint row, gint column, GdkEventButton * bevent);
static void new_cb (GtkWidget *, GtkWidget *);
static void edit_cb (GtkWidget *, GtkWidget *);
static void duplicate_cb (GtkWidget *, GtkWidget *);
static void delete_cb (GtkWidget *, GtkWidget *);
static void nb_add_cb (GtkWidget *, GtkWidget *);
static void nb_add_cb1 (GtkWidget *, GtkWidget *);
static void nb_add_cb2 (GtkWidget *, GtkWidget *);
static void nb_add_cb3 (GtkWidget *, GtkWidget *);



void
open_mailbox_manager ()
{
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *frame;



  /* only one mailbox manager window */
  if (mmw)
    return;

  mmw = g_malloc (sizeof (MailboxManagerWindow));


  /* dialog window */
  mmw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mmw->window), "Mailbox Manager");
  gtk_window_set_wmclass (GTK_WINDOW (mmw->window), "mailbox_manager", "Balsa");
  gtk_window_position (GTK_WINDOW (mmw->window), GTK_WIN_POS_CENTER);
  gtk_container_border_width (GTK_CONTAINER (mmw->window), 0);

  gtk_signal_connect (GTK_OBJECT (mmw->window),
		      "destroy",
		      (GtkSignalFunc) close_mailbox_manager,
		      NULL);

  gtk_signal_connect (GTK_OBJECT (mmw->window),
		      "delete_event",
		      (GtkSignalFunc) gtk_false,
		      NULL);


  
  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mmw->window)->vbox), hbox, TRUE, TRUE, 5);
  gtk_widget_show (hbox);


  /* dialog frames */
  frame = gtk_frame_new ("Mailbox List");
  gtk_container_add (GTK_CONTAINER (frame), create_mailbox_list_frame ());
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 5);
  gtk_widget_show (frame);

  
  frame = gtk_frame_new ("Edit/New Mailbox");
  gtk_container_add (GTK_CONTAINER (frame), create_mailbox_edit_frame ());
  gtk_box_pack_start (GTK_BOX (hbox), frame, TRUE, TRUE, 5);
  gtk_widget_show (frame);


  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  button = gnome_stock_button (GNOME_STOCK_BUTTON_CLOSE);
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect_object (GTK_OBJECT (button),
			     "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (mmw->window));

  gtk_widget_show (button);



  /* show the whole thing */
  refresh_mailbox_manager ();
  gtk_widget_show (mmw->window);
}


void
close_mailbox_manager ()
{
  g_free (mmw);
  mmw = NULL;
}




/* sets the list of mailboxes in the mailbox manager window */
void
refresh_mailbox_manager ()
{
  GList *list;
  Mailbox *mailbox;
  gchar *list_items[3];

  gtk_clist_freeze (GTK_CLIST (mmw->list));
  gtk_clist_clear (GTK_CLIST (mmw->list));

  list = g_list_first (balsa_app.mailbox_list);
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



static GtkWidget *
create_mailbox_list_frame ()
{
  GtkWidget *vbox;
  GtkWidget *bbox;
  GtkWidget *button;


  static gchar *titles[] =
  {
    "Account",
    "Type"
  };


  /* return widget */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_border_width (GTK_CONTAINER (vbox), 5);
  gtk_widget_show (vbox);


  /* accounts clist */
  mmw->list = gtk_clist_new_with_titles (2, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (mmw->list));
  gtk_clist_set_selection_mode (GTK_CLIST (mmw->list), GTK_SELECTION_SINGLE);

  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 0, 100);
  gtk_clist_set_column_width (GTK_CLIST (mmw->list), 1, 50);

  gtk_clist_set_policy (GTK_CLIST (mmw->list),
			GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);

  gtk_box_pack_start (GTK_BOX (vbox), mmw->list, TRUE, TRUE, 10);

  gtk_signal_connect (GTK_OBJECT (mmw->list),
		      "select_row",
		      (GtkSignalFunc) select_row_cb,
		      NULL);

  gtk_widget_show (mmw->list);


  /* one button bbox */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (vbox), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
				 BALSA_BUTTON_WIDTH,
				 BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  /* duplicate account button */
  button = gtk_button_new_with_label ("Duplicate");
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) duplicate_cb,
		      NULL);

  gtk_widget_show (button);


  /* delete account button */
  button = gtk_button_new_with_label ("Delete");
  gtk_container_add (GTK_CONTAINER (bbox), button);

  gtk_signal_connect (GTK_OBJECT (button),
		      "clicked",
		      (GtkSignalFunc) delete_cb,
		      NULL);

  gtk_widget_show (button);


  return vbox;
}



static GtkWidget *
create_mailbox_edit_frame ()
{
  GtkWidget *notebook;


  notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
  gtk_notebook_set_show_border ( GTK_NOTEBOOK(notebook), FALSE);
  gtk_widget_show (notebook);


  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), 
			    nb_main_new_mailbox (), 
			    gtk_label_new("new"));	/* 0 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_main_edit_local (), 
			    gtk_label_new("editlocal"));/* 1 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_main_edit_pop3 (),
			    gtk_label_new("editpop3"));	/* 2 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_main_edit_imap (),
			    gtk_label_new("editimap"));	/* 3 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_main_edit_nntp (),
			    gtk_label_new("editnntp"));	/* 4 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_main_delete_yesno (),
			    gtk_label_new("yesno"));	/* 5 */


  return notebook;
}



/*
 * callbacks
 */
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
nb_add_cb (GtkWidget *widget, GtkWidget *notebook)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 0);
}


static void
nb_add_cb1 (GtkWidget *widget, GtkWidget *notebook)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 1);
}


static void
nb_add_cb2 (GtkWidget *widget, GtkWidget *notebook)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 2);
}


static void
nb_add_cb3 (GtkWidget *widget, GtkWidget *notebook)
{
	gtk_notebook_set_page(GTK_NOTEBOOK(notebook), 3);
}


static void
edit_cb (GtkWidget * widget, GtkWidget * notebook)
{
  Mailbox *mailbox;
  mailbox = gtk_clist_get_row_data (GTK_CLIST (mmw->list), ((gint) GTK_CLIST (mmw->list)->selection->data));
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


static GtkWidget *
nb_add_create ()
{
  GtkWidget *notebook;

  notebook = gtk_notebook_new ();
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK(notebook), FALSE);
  gtk_widget_show (notebook);

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), 
			    nb_add_new_local (), 
			    gtk_label_new("local")); /* 0 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_add_new_pop3 (),
			    gtk_label_new("pop3"));   /* 1 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_add_new_imap (),
			    gtk_label_new("imap"));   /* 2 */

  gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
			    nb_add_new_nntp (),
			    gtk_label_new("nntp"));   /* 3 */


  return notebook;
}


static GtkWidget *
nb_add_new_local ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (1, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ("Path:");
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

  return vbox;
}


static GtkWidget *
nb_add_new_pop3 ()
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (3, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  /* POP server name */
  label = gtk_label_new ("POP3 server:");
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


  /* username on POP3 server */
  label = gtk_label_new ("Username:");
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


  /* password on POP3 server */
  label = gtk_label_new ("Password:");
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

  return vbox;
}


static GtkWidget *
nb_add_new_imap ()
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
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ("IMAP server:");
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

  label = gtk_label_new ("Username:");
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

  label = gtk_label_new ("Password:");
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

  label = gtk_label_new ("Mailbox:");
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


static GtkWidget *
nb_add_new_nntp ()
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
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ("NNTP server:");
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

  label = gtk_label_new ("Username:");
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

  label = gtk_label_new ("Password:");
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

  label = gtk_label_new ("Newsgroup:");
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


/*
 * New mailbox
 */
static GtkWidget *
nb_main_new_mailbox (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *mailboxtype;
  GtkWidget *notebook = nb_add_create();
  GtkWidget *menuofmailboxtypes;
  GtkWidget *menuitem;


  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);


  table = gtk_table_new (3, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
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


  label = gtk_label_new ("Type:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (label);

  mailboxtype = gtk_option_menu_new ();
  gtk_table_attach (GTK_TABLE (table), mailboxtype, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (mailboxtype);

  menuofmailboxtypes = gtk_menu_new ();

  menuitem = gtk_menu_item_new_with_label ("mbx");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);
  
  menuitem = gtk_menu_item_new_with_label ("mtx");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("tenex");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("mbox");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("mmdf");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("unix");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("mh");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("POP3");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb1,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("IMAP");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb2,
                   notebook);

  menuitem = gtk_menu_item_new_with_label ("NNTP");
  gtk_menu_append (GTK_MENU (menuofmailboxtypes), menuitem);
  gtk_widget_show (menuitem);
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
                   (GtkSignalFunc) nb_add_cb3,
                   notebook);

  gtk_option_menu_set_menu (GTK_OPTION_MENU (mailboxtype),
			    menuofmailboxtypes);

  gtk_box_pack_start (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);


  button = gtk_button_new_with_label ("Add");
  gtk_widget_set_usize (button, BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  return vbox;
}


/*
 * Edit - Local mailboxes
 */
static GtkWidget *
nb_main_edit_local (void)
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
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
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
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  return vbox;
}


/*
 * Edit - POP3
 */
static GtkWidget *
nb_main_edit_pop3 (void)
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
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
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



  button = gtk_button_new_with_label ("Update");
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);


  return vbox;
}


/* 
 * Edit - IMAP
 */
static GtkWidget *
nb_main_edit_imap (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
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


  /* IMAP server name */
  label = gtk_label_new ("IMAP server:");
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


  /* username on IMAP server */
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


  /* password on IMAP server */
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

  /* Mailbox on IMAP server */
  label = gtk_label_new ("Mailbox:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (entry);

  return vbox;
}


/*
 * Edit - NNTP
 */
static GtkWidget *
nb_main_edit_nntp (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *entry;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (5, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
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


  /* NNTP server name */
  label = gtk_label_new ("NNTP server:");
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


  /* username on NNTP server */
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


  /* password on NNTP server */
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

  /* Newsgroup on NNTP server */
  label = gtk_label_new ("Newsgroup:");
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (label);


  entry = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL,
		    GTK_EXPAND | GTK_FILL,
		    0, 0);
  gtk_widget_show (entry);

  return vbox;
}

/*
 * Confirm delete
 */
static GtkWidget *
nb_main_delete_yesno (void)
{
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *label;
  GtkWidget *button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  label = gtk_label_new ("Are you sure you wish to delete this mailbox?");
  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  hbox = gtk_hbox_new (TRUE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  button = gtk_button_new_with_label ("Yes");
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("No");
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  gtk_widget_show (button);

  return vbox;
}
