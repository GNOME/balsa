/* Balsa E-Mail Client
 * Copyright (C) 1998 Jay Painter and Stuart Parmenter
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
#include "mailbox.h"
#include "misc.h"
#include "save-restore.h"

/*
 * The new created mailbox has to be inserted into the Accounts
 * vector of the config file.
 */

void
add_mailbox_config (Mailbox * mailbox)
{
  GString *gstring;
  gchar **mblist;
  gint    nboxes;
  
  GList *list = NULL;
  Mailbox *mb;

  gint i = 0;

  gstring = g_string_new (NULL);
  
  gnome_config_get_vector ("/balsa/Global/Accounts", &nboxes, &mblist);
  mblist = g_realloc(mblist, sizeof (char*) * (nboxes + 1));
  mblist[nboxes] = mailbox->name;
  gnome_config_set_vector ("/balsa/Global/Accounts", nboxes + 1, mblist);
    
  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", mailbox->name);
  gnome_config_push_prefix (gstring->str);

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      gnome_config_set_int ("Type", 0);
      gnome_config_set_string ("Path", MAILBOX_LOCAL (mailbox)->path);
      break;
    case MAILBOX_POP3:
      gnome_config_set_int ("Type", 1);
      gnome_config_set_string ("username", MAILBOX_POP3 (mailbox)->user);
      gnome_config_set_string ("password", MAILBOX_POP3 (mailbox)->passwd);
      gnome_config_set_string ("server", MAILBOX_POP3 (mailbox)->server);
      break;
    case MAILBOX_IMAP:
      gnome_config_set_int ("Type", 2);
      gnome_config_set_string ("username", MAILBOX_IMAP (mailbox)->user);
      gnome_config_set_string ("password", MAILBOX_IMAP (mailbox)->passwd);
      gnome_config_set_string ("server", MAILBOX_IMAP (mailbox)->server);
      gnome_config_set_int ("port", MAILBOX_IMAP (mailbox)->port);
      gnome_config_set_string ("Path", MAILBOX_IMAP (mailbox)->path);
      break;
    }

  gnome_config_pop_prefix ();

  gnome_config_sync ();
  g_string_free (gstring, TRUE);
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
  mblist = g_new (gchar *, g_node_n_children (balsa_app.mailbox_nodes));
#if 0
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
#endif
  gnome_config_sync ();
  g_string_free (gstring, 1);
}


void
update_mailbox_config (Mailbox * mailbox, gchar* old_mbox_name)
{
  GString *gstring;
  gchar **mblist;
  gchar  *mbox_name;
  GList *list = NULL;
  Mailbox *mb;
  gint    nboxes;
  
  gint i = 0;

  gstring = g_string_new (NULL);

  gnome_config_get_vector ("/balsa/Global/Accounts", &nboxes, &mblist);
  for (i = 0, mbox_name = mblist[0]; i < nboxes; mbox_name++)
    {
      if (strcmp(mbox_name, old_mbox_name) == 0)
	break;
      i++;
    }
  if (i == nboxes)
    {
      g_warning("mailbox not found in `Accounts' resource");
      /*
	@mla@ FIXME this is definitly wrong. Could be a `normal'
	mbox, which the user wants to rename. Have to look into this
	later.
      */
      mblist = g_realloc(mblist, (nboxes+1) * sizeof (gchar*));
      mblist[nboxes++] = mailbox->name;
    }
  else
    {
      mblist[i]      = mailbox->name;
    }
  gnome_config_set_vector ("/balsa/Global/Accounts", nboxes, mblist);
  i = 0;
  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", mailbox->name);
  gnome_config_push_prefix (gstring->str);

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      gnome_config_set_int ("Type", 0);
      gnome_config_set_string ("Path", MAILBOX_LOCAL (mailbox)->path);
      break;
    case MAILBOX_POP3:
      gnome_config_set_int ("Type", 1);
      gnome_config_set_string ("username", MAILBOX_POP3 (mailbox)->user);
      gnome_config_set_string ("password", MAILBOX_POP3 (mailbox)->passwd);
      gnome_config_set_string ("server", MAILBOX_POP3 (mailbox)->server);
      break;
    case MAILBOX_IMAP:
      gnome_config_set_int ("Type", 2);
      gnome_config_set_string ("username", MAILBOX_IMAP (mailbox)->user);
      gnome_config_set_string ("password", MAILBOX_IMAP (mailbox)->passwd);
      gnome_config_set_string ("server", MAILBOX_IMAP (mailbox)->server);
      gnome_config_set_int ("port", MAILBOX_IMAP (mailbox)->port);
      gnome_config_set_string ("Path", MAILBOX_IMAP (mailbox)->path);
      break;
    }

  gnome_config_pop_prefix ();

  gnome_config_sync ();
  g_string_free (gstring, TRUE);
}

