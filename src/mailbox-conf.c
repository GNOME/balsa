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

    GtkWidget *ok;
    GtkWidget *cancel;

    GtkWidget *window;
    GtkWidget *notebook;

    MailboxConfPageType next_page;

    /* for local mailboxes */
    GtkWidget *local_mailbox_name;
    GtkWidget *local_mailbox_path;

    /* for imap mailboxes */
    GtkWidget *imap_mailbox_name;
    GtkWidget *imap_server;
    GtkWidget *imap_port;
    GtkWidget *imap_username;
    GtkWidget *imap_password;
    GtkWidget *imap_folderpath;

    /* for pop3 mailboxes */
    GtkWidget *pop_mailbox_name;
    GtkWidget *pop_server;
    GtkWidget *pop_port;
    GtkWidget *pop_username;
    GtkWidget *pop_password;
    GtkWidget *pop_check;
    GtkWidget *pop_delete_from_server;

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
  Mailbox *mb = *(Mailbox**) data;

  if (!n1 || n1->mailbox != mb)
     return FALSE;

  *(++d) = g1;
  return TRUE;
}


static GNode *
find_gnode_in_mbox_list (GNode * gnode_list, Mailbox * mailbox)
{
  gpointer d[2];
  GNode *retval;

  d[0] = mailbox;
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
  gchar *msg, *msg1;
  gint clicked_button;
  GtkWidget *ask;

  if (mailbox->type == MAILBOX_MH
      || mailbox->type == MAILBOX_MAILDIR
      || mailbox->type == MAILBOX_MBOX)
    {
      msg = _("This will remove the mailbox %s and it's files permanently from your system.\n"
	       "Are you sure you want to remove this mailbox?");
    }
  else
    {
      msg = _("This will remove the mailbox %s from the list of mailboxes\n"
	  "You may use \"Add Mailbox\" later to access this mailbox again\n"
	       "Are you sure you want to remove this mailbox?");
    }

  msg1 = g_strdup_printf(msg, mailbox->name);
  ask = gnome_message_box_new (msg1,
			       GNOME_MESSAGE_BOX_QUESTION,
		       GNOME_STOCK_BUTTON_YES, GNOME_STOCK_BUTTON_NO, NULL);
  g_free(msg1);

  gnome_dialog_set_default (GNOME_DIALOG (ask), 1);
  gtk_window_set_modal (GTK_WINDOW (ask), TRUE);
  clicked_button = gnome_dialog_run (GNOME_DIALOG (ask));
  if (clicked_button == 1)
    {
      return;
    }

  /* Don't forget to remove the node from balsa's mailbox list */
  if (mailbox->type == MAILBOX_POP3)
    {
      balsa_app.inbox_input = g_list_remove (balsa_app.inbox_input, mailbox);
    }
  else
    {
      gnode = find_gnode_in_mbox_list (balsa_app.mailbox_nodes, mailbox);
      if (!gnode)
	{
	  fprintf (stderr, _("Oooop! mailbox not found in balsa_app.mailbox "
		   "nodes?\n"));
	}
      else
	{
	  g_node_unlink (gnode);
	}
    }
  /* Delete it from the config file and internal nodes */
  config_mailbox_delete (mailbox);

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
    balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));
}


