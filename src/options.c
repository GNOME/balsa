/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter and Stuart Parmenter
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "options.h"
#include "index.h"
#include "mailbox.h"


#define DEBUG 1


Balsa_Options *mainOptions;
static Personality *optionspersselected;
static GtkWidget *personmenu;
static personality_box_options *options = NULL;

void update_personality_box (GtkWidget *, personality_box_options *);

gint
delete_event (GtkWidget * widget, gpointer data)
{
  return (TRUE);
}

gint
options_init (void)
{
  int i = 0;
  Personality *pers = NULL;
  GString *gstring, *buffer;

  gstring = g_string_new (NULL);
  buffer = g_string_new (NULL);

  mainOptions = g_malloc0 (sizeof (Balsa_Options));

  mainOptions->pers = NULL;
  mainOptions->mailboxes = NULL;

  for (i = 0;; i++)
    {
      g_string_truncate (buffer, 0);
      g_string_sprintf (buffer, "/balsa/Accounts/%i", i);

      if (gnome_config_get_string (buffer->str))
	{
	  g_string_truncate (gstring, 0);
	  gstring = g_string_append (gstring, "/balsa/");
	  gstring = g_string_append (gstring, gnome_config_get_string (buffer->str));
	  gstring = g_string_append (gstring, "/");

	  fprintf (stderr, "%s\n", gstring->str);

	  gnome_config_pop_prefix ();
	  gnome_config_push_prefix (gstring->str);

	  pers = g_malloc0 (sizeof (Personality));
	  pers->persnum = i;
	  pers->name = gnome_config_get_string ("AccountName");
	  pers->type = gnome_config_get_int ("AccountType=0");
	  pers->realname = gnome_config_get_string ("RealName");
	  pers->replyto = gnome_config_get_string ("Replyto");

#ifdef DEBUG
	  fprintf (stderr, "AccoutName:   %s\n", pers->name);
	  fprintf (stderr, "-------------------------------\n");
	  fprintf (stderr, "AccountType:  %i\n", pers->type);
	  fprintf (stderr, "RealName:     %s\n", pers->realname);
	  fprintf (stderr, "Replyto:      %s\n", pers->replyto);
#endif

	  switch (pers->type)
	    {
	    case PERS_POP3:
	      pers->p_pop3server = gnome_config_get_string ("POP3_pop3server");
	      pers->p_smtpserver = gnome_config_get_string ("POP3_smtpserver");
	      pers->p_username = gnome_config_get_string ("POP3_username");
	      pers->p_password = gnome_config_get_string ("POP3_password");
	      pers->p_checkmail = gnome_config_get_int ("POP3_checkmail=1");
#ifdef DEBUG
	      fprintf (stderr, "---POP3-----\n");
	      fprintf (stderr, "POP3 Server:  %s\n", pers->p_pop3server);
	      fprintf (stderr, "SMTP Server:  %s\n", pers->p_smtpserver);
	      fprintf (stderr, "Username:     %s\n", pers->p_username);
	      fprintf (stderr, "Password:     %s\n", pers->p_password);
	      fprintf (stderr, "Checkmail:    %i\n", pers->p_checkmail);
#endif
	      break;
	    case PERS_IMAP:
	      pers->i_imapserver = gnome_config_get_string ("IMAP_imapserver");
	      pers->i_smtpserver = gnome_config_get_string ("IMAP_smtpserver");
	      pers->i_username = gnome_config_get_string ("IMAP_username");
	      pers->i_password = gnome_config_get_string ("IMAP_password");
	      pers->i_checkmail = gnome_config_get_int ("IMAP_checkmail=1");
#ifdef DEBUG
	      fprintf (stderr, "---IMAP-----\n");
	      fprintf (stderr, "IMAP Server:  %s\n", pers->i_imapserver);
	      fprintf (stderr, "SMTP Server:  %s\n", pers->i_smtpserver);
	      fprintf (stderr, "Username:     %s\n", pers->i_username);
	      fprintf (stderr, "Password:     %s\n", pers->i_password);
	      fprintf (stderr, "Checkmail:    %i\n", pers->i_checkmail);
#endif
	      break;
	    case PERS_LOCAL:
	      pers->l_mblocation = gnome_config_get_string ("LOCAL_mblocation");
	      pers->l_smtpserver = gnome_config_get_string ("LOCAL_smtpserver");
	      pers->l_checkmail = gnome_config_get_int ("LOCAL_checkmail=1");
#ifdef DEBUG
	      fprintf (stderr, "---Local----\n");
	      fprintf (stderr, "Mailbox file: %s\n", pers->l_mblocation);
	      fprintf (stderr, "SMTP Server:  %s\n", pers->l_smtpserver);
	      fprintf (stderr, "Checkmail:    %i\n", pers->l_checkmail);
#endif
	      break;
	    }
#ifdef DEBUG
	  fprintf (stderr, "\n");
#endif

	  mainOptions->pers = g_list_append (mainOptions->pers, pers);
	  gnome_config_pop_prefix ();
	}
      else
	{
	  if (i == 0)
	    {
	      pers = g_malloc0 (sizeof (Personality));

	      gnome_config_push_prefix ("/balsa/Accounts/");
	      gnome_config_set_string ("0", "Default");
	      gnome_config_pop_prefix ();

	      gnome_config_push_prefix ("/balsa/Default/");

	      gnome_config_set_string ("AccountName", "Default");
	      gnome_config_set_int ("AccountType", 0);

	      gnome_config_set_string ("Realname", "");
	      gnome_config_set_string ("Replyto", "");

	      gnome_config_set_string ("POP3_pop3server", "");
	      gnome_config_set_string ("POP3_smtpserver", "");
	      gnome_config_set_string ("POP3_username", "");
	      gnome_config_set_string ("POP3_password", "");

	      gnome_config_set_string ("IMAP_imapserver", "");
	      gnome_config_set_string ("IMAP_smtpserver", "");
	      gnome_config_set_string ("IMAP_username", "");
	      gnome_config_set_string ("IMAP_password", "");

	      gnome_config_set_string ("LOCAL_mblocation", "");
	      gnome_config_set_string ("LOCAL_smtpserver", "");

	      gnome_config_pop_prefix ();
	      gnome_config_sync ();

	      pers->name = "Default";
	      pers->type = 0;
	      pers->realname = NULL;
	      pers->replyto = NULL;

	      pers->p_pop3server = NULL;
	      pers->p_smtpserver = NULL;
	      pers->p_username = NULL;
	      pers->p_password = NULL;
	      pers->p_checkmail = 1;

	      mainOptions->pers = g_list_append (mainOptions->pers, pers);
	    }
	  gnome_config_pop_prefix ();
	  gnome_config_sync ();
	  g_string_free (gstring, 1);
	  g_string_free (buffer, 1);
	  return i;
	}
    }
  gnome_config_pop_prefix ();
  gnome_config_sync ();
  g_string_free (gstring, 1);
  g_string_free (buffer, 1);
  return i;
}

