/* -*-mode:c; c-style:k&r; c-basic-offset:2; -*- */
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

#include <sys/stat.h>

#include "balsa-app.h"
#include "mailbox-conf.h"
#include "main-window.h"
#include "misc.h"
#include "pref-manager.h"
#include "save-restore.h"

#include "libbalsa.h"

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
    MC_PAGE_IMAP_DIR,
  }
MailboxConfPageType;



typedef struct _MailboxConfWindow MailboxConfWindow;
struct _MailboxConfWindow
  {
    LibBalsaMailbox *mailbox;

    gboolean add;

    GtkWidget *ok;
    GtkWidget *cancel;

    GtkWidget *window;
    GtkWidget *notebook;

    MailboxConfPageType the_page;

    /* for local mailboxes */
    GtkWidget *local_mailbox_name;
    GtkWidget *local_mailbox_path;

    /* for imap mailboxes & directories */
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
    GtkWidget *pop_use_apop;
  };

static MailboxConfWindow *mcw;


/* callbacks */
static void mailbox_conf_close (GtkWidget * widget, gboolean save);
static void next_cb (GtkWidget * widget);

/* misc functions */
static void mailbox_conf_set_values (LibBalsaMailbox * mailbox);
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
  LibBalsaMailbox *mb = *(LibBalsaMailbox**) data;

  if (!n1 || n1->mailbox != mb)
     return FALSE;

  *(++d) = g1;
  return TRUE;
}

/* find_gnode_in_mbox_list:
   looks for given mailbox in th GNode tree, usually but not limited to
   balsa_app.mailox_nodes
*/
GNode *
find_gnode_in_mbox_list (GNode * gnode_list, LibBalsaMailbox * mailbox)
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

static void
yes_no_reply_cb (gint reply, gpointer data)
{
  gboolean *answer = (gboolean*)data;

  if ( reply == GNOME_YES )
    *answer = TRUE;
  else if ( reply == GNOME_NO )
    *answer = FALSE;
  else
    g_error("Unknown replyto Yes/No dialog: %d\n", reply);
}

void
mailbox_conf_delete (LibBalsaMailbox * mailbox)
{
  GNode *gnode;
  gchar *msg;
  gboolean answer = FALSE;
  GtkWidget *ask;

  if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox))
  {
    /* FIXME: Should prompt to remove file aswell */
    msg = g_strdup_printf ( _("This will remove the mailbox %s and it's files permanently from your system.\n"
			      "Are you sure you want to remove this mailbox?"), mailbox->name );
  }
  else
  {
    msg = g_strdup_printf ( _("This will remove the mailbox %s from the list of mailboxes\n"
			      "You may use \"Add Mailbox\" later to access this mailbox again\n"
			      "Are you sure you want to remove this mailbox?") , mailbox->name);
  }
  
  ask = gnome_question_dialog_modal_parented(msg,
					     (GnomeReplyCallback)yes_no_reply_cb,
					     (gpointer)&answer,
					     GTK_WINDOW(balsa_app.main_window));
					     
  g_free(msg);

  gnome_dialog_run (GNOME_DIALOG (ask));
  if (answer == FALSE)
  {
    return;
  }
  
  /* Don't forget to remove the node from balsa's mailbox list */
  if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
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
  if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
    mblist_close_mailbox (mailbox);
  
  /* Delete local files */
  if (LIBBALSA_IS_MAILBOX_LOCAL(mailbox))
    mailbox_remove_files (LIBBALSA_MAILBOX_LOCAL (mailbox)->path);
  
  if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
    update_pop3_servers ();
  else
    balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));
}

/*
 * If mailbox is NULL then prompt for a type of mailbox and configure it
 * If mailbox is not null then configure that mailbox. If add_mbox is TRUE
 * then the button will say "Add" rather than "Modify"
 *
 * If mailbox is given, and add is TRUE and the user presses cancel then 
 * the mailbox will be destroyed
 */