void
mailbox_conf_new (Mailbox * mailbox, gint add_mbox, MailboxType type)
{
  GtkWidget *bbox;

  if (mcw)
    return;

  mcw = g_malloc (sizeof (MailboxConfWindow));
  mcw->mailbox = mcw->current = 0;
  mcw->next_page = MC_PAGE_LOCAL;	/* default next page to LOCAL */
  if (add_mbox)
    mcw->current = mailbox;
  else
    mcw->mailbox = mailbox;

  mcw->window = gnome_dialog_new (_("Mailbox Configurator"), NULL);
  gnome_dialog_set_parent (GNOME_DIALOG (mcw->window), GTK_WINDOW (balsa_app.main_window));

  gtk_signal_connect (GTK_OBJECT (mcw->window),
		      "delete_event",
		      (GtkSignalFunc) mailbox_conf_close,
		      FALSE);


  /* notbook for action area of dialog */
  mcw->notebook = gtk_notebook_new ();
  gtk_container_set_border_width (GTK_CONTAINER (mcw->notebook), 5);
  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (mcw->window)->vbox),
		      mcw->notebook, TRUE, TRUE, 0);
  gtk_notebook_set_show_tabs (GTK_NOTEBOOK (mcw->notebook), FALSE);
  gtk_notebook_set_show_border (GTK_NOTEBOOK (mcw->notebook), FALSE);

  /* notebook pages */
  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_new_page (),
			    NULL);

  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_local_mailbox_page (),
			    NULL);

  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_pop_mailbox_page (),
			    NULL);

  gtk_notebook_append_page (GTK_NOTEBOOK (mcw->notebook),
			    create_imap_mailbox_page (),
			    NULL);

  /* close button (bottom dialog) */
  bbox = GNOME_DIALOG (mcw->window)->action_area;
  gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
  gtk_button_box_set_spacing (GTK_BUTTON_BOX (bbox), 5);
  gtk_button_box_set_child_size (GTK_BUTTON_BOX (bbox),
			   BALSA_BUTTON_WIDTH / 2, BALSA_BUTTON_HEIGHT / 2);

  if (type != MAILBOX_UNKNOWN)
    {
      GtkWidget *pixmap;
      pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_NEW);
      mcw->ok = gnome_pixmap_button (pixmap, _("Add"));
      gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  (GtkSignalFunc) mailbox_conf_close, (void *) TRUE);

    }
  else if (mcw->mailbox)
    {
      GtkWidget *pixmap;
      pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_SAVE);
      mcw->ok = gnome_pixmap_button (pixmap, _("Update"));
      gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  (GtkSignalFunc) mailbox_conf_close, (void *) TRUE);
    }

  else
    /* for new mailbox */
    {
      mcw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
      gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			  GTK_SIGNAL_FUNC (next_cb), NULL);
    }


  /* cancel button */
  mcw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (bbox), mcw->cancel);
  gtk_signal_connect (GTK_OBJECT (mcw->cancel), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, FALSE);

  gtk_widget_show_all (mcw->window);

  if(type == MAILBOX_UNKNOWN)
    mailbox_conf_set_values (mcw->mailbox);
  else {
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
  }

}


static void
mailbox_conf_set_values (Mailbox * mailbox)
{
    char port[10]; /* Max size of the number is 65536... we're okay */
    
    if (!mailbox)
	return;
    
    switch (mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
	gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), 
			       MC_PAGE_LOCAL);
	if (mailbox) {
	    gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_name), 
				mailbox->name);
	    gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_path), 
				MAILBOX_LOCAL (mailbox)->path);
	}
	break;
    case MAILBOX_POP3:
	if (mailbox)
	{
	    sprintf( port, "%d", MAILBOX_POP3( mailbox )->server->port );
	    
	    gtk_entry_set_text (GTK_ENTRY (mcw->pop_mailbox_name),
				mailbox->name);
	    gtk_entry_set_text (
		GTK_ENTRY (mcw->pop_server),
		MAILBOX_POP3 (mailbox)->server->host);
	    gtk_entry_set_text (GTK_ENTRY (mcw->pop_port), port);
	    gtk_entry_set_text (
		GTK_ENTRY (mcw->pop_username), 
		MAILBOX_POP3 (mailbox)->server->user);
	    gtk_entry_set_text (
		GTK_ENTRY (mcw->pop_password), 
		MAILBOX_POP3 (mailbox)->server->passwd);
	    gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (mcw->pop_check), 
		MAILBOX_POP3 (mailbox)->check);
	    gtk_toggle_button_set_active (
		GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server), 
		MAILBOX_POP3 (mailbox)->delete_from_server);
	}
	gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
	break;
    case MAILBOX_IMAP:
	if (mailbox)
	{
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_mailbox_name), 
				mailbox->name);
	    sprintf( port, "%d", MAILBOX_IMAP( mailbox )->server->port );
	    
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_server), 
				MAILBOX_IMAP(mailbox)->server->host);
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_username), 
				MAILBOX_IMAP(mailbox)->server->user);
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_password), 
				MAILBOX_IMAP(mailbox)->server->passwd);
	    gtk_entry_set_text (GTK_ENTRY (mcw->imap_port), port );
	    gtk_entry_set_text (GTK_ENTRY(
		gnome_entry_gtk_entry(GNOME_ENTRY (mcw->imap_folderpath))),
				MAILBOX_IMAP(mailbox)->path );
	}
	gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), 
			       MC_PAGE_IMAP);
	break;
    case MAILBOX_UNKNOWN:
	/* do nothing for now */
	break;
    }
}