gint
load_mailboxes (gchar * name)
{
  MailboxType mailbox_type;
  Mailbox *mailbox;
  gint type;
  GString *gstring = g_string_new (NULL);
  gchar *path;
  GNode *node;

  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s", name);


  if (!gnome_config_has_section (gstring->str))
    return FALSE;


  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", name);
  gnome_config_pop_prefix ();
  gnome_config_push_prefix (gstring->str);
  type = gnome_config_get_int ("Type=0");
  path = gnome_config_get_string ("Path");


  switch (type)
    {

      /* Local mailbox */
    case 0:
      mailbox_type = mailbox_valid (path);
      if (mailbox_type != MAILBOX_UNKNOWN)
	{
	  mailbox = mailbox_new (mailbox_type);
	  mailbox->name = g_strdup (name);
	  MAILBOX_LOCAL (mailbox)->path = g_strdup (path);

	  if (strcmp ("Inbox", name) == 0)
	    {
	      balsa_app.inbox = mailbox;
	    }
	  else if (strcmp ("Outbox", name) == 0)
	    {
	      balsa_app.outbox = mailbox;
	    }
	  else if (strcmp ("Trash", name) == 0)
	  {
	    balsa_app.trash = mailbox;
	  }
	  else
	  {
	    if (mailbox_type == MAILBOX_MH)
	      node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, TRUE));
	    else
	      node = g_node_new (mailbox_node_new (g_strdup (mailbox->name), mailbox, FALSE));
	    g_node_append (balsa_app.mailbox_nodes, node);
	  }
	}
      break;

      /*  POP3  */
    case 1:
      mailbox = mailbox_new (MAILBOX_POP3);
      mailbox->name = g_strdup (name);
      MAILBOX_POP3 (mailbox)->user = gnome_config_get_string ("username");
      MAILBOX_POP3 (mailbox)->passwd = gnome_config_get_string ("password");
      MAILBOX_POP3 (mailbox)->server = gnome_config_get_string ("server");
      balsa_app.inbox_input = g_list_append (balsa_app.inbox_input, mailbox);
      break;

      /*  IMAP  */
    case 2:
      mailbox = mailbox_new (MAILBOX_IMAP);
      mailbox->name = g_strdup (name);
      MAILBOX_IMAP (mailbox)->user = gnome_config_get_string ("username");
      MAILBOX_IMAP (mailbox)->passwd = gnome_config_get_string ("password");
      MAILBOX_IMAP (mailbox)->server = gnome_config_get_string ("server");
      MAILBOX_IMAP (mailbox)->port = get_int_set_default ("port", 143);
      MAILBOX_IMAP (mailbox)->path = gnome_config_get_string ("Path");
      node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
      break;
    }


  gnome_config_pop_prefix ();
  gnome_config_pop_prefix ();
  gnome_config_sync ();
  g_string_free (gstring, 1);
  g_free (path);
  return TRUE;
}


/*
 * global settings
 */
void
restore_global_settings ()
{
  GString *path;
  gchar tmp[PATH_MAX];

  /* set to Global configure section */
  gnome_config_push_prefix ("/balsa/Global/");

  /* user's real name */
  balsa_app.real_name = get_string_set_default ("real name", g_get_real_name ());

  /* user name */
  balsa_app.username = get_string_set_default ("user name", g_get_user_name ());

  /* hostname */
  balsa_app.hostname = get_string_set_default ("host name", "localhost");

  /* directory */
  path = g_string_new (NULL);
  g_string_sprintf (path, "%s/Mail", g_get_home_dir ());
  balsa_app.local_mail_directory = get_string_set_default ("local mail directory", path->str);

  load_mailboxes ("Inbox");
  load_mailboxes ("Outbox");
  load_mailboxes ("Trash");

  /* smtp server */
  balsa_app.smtp_server = get_string_set_default ("smtp server", "localhost");

  /* main window width & height */
  balsa_app.mw_width = get_int_set_default ("main window width", balsa_app.mw_width);
  balsa_app.mw_height = get_int_set_default ("main window height", balsa_app.mw_height);

  /* toolbar style */
  balsa_app.toolbar_style = get_int_set_default ("toolbar style", (gint) balsa_app.toolbar_style);

  /* debuging */
  balsa_app.debug = get_int_set_default ("debug", (gint) balsa_app.debug);

  /* mdi style */
  balsa_app.mdi_style = get_int_set_default ("mdi style", (gint) balsa_app.mdi_style);


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
  gnome_config_set_string ("smtp server", balsa_app.smtp_server);

  gnome_config_set_string ("local mail directory", balsa_app.local_mail_directory);
  gnome_config_set_int ("main window width", (gint) balsa_app.mw_width);
  gnome_config_set_int ("main window height", (gint) balsa_app.mw_height);
  gnome_config_set_int ("toolbar style", (gint) balsa_app.toolbar_style);
  gnome_config_set_int ("mdi style", (gint) balsa_app.mdi_style);
  gnome_config_set_int ("debug", (gint) balsa_app.debug);

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
