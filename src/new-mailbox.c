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
#include "main-window.h"
#include "misc.h"
#include "save-restore.h"

/* we'll create the notebook pages in the
 * order of these enumerated types so they 
 * can be refered to easily
 */
typedef enum
  {
    NM_PAGE_LOCAL,
    NM_PAGE_POP3,
    NM_PAGE_IMAP,
  }
NewMailboxPageType;



typedef struct _NewMailboxWindow NewMailboxWindow;
struct _NewMailboxWindow
  {
    Mailbox *mailbox;

    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *ok;
    GtkWidget *cancel;


    NewMailboxPageType next_page;


    /* for local mailboxes */
    GtkWidget *local_mailbox_name;
    GtkWidget *local_type_menu;
    GtkWidget *local_mailbox_path;

    /* for imap mailboxes */

    GtkWidget *imap_mailbox_name;
    GtkWidget *imap_server;
    GtkWidget *imap_port;
    GtkWidget *imap_mailbox_path;
    GtkWidget *imap_username;
    GtkWidget *imap_password;
    int imap_use_fixed_path;

    GtkWidget *pop_mailbox_name;
    GtkWidget *pop_server;
    GtkWidget *pop_port;
    GtkWidget *pop_username;
    GtkWidget *pop_password;

  };

static MailboxType new_mailbox_type = -1;
static GList *open_mailbox_list = NULL;

/* ask mailbox type stuff */
static void ask_mailbox_type ();

/* notebook pages */
static GtkWidget *create_local_mailbox_page ();
static GtkWidget *create_pop_mailbox_page ();
static GtkWidget *create_imap_mailbox_page ();


/* callbacks */
static void destroy_new_mailbox (GtkWidget * widget);
static void refresh_new_mailbox (NewMailboxWindow * nmw);

/* callbacks for new page */
static void local_mailbox_type_cb (GtkWidget * widget);
static void pop_mailbox_type_cb (GtkWidget * widget);
static void imap_mailbox_type_cb (GtkWidget * widget);

static void ok_new_mailbox (GtkWidget * widget);
static void close_new_mailbox (GtkWidget * widget);
static void next_cb (GtkWidget * widget);

/* callbacks for local page */
static void local_mailbox_name_changed_cb (GtkWidget * widget);
static void local_mailbox_standard_path_cb (GtkWidget * widget);
static void local_mailbox_fixed_path_cb (GtkWidget * widget);

/* callbacks for imap page */
static void imap_mailbox_name_changed_cb (GtkWidget * widget);
static void imap_mailbox_standard_path_cb (GtkWidget * widget);
static void imap_mailbox_fixed_path_cb (GtkWidget * widget);

/* what pop callbacks??? */

/* misc */
static void set_new_mailbox_data (GtkObject * object, NewMailboxWindow * nmw);
static NewMailboxWindow *get_new_mailbox_data (GtkObject * object);

void
new_mailbox_dlg ()
{
  ask_mailbox_type ();
}