void
mailbox_conf_new (LibBalsaMailbox * mailbox, gint add_mbox)
{
  GtkWidget *bbox;

  if (mcw)
    return;

  g_return_if_fail ( ! (mailbox == NULL && add_mbox == FALSE) );

  mcw = g_malloc (sizeof (MailboxConfWindow));

  mcw->mailbox = 0;

  mcw->the_page = MC_PAGE_LOCAL;	/* default next page to LOCAL */

  mcw->mailbox = mailbox;

  mcw->add = add_mbox;

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

  if ( mailbox == NULL )
  {
    mcw->ok = gnome_stock_button (GNOME_STOCK_BUTTON_NEXT);
    gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
    gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			GTK_SIGNAL_FUNC (next_cb), NULL);
    
  }
  else if ( add_mbox )
  {
    GtkWidget *pixmap;
    pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_NEW);
    mcw->ok = gnome_pixmap_button (pixmap, _("Add"));
    gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
    gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			(GtkSignalFunc) mailbox_conf_close, (void *) TRUE);
  }
  else 
  {
    GtkWidget *pixmap;
    pixmap = gnome_stock_pixmap_widget (NULL, GNOME_STOCK_PIXMAP_SAVE);
    mcw->ok = gnome_pixmap_button (pixmap, _("Update"));
    gtk_container_add (GTK_CONTAINER (bbox), mcw->ok);
    gtk_signal_connect (GTK_OBJECT (mcw->ok), "clicked",
			(GtkSignalFunc) mailbox_conf_close, (void *) TRUE);
  }

  /* cancel button */
  mcw->cancel = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_container_add (GTK_CONTAINER (bbox), mcw->cancel);
  gtk_signal_connect (GTK_OBJECT (mcw->cancel), "clicked",
		      (GtkSignalFunc) mailbox_conf_close, FALSE);

  gtk_widget_show_all (mcw->window);

  if ( mailbox ) 
    mailbox_conf_set_values (mcw->mailbox);
}


static void
mailbox_conf_set_values (LibBalsaMailbox * mailbox)
{
  gchar *port;
  
  g_return_if_fail ( LIBBALSA_IS_MAILBOX (mailbox) );
  
  if ( LIBBALSA_IS_MAILBOX_LOCAL(mailbox) )
  {
    LibBalsaMailboxLocal *local;

    mcw->the_page = MC_PAGE_LOCAL;

    local = LIBBALSA_MAILBOX_LOCAL(mailbox);

    gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), 
			   MC_PAGE_LOCAL);
    if ( mailbox->name )
      gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_name), 
			  mailbox->name);

    if ( local->path )
      gtk_entry_set_text (GTK_ENTRY (mcw->local_mailbox_path), 
			  local->path);
  }
  else if ( LIBBALSA_IS_MAILBOX_POP3(mailbox) )
  {
    LibBalsaMailboxPop3 *pop3;
    LibBalsaServer *server;

    mcw->the_page = MC_PAGE_POP3;
    
    pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    port = g_strdup_printf ("%d", server->port);

    if (  mailbox->name )
      gtk_entry_set_text (GTK_ENTRY (mcw->pop_mailbox_name),
			  mailbox->name);

    if ( server->host )
      gtk_entry_set_text (GTK_ENTRY (mcw->pop_server), server->host);

    gtk_entry_set_text (GTK_ENTRY (mcw->pop_port), port);

    if ( server->user )
      gtk_entry_set_text (GTK_ENTRY (mcw->pop_username), server->user);

    if ( server->passwd )
      gtk_entry_set_text (GTK_ENTRY (mcw->pop_password), server->passwd);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mcw->pop_use_apop), 
				  pop3->use_apop);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mcw->pop_check), 
				  pop3->check);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server), 
				  pop3->delete_from_server);

    gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
    g_free(port);
  }  
  else if ( LIBBALSA_IS_MAILBOX_IMAP(mailbox) )
  {
    LibBalsaMailboxImap *imap;
    LibBalsaServer *server;

    mcw->the_page = MC_PAGE_IMAP;

    imap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    if ( mailbox->name ) 
      gtk_entry_set_text (GTK_ENTRY (mcw->imap_mailbox_name), 
			  mailbox->name);
    
    port = g_strdup_printf("%d", server->port);
    
    if ( server->host ) 
      gtk_entry_set_text (GTK_ENTRY (mcw->imap_server), server->host);
    if ( server->user )
      gtk_entry_set_text (GTK_ENTRY (mcw->imap_username), server->user);
    if ( server->passwd )
      gtk_entry_set_text (GTK_ENTRY (mcw->imap_password), server->passwd);
    gtk_entry_set_text (GTK_ENTRY (mcw->imap_port), port );
    
    if ( imap->path )
      gtk_entry_set_text (GTK_ENTRY(
	gnome_entry_gtk_entry(GNOME_ENTRY (mcw->imap_folderpath))),
			  imap->path );

    gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), 
			   MC_PAGE_IMAP);
    g_free(port);
  } else {
    /* do nothing for now */
  }
}