/* Returns 1 if everything was okey
 * Returns -2 if there was a blank field and the user wants to re-edit
 * Returns -1 if there was a blank field and the user wants to cancel
 */

static int
check_for_blank_fields(Mailbox *mailbox)
{
    gchar *msg = NULL;
    GtkWidget *ask;
    gint clicked_button;
    
    switch(mailbox->type) {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
    case MAILBOX_UNKNOWN:
	return 1;
	break;
    case MAILBOX_IMAP:
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->imap_mailbox_name)), "")) 
	    msg = _("You need to fill in the Mailbox Name field.");
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(
	    gnome_entry_gtk_entry(GNOME_ENTRY (mcw->imap_folderpath)))),"")) 
	    msg = _("You need to fill in the folderpath field.");
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)), "")) 
	    msg = _("You need to fill in the IMAP Server field");
	if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)), ""))
	    msg = _("You need to fill in the username field");
	if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)), ""))
	    msg = _("You need to fill in the password field");
	if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), "")) 
	    msg = _("You need to fill in the port field");
	break;

  case MAILBOX_MBOX:
    if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)), "")) 
      msg = _("You need to fill in the Mailbox Name field.");
    
    break;
  case MAILBOX_POP3:
    if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)), "") ||
       !strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)), "") ||
       !strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)), "") ||
       !strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_port)), "") ||
       !strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)), ""))  {
      
      
      if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)),"")) 
	{
	  msg = _("You need to fill in the Mailbox Name field.");
	}
      else if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)),""))
	{
	  msg = _("You need to fill in the user field.");
	}
      else if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)),""))
	{
	  msg = _("You need to fill in the password field.");
	}
      else if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)), ""))
	{
	  msg = _("You need to fill in the server field.");
	}
      else if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_port)), ""))
	{
	  msg = _("You need to fill in the port field.");
	}
      else
	{
	  msg = _("Some of the fields are blank.");
	}
    }
    break;
    
  }
  
  if(msg == NULL)   /* msg == NULL only if no fields where blank */
    return 1;
  
  ask = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_QUESTION,
			      GNOME_STOCK_BUTTON_OK, 
			      GNOME_STOCK_BUTTON_CANCEL, NULL);
  
  
  gnome_dialog_set_default(GNOME_DIALOG(ask), 1);
  gtk_window_set_modal(GTK_WINDOW(ask), TRUE);
  clicked_button = gnome_dialog_run(GNOME_DIALOG(ask));
  
  if(clicked_button == 1)
    return -2;               /* Was a blank, want to re-edit */
  else
    return -1;               /* Was a blank, want to cancel */
}