void
edit_mailbox_dlg (Mailbox * mailbox, MailboxType type)
{
  NewMailboxWindow *nmw;
  GList *list;
  GtkWidget *bbox;
  GtkWidget *label;

  new_mailbox_type = -1;

#if 0
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
#endif

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
  gtk_notebook_set_show_border (GTK_NOTEBOOK (nmw->notebook), FALSE);
  gtk_widget_show (nmw->notebook);


  /* notebook pages */
  label = gtk_label_new ("lp");
  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_local_mailbox_page (nmw),
			    label);

  label = gtk_label_new ("pp");
  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_pop_mailbox_page (nmw),
			    label);

  label = gtk_label_new ("ip");
  gtk_notebook_append_page (GTK_NOTEBOOK (nmw->notebook),
			    create_imap_mailbox_page (nmw),
			    label);

  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (nmw->window)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  /* ok button */
  nmw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (bbox), nmw->ok);

  set_new_mailbox_data (GTK_OBJECT (nmw->ok), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->ok),
		      "clicked",
		      (GtkSignalFunc) ok_new_mailbox,
		      NULL);

  gtk_widget_show (nmw->ok);


  /* cancel button */
  nmw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (bbox), nmw->cancel);

  set_new_mailbox_data (GTK_OBJECT (nmw->cancel), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->cancel),
		      "clicked",
		      (GtkSignalFunc) close_new_mailbox,
		      NULL);

  gtk_widget_show (nmw->cancel);

  refresh_new_mailbox (nmw);

  switch (type)
    {
    case MAILBOX_MBOX:
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_LOCAL);
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), MAILBOX_LOCAL (mailbox)->path);
	}
      break;
    case MAILBOX_POP3:
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (nmw->pop_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (nmw->pop_server), MAILBOX_POP3 (mailbox)->server);
	  gtk_entry_set_text (GTK_ENTRY (nmw->pop_username), MAILBOX_POP3 (mailbox)->user);
	  gtk_entry_set_text (GTK_ENTRY (nmw->pop_password), MAILBOX_POP3 (mailbox)->passwd);
	}
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_POP3);
      break;
    case MAILBOX_IMAP:
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (nmw->imap_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (nmw->imap_server), MAILBOX_IMAP (mailbox)->server);
	  gtk_entry_set_text (GTK_ENTRY (nmw->imap_mailbox_path), MAILBOX_IMAP (mailbox)->path);
	  gtk_entry_set_text (GTK_ENTRY (nmw->imap_username), MAILBOX_IMAP (mailbox)->user);
	  gtk_entry_set_text (GTK_ENTRY (nmw->imap_password), MAILBOX_IMAP (mailbox)->passwd);
	}
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_IMAP);
      break;
    }

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
ok_new_mailbox (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GtkWidget *menu;
  GtkWidget *menuitem;

  Mailbox *mailbox;
  GNode *node;

  switch (nmw->next_page)
    {
    case NM_PAGE_LOCAL:
      menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (nmw->local_type_menu));
      menuitem = gtk_menu_get_active (GTK_MENU (menu));

      mailbox = mailbox_new ((MailboxType) gtk_object_get_user_data (GTK_OBJECT (menuitem)));
      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_name)));
      MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_path)));
      node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);

      add_mailbox_config (mailbox);
      break;

    case NM_PAGE_POP3:

      menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (nmw->local_type_menu));
      menuitem = gtk_menu_get_active (GTK_MENU (menu));
      mailbox = mailbox_new (MAILBOX_POP3);
      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->pop_mailbox_name)));
      MAILBOX_POP3 (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->pop_username)));
      MAILBOX_POP3 (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->pop_password)));
      MAILBOX_POP3 (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->pop_server)));
      balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
      add_mailbox_config (mailbox);
      break;

    case NM_PAGE_IMAP:

      menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (nmw->local_type_menu));
      menuitem = gtk_menu_get_active (GTK_MENU (menu));

      mailbox = mailbox_new (MAILBOX_IMAP);

      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->imap_mailbox_name)));
      MAILBOX_IMAP (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->imap_username)));
      MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->imap_password)));
      MAILBOX_IMAP (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->imap_mailbox_path)));
      MAILBOX_IMAP (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (nmw->imap_server)));
      MAILBOX_IMAP (mailbox)->port = strtol (gtk_entry_get_text (GTK_ENTRY (nmw->imap_port)), (char **) NULL, 10);

      node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
      add_mailbox_config (mailbox);
      break;
    }


  /* close the new mailbox window */
  refresh_mailbox_manager ();
  refresh_main_window ();
  gtk_widget_destroy (nmw->window);
}


static void
refresh_new_mailbox (NewMailboxWindow * nmw)
{
  GString *str = g_string_new (NULL);

  if (nmw->mailbox == NULL)
    {
      nmw->next_page = NM_PAGE_LOCAL;
      gtk_window_set_title (GTK_WINDOW (nmw->window), "New Mailbox");
    }
  else
    {
      g_string_assign (str, "Edit ");
      g_string_append (str, nmw->mailbox->name);
      gtk_window_set_title (GTK_WINDOW (nmw->window), str->str);
    }

  /* cleanup */
  g_string_free (str, TRUE);
}

static void
continue_ask_new_mailbox (GtkWidget * widget, gpointer data)
{
  edit_mailbox_dlg (NULL, new_mailbox_type);
}

static void
close_ask_new_mailbox (GtkWidget * widget, GtkWidget * window)
{
  gtk_widget_destroy (window);
}