/* Returns 1 if everything was okey
 * Returns -2 if there was a blank field and the user wants to re-edit
 * Returns -1 if there was a blank field and the user wants to cancel
 */
/* FIXME: We have code that prompts for a password if it is empty. Check */
static int
check_for_blank_fields(MailboxConfPageType mbox_type)
{
    gchar *msg = NULL;
    GtkWidget *ask;
    gint clicked_button;
    
    switch(mbox_type) {
    case MC_PAGE_LOCAL:
	if( !*gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)) )
	    msg = _("You need to fill in the Mailbox Name field.");
	if( !*gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_path)) )
	    msg = _("You need to fill in the Mailbox Path field.");
	break;

    case MC_PAGE_IMAP:
    case MC_PAGE_IMAP_DIR:
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->imap_mailbox_name)), "")) 
	    msg = _("You need to fill in the Mailbox Name field.");
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(
	    gnome_entry_gtk_entry(GNOME_ENTRY (mcw->imap_folderpath)))),"")) 
	    msg = _("You need to fill in the folderpath field.");
	if(!strcmp(gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)), "")) 
	    msg = _("You need to fill in the IMAP Server field");
	if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)), ""))
	    msg = _("You need to fill in the username field");
	if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)), "")) 
	    msg = _("You need to fill in the port field");
	break;

  case MC_PAGE_POP3:
    if(!strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_mailbox_name)), "") ||
       !strcmp(gtk_entry_get_text (GTK_ENTRY (mcw->pop_username)), "") ||
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
    case MC_PAGE_NEW:
	g_warning("a 'Cannot happen' occured. Report it");
    
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
conf_update_mailbox (LibBalsaMailbox * mailbox, gchar * old_mbox_pkey)
{
  LibBalsaMailboxImap *mb_imap;
  LibBalsaMailboxPop3 *mb_pop3;
  LibBalsaServer *server;

  int field_check;

  if (!mailbox)
    return 1;

  field_check = check_for_blank_fields(mcw->the_page);
  
  if(field_check != 1) return field_check;
  
  if ( LIBBALSA_IS_MAILBOX_LOCAL (mailbox) )
  {
    gchar *filename;
    
    filename =
      gtk_entry_get_text (GTK_ENTRY ((mcw->local_mailbox_path)));
    g_free (mailbox->name);
    g_free (LIBBALSA_MAILBOX_LOCAL (mailbox)->path);
    mailbox->name = g_strdup (gtk_entry_get_text (GTK_ENTRY (mcw->local_mailbox_name)));
    LIBBALSA_MAILBOX_LOCAL (mailbox)->path = g_strdup (filename);
    config_mailbox_update (mailbox, old_mbox_pkey);
  }
  else if ( LIBBALSA_IS_MAILBOX_POP3 (mailbox) ) 
  {
    mb_pop3 = LIBBALSA_MAILBOX_POP3(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);

    g_free (mailbox->name);
    
    mailbox->name = g_strdup (gtk_entry_get_text (
      GTK_ENTRY (mcw->pop_mailbox_name)));
    
    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY(mcw->pop_username)));

    libbalsa_server_set_password(server,
				 gtk_entry_get_text(GTK_ENTRY(mcw->pop_password)));

    libbalsa_server_set_host(server,
			     gtk_entry_get_text(GTK_ENTRY(mcw->pop_server)),
			     atoi(gtk_entry_get_text(GTK_ENTRY(mcw->pop_port))));

    mb_pop3->use_apop = GTK_TOGGLE_BUTTON (mcw->pop_use_apop)->active;
    mb_pop3->check = GTK_TOGGLE_BUTTON (mcw->pop_check)->active;
    mb_pop3->delete_from_server = GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server)->active;
    
    config_mailbox_update (mailbox, old_mbox_pkey);
  } 
  else if ( LIBBALSA_IS_MAILBOX_IMAP (mailbox) )
  {
    gchar *path;

    mb_imap = LIBBALSA_MAILBOX_IMAP(mailbox);
    server = LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox);
    
    g_free (mailbox->name);
    
    mailbox->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->imap_mailbox_name)));
    

    libbalsa_server_set_username(server,
				 gtk_entry_get_text(GTK_ENTRY(mcw->imap_username)));

    libbalsa_server_set_password(server,
				 gtk_entry_get_text(GTK_ENTRY(mcw->imap_password)));

    libbalsa_server_set_host(server,
			     gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)),
			     atoi(gtk_entry_get_text(GTK_ENTRY(mcw->imap_port))));

    path = gtk_entry_get_text(GTK_ENTRY(gnome_entry_gtk_entry(GNOME_ENTRY(mcw->imap_folderpath))));

    if ( path == NULL || path[0] == '\0' )
      libbalsa_mailbox_imap_set_path(mb_imap, "INBOX");
    else
      libbalsa_mailbox_imap_set_path(mb_imap, path);
				       
    config_mailbox_update (mailbox, old_mbox_pkey);
  } 
  else 
  {
    /* Do nothing for now */
  }

  return 1;
}