static int
conf_update_mailbox (Mailbox * mailbox, gchar * old_mbox_pkey)
{
  int field_check;

  if (!mailbox)
    return 1;
  
  switch (mailbox->type)
    {
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
    case MAILBOX_MBOX:
      {
	gchar *filename;
	field_check = check_for_blank_fields(mailbox);
	
	if(field_check == -1) 
	  return -1;
	else if(field_check == -2)
	  return -2;
	
	filename =
	  gtk_entry_get_text (GTK_ENTRY ((mcw->local_mailbox_path)));
	g_free (mailbox->name);
	g_free (MAILBOX_LOCAL (mailbox)->path);
	mailbox->name = g_strdup (gtk_entry_get_text (
	    GTK_ENTRY (mcw->local_mailbox_name)));
	MAILBOX_LOCAL (mailbox)->path = g_strdup (filename);

	config_mailbox_update (mailbox, old_mbox_pkey);
      }
      break;

    case MAILBOX_POP3:
      field_check = check_for_blank_fields(mailbox);
      if(field_check == -1)
	return -1;
      else if(field_check == -2)
	return 1;
      
      g_free (mailbox->name);
      g_free (MAILBOX_POP3 (mailbox)->server->user);
      g_free (MAILBOX_POP3 (mailbox)->server->passwd);
      g_free (MAILBOX_POP3 (mailbox)->server->host);

      mailbox->name = g_strdup (gtk_entry_get_text (
	  GTK_ENTRY (mcw->pop_mailbox_name)));
      MAILBOX_POP3 (mailbox)->server->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)));
      MAILBOX_POP3 (mailbox)->server->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)));
      MAILBOX_POP3 (mailbox)->server->host = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)));
      MAILBOX_POP3 (mailbox)->server->port= atoi( gtk_entry_get_text (GTK_ENTRY (mcw->pop_port)));
      MAILBOX_POP3 (mailbox)->check = GTK_TOGGLE_BUTTON (mcw->pop_check)->active;
      MAILBOX_POP3 (mailbox)->delete_from_server = GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server)->active;

      config_mailbox_update (mailbox, old_mbox_pkey);
      break;

    case MAILBOX_IMAP:
      field_check = check_for_blank_fields(mailbox);
      if(field_check == -1)
	return -1;
      else if(field_check == -2)
	return 1;

      if( mailbox->name ) {
	      g_free (mailbox->name);
	      mailbox->name = NULL;
      }

      if( MAILBOX_IMAP (mailbox)->server->user ) {
	      g_free (MAILBOX_IMAP (mailbox)->server->user);
	      MAILBOX_IMAP (mailbox)->server->user = NULL;
      }

      if( MAILBOX_IMAP (mailbox)->server->passwd ) {
	      g_free (MAILBOX_IMAP (mailbox)->server->passwd);
	      MAILBOX_IMAP (mailbox)->server->passwd = NULL;
      }

      if( MAILBOX_IMAP (mailbox)->path ) {
	      g_free (MAILBOX_IMAP (mailbox)->path);
	      MAILBOX_IMAP (mailbox)->path = NULL;
      }

      if( MAILBOX_IMAP (mailbox)->server->host ) {
	      g_free (MAILBOX_IMAP (mailbox)->server->host);
	      MAILBOX_IMAP (mailbox)->server->host = NULL;
      }

      mailbox->name = g_strdup (gtk_entry_get_text (
	  GTK_ENTRY (mcw->imap_mailbox_name)));

      MAILBOX_IMAP (mailbox)->server->user = g_strdup (
	  gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));

      MAILBOX_IMAP (mailbox)->server->passwd = g_strdup (
	  gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));

      MAILBOX_IMAP (mailbox)->path =  g_strdup ( gtk_entry_get_text (
	  GTK_ENTRY(gnome_entry_gtk_entry(
	      GNOME_ENTRY (mcw->imap_folderpath)))));
	  
      if ( MAILBOX_IMAP( mailbox )->path == NULL ) 
	  MAILBOX_IMAP (mailbox)->path = g_strdup ("INBOX");
      else if( MAILBOX_IMAP (mailbox)->path[0] == '\0' ) {
	  g_free (MAILBOX_IMAP (mailbox)->path);
	  MAILBOX_IMAP (mailbox)->path = g_strdup ("INBOX");
      }

      MAILBOX_IMAP (mailbox)->server->host = g_strdup (
	  gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
      MAILBOX_IMAP (mailbox)->server->port = strtol (
	  gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

      config_mailbox_update (mailbox, old_mbox_pkey);
      break;

    case MAILBOX_UNKNOWN:
      /* Do nothing for now */
      break;
    }
  return 1;
}


