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

#include "config.h"

#include <gnome.h>
#include "balsa-app.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "misc.h"
#include "save-restore.h"

/* we'll create the notebook pages in the
 * order of these enumerated types so they 
 * can be refered to easily
 */
typedef enum
  {
    MC_PAGE_NEW,
    MC_PAGE_LOCAL,
    MC_PAGE_POP3,
    MC_PAGE_IMAP,
  }
MailboxConfPageType;



typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow
  {
    Mailbox *mailbox;

    GtkWidget *bbox;
    GtkWidget *ok;
    GtkWidget *cancel;

    GtkWidget *window;
    GtkWidget *notebook;

    MailboxConfPageType next_page;

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

static MailboxConfWindow *mcw;


static MailboxType new_mailbox_type = -1;

/* callbacks */
static void mailbox_conf_close (GtkWidget * widget, gboolean save);
static void next_cb (GtkWidget * widget);

/* misc functions */
static void mailbox_conf_set_values (Mailbox * mailbox);

/* notebook pages */
static GtkWidget *create_new_page ();
static GtkWidget *create_local_mailbox_page ();
static GtkWidget *create_pop_mailbox_page ();
static GtkWidget *create_imap_mailbox_page ();


void
mailbox_conf_new (Mailbox * mailbox)
{
  GtkWidget *label;
  GtkWidget *bbox;

  if (mcw)
    return;

  mcw = g_malloc (sizeof (MailboxConfWindow));
  mcw->mailbox = mailbox;

  mcw->window = gtk_dialog_new ();
  gtk_window_set_title (GTK_WINDOW (mcw->window), _ ("Mailbox Configurator"));
  gtk_container_border_width (GTK_CONTAINER (mcw->window), 0);

  gtk_signal_connect (GTK_OBJECT (mcw->window),
		      "delete_event",
		      (GtkSignalFunc) mailbox_conf_close,
		      FALSE);


  /* notbook for action area of dialog */
  mcw->notebook = gtk_notebook_new ();
  gtk_container_border_width (GTK_CONTAINER (mcw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mcw->window)->vbox), mcw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (mcw->notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (mcw->notebook), FALSE);


  /* notebook pages */
  label = gtk_label_new ("np");
  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_new_page (),
			    label);

  label = gtk_label_new ("lp");
  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_local_mailbox_page (),
			    label);

  label = gtk_label_new ("pp");
  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_pop_mailbox_page (),
			    label);

  label = gtk_label_new ("ip");
  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_imap_mailbox_page (),
			    label);

  /* close button (bottom dialog) */
  mcw->bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mcw->window)->action_area), mcw->bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (mcw->bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (mcw->bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (mcw->bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (mcw->bbox);

  if (mailbox)
    {
      mcw->ok = gtk_button_new_with_label ("Update");
      gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  (GtkSignalFunc) mailbox_conf_close, TRUE);
    }

  /* cancel button */
  mcw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->cancel);
  gtk_signal_connect (GTK_OBJECT (mcw->cancel), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, FALSE);

  mailbox_conf_set_values (mailbox);

  gtk_widget_show_all (mcw->window);
}


static void
mailbox_conf_set_values (Mailbox * mailbox)
{
  if (!mailbox)
    return;

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_LOCAL);
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_path), MAILBOX_LOCAL (mailbox)->path);
	}
      break;
    case MAILBOX_POP3:
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (mcw->pop_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (mcw->pop_server), MAILBOX_POP3 (mailbox)->server);
	  gtk_entry_set_text (GTK_ENTRY (mcw->pop_username), MAILBOX_POP3 (mailbox)->user);
	  gtk_entry_set_text (GTK_ENTRY (mcw->pop_password), MAILBOX_POP3 (mailbox)->passwd);
	}
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
      break;
    case MAILBOX_IMAP:
      if (mailbox)
	{
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_mailbox_name), mailbox->name);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_server), MAILBOX_IMAP (mailbox)->server);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_mailbox_path), MAILBOX_IMAP (mailbox)->path);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_username), MAILBOX_IMAP (mailbox)->user);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_password), MAILBOX_IMAP (mailbox)->passwd);
	}
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_IMAP);
      break;
    }
}