static void
fill_in_imap_data(gchar **name, gchar **path)
{
    gchar * fos;
    fos= gtk_entry_get_text(GTK_ENTRY(gnome_entry_gtk_entry(
	GNOME_ENTRY (mcw->imap_folderpath))));

    if( !( *name = 
	   g_strdup(gtk_entry_get_text(GTK_ENTRY(mcw->imap_mailbox_name))) )
	|| strlen(g_strstrip(*name)) == 0) {
	if(*name) g_free(*name);

	*name = g_strdup_printf( 
	    _("%s on %s"), fos, 
	    gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)));
    }
    *path   = g_strdup ( fos);
}

static gboolean
conf_add_mailbox (LibBalsaMailbox **mbox)
{
  LibBalsaMailbox *mailbox = NULL;
  GNode *node;
  int field_check;

  *mbox = NULL;

  field_check = check_for_blank_fields(mcw->the_page);
  if(field_check == -1)
      return FALSE;
  
  switch (mcw->the_page)		/* see what page we are on */
    {

/* Local Mailboxes */
    case MC_PAGE_LOCAL:
      {
	gchar *filename = gtk_entry_get_text (GTK_ENTRY ((mcw->local_mailbox_path)));

	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_local_new (filename, TRUE));
	
	mailbox->name = g_strdup (gtk_entry_get_text (
	  GTK_ENTRY (mcw->local_mailbox_name)));
	node = g_node_new (mailbox_node_new (mailbox->name, mailbox,
					     mailbox->is_directory));
	g_node_append (balsa_app.mailbox_nodes, node);
      }
      break;

/* POP3 Mailboxes */
    case MC_PAGE_POP3:
      mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_pop3_new());

      mailbox->name = g_strdup (gtk_entry_get_text (
	  GTK_ENTRY (mcw->pop_mailbox_name)));

      libbalsa_server_set_username(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				    gtk_entry_get_text(GTK_ENTRY(mcw->pop_username)));
      libbalsa_server_set_password(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				    gtk_entry_get_text(GTK_ENTRY(mcw->pop_password)));
      libbalsa_server_set_host(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				gtk_entry_get_text(GTK_ENTRY(mcw->pop_server)),
				atoi (gtk_entry_get_text(GTK_ENTRY(mcw->pop_port))));

      LIBBALSA_MAILBOX_POP3 (mailbox)->check = GTK_TOGGLE_BUTTON (mcw->pop_check)->active;
      LIBBALSA_MAILBOX_POP3 (mailbox)->delete_from_server = GTK_TOGGLE_BUTTON (mcw->pop_delete_from_server)->active;

      balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
      break;