void
add_new_personality (GtkWidget * widget, GtkWidget * entry)
{
  Personality *pers = g_malloc0 (sizeof (Personality));
  gchar *name = gtk_entry_get_text (GTK_ENTRY (entry));
  GString *gstring = g_string_new (NULL);
  int num;
  GtkWidget *menuitem;

  mainOptions->pers = g_list_last (mainOptions->pers);

  num = ((Personality *) mainOptions->pers->data)->persnum;
  g_string_sprintf (gstring, "%i", num + 1);

  gnome_config_push_prefix ("/balsa/Accounts/");
  gnome_config_set_string (gstring->str, name);
  gnome_config_pop_prefix ();


  g_string_truncate (gstring, 0);
  gstring = g_string_append (gstring, "/balsa/");
  gstring = g_string_append (gstring, name);
  gstring = g_string_append (gstring, "/");

  gnome_config_push_prefix (gstring->str);

  gnome_config_set_string ("AccountName", name);
  gnome_config_set_int ("AccountType", 0);

  gnome_config_set_string ("Realname", "");
  gnome_config_set_string ("Replyto", "");

  gnome_config_set_string ("POP3_pop3server", "");
  gnome_config_set_string ("POP3_smtpserver", "");
  gnome_config_set_string ("POP3_username", "");
  gnome_config_set_string ("POP3_password", "");

  gnome_config_set_string ("IMAP_imapserver", "");
  gnome_config_set_string ("IMAP_smtpserver", "");
  gnome_config_set_string ("IMAP_username", "");
  gnome_config_set_string ("IMAP_password", "");

  gnome_config_set_string ("LOCAL_mblocation", "");
  gnome_config_set_string ("LOCAL_smtpserver", "");

  gnome_config_pop_prefix ();
  gnome_config_sync ();

  pers->persnum = num + 1;
  pers->name = name;
  pers->type = 0;
  pers->realname = NULL;
  pers->replyto = NULL;

  pers->p_pop3server = NULL;
  pers->p_smtpserver = NULL;
  pers->p_username = NULL;
  pers->p_password = NULL;
  pers->p_checkmail = 1;

  mainOptions->pers = g_list_append (mainOptions->pers, pers);
  gnome_config_pop_prefix ();
  gnome_config_sync ();
  g_string_free (gstring, 1);

  menuitem = gtk_menu_item_new_with_label (pers->name);

  gtk_object_set_user_data (GTK_OBJECT (menuitem), ((Personality *) (pers)));
  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
		      (GtkSignalFunc) update_personality_box,
		      options);

  gtk_menu_append (GTK_MENU (personmenu), menuitem);
  gtk_widget_show (menuitem);

}