static void
conf_update_mailbox (Mailbox * mailbox)
{
  if (!mailbox)
    return;

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MAILDIR:
    case MAILBOX_MH:

/* FIXME  -  need to reset mailbox type?
   menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (mcw->local_type_menu));
   menuitem = gtk_menu_get_active (GTK_MENU (menu));
 */
      g_free (mailbox->name);
      g_free (MAILBOX_LOCAL (mailbox)->path);
      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)));
      MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_path)));

      update_mailbox_config (mailbox);
      break;

    case MAILBOX_POP3:
      g_free (mailbox->name);
      g_free (MAILBOX_POP3 (mailbox)->user);
      g_free (MAILBOX_POP3 (mailbox)->passwd);
      g_free (MAILBOX_POP3 (mailbox)->server);

      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)));
      MAILBOX_POP3 (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)));
      MAILBOX_POP3 (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)));
      MAILBOX_POP3 (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)));

      update_mailbox_config (mailbox);
      break;

    case MAILBOX_IMAP:
      g_free (mailbox->name);
      g_free (MAILBOX_IMAP (mailbox)->user);
      g_free (MAILBOX_IMAP (mailbox)->passwd);
      g_free (MAILBOX_IMAP (mailbox)->path);
      g_free (MAILBOX_IMAP (mailbox)->server);

      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_name)));
      MAILBOX_IMAP (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
      MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
      MAILBOX_IMAP (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_path)));
      MAILBOX_IMAP (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
      MAILBOX_IMAP (mailbox)->port = strtol (gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

      update_mailbox_config (mailbox);
      break;
    }
}

static void
mailbox_conf_close (GtkWidget * widget, gboolean save)
{
  GtkWidget *menu;
  GtkWidget *menuitem;

  Mailbox *mailbox;
  GNode *node;

  mailbox = mcw->mailbox;

  if (mcw->mailbox && save)
    {
      conf_update_mailbox (mcw->mailbox);
      /* TODO cleanup */
      return;
    }

  if (save)
    switch (mcw->next_page)
      {

      case MC_PAGE_LOCAL:
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (mcw->local_type_menu));
	menuitem = gtk_menu_get_active (GTK_MENU (menu));

	mailbox = mailbox_new ((MailboxType) gtk_object_get_user_data (GTK_OBJECT (menuitem)));
	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)));
	MAILBOX_LOCAL (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_path)));
	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, FALSE));
	g_node_append (balsa_app.mailbox_nodes, node);

	add_mailbox_config (mailbox);
	break;

      case MC_PAGE_POP3:

	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (mcw->local_type_menu));
	menuitem = gtk_menu_get_active (GTK_MENU (menu));
	mailbox = mailbox_new (MAILBOX_POP3);
	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)));
	MAILBOX_POP3 (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)));
	MAILBOX_POP3 (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)));
	MAILBOX_POP3 (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)));
	balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
	add_mailbox_config (mailbox);
	break;

      case MC_PAGE_IMAP:
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (mcw->local_type_menu));
	menuitem = gtk_menu_get_active (GTK_MENU (menu));

	mailbox = mailbox_new (MAILBOX_IMAP);

	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_name)));
	MAILBOX_IMAP (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
	MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
	MAILBOX_IMAP (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_path)));
	MAILBOX_IMAP (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
	MAILBOX_IMAP (mailbox)->port = strtol (gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, FALSE));
	g_node_append (balsa_app.mailbox_nodes, node);
	add_mailbox_config (mailbox);
	break;
      }

  /* close the new mailbox window */
  gtk_widget_destroy (mcw->window);
  g_free (mcw);
  mcw = NULL;
}


static void
set_next_page (GtkWidget * widget, MailboxConfPageType type)
{
  mcw->next_page = type;
}

/*
 * create notebook pages
 */
static GtkWidget *
create_new_page ()
{
  GtkWidget *vbox;
  GtkWidget *bbox;
  GtkWidget *button;
  GtkWidget *radio_button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  /* radio buttons */
  /* local mailbox */
  radio_button = gtk_radio_button_new_with_label (NULL, "Local");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), MC_PAGE_LOCAL);
  gtk_widget_show (radio_button);

  /* pop3 mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "POP3");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), MC_PAGE_POP3);
  gtk_widget_show (radio_button);


  /* imap mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "IMAP");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), MC_PAGE_IMAP);
  gtk_widget_show (radio_button);

  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (vbox), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);

  /* ok button */
  button = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (bbox), button);
  gtk_signal_connect (GTK_OBJECT (button), "clicked", GTK_SIGNAL_FUNC (next_cb), NULL);
  gtk_widget_show (button);

  return vbox;
}



static GtkWidget *
create_local_mailbox_page ()
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


  mcw->local_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->local_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

/*
   gtk_signal_connect (GTK_OBJECT (mcw->local_mailbox_name),
   "changed",
   (GtkSignalFunc) local_mailbox_name_changed_cb,
   NULL);
 */
  gtk_widget_show (mcw->local_mailbox_name);


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

  menuitem =
    append_menuitem_connect (GTK_MENU (menu),
			     mailbox_type_description (MAILBOX_MH),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MH);

  menuitem =
    append_menuitem_connect (GTK_MENU (menu),
			     mailbox_type_description (MAILBOX_MAILDIR),
			     NULL,
			     NULL,
			     (gpointer) MAILBOX_MAILDIR);

  mcw->local_type_menu = gtk_option_menu_new ();
  gtk_widget_set_usize (mcw->local_type_menu, 0, BALSA_BUTTON_HEIGHT);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (mcw->local_type_menu), menu);
  gtk_table_attach (GTK_TABLE (table), mcw->local_type_menu, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL,
		    0, 10);
  gtk_widget_show (mcw->local_type_menu);



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
/*
   gtk_signal_connect (GTK_OBJECT (radio_button),
   "clicked",
   (GtkSignalFunc) local_mailbox_standard_path_cb,
   NULL);
 */
  gtk_widget_show (radio_button);


  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "Fixed Path");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);