static void
set_new_mailbox_type (GtkWidget * widget, MailboxType type)
{
  new_mailbox_type = type;
}

/*
 * create notebook pages
 */
static void
ask_mailbox_type ()
{
  GtkWidget *dialog;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *radio_button;

  dialog = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (dialog), "New Mailbox");
  gtk_container_border_width (GTK_CONTAINER (dialog), 0);

  gtk_signal_connect (GTK_OBJECT (dialog), "delete_event", GTK_SIGNAL_FUNC (gtk_false), NULL);

  /* radio buttons */
  /* local mailbox */
  radio_button = gtk_radio_button_new_with_label (NULL, "Local");
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_new_mailbox_type), MAILBOX_MBOX);
  gtk_widget_show (radio_button);

  /* pop3 mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "POP3");
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_new_mailbox_type), MAILBOX_POP3);
  gtk_widget_show (radio_button);


  /* imap mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "IMAP");
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_new_mailbox_type), MAILBOX_IMAP);
  gtk_widget_show (radio_button);

  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->action_area), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);

  /* ok button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (continue_ask_new_mailbox), NULL);
  gtk_widget_show (button);


  /* cancel button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (close_ask_new_mailbox), dialog);
  gtk_widget_show (button);

  gtk_widget_show (dialog);
}



static GtkWidget *
create_local_mailbox_page (NewMailboxWindow * nmw)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *menu;
  GtkWidget *menuitem;
  GtkWidget *frame;
  GtkWidget *radio_button;


  return_widget = table = gtk_table_new (3, 2, FALSE);
  gtk_widget_show (table);


  /* mailbox name */
  label = gtk_label_new ("Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  nmw->local_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->local_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  set_new_mailbox_data (GTK_OBJECT (nmw->local_mailbox_name), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->local_mailbox_name),
		      "changed",
		      (GtkSignalFunc) local_mailbox_name_changed_cb,
		      NULL);

  gtk_widget_show (nmw->local_mailbox_name);


  /* mailbox type */
  label = gtk_label_new ("Mailbox Type:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);

  menu = gtk_menu_new ();

  menuitem =
    append_menuitem_connect (GTK_MENU (menu),
			     mailbox_type_description (MAILBOX_MBOX),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MBOX);
  set_new_mailbox_data (GTK_OBJECT (menuitem), nmw);

  menuitem =
    append_menuitem_connect (GTK_MENU (menu),
			     mailbox_type_description (MAILBOX_MH),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MH);
  set_new_mailbox_data (GTK_OBJECT (menuitem), nmw);

  menuitem =
    append_menuitem_connect (GTK_MENU (menu),
			     mailbox_type_description (MAILBOX_MAILDIR),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MAILDIR);

  nmw->local_type_menu = gtk_option_menu_new ();
  gtk_widget_set_usize (nmw->local_type_menu, 0, BALSA_BUTTON_HEIGHT);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (nmw->local_type_menu), menu);
  gtk_table_attach (GTK_TABLE (table), nmw->local_type_menu, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL,
		    0, 10);
  gtk_widget_show (nmw->local_type_menu);



  /* mailbox path */
  frame = gtk_frame_new ("Mailbox Path");
  gtk_table_attach (GTK_TABLE (table), frame, 0, 2, 2, 3,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 10);
  gtk_widget_show (frame);

  table = gtk_table_new (2, 2, FALSE);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);


  radio_button = gtk_radio_button_new_with_label (NULL, "Standard Mailbox Path");
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) local_mailbox_standard_path_cb,
		      NULL);

  gtk_widget_show (radio_button);


  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "Fixed Path");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) local_mailbox_fixed_path_cb,
		      NULL);

  gtk_widget_show (radio_button);

  nmw->local_mailbox_path = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->local_mailbox_path, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 0);
  set_new_mailbox_data (GTK_OBJECT (nmw->local_mailbox_path), nmw);
  gtk_widget_show (nmw->local_mailbox_path);
  gtk_widget_set_sensitive (nmw->local_mailbox_path, FALSE);


  return return_widget;
}