void
new_options_box (GtkWidget * widget, gpointer data)
{
  GtkWidget *window;
  GtkWidget *okbutton;
  GtkWidget *cancelbutton;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *entry;
  GtkWidget *label;

  window = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_title (GTK_WINDOW (window), (char *) data);
  gtk_widget_set_usize (GTK_WIDGET (window), 320, 200);

  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  gtk_container_border_width (GTK_CONTAINER (window), 10);

  vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_widget_show (vbox);

  label = gtk_label_new ("Enter a name for your the new account:\n");
  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (vbox), entry, TRUE, TRUE, 0);
  gtk_widget_show (entry);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
  gtk_widget_show (hbox);

  okbutton = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_box_pack_start (GTK_BOX (hbox), okbutton, FALSE, TRUE, 0);
  gtk_widget_show (okbutton);
  gtk_signal_connect (GTK_OBJECT (okbutton), "clicked",
		      GTK_SIGNAL_FUNC (add_new_personality), entry);
  gtk_signal_connect_object (GTK_OBJECT (okbutton), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));

  cancelbutton = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_box_pack_start (GTK_BOX (hbox), cancelbutton, FALSE, TRUE, 0);
  gtk_widget_show (cancelbutton);
  gtk_signal_connect (GTK_OBJECT (cancelbutton), "clicked",
		      GTK_SIGNAL_FUNC (delete_event), NULL);
  gtk_signal_connect_object (GTK_OBJECT (cancelbutton), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));

  gtk_widget_show (window);
}




/*
   FIXME:
   update personalities list and gnome-config stuff... this needs to be updated for
   IMAP and local mailboxes... the options_init seems to read things in fine, and works well.
   Let me know if you have any ideas?
   -Pav
 */