static Mailbox *
conf_add_mailbox ()
{
  Mailbox *mailbox = NULL;
  MailboxType type;
  GNode *node;
  GString *fos;
  GString *idstr;
  int field_check;

  MailboxConfPageType cur_page;
  cur_page = gtk_notebook_get_current_page (GTK_NOTEBOOK (mcw->notebook));

  switch (cur_page)		/* see what page we are on */
    {


/* Local Mailboxes */
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
		ptr = g_strdup_printf (_("Cannot create mailbox '%s': %s\n"), filename, strerror (errno));
		msgbox = gnome_message_box_new (ptr, GNOME_MESSAGE_BOX_ERROR, _("Cancel"), NULL);
		free (ptr);
		gtk_window_set_modal (GTK_WINDOW (msgbox), TRUE);
		gnome_dialog_run (GNOME_DIALOG (msgbox));
		return NULL;
	      }
	    close (fd);
	    type = MAILBOX_MBOX;
	  }
	mailbox = BALSA_MAILBOX(mailbox_new (type));

	field_check = check_for_blank_fields(mailbox);
	if(field_check == -2)
	  break;
	else if(field_check == -1)
	  return NULL;
	
	mailbox->name = g_strdup (gtk_entry_get_text (
	    GTK_ENTRY (mcw->local_mailbox_name)));
	MAILBOX_LOCAL (mailbox)->path = g_strdup (filename);
	node = g_node_new (mailbox_node_new (mailbox->name, mailbox,
					     mailbox->type != MAILBOX_MBOX));
	g_node_append (balsa_app.mailbox_nodes, node);
      }
      break;

/* POP3 Mailboxes */
    case MC_PAGE_POP3:
      mailbox = BALSA_MAILBOX(mailbox_new (MAILBOX_POP3));

      field_check = check_for_blank_fields(mailbox);
      if(field_check == -2)
	break;
      else if(field_check == -1) {
	  gtk_object_destroy(GTK_OBJECT(mailbox));
	  return NULL;
      }

      mailbox->name = g_strdup (gtk_entry_get_text (
	  GTK_ENTRY (mcw->pop_mailbox_name)));
      MAILBOX_POP3 (mailbox)->server->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)));
      MAILBOX_POP3 (mailbox)->server->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_password)));
      MAILBOX_POP3 (mailbox)->server->host = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->pop_server)));
      MAILBOX_POP3 (mailbox)->server->port = atoi (gtk_entry_get_text (GTK_ENTRY (mcw->pop_port)));
      MAILBOX_POP3 (mailbox)->check = GTK_TOGGLE_BUTTON (mcw->pop_check)->active;
      MAILBOX_POP3 (mailbox)->delete_from_server = GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server)->active;

      balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
      break;


/* IMAP Mailboxes */
    case MC_PAGE_IMAP:
      mailbox = BALSA_MAILBOX(mailbox_new (MAILBOX_IMAP));

      field_check = check_for_blank_fields(mailbox);
      if(field_check == -2)
	break;
      else if(field_check == -1)
	return NULL;

      fos=(GString *) gtk_entry_get_text(GTK_ENTRY(gnome_entry_gtk_entry(GNOME_ENTRY (mcw->imap_folderpath))));
      idstr=(GString *) g_string_new((const gchar *)fos);
      /* used to build a string: MAILBOXNAME " on " SERVERNAME */
      g_string_append((GString *)idstr, _(" on "));
      g_string_append((GString *)idstr, (gchar *) gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)));
     
      MAILBOX_IMAP (mailbox)->path = g_strdup ((const gchar*)fos);
      mailbox->name = g_strdup (idstr->str);
      MAILBOX_IMAP (mailbox)->server->user = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
      MAILBOX_IMAP (mailbox)->server->passwd = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
      
      
      if (!MAILBOX_IMAP (mailbox)->path[0])
	{
	  g_free (MAILBOX_IMAP (mailbox)->path);
	  MAILBOX_IMAP (mailbox)->path = g_strdup ("INBOX");
	}
      MAILBOX_IMAP (mailbox)->server->host = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
      MAILBOX_IMAP (mailbox)->server->port = strtol (gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), (char **) NULL, 10);

      node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);

      break;

    case MC_PAGE_NEW:
	    g_warning( "An unimportant can\'t-happen has occurred. mailbox-conf.c:712" );
	    break;
    }

  config_mailbox_add (mailbox, NULL);
  mailbox_add_for_checking (mailbox);
  return mailbox;
}