/*
   gtk_signal_connect (GTK_OBJECT (radio_button),
   "clicked",
   (GtkSignalFunc) local_mailbox_fixed_path_cb,
   NULL);
 */
  gtk_widget_show (radio_button);

  mcw->local_mailbox_path = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->local_mailbox_path, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 0);

  gtk_widget_show (mcw->local_mailbox_path);
  gtk_widget_set_sensitive (mcw->local_mailbox_path, FALSE);


  return return_widget;
}

static GtkWidget *
create_pop_mailbox_page ()
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

  mcw->pop_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

/*
   gtk_signal_connect (GTK_OBJECT (mcw->pop_mailbox_name),
   "changed",
   (GtkSignalFunc) pop_mailbox_name_changed_cb,
   NULL);
 */

  gtk_widget_show (mcw->pop_mailbox_name);

  /* pop server */

  label = gtk_label_new ("Server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  mcw->pop_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_server, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->pop_server), "localhost");

  gtk_widget_show (mcw->pop_server);

  /* username  */

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  mcw->pop_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_username, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->pop_username), balsa_app.username);

  gtk_widget_show (mcw->pop_username);

  /* password field */

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  mcw->pop_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_password, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
  gtk_entry_set_visibility (GTK_ENTRY (mcw->pop_password), FALSE);

  gtk_widget_show (mcw->pop_password);
  return return_widget;
}


static GtkWidget *
create_imap_mailbox_page ()
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *menu;
  GtkWidget *menuitem;
  GtkWidget *frame;
  GtkWidget *radio_button;

  mcw->imap_use_fixed_path = FALSE;

  return_widget = table = gtk_table_new (6, 2, FALSE);
  gtk_widget_show (table);

  /* mailbox name */

  label = gtk_label_new ("IMAP Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);
  mcw->imap_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);
/*
   gtk_signal_connect (GTK_OBJECT (mcw->imap_mailbox_name),
   "changed",
   (GtkSignalFunc) imap_mailbox_name_changed_cb,
   NULL);
 */
  gtk_widget_show (mcw->imap_mailbox_name);

  /* imap server */

  label = gtk_label_new ("Server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  mcw->imap_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_server, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_server), "localhost");

  gtk_widget_show (mcw->imap_server);

  /* imap server port number */

  label = gtk_label_new ("Port:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);


  mcw->imap_port = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_port, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_port), "143");

  gtk_widget_show (mcw->imap_port);

  /* username  */

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  mcw->imap_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_username, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_username), balsa_app.username);

  gtk_widget_show (mcw->imap_username);

  /* password field */

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (label);
  mcw->imap_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_password, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_set_visibility (GTK_ENTRY (mcw->imap_password), FALSE);


  gtk_widget_show (mcw->imap_password);

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

/*
   gtk_signal_connect (GTK_OBJECT (radio_button),
   "clicked",
   (GtkSignalFunc) imap_mailbox_standard_path_cb,
   NULL);
 */
  gtk_widget_show (radio_button);


  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "Path ...");
  gtk_table_attach (GTK_TABLE (table), radio_button, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL | GTK_EXPAND,
		    0, 0);
/*
   gtk_signal_connect (GTK_OBJECT (radio_button),
   "clicked",
   (GtkSignalFunc) imap_mailbox_fixed_path_cb,
   NULL);
 */
  gtk_widget_show (radio_button);

  mcw->imap_mailbox_path = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_mailbox_path, 1, 2, 1, 2,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND,
		    0, 0);
  gtk_widget_show (mcw->imap_mailbox_path);
  gtk_widget_set_sensitive (mcw->imap_mailbox_path, FALSE);

  return return_widget;
}


/*
 * callbacks
 */
static void
next_cb (GtkWidget * widget)
{
  gtk_widget_destroy (mcw->bbox);
  mcw->bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (mcw->window)->action_area), mcw->bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (mcw->bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (mcw->bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (mcw->bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (mcw->bbox);

  if (mcw->mailbox)
    mcw->ok = gtk_button_new_with_label ("Update");
  else
    mcw->ok = gtk_button_new_with_label ("Add");
  gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->ok);
  gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, TRUE);

  mcw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->cancel);
  gtk_signal_connect (GTK_OBJECT (mcw->cancel), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, FALSE);

  gtk_widget_show_all (mcw->bbox);

  switch (mcw->next_page)
    {
    case MC_PAGE_LOCAL:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_LOCAL);
      break;

    case MC_PAGE_POP3:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
      break;

    case MC_PAGE_IMAP:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_IMAP);
      break;
    }
}