void
change_options (GtkWidget * widget, personality_box_options * options)
{
  GString *gstring = g_string_new (NULL);
  Personality *currentpers = gtk_object_get_user_data (GTK_OBJECT (widget));

  gnome_config_pop_prefix ();
  gnome_config_sync ();

  gstring = g_string_append (gstring, "/balsa/");
  gstring = g_string_append (gstring, optionspersselected->name);
  gstring = g_string_append (gstring, "/");
  gnome_config_clean_section (gstring->str);

  gnome_config_push_prefix (gstring->str);

  gnome_config_set_string ("Realname", gtk_entry_get_text (GTK_ENTRY (options->realname)));
  gnome_config_set_string ("Replyto", gtk_entry_get_text (GTK_ENTRY (options->replyto)));

  gnome_config_set_string ("POP3_pop3server", gtk_entry_get_text (GTK_ENTRY (options->pop3_pop3server)));
  gnome_config_set_string ("POP3_smtpserver", gtk_entry_get_text (GTK_ENTRY (options->pop3_smtpserver)));
  gnome_config_set_string ("POP3_username", gtk_entry_get_text (GTK_ENTRY (options->pop3_username)));
  gnome_config_set_string ("POP3_password", gtk_entry_get_text (GTK_ENTRY (options->pop3_password)));

  gnome_config_set_string ("IMAP_imapserver", gtk_entry_get_text (GTK_ENTRY (options->imap_imapserver)));
  gnome_config_set_string ("IMAP_smtpserver", gtk_entry_get_text (GTK_ENTRY (options->imap_smtpserver)));
  gnome_config_set_string ("IMAP_username", gtk_entry_get_text (GTK_ENTRY (options->imap_username)));
  gnome_config_set_string ("IMAP_password", gtk_entry_get_text (GTK_ENTRY (options->imap_password)));

  gnome_config_set_string ("LOCAL_mblocation", gtk_entry_get_text (GTK_ENTRY (options->local_mblocation)));
  gnome_config_set_string ("LOCAL_smtpserver", gtk_entry_get_text (GTK_ENTRY (options->local_smtpserver)));

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}

void
update_personality_box (GtkWidget * widget, personality_box_options * options)
{
  char *buffer;
  Personality *currentpers = gtk_object_get_user_data (GTK_OBJECT (widget));

  change_options (widget, options);

  gtk_entry_set_text (GTK_ENTRY (options->realname), currentpers->realname);
  gtk_entry_set_text (GTK_ENTRY (options->replyto), currentpers->replyto);

  gtk_entry_set_text (GTK_ENTRY (options->pop3_pop3server), currentpers->p_pop3server);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_smtpserver), currentpers->p_smtpserver);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_username), currentpers->p_username);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_password), currentpers->p_password);

  gtk_entry_set_text (GTK_ENTRY (options->imap_imapserver), currentpers->i_imapserver);
  gtk_entry_set_text (GTK_ENTRY (options->imap_smtpserver), currentpers->i_smtpserver);
  gtk_entry_set_text (GTK_ENTRY (options->imap_username), currentpers->i_username);
  gtk_entry_set_text (GTK_ENTRY (options->imap_password), currentpers->i_password);

  optionspersselected = currentpers;
}

/*
   --------------------------------------------
   |                 Settings                 |
   --------------------------------------------
   | Account:      [--<Default>--]     [New]  |
   | ---------------------------------------- |
   | Realname:     [_____________]            |
   | Reply-to:     [_____________]            |
   |  ____  ____  _____                       |
   | |POP3||IMAP||Local|_____________________ |
   |/                                        \|
   || POP3 server: [_____________]           ||
   || SMTP server: [_____________]           ||
   || Username:    [_____________]           ||
   || Password:    [_____________]           ||
   || Default      [-------------]           ||
   ||    mailbox:                            ||
   || Check mail:  [x]                       ||
   |\----------------------------------------/|
   |       [ OK ]             [Cancel]        |
   --------------------------------------------
 */