static void
mailbox_conf_close (GtkWidget * widget, gboolean save)
{
  Mailbox *mailbox;
  int return_value;

  mailbox = mcw->mailbox;

  if (mailbox && save)		/* we are updating the mailbox */
    {
      gchar *old_mbox_pkey = mailbox_get_pkey(mailbox);
      return_value = conf_update_mailbox (mcw->mailbox, old_mbox_pkey);
      g_free (old_mbox_pkey);

      if (mailbox->type == MAILBOX_POP3 &&
	  return_value != -1)                /* redraw the pop3 server list */
	update_pop3_servers ();
      else                                   /* redraw the main mailbox list */
	balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));	

      if(return_value != -1) {
	gtk_widget_destroy (mcw->window);
	g_free (mcw);
	mcw = NULL;
      }

      return;			/* don't continue */
    }



  if (save)
    {
      mailbox = conf_add_mailbox ();
      if (!mailbox)
	return;
    }

  if (mailbox)
    {
      if (mailbox->type == MAILBOX_POP3)
	update_pop3_servers ();
      else
	balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));
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
  GtkWidget *label;
  GtkWidget *vbox;
  GtkWidget *radio_button;
  GtkWidget *pixmap;
  gchar *logo;

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox);

  /*logo = gnome_unconditional_pixmap_file ("balsa/balsa_icon.png");*/
  logo = balsa_pixmap_finder( "balsa/balsa_icon.png" );
  pixmap = gnome_pixmap_new_from_file (logo);
  g_free (logo);
  gtk_box_pack_start (GTK_BOX (vbox), pixmap, FALSE, FALSE, 0);
  gtk_widget_show (pixmap);

  label = gtk_label_new (_("New mailbox type:"));
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  /* radio buttons */
  /* local mailbox */
  radio_button = gtk_radio_button_new_with_label (NULL, _("Local mailbox"));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), (gpointer) MC_PAGE_LOCAL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_widget_show (radio_button);

  /* imap mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), _("IMAP server"));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", GTK_SIGNAL_FUNC (set_next_page), (gpointer) MC_PAGE_IMAP);
  gtk_widget_show (radio_button);

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
  label = gtk_label_new (_("Mailbox Name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  mcw->local_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->local_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  /* path to file */
  label = gtk_label_new (_("Mailbox path:"));
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

  return_widget = table = gtk_table_new (7, 2, FALSE);
  gtk_widget_show (table);

  /* mailbox name */

  label = gtk_label_new (_("Mailbox Name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  mcw->pop_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_widget_show (mcw->pop_mailbox_name);

  /* pop server */

  label = gtk_label_new (_("Server:"));
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

  /* pop port */

  label = gtk_label_new (_("Port:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  gtk_widget_show (label);

  mcw->pop_port = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->pop_port, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->pop_port), "110");

  gtk_widget_show (mcw->pop_port);

  /* username  */

  label = gtk_label_new (_("Username:"));
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

  label = gtk_label_new (_("Password:"));
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

  /* toggle for check */

  mcw->pop_check = gtk_check_button_new_with_label (_("Check"));
  gtk_table_attach (GTK_TABLE (table), mcw->pop_check, 0, 2, 5, 6,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (mcw->pop_check);

  /* toggle for deletion from server */

  mcw->pop_delete_from_server = gtk_check_button_new_with_label (_("Delete from server"));
  gtk_table_attach (GTK_TABLE (table), mcw->pop_delete_from_server, 0, 2, 6, 7,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (mcw->pop_delete_from_server);

  return return_widget;
}

static GtkWidget *
create_imap_mailbox_page (void)
{
  GtkWidget *return_widget;
  GtkWidget *table;
  GtkWidget *label;

  return_widget = gtk_vbox_new (0, FALSE);

  table = gtk_table_new (6, 2, FALSE);
  gtk_box_pack_start (GTK_BOX (return_widget), table, FALSE, FALSE, 0);

  /* mailbox name */
  label = gtk_label_new (_("Mailbox Name:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1,
		    GTK_FILL, GTK_FILL, 10, 10);

  mcw->imap_mailbox_name = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_mailbox_name, 1, 2, 0, 1,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 10);

  /* imap server */
  label = gtk_label_new (_("Server:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_server, 1, 2, 1, 2,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_server), "localhost");

  /* imap server port number */
  label = gtk_label_new (_("Port:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 2, 3,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_port = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_port, 1, 2, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_port), "143");


  /* username  */
  label = gtk_label_new (_("Username:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 3, 4,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_username, 1, 2, 3, 4,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_append_text (GTK_ENTRY (mcw->imap_username), g_get_user_name ());


  /* password field */
  label = gtk_label_new (_("Password:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 4, 5,
		    GTK_FILL, GTK_FILL,
		    10, 10);

  mcw->imap_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), mcw->imap_password, 1, 2, 4, 5,
		    GTK_EXPAND | GTK_FILL, GTK_FILL,
		    0, 10);

  gtk_entry_set_visibility (GTK_ENTRY (mcw->imap_password), FALSE);

  label = gtk_label_new (_("Folder path:"));
  gtk_misc_set_alignment (GTK_MISC (label), 1.0, 0.5);

  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 5, 6,
                    GTK_FILL, GTK_FILL,
                    10, 10);

  mcw->imap_folderpath= gnome_entry_new("IMAP Folder History");
  gtk_entry_append_text(GTK_ENTRY(gnome_entry_gtk_entry(
      GNOME_ENTRY(mcw->imap_folderpath))), "INBOX");

  gnome_entry_append_history(GNOME_ENTRY(mcw->imap_folderpath), 1, "INBOX");
  gnome_entry_append_history(GNOME_ENTRY(mcw->imap_folderpath), 1, 
			     "INBOX.Sent");
  gnome_entry_append_history(GNOME_ENTRY(mcw->imap_folderpath), 1, 
			     "INBOX.Draft");
  gnome_entry_append_history(GNOME_ENTRY(mcw->imap_folderpath), 1, 
			     "INBOX.outbox");

  gtk_table_attach (GTK_TABLE (table), mcw->imap_folderpath, 1, 2, 5, 6,
                    GTK_EXPAND | GTK_FILL, GTK_FILL,
                    0, 10);

  return return_widget;
}


/*
 * callbacks
 */
static void
next_cb (GtkWidget * widget)
{
  GtkWidget *bbox;

  bbox = GNOME_DIALOG (mcw->window)->action_area;
  gtk_container_remove (GTK_CONTAINER (bbox), mcw->ok);

  gtk_widget_destroy (mcw->ok);
  if (mcw->mailbox)
    {
      mcw->ok = gtk_button_new_with_label (_("Update"));
    }
  else
    {
      GtkWidget *pixmap;
      pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_NEW);
      mcw->ok = gnome_pixmap_button (pixmap, _("Add"));
      gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
		       (GtkSignalFunc) mailbox_conf_close, (gpointer) TRUE);
    }
  gtk_widget_show (mcw->ok);

  gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
  gtk_box_reorder_child (GTK_BOX (bbox), mcw->ok, 0);

  switch (mcw->next_page)
    {
    case MC_PAGE_LOCAL:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_LOCAL);
      break;

    case MC_PAGE_POP3:
      set_next_page (NULL, MC_PAGE_POP3);
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
  {"S", N_("Mailbox")};

  gint clicked_button;

  window = gnome_dialog_new ("IMAP", GNOME_STOCK_BUTTON_CLOSE, NULL);
  gnome_dialog_set_parent (GNOME_DIALOG (window), GTK_WINDOW (balsa_app.main_window));

#ifdef ENABLE_NLS
  titles[1]=_(titles[1]);
#endif
  clist = gtk_clist_new_with_titles (2, titles);
  gtk_clist_set_column_width (GTK_CLIST (clist), 1, 16);
/*
   gtk_clist_set_policy (GTK_CLIST (clist), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
 */
  gtk_box_pack_start (GTK_BOX (GNOME_DIALOG (window)->vbox), clist, TRUE, TRUE, 0);
  gtk_widget_show (clist);

  titles[0] = NULL;
  titles[1] = "INBOX";
  gtk_clist_append (GTK_CLIST (clist), titles);

  gtk_window_set_modal (GTK_WINDOW (window), TRUE);
  clicked_button = gnome_dialog_run (GNOME_DIALOG (window));
  if (clicked_button == 0)
    {
      return;
    }
}



