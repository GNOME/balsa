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
#include <errno.h>
#include <fcntl.h>

#include <gnome.h>
#include <string.h>

#include "balsa-app.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "misc.h"
#include "pref-manager.h"
#include "save-restore.h"
#include "mblist-window.h"

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
    Mailbox *current;

    GtkWidget *bbox;
    GtkWidget *ok;
    GtkWidget *cancel;

    GtkWidget *window;
    GtkWidget *notebook;

    MailboxConfPageType next_page;

    /* for local mailboxes */
    GtkWidget *local_mailbox_name;
    GtkWidget *local_mailbox_path;

    /* for imap mailboxes */

    GtkWidget *imap_server;
    GtkWidget *imap_port;
    GtkWidget *imap_username;
    GtkWidget *imap_password;

    GtkWidget *pop_mailbox_name;
    GtkWidget *pop_server;
    GtkWidget *pop_port;
    GtkWidget *pop_username;
    GtkWidget *pop_password;

  };

static MailboxConfWindow *mcw;


/* callbacks */
static void mailbox_conf_close (GtkWidget * widget, gboolean save);
static void next_cb (GtkWidget * widget);

/* misc functions */
static void mailbox_conf_set_values (Mailbox * mailbox);
static void mailbox_remove_files (gchar * name);
/* notebook pages */
static GtkWidget *create_new_page (void);
static GtkWidget *create_local_mailbox_page (void);
static GtkWidget *create_pop_mailbox_page (void);
static GtkWidget *create_imap_mailbox_page (void);


void mailbox_conf_edit_imap_server (GtkWidget * widget, gpointer data);

/*
 * Find  the named mailbox from the balsa_app.mailbox_nodes by it's
 * name
 */

static gint
find_mailbox_func (GNode * g1, gpointer data)
{
  MailboxNode *n1 = (MailboxNode *) g1->data;
  gpointer *d = data;
  gchar *name = *(gchar **) data;

  if (!n1)
    return FALSE;
  if (strcmp (n1->name, name) != 0)
    {
      return FALSE;
    }
  *(++d) = g1;
  return TRUE;
}


static GNode *
find_gnode_in_mbox_list (GNode * gnode_list, gchar * mbox_name)
{
  gpointer d[2];
  GNode *retval;

  d[0] = mbox_name;
  d[1] = NULL;

  g_node_traverse (gnode_list, G_IN_ORDER, G_TRAVERSE_LEAFS, -1, find_mailbox_func, d);
  retval = d[1];
  return retval;
}

void
mailbox_remove_files (gchar * mbox_path)
{
  gchar cmd[PATH_MAX + 8];
  snprintf (cmd, sizeof (cmd), "rm -rf '%s'", mbox_path);
  system (cmd);
}

void
mailbox_conf_delete (Mailbox * mailbox)
{
  GNode *gnode;
  gchar *msg;
  gint clicked_button;
  GtkWidget *ask;

  if (mailbox->type == MAILBOX_MH
      || mailbox->type == MAILBOX_MAILDIR
      || mailbox->type == MAILBOX_MBOX)
    {
      msg = _ ("This will remove the mailbox and it's files permanently from your system.\n"
	       "Are you sure you want to remove this mailbox?");
    }
  else
    {
      msg = _ ("This will remove the mailbox from the list of mailboxes\n"
	  "You may use \"Add Mailbox\" later to access this mailbox again\n"
	       "Are you sure you want to remove this mailbox?");
    }


  ask = gnome_message_box_new (msg,
			       GNOME_MESSAGE_BOX_QUESTION,
		       GNOME_STOCK_BUTTON_YES, GNOME_STOCK_BUTTON_NO, NULL);

  gnome_dialog_set_default (GNOME_DIALOG (ask), 1);
  gnome_dialog_set_modal (GNOME_DIALOG (ask));
  clicked_button = gnome_dialog_run (GNOME_DIALOG (ask));
  if (clicked_button == 1)
    {
      return;
    }

  /* Don't forget to remove the node from balsa's mailbox list */
  if (mailbox->type == MAILBOX_POP3)
    {
      g_list_remove (balsa_app.inbox_input, mailbox);
    }
  else
    {
      gnode = find_gnode_in_mbox_list (balsa_app.mailbox_nodes, mailbox->name);
      if (!gnode)
	{
	  fprintf (stderr, "Oooop! mailbox not found in balsa_app.mailbox "
		   "nodes?\n");
	}
      else
	{
	  g_node_unlink (gnode);
	}
    }
  /* Delete it from the config file and internal nodes */
  config_mailbox_delete (mailbox->name);

  /* Close the mailbox, in case it was open */
  if (mailbox->type != MAILBOX_POP3)
    mblist_close_mailbox (mailbox);

  /* Delete local files */
  if (mailbox->type == MAILBOX_MBOX
      || mailbox->type == MAILBOX_MAILDIR
      || mailbox->type == MAILBOX_MH)
    mailbox_remove_files (MAILBOX_LOCAL (mailbox)->path);

  if (mailbox->type == MAILBOX_POP3)
    update_pop3_servers ();
  else
    mblist_redraw ();
}