void
personality_box (GtkWidget * widget, gpointer data)
{
  GList *list;
  GtkWidget *window;
  GtkWidget *label;
  GtkWidget *notebook;
  GtkWidget *vbox;
  GtkWidget *table;
  GtkWidget *table1;
  GtkWidget *frame;
  GtkWidget *hsep;
  GtkWidget *newbutton;
  GtkWidget *okbutton, *cancelbutton;
  GtkWidget *menuitem;
  Personality *currentpers;

  optionspersselected = g_malloc0 (sizeof (Personality));

  options = g_malloc (sizeof (personality_box_options));

  window = gtk_window_new (GTK_WINDOW_DIALOG);
  gtk_window_set_title (GTK_WINDOW (window), "Settings");
  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);
  gtk_container_border_width (GTK_CONTAINER (window), 3);

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_container_add (GTK_CONTAINER (window), vbox);
  gtk_widget_show (vbox);

  table = gtk_table_new (6, 5, FALSE);
  gtk_box_pack_start (GTK_BOX (vbox), table, TRUE, TRUE, 0);
  gtk_widget_show (table);

  label = gtk_label_new ("Account:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 2, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  personmenu = gtk_menu_new ();

  if (mainOptions->pers)
    {
      list = mainOptions->pers;
      list = g_list_first (list);
      currentpers = ((Personality *) (list->data));

      while (list)
	{
	  fprintf (stderr, "%s\n", ((Personality *) (list->data))->name);
	  menuitem = gtk_menu_item_new_with_label (((Personality *) (list->data))->name);

	  gtk_object_set_user_data (GTK_OBJECT (menuitem), ((Personality *) (list->data)));
	  gtk_signal_connect (GTK_OBJECT (menuitem), "activate",
			      (GtkSignalFunc) update_personality_box,
			      options);

	  gtk_menu_append (GTK_MENU (personmenu), menuitem);
	  gtk_widget_show (menuitem);
	  list = list->next;
	}
    }
  options->pop3_default_mailbox = gtk_option_menu_new ();
  gtk_table_attach (GTK_TABLE (table), options->pop3_default_mailbox, 2, 4, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_option_menu_set_menu (GTK_OPTION_MENU (options->pop3_default_mailbox), personmenu);
  gtk_widget_show (options->pop3_default_mailbox);

  newbutton = gtk_button_new_with_label ("New");
  gtk_table_attach (GTK_TABLE (table), newbutton, 4, 5, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (newbutton);

  gtk_signal_connect (GTK_OBJECT (newbutton), "clicked",
		      GTK_SIGNAL_FUNC (new_options_box), NULL);

  hsep = gtk_hseparator_new ();
  gtk_table_attach (GTK_TABLE (table), hsep, 0, 5, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (hsep);

  label = gtk_label_new ("Real name:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 2, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->realname = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), options->realname, 2, 4, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->realname);

  label = gtk_label_new ("Reply-to:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table), label, 0, 2, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->replyto = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table), options->replyto, 2, 4, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->replyto);

/* Notebook */

  notebook = gtk_notebook_new ();
  gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);
  gtk_table_attach (GTK_TABLE (table), notebook, 0, 5, 4, 5, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_EXPAND | GTK_FILL | GTK_SHRINK, 0, 0);
  gtk_widget_show (notebook);

  frame = gtk_frame_new ("");
  gtk_container_border_width (GTK_CONTAINER (frame), 0);
  gtk_widget_show (frame);

  table1 = gtk_table_new (6, 2, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), table1);
  gtk_widget_show (table1);

  label = gtk_label_new ("POP3 server:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_pop3server = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_pop3server, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_pop3server);

  label = gtk_label_new ("SMTP server:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_smtpserver = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_smtpserver, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_smtpserver);

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_username, 1, 2, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_username);

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_password, 1, 2, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_password);

  label = gtk_label_new ("Default mailbox:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_default_mailbox = gtk_option_menu_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_default_mailbox, 1, 2, 4, 5, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_default_mailbox);

  label = gtk_label_new ("Check mail:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->pop3_check_mail = gtk_check_button_new ();
  gtk_table_attach (GTK_TABLE (table1), options->pop3_check_mail, 1, 2, 5, 6, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->pop3_check_mail);

  label = gtk_label_new ("POP3");
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
  gtk_widget_show (label);

/* IMAP */

  frame = gtk_frame_new ("");
  gtk_container_border_width (GTK_CONTAINER (frame), 0);
  gtk_widget_show (frame);

  table1 = gtk_table_new (6, 2, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), table1);
  gtk_widget_show (table1);

  label = gtk_label_new ("IMAP server:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_imapserver = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_imapserver, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_imapserver);

  label = gtk_label_new ("SMTP server:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_smtpserver = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_smtpserver, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_smtpserver);

  label = gtk_label_new ("Username:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_username = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_username, 1, 2, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_username);

  label = gtk_label_new ("Password:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_password = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_password, 1, 2, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_password);

  label = gtk_label_new ("Default mailbox:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 4, 5, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_default_mailbox = gtk_option_menu_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_default_mailbox, 1, 2, 4, 5, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_default_mailbox);

  label = gtk_label_new ("Check mail:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 5, 6, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->imap_check_mail = gtk_check_button_new ();
  gtk_table_attach (GTK_TABLE (table1), options->imap_check_mail, 1, 2, 5, 6, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->imap_check_mail);

  label = gtk_label_new ("IMAP");
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
  gtk_widget_show (label);


/* Local Mailboxes */

  frame = gtk_frame_new ("");
  gtk_container_border_width (GTK_CONTAINER (frame), 0);
  gtk_widget_show (frame);

  table1 = gtk_table_new (4, 2, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), table1);
  gtk_widget_show (table1);

  label = gtk_label_new ("Local mailbox:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->local_mblocation = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->local_mblocation, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->local_mblocation);

  label = gtk_label_new ("SMTP server:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->local_smtpserver = gtk_entry_new ();
  gtk_table_attach (GTK_TABLE (table1), options->local_smtpserver, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->local_smtpserver);

  label = gtk_label_new ("Default mailbox:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->local_default_mailbox = gtk_option_menu_new ();
  gtk_table_attach (GTK_TABLE (table1), options->local_default_mailbox, 1, 2, 2, 3, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->local_default_mailbox);

  label = gtk_label_new ("Check mail:");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (label);

  options->local_check_mail = gtk_check_button_new ();
  gtk_table_attach (GTK_TABLE (table1), options->local_check_mail, 1, 2, 3, 4, GTK_EXPAND | GTK_FILL | GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (options->local_check_mail);

  label = gtk_label_new ("Local");
  gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
  gtk_widget_show (label);


/* Ok/Cancel buttons */

  okbutton = gnome_stock_button (GNOME_STOCK_BUTTON_OK);
  gtk_table_attach (GTK_TABLE (table), okbutton, 0, 1, 5, 6, GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (okbutton);

  gtk_signal_connect (GTK_OBJECT (okbutton), "clicked",
		      GTK_SIGNAL_FUNC (change_options), options);

  gtk_signal_connect_object (GTK_OBJECT (okbutton), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));

  cancelbutton = gnome_stock_button (GNOME_STOCK_BUTTON_CANCEL);
  gtk_table_attach (GTK_TABLE (table), cancelbutton, 4, 5, 5, 6, GTK_SHRINK, GTK_SHRINK, 0, 0);
  gtk_widget_show (cancelbutton);

  gtk_signal_connect (GTK_OBJECT (cancelbutton), "clicked",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  gtk_signal_connect_object (GTK_OBJECT (cancelbutton), "clicked",
			     GTK_SIGNAL_FUNC (gtk_widget_destroy),
			     GTK_OBJECT (window));


  gtk_entry_set_text (GTK_ENTRY (options->realname), currentpers->realname);
  gtk_entry_set_text (GTK_ENTRY (options->replyto), currentpers->replyto);

  gtk_entry_set_text (GTK_ENTRY (options->pop3_pop3server), currentpers->p_pop3server);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_smtpserver), currentpers->p_smtpserver);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_username), currentpers->p_username);
  gtk_entry_set_text (GTK_ENTRY (options->pop3_password), currentpers->p_password);

  gtk_entry_set_text (GTK_ENTRY (options->imap_imapserver), currentpers->i_imapserver);
  gtk_entry_set_text (GTK_ENTRY (options->imap_smtpserver), currentpers->i_smtpserver);
  gtk_entry_set_text (GTK_ENTRY (options->imap_username), currentpers->i_username);
  gtk_entry_set_text (GTK_ENTRY (options->imap_password), currentpers->i_password);

  optionspersselected = currentpers;

  gtk_widget_show (window);
}