static GtkWidget *
create_pop_mailbox_page (NewMailboxWindow * nmw)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;

  return_widget = table = gtk_table_new (4, 2, FALSE);
  gtk_widget_show (table);

  /* mailbox name */

  label = gtk_label_new ("Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  nmw->pop_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->pop_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  set_new_mailbox_data (GTK_OBJECT (nmw->pop_mailbox_name), nmw);

/*
   gtk_signal_connect (GTK_OBJECT (nmw->pop_mailbox_name),
   "changed",
   (GtkSignalFunc) pop_mailbox_name_changed_cb,
   NULL);
 */

  gtk_widget_show (nmw->pop_mailbox_name);

  /* pop server */

  label = gtk_label_new ("Server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  nmw->pop_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->pop_server, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (nmw->pop_server), "localhost");

  set_new_mailbox_data (GTK_OBJECT (nmw->pop_server), nmw);

  gtk_widget_show (nmw->pop_server);

  /* username  */

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  nmw->pop_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->pop_username, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (nmw->pop_username), balsa_app.username);
  set_new_mailbox_data (GTK_OBJECT (nmw->pop_username), nmw);

  gtk_widget_show (nmw->pop_username);

  /* password field */

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  nmw->pop_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->pop_password, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_entry_set_visibility (GTK_ENTRY (nmw->pop_password), FALSE);

  set_new_mailbox_data (GTK_OBJECT (nmw->pop_password), nmw);

  gtk_widget_show (nmw->pop_password);
  return return_widget;
}


static GtkWidget *
create_imap_mailbox_page (NewMailboxWindow * nmw)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *menu;
  GtkWidget *menuitem;
  GtkWidget *frame;
  GtkWidget *radio_button;

  nmw->imap_use_fixed_path = FALSE;

  return_widget = table = gtk_table_new (6, 2, FALSE);
  gtk_widget_show (table);

  /* mailbox name */

  label = gtk_label_new ("IMAP Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);
  nmw->imap_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  set_new_mailbox_data (GTK_OBJECT (nmw->imap_mailbox_name), nmw);

  gtk_signal_connect (GTK_OBJECT (nmw->imap_mailbox_name),
		      "changed",
		      (GtkSignalFunc) imap_mailbox_name_changed_cb,
		      NULL);

  gtk_widget_show (nmw->imap_mailbox_name);

  /* imap server */

  label = gtk_label_new ("Server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  nmw->imap_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_server, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (nmw->imap_server), "localhost");

  set_new_mailbox_data (GTK_OBJECT (nmw->imap_server), nmw);

  gtk_widget_show (nmw->imap_server);

  /* imap server port number */

  label = gtk_label_new ("Port:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  nmw->imap_port = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_port, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (nmw->imap_port), "143");

  set_new_mailbox_data (GTK_OBJECT (nmw->imap_port), nmw);
  gtk_widget_show (nmw->imap_port);

  /* username  */

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  nmw->imap_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_username, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (nmw->imap_username), balsa_app.username);
  set_new_mailbox_data (GTK_OBJECT (nmw->imap_username), nmw);

  gtk_widget_show (nmw->imap_username);

  /* password field */

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  nmw->imap_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_password, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_set_visibility (GTK_ENTRY (nmw->imap_password), FALSE);

  set_new_mailbox_data (GTK_OBJECT (nmw->imap_password), nmw);

  gtk_widget_show (nmw->imap_password);

  /* imap mailbox path */

  frame = gtk_frame_new ("Mailbox Path");
  gtk_table_attach (GTK_TABLE (table), frame, 0, 2, 5, 6,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 10);
  gtk_widget_show (frame);

  table = gtk_table_new (2, 2, FALSE);
  gtk_container_border_width (GTK_CONTAINER (table), 5);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);

  radio_button = gtk_radio_button_new_with_label (NULL, "INBOX");
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);

  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) imap_mailbox_standard_path_cb,
		      NULL);

  gtk_widget_show (radio_button);


  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "Path ...");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);

  set_new_mailbox_data (GTK_OBJECT (radio_button), nmw);
  gtk_signal_connect (GTK_OBJECT (radio_button),
		      "clicked",
		      (GtkSignalFunc) imap_mailbox_fixed_path_cb,
		      NULL);

  gtk_widget_show (radio_button);

  nmw->imap_mailbox_path = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), nmw->imap_mailbox_path, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 0);
  set_new_mailbox_data (GTK_OBJECT (nmw->imap_mailbox_path), nmw);
  gtk_widget_show (nmw->imap_mailbox_path);
  gtk_widget_set_sensitive (nmw->imap_mailbox_path, FALSE);

  return return_widget;
}


