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
#include <pwd.h>

#include "balsa-app.h"
#include "misc.h"
#include "save-restore.h"



void
add_mailbox_config (gchar * name, gchar * path, gint type)
{
  GString *gstring;
  gchar **mblist;

  GList *list=NULL;
  Mailbox *mailbox;

  gint i = 0;

  gstring = g_string_new (NULL);

  mblist = g_new (gchar *, g_list_length (balsa_app.mailbox_list));

  list = g_list_first (balsa_app.mailbox_list);
  if (!list)
    {
      printf("error adding new mailbox, aborting\n");
      return;
    }

  for (i = 0; list; i++)
    {
      mailbox = list->data;
      mblist[i] = g_strdup (mailbox->name);
      list = list->next;
    }
  
  gnome_config_set_vector ("/balsa/Global/Accounts", i, (const char *const *) mblist);

  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", name);
  gnome_config_push_prefix (gstring->str);
  gnome_config_set_string ("Path", path);
  gnome_config_set_int ("Type", type);
  gnome_config_pop_prefix ();

  gnome_config_sync ();
  g_string_free (gstring, 1);
}


void
delete_mailbox_config (gchar * name)
{
  GString *gstring;
  gchar **mblist;
  gint i;
  GList *list;
  Mailbox *mailbox;

  gstring = g_string_new (NULL);
  g_string_sprintf (gstring, "/balsa/%s", name);
  gnome_config_clean_section (gstring->str);

  /* TODO we should prolly lower this by one here, so save on some memory... */
  mblist = g_new (gchar *, g_list_length (balsa_app.mailbox_list));

  list = g_list_first (balsa_app.mailbox_list);
  for (i = 0; list; list = list->next, i++, mailbox = list->data)
    {
      if (!strcmp (name, mailbox->name))
	{
	  balsa_app.mailbox_list = g_list_remove (balsa_app.mailbox_list, list->data);
	  i--;			/* lets move this back down one */
	}
      else
	{
	  mblist[i] = g_strdup (mailbox->name);
	}
    }
  gnome_config_sync ();
  g_string_free (gstring, 1);
}


void
change_mailbox_config (gchar * name, gchar * setting, gchar * value)
{
  GString *gstring;

  gstring = g_string_new (NULL);

  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", name);
  gnome_config_push_prefix (gstring->str);
  gnome_config_set_string (setting, value);
  gnome_config_pop_prefix ();

  gnome_config_sync ();
  g_string_free (gstring, 1);
}


gint
load_mailboxes (gchar * name)
{
  MailboxType mailbox_type;
  Mailbox *mailbox;
  gint i = 0;
  gint type;
  GString *gstring;
  gstring = g_string_new (NULL);


  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s", name);


  if (!gnome_config_has_section (gstring->str))
    return FALSE;


  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", name);
  gnome_config_pop_prefix ();
  gnome_config_push_prefix (gstring->str);
  type = gnome_config_get_int ("Type=0");
  

  switch (type)
    {

    /* Local mailbox */
    case 0:
      mailbox_type = mailbox_valid (name);
      if (mailbox_type != MAILBOX_UNKNOWN)
	{
	  mailbox = mailbox_new (mailbox_type);
	  mailbox->name = g_strdup (name);
	  MAILBOX_LOCAL (mailbox)->path = gnome_config_get_string ("Path");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
	}
      break;
 
    /*  POP3  */
    case 1:	
      mailbox =  mailbox_new (MAILBOX_POP3);
      mailbox->name = g_strdup (name);
      MAILBOX_POP3 (mailbox)->user = gnome_config_get_string ("username");
      MAILBOX_POP3 (mailbox)->passwd = gnome_config_get_string ("password");
      MAILBOX_POP3 (mailbox)->server = gnome_config_get_string ("server");
      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
      break;

    /*  IMAP  */
    case 2:
      mailbox =  mailbox_new (MAILBOX_IMAP);
      mailbox->name = g_strdup (name);
      MAILBOX_IMAP (mailbox)->user = gnome_config_get_string ("username");
      MAILBOX_IMAP (mailbox)->passwd = gnome_config_get_string ("password");
      MAILBOX_IMAP (mailbox)->server = gnome_config_get_string ("server");
      MAILBOX_IMAP (mailbox)->path = gnome_config_get_string ("Path");
      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
      break;

    /*  NNTP  */
    case 3:
      mailbox = mailbox_new (MAILBOX_NNTP);
      mailbox->name = g_strdup (name);
      MAILBOX_NNTP (mailbox)->server = gnome_config_get_string ("server");
      MAILBOX_NNTP (mailbox)->newsgroup = gnome_config_get_string ("newsgroup");
      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
      break;
    }


  gnome_config_pop_prefix ();
  gnome_config_pop_prefix ();
  gnome_config_sync ();
  g_string_free (gstring, 1);
}


/*
 * global settings
 */
void
restore_global_settings ()
{
  GString *path;
  struct passwd *pw;

  pw = getpwuid (getuid ());

  /* set to Global configure section */
  gnome_config_push_prefix ("/balsa/Global/");

  /* user's real name */
  balsa_app.real_name = get_string_set_default ("real name", pw->pw_gecos);

  /* user name */
  balsa_app.username = get_string_set_default ("user name", pw->pw_name);

  /* hostname */
  balsa_app.hostname = get_string_set_default ("host name", mylocalhost ());

  /* organization */
  balsa_app.organization = get_string_set_default ("organization", "None");

  /* directory */
  path = g_string_new (NULL);
  g_string_sprintf (path, "%s/Mail", pw->pw_dir);
  balsa_app.local_mail_directory = get_string_set_default ("local mail directory", path->str);
  g_string_free (path, 1);

  /* smtp server */
  balsa_app.smtp_server = get_string_set_default ("smtp server", "localhost");

  /* main window width & height */
  balsa_app.mw_width = get_int_set_default ("main window width", balsa_app.mw_width);
  balsa_app.mw_height = get_int_set_default ("main window height", balsa_app.mw_height);

  /* toolbar style */
  balsa_app.toolbar_style = get_int_set_default ("toolbar style", (gint) balsa_app.toolbar_style);

  /* debuging */
  balsa_app.debug = get_int_set_default ("debug", (gint) balsa_app.debug);


  /* save changes */
  gnome_config_pop_prefix ();
  gnome_config_sync ();
}


void
save_global_settings ()
{
  gnome_config_push_prefix ("/balsa/Global/");

  gnome_config_set_string ("real name", balsa_app.real_name);
  gnome_config_set_string ("user name", balsa_app.username);
  gnome_config_set_string ("host name", balsa_app.hostname);
  gnome_config_set_string ("organization", balsa_app.organization);
  gnome_config_set_string ("smtp server", balsa_app.smtp_server);
  gnome_config_set_string ("local mail directory", balsa_app.local_mail_directory);
  gnome_config_set_int ("main window width", (gint) balsa_app.mw_width);
  gnome_config_set_int ("main window height", (gint) balsa_app.mw_height);
  gnome_config_set_int ("toolbar style", (gint) balsa_app.toolbar_style);
  gnome_config_set_int ("debug", (gint) balsa_app.debug);

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