void
mailbox_conf_new (Mailbox * mailbox, gint add_mbox, MailboxType type)
{
  GtkWidget *label;

  if (mcw)
    return;

  mcw = g_malloc (sizeof (MailboxConfWindow));
  mcw->mailbox = mcw->current = 0;
  mcw->next_page = MC_PAGE_LOCAL;
  if (add_mbox)
    mcw->current = mailbox;
  else
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

  if (mcw->mailbox)
    {
      mcw->ok = gtk_button_new_with_label ("Update");
      gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  (GtkSignalFunc) mailbox_conf_close, (void *) TRUE);
    }

  else
    /* for new mailbox */
    {
      mcw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
      gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  GTK_SIGNAL_FUNC (next_cb), NULL);
    }


  /* cancel button */
  mcw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->cancel);
  gtk_signal_connect (GTK_OBJECT (mcw->cancel), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, FALSE);

  mailbox_conf_set_values (mcw->mailbox);

  switch (type)
    {
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
    case MAILBOX_MH:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_LOCAL);
      break;
    case MAILBOX_POP3:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
      break;
    case MAILBOX_IMAP:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_IMAP);
      break;
    default:
      break;
    }

  gtk_widget_show_all (mcw->window);
}


static void
mailbox_conf_set_values (Mailbox * mailbox)
{
  if (!mailbox)
    return;

  switch (mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
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
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_server), MAILBOX_IMAP (mailbox)->server);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_username), MAILBOX_IMAP (mailbox)->user);
	  gtk_entry_set_text (GTK_ENTRY (mcw->imap_password), MAILBOX_IMAP (mailbox)->passwd);
	  {
	    gchar tmp[10];
	    sprintf (tmp, "%i", MAILBOX_IMAP (mailbox)->port);
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_server), tmp);
	  }
	}
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_IMAP);
      break;
    case MAILBOX_UNKNOWN:
      /* do nothing for now */
      break;
    }
}


static void
conf_update_mailbox (Mailbox * mailbox, gchar * old_mbox_name)
{
  if (!mailbox)
    return;

  switch (mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
      {
	gchar *filename =
	gtk_entry_get_text (GTK_ENTRY (gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (mcw->local_mailbox_path))));
	g_free (mailbox->name);
	g_free (MAILBOX_LOCAL (mailbox)->path);
	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)));
	MAILBOX_LOCAL (mailbox)->path = g_strdup (filename);

	config_mailbox_update (mailbox, old_mbox_name);
      }
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

      config_mailbox_update (mailbox, old_mbox_name);
      break;

    case MAILBOX_IMAP:
      g_free (mailbox->name);
      g_free (MAILBOX_IMAP (mailbox)->user);
      g_free (MAILBOX_IMAP (mailbox)->passwd);
      g_free (MAILBOX_IMAP (mailbox)->path);
      g_free (MAILBOX_IMAP (mailbox)->server);