/*
 * callbacks
 */
static void
local_mailbox_type_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  NewMailboxPageType type = (NewMailboxPageType) gtk_object_get_user_data (GTK_OBJECT (widget));
  nmw->next_page = type;
}

static void
pop_mailbox_type_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  NewMailboxPageType type = (NewMailboxPageType) gtk_object_get_user_data (GTK_OBJECT (widget));
  nmw->next_page = type;
}


static void
imap_mailbox_type_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  NewMailboxPageType type = (NewMailboxPageType) gtk_object_get_user_data (GTK_OBJECT (widget));
  nmw->next_page = type;
}

static void
next_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  switch (nmw->next_page)
    {
    case NM_PAGE_LOCAL:
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_LOCAL);
      break;

    case NM_PAGE_POP3:
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_POP3);
      break;

    case NM_PAGE_IMAP:
      gtk_notebook_set_page (GTK_NOTEBOOK (nmw->notebook), NM_PAGE_IMAP);
      break;
    }
}


/*
 * local page callbacks
 */
static void
local_mailbox_name_changed_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GString *str;

  str = g_string_new (balsa_app.local_mail_directory);
  g_string_append_c (str, '/');
  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (widget)));

  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), str->str);
  g_string_free (str, TRUE);
}


static void
local_mailbox_standard_path_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GString *str;

  str = g_string_new (balsa_app.local_mail_directory);
  g_string_append_c (str, '/');
  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (nmw->local_mailbox_name)));

  gtk_widget_set_sensitive (nmw->local_mailbox_path, FALSE);
  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), str->str);

  g_string_free (str, TRUE);

}

static void
local_mailbox_fixed_path_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  gtk_widget_set_sensitive (nmw->local_mailbox_path, TRUE);
  gtk_entry_set_text (GTK_ENTRY (nmw->local_mailbox_path), "");
}


/*
 * pop3 page callbacks
 *
 * there are none
 */

/*
 * imap4 page callbacks
 */

static void
imap_mailbox_name_changed_cb (GtkWidget * widget)
{
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));
  GString *str;

  str = g_string_new ("INBOX");

  if (nmw->imap_use_fixed_path)
    {

      g_string_append_c (str, '.');
      g_string_append (str, gtk_entry_get_text (GTK_ENTRY (widget)));

    }

  gtk_entry_set_text (GTK_ENTRY (nmw->imap_mailbox_path), str->str);
  g_string_free (str, TRUE);
}

static void
imap_mailbox_fixed_path_cb (GtkWidget * widget)
{

  GString *str;
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  str = g_string_new ("INBOX.");
  g_string_append (str, gtk_entry_get_text (GTK_ENTRY (nmw->imap_mailbox_name)));
  gtk_entry_set_text (GTK_ENTRY (nmw->imap_mailbox_path), str->str);
  g_string_free (str, TRUE);
  gtk_widget_set_sensitive (nmw->imap_mailbox_path, TRUE);
  nmw->imap_use_fixed_path = TRUE;
}

static void
imap_mailbox_standard_path_cb (GtkWidget * widget)
{

  GString *str;
  NewMailboxWindow *nmw = get_new_mailbox_data (GTK_OBJECT (widget));

  str = g_string_new ("INBOX");
  gtk_widget_set_sensitive (nmw->imap_mailbox_path, FALSE);
  gtk_entry_set_text (GTK_ENTRY (nmw->imap_mailbox_path), str->str);
  g_string_free (str, TRUE);
  nmw->imap_use_fixed_path = FALSE;
}


/*
 * set/get data convience functions used for attaching the
 * NewMailboxWindow structure to GTK objects so it can be retrieved
 * in callbacks
 */
static void
set_new_mailbox_data (GtkObject * object, NewMailboxWindow * nmw)
{
  gtk_object_set_data (object, "new_mailbox_data", (gpointer) nmw);
}


static NewMailboxWindow *
get_new_mailbox_data (GtkObject * object)
{
  return gtk_object_get_data (object, "new_mailbox_data");
}