/* IMAP Mailboxes */
    case MC_PAGE_IMAP_DIR: 
    {
	ImapDir *dir = imapdir_new();
	fill_in_imap_data(&dir->name, &dir->path);

	dir->user   = g_strdup ( gtk_entry_get_text (GTK_ENTRY (mcw->imap_username)));
	dir->passwd = g_strdup ( gtk_entry_get_text (GTK_ENTRY (mcw->imap_password)));
	dir->host   = g_strdup ( gtk_entry_get_text (GTK_ENTRY (mcw->imap_server)));
	dir->port   = atol     ( gtk_entry_get_text (GTK_ENTRY (mcw->imap_port)) );

	imapdir_scan(dir);
	if(!G_NODE_IS_LEAF(dir->file_tree)) {
	    config_imapdir_add(dir);
	    g_node_append (balsa_app.mailbox_nodes, dir->file_tree);
	    dir->file_tree = NULL;
	    imapdir_destroy(dir);
	    return TRUE;
	} else
	    imapdir_destroy(dir);
	/* and assume it was ordinary IMAP mailbox */
    }
    case MC_PAGE_IMAP:
    {
      LibBalsaMailboxImap * m;
      gchar *path;

	mailbox = LIBBALSA_MAILBOX(libbalsa_mailbox_imap_new());
	m = LIBBALSA_MAILBOX_IMAP(mailbox);

	fill_in_imap_data(&mailbox->name, &path);

	libbalsa_server_set_username(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				      gtk_entry_get_text(GTK_ENTRY(mcw->imap_username)));
	libbalsa_server_set_password(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				      gtk_entry_get_text(GTK_ENTRY(mcw->imap_password)));
	libbalsa_server_set_host(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox),
				  gtk_entry_get_text(GTK_ENTRY(mcw->imap_server)),
				  atol(gtk_entry_get_text(GTK_ENTRY(mcw->imap_port))));
      gtk_signal_connect(GTK_OBJECT(LIBBALSA_MAILBOX_REMOTE_SERVER(mailbox)), 
			 "get-password",GTK_SIGNAL_FUNC(ask_password),mailbox);

	if (path == NULL || path[0] == '\0' )
	    /* FIXME: disable when IMAPDir stuff becomes functional */
	{
	  libbalsa_mailbox_imap_set_path(m, "INBOX");
	} else {
	  libbalsa_mailbox_imap_set_path(m, path);
	}
	g_free(path);

	node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
	g_node_append (balsa_app.mailbox_nodes, node);

	break;
    }

    case MC_PAGE_NEW:
	    g_warning( "An unimportant can\'t-happen has occurred. mailbox-conf.c:712" );
	    break;
    }

  config_mailbox_add (mailbox, NULL);
  *mbox = mailbox;
  return TRUE;
}