#if 0
      mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_name)));
      MAILBOX_IMAP (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
      MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
      MAILBOX_IMAP (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_path)));
      if (!MAILBOX_IMAP (mailbox)->path[0])
	{
	  g_free (MAILBOX_IMAP (mailbox)->path);
	  MAILBOX_IMAP (mailbox)->path = g_strdup ("INBOX");
	}
      MAILBOX_IMAP (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
      MAILBOX_IMAP (mailbox)->port = strtol (gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

      config_mailbox_update (mailbox, old_mbox_name);
#endif
      break;
    case MAILBOX_UNKNOWN:
      /* Do nothing for now */
      break;
    }
}

static void
mailbox_conf_close (GtkWidget * widget, gboolean save)
{
  Mailbox *mailbox;
  MailboxType type;
  GNode *node;

  mailbox = mcw->mailbox;

  if (mcw->mailbox && save)
    {
      gchar *old_mbox_name = g_strdup (mailbox->name);
      conf_update_mailbox (mcw->mailbox, old_mbox_name);
      g_free (old_mbox_name);
      if (mailbox->type == MAILBOX_POP3)
	update_pop3_servers ();
      else
	mblist_redraw ();
      /* TODO cleanup */
      return;
    }

  if (save)
    switch (mcw->next_page)
      {
      case MC_PAGE_LOCAL:
	{
	  gchar *filename = gtk_entry_get_text (GTK_ENTRY ((mcw->local_mailbox_path)));

	  type = mailbox_valid (filename);
	  if (type == MAILBOX_UNKNOWN)
	    {
	      int fd = creat (filename, S_IRUSR | S_IWUSR);
	      if (fd < 0)
		{
		  GtkWidget *msgbox;
		  gchar *ptr;
		  asprintf (&ptr, _ ("Cannot create mailbox '%s': %s\n"), filename, strerror (errno));
		  msgbox = gnome_message_box_new (ptr, GNOME_MESSAGE_BOX_ERROR, _ ("Cancel"), NULL);
		  free (ptr);
		  gnome_dialog_set_modal (GNOME_DIALOG (msgbox));
		  gnome_dialog_run (GNOME_DIALOG (msgbox));
		  return;
		}
	      close (fd);
	      type = MAILBOX_MBOX;
	    }
	  mailbox = mailbox_new (type);
	  mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)));
	  MAILBOX_LOCAL (mailbox)->path = g_strdup (filename);
	  node = g_node_new (mailbox_node_new (mailbox->name, mailbox,
					    mailbox->type != MAILBOX_MBOX));
	  g_node_append (balsa_app.mailbox_nodes, node);
	  config_mailbox_add (mailbox, NULL);
	}
	break;

      case MC_PAGE_POP3:
	mailbox = mailbox_new (MAILBOX_POP3);
	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)));
	MAILBOX_POP3 (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)));
	MAILBOX_POP3 (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)));
	MAILBOX_POP3 (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)));
	balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
	config_mailbox_add (mailbox, NULL);
	break;

      case MC_PAGE_IMAP:
#if 0
	mailbox = mailbox_new (MAILBOX_IMAP);
	mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_name)));
	MAILBOX_IMAP (mailbox)->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
	MAILBOX_IMAP (mailbox)->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
	MAILBOX_IMAP (mailbox)->path = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_mailbox_path)));
	if (!MAILBOX_IMAP (mailbox)->path[0])
	  {
	    g_free (MAILBOX_IMAP (mailbox)->path);
	    MAILBOX_IMAP (mailbox)->path = g_strdup ("INBOX");
	  }
	MAILBOX_IMAP (mailbox)->server = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
	MAILBOX_IMAP (mailbox)->port = strtol (gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

	node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
	g_node_append (balsa_app.mailbox_nodes, node);

	config_mailbox_add (mailbox, NULL);
#endif
	break;
      case MC_PAGE_NEW:
	g_warning ("mailbox_conf_close: Invalid mcw->next_page value\n");
	break;
      }

  if (mailbox)
    {
      if (mailbox->type == MAILBOX_POP3)
	update_pop3_servers ();
      else
	mblist_redraw ();
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
create_new_page (void)
{
  GtkWidget *vbox;
  GtkWidget *bbox;
  GtkWidget *radio_button;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  /* radio buttons */
  /* local mailbox */
  radio_button = gtk_radio_button_new_with_label (NULL, "Local");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), (void *) MC_PAGE_LOCAL);
  gtk_toggle_button_set_state (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_widget_show (radio_button);

  /* pop3 mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "POP3");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), (void *) MC_PAGE_POP3);
  gtk_widget_show (radio_button);


  /* imap mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), "IMAP");
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), (void *) MC_PAGE_IMAP);
  gtk_widget_show (radio_button);

  /* close button (bottom dialog) */
  bbox = gtk_hbutton_box_new ();
  gtk_box_pack_start (GTK_BOX (vbox), bbox, TRUE, TRUE, 0);
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox), BALSA_BUTTON_WIDTH, BALSA_BUTTON_HEIGHT);
  gtk_widget_show (bbox);


  return vbox;
}



static GtkWidget *
create_local_mailbox_page (void)
{
  GtkWidget *vbox;
  GtkWidget *label;
  GtkWidget *table;
  GtkWidget *file;

  vbox = gtk_vbox_new (FALSE, 0);

  table = gtk_table_new (2, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 5);

  /* mailbox name */
  label = gtk_label_new ("Mailbox Name:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  mcw->local_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->local_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  /* path to file */
  label = gtk_label_new ("Mailbox path:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL, 10, 10);

  file = gnome_file_entry_new ("Mailbox Path", "Mailbox Path");
  mcw->local_mailbox_path = gnome_file_entry_gtk_entry (GNOME_FILE_ENTRY (file));
  gtk_table_attach (GTK_TABLE (table), file, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  return vbox;
}

static GtkWidget *
create_pop_mailbox_page (void)
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

  gtk_entry_append_text (GTK_ENTRY (mcw->pop_username), g_get_user_name ());

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
create_imap_mailbox_page (void)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;
  GtkWidget *button;

  return_widget = gtk_vbox_new (0, FALSE);

  table = gtk_table_new (4, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (return_widget), table, FALSE, FALSE, 0);

  /* imap server */
  label = gtk_label_new ("Server:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_server, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_server), "localhost");

  /* imap server port number */
  label = gtk_label_new ("Port:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_port = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_port, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_port), "143");


  /* username  */
  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_username, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_username), g_get_user_name ());


  /* password field */
  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_password, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_set_visibility (GTK_ENTRY (mcw->imap_password), FALSE);

  button = gtk_button_new_with_label ("mailboxes");
  gtk_box_pack_start (GTK_BOX (return_widget), button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
		      (GtkSignalFunc) mailbox_conf_edit_imap_server, NULL);

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
    mcw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_container_add (GTK_CONTAINER (mcw->bbox), mcw->ok);
  gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, (void *) TRUE);

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
    case MC_PAGE_NEW:
      g_warning ("In mailbox_conf.c: next_cb: Invalid value of mcw->next_page\n");
      break;
    }
}

void
mailbox_conf_edit_imap_server (GtkWidget * widget, gpointer data)
{
  GtkWidget *window;
  GtkWidget *clist;
  gchar *titles[2] =
  {"S", "Mailbox"};

  gint clicked_button;

  window = gnome_dialog_new ("IMAP", GNOME_STOCK_BUTTON_CLOSE, NULL);

  clist = gtk_clist_new_with_titles (2, titles);
  gtk_clist_set_column_width (GTK_CLIST (clist), 1, 16);
  gtk_clist_set_policy (GTK_CLIST (clist), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (window)->vbox), clist, TRUE, TRUE, 0);
  gtk_widget_show (clist);

  titles[0] = NULL;
  titles[1] = "INBOX";
  gtk_clist_append (GTK_CLIST (clist), titles);

  clicked_button = gnome_dialog_run_and_destroy (GNOME_DIALOG (window));
}