static void
mailbox_conf_close (GtkWidget * widget, gboolean save)
{
  int return_value;

  if ( save ) 
  {
    LibBalsaMailbox *mailbox = mcw->mailbox;
    mailbox = mcw->mailbox;
    
    if (mcw->add == FALSE)		/* we are updating the mailbox */
    {
      gchar *old_mbox_pkey = mailbox_get_pkey(mailbox);
      return_value = conf_update_mailbox (mcw->mailbox, old_mbox_pkey);
      g_free (old_mbox_pkey);
      
      if (LIBBALSA_IS_MAILBOX_POP3(mailbox) && return_value != -1)
	/* redraw the pop3 server list */
	update_pop3_servers ();
      else                                   /* redraw the main mailbox list */
	balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));	
      
      if(return_value == -1)
	return;
    }
    else 
    {
      if( !conf_add_mailbox ( &mailbox) ) 
	return;
      
      if (LIBBALSA_IS_MAILBOX_POP3(mailbox))
	update_pop3_servers ();
      else
	balsa_mblist_redraw (BALSA_MBLIST (balsa_app.mblist));
    }
  }
  else
  {
    /* If we were given a mailbox to add and the user canceled then
     * destroy that mailbox
     */
    if ( mcw->mailbox && mcw->add ) 
      gtk_object_destroy ( GTK_OBJECT(mcw->mailbox) ) ;
  }
  
  /* close the new mailbox window */
  gtk_widget_destroy (mcw->window);
  g_free (mcw);
  mcw = NULL;
}

static void
set_the_page (GtkWidget * widget, MailboxConfPageType type)
{
  mcw->the_page = type;
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
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", 
		      GTK_SIGNAL_FUNC(set_the_page), (gpointer) MC_PAGE_LOCAL);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (radio_button), TRUE);
  gtk_widget_show (radio_button);

  /* imap mailbox */
  radio_button = gtk_radio_button_new_with_label
    (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), 
     _("Single IMAP folder"));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", 
		      GTK_SIGNAL_FUNC (set_the_page), (gpointer) MC_PAGE_IMAP);
  gtk_widget_show (radio_button);

  /* imapdir entry */
  radio_button = gtk_radio_button_new_with_label
      (gtk_radio_button_group (GTK_RADIO_BUTTON (radio_button)), 
       _("IMAP folder set"));
  gtk_box_pack_start (GTK_BOX (vbox), radio_button, FALSE, FALSE, 0);
  gtk_signal_connect (GTK_OBJECT (radio_button), "clicked", 
		      GTK_SIGNAL_FUNC (set_the_page), 
		      (gpointer) MC_PAGE_IMAP_DIR);
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

  return_widget = table = gtk_table_new (8, 2, FALSE);
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

  /* toggle for apop */

  mcw->pop_use_apop = gtk_check_button_new_with_label (_("Use APOP"));
  gtk_table_attach (GTK_TABLE (table), mcw->pop_use_apop, 0, 2, 5, 6,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (mcw->pop_use_apop);

  /* toggle for check */

  mcw->pop_check = gtk_check_button_new_with_label (_("Check"));
  gtk_table_attach (GTK_TABLE (table), mcw->pop_check, 0, 2, 6, 7,
		    GTK_FILL, GTK_FILL,
		    10, 10);
  gtk_widget_show (mcw->pop_check);

  /* toggle for deletion from server */

  mcw->pop_delete_from_server = gtk_check_button_new_with_label (_("Delete from server"));
  gtk_table_attach (GTK_TABLE (table), mcw->pop_delete_from_server, 0, 2, 7, 8,
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

  switch (mcw->the_page)
    {
    case MC_PAGE_LOCAL:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_LOCAL);
      break;

    case MC_PAGE_POP3:
      gtk_notebook_set_page (GTK_NOTEBOOK (mcw->notebook), MC_PAGE_POP3);
      break;

    case MC_PAGE_IMAP:
    case MC_PAGE_IMAP_DIR:
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



