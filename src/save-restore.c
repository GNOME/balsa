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
#include <proplist.h>

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
  GNode *node;
  proplist_t dict, new;

  dict = PLGetDictionaryEntry (balsa_app.proplist, PLMakeString ("accounts"));

  switch (mailbox->type)
    {
    case MAILBOX_MBOX:
    case MAILBOX_MH:
    case MAILBOX_MAILDIR:
      new = PLMakeDictionaryFromEntries (
					  PLMakeString ("Type"),
					  PLMakeString ("0"),
					  PLMakeString ("Path"),
			       PLMakeString (MAILBOX_LOCAL (mailbox)->path),
					  NULL);
      dict = PLInsertDictionaryEntry (balsa_app.proplist, PLMakeString (mailbox->name), new);

      node = g_node_new (mailbox_node_new (mailbox->name, mailbox,
					   mailbox->type != MAILBOX_MBOX));
      break;
    case MAILBOX_POP3:
      new = PLMakeDictionaryFromEntries (
					  PLMakeString ("Type"),
					  PLMakeString ("1"),
					  PLMakeString ("Usename"),
				PLMakeString (MAILBOX_POP3 (mailbox)->user),
					  PLMakeString ("Password"),
			      PLMakeString (MAILBOX_POP3 (mailbox)->passwd),
					  PLMakeString ("Server"),
			      PLMakeString (MAILBOX_POP3 (mailbox)->server),
					  NULL);
      dict = PLInsertDictionaryEntry (balsa_app.proplist, PLMakeString (mailbox->name), new);
      break;
    case MAILBOX_IMAP:
      new = PLMakeDictionaryFromEntries (
				  PLMakeString ("Type"), PLMakeString ("2"),
					  PLMakeString ("Server"),
			      PLMakeString (MAILBOX_IMAP (mailbox)->server),
					  PLMakeString ("Port"),
				PLMakeString (MAILBOX_IMAP (mailbox)->port),
					  PLMakeString ("Path"),
				PLMakeString (MAILBOX_IMAP (mailbox)->path),
					  PLMakeString ("Username"),
				PLMakeString (MAILBOX_IMAP (mailbox)->user),
					  PLMakeString ("Password"),
			      PLMakeString (MAILBOX_IMAP (mailbox)->passwd),
					  NULL);
      dict = PLInsertDictionaryEntry (balsa_app.proplist, PLMakeString (mailbox->name), new);

      node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
      break;
    }
}


void
delete_mailbox_config (gchar * name)
{
  proplist_t dict, accts;
  Mailbox *mailbox;

  accts = PLGetDictionaryEntry (balsa_app.proplist, PLMakeString ("accounts"));
  dict = PLGetDictionaryEntry (accts, PLMakeString (name));

  accts = PLRemoveDictionaryEntry (accts, dict);
}


void
update_mailbox_config (Mailbox * mailbox, gchar * old_mbox_name)
{
  GString *gstring;
  gchar **mblist;
  gchar *mbox_name;
  GList *list = NULL;
  Mailbox *mb;
  gint nboxes;

  gint i = 0;
#if 0
  gstring = g_string_new (NULL);

  gnome_config_get_vector ("/balsa/Global/Accounts", &nboxes, &mblist);
  for (i = 0, mbox_name = mblist[0]; i < nboxes; mbox_name++)
    {
      if (strcmp (mbox_name, old_mbox_name) == 0)
	break;
      i++;
    }
  if (i == nboxes)
    {
      g_warning ("mailbox not found in `Accounts' resource");
      /*
         @mla@ FIXME this is definitly wrong. Could be a `normal'
         mbox, which the user wants to rename. Have to look into this
         later.
       */
      mblist = g_realloc (mblist, (nboxes + 1) * sizeof (gchar *));
      mblist[nboxes++] = mailbox->name;
    }
  else
    {
      mblist[i] = mailbox->name;
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
#endif
}

gint
load_mailboxes (gchar * name)
{
  proplist_t dict, accts, mb, temp_str, temp_elem;

  GString *gstring = g_string_new (NULL);
  MailboxType mailbox_type;
  Mailbox *mailbox;
  gchar *path, *mailbox_name;
  GNode *node;
  gint type;

  temp_str = PLMakeString ("accounts");
  accts = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);

  if (accts == NULL)
    return FALSE;

  temp_str = PLMakeString (name);
  mb = PLGetDictionaryEntry (accts, temp_str);
  PLRelease (temp_str);

  /*
   * Grab the mailbox type
   */
  temp_str = PLMakeString ("Type");
  temp_elem = PLGetDictionaryEntry (mb, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      /* Default to 0 if, for some strange reason, it isn't set. */
      type = 0;
    }
  else
    type = atoi (PLGetString (temp_elem));


  /*
   * Grab the "Friendly Mailbox Name"
   */
  temp_str = PLMakeString ("Name");
  temp_elem = PLGetDictionaryEntry (mb, temp_str);
  PLRelease (temp_str);

  temp_str = PLMakeString ("Path");
  temp_elem = PLGetDictionaryEntry (mb, temp_str);
  PLRelease (temp_str);

  if (temp_elem == NULL)
    {
      mailbox_name = g_strdup (name);
    }
  else
    mailbox_name = g_strdup (PLGetString (temp_elem));

  switch (type)
    {

      /* Local mailbox */
    case 0:
      temp_str = PLMakeString ("Path");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);

      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Path isn't set in local mailbox!\n");
	  return FALSE;
	}

      path = g_strdup (PLGetString (temp_elem));

      mailbox_type = mailbox_valid (path);
      if (mailbox_type != MAILBOX_UNKNOWN)
	{
	  mailbox = mailbox_new (mailbox_type);
	  mailbox->name = g_strdup (mailbox_name);
	  MAILBOX_LOCAL (mailbox)->path = g_strdup (path);
	  g_free (path);
	}
      else
	return FALSE;
      break;

      /*  POP3  */
    case 1:
      mailbox = mailbox_new (MAILBOX_POP3);
      mailbox->name = mailbox_name;

      /* Grab the Username */
      temp_str = PLMakeString ("Username");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Username isn't set in POP3 mailbox!\n");
	  return FALSE;
	}
      MAILBOX_POP3 (mailbox)->user = PLGetString (temp_elem);

      /* Grab the Password */
      temp_str = PLMakeString ("Password");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Password isn't set in POP3 mailbox!\n");
	  return FALSE;
	}
      MAILBOX_POP3 (mailbox)->passwd = PLGetString (temp_elem);

      /* Grab the Server */
      temp_str = PLMakeString ("Server");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Server isn't set in POP3 mailbox!\n");
	  return FALSE;
	}
      MAILBOX_POP3 (mailbox)->server = PLGetString (temp_elem);

      balsa_app.inbox_input =
	g_list_append (balsa_app.inbox_input, mailbox);
      break;

      /*  IMAP  */
    case 2:
      mailbox = mailbox_new (MAILBOX_IMAP);
      mailbox->name = mailbox_name;

      /* Grab the Username */
      temp_str = PLMakeString ("Username");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Username isn't set in IMAP mailbox!\n");
	  return FALSE;
	}
      MAILBOX_IMAP (mailbox)->user = PLGetString (temp_elem);

      /* Grab the password */
      temp_str = PLMakeString ("Password");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Password isn't set in IMAP mailbox!\n");
	  return FALSE;
	}
      MAILBOX_IMAP (mailbox)->passwd = PLGetString (temp_elem);

      /* Grab the Server */
      temp_str = PLMakeString ("Server");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Server isn't set in IMAP mailbox!\n");
	  return FALSE;
	}
      MAILBOX_IMAP (mailbox)->server = PLGetString (temp_elem);

      /* Grab the port */
      temp_str = PLMakeString ("Port");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Port isn't set in IMAP mailbox!\n");
	  return FALSE;
	}
      MAILBOX_IMAP (mailbox)->port = atoi (PLGetString (temp_elem));

      /* Grab the path */
      temp_str = PLMakeString ("Path");
      temp_elem = PLGetDictionaryEntry (mb, temp_str);
      PLRelease (temp_str);
      if (temp_elem == NULL)
	{
	  fprintf (stderr, "*** Path isn't set in IMAP mailbox!\n");
	  return FALSE;
	}
      MAILBOX_IMAP (mailbox)->path = g_strdup (PLGetString (temp_elem));

      node = g_node_new (mailbox_node_new (mailbox->name, mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
      break;
    }

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
	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name),
					     mailbox, TRUE));
      else
	node = g_node_new (mailbox_node_new (g_strdup (mailbox->name),
					     mailbox, FALSE));
      g_node_append (balsa_app.mailbox_nodes, node);
    }
  return TRUE;
}


/*
 * global settings
 */
gint
restore_global_settings (void)
{
  GString *path;
  gchar tmp[PATH_MAX];
  proplist_t dict, temp_str, temp_elem;

  temp_str = PLMakeString ("global");
  dict = PLGetDictionaryEntry (balsa_app.proplist, temp_str);
  PLRelease (temp_str);
  if (dict == NULL)
    {
      fprintf (stderr, "Global settings not present in config file!\n");
      return FALSE;
    }

  /* user's real name */
  temp_str = PLMakeString ("RealName");
  temp_elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      fprintf (stderr, "RealName not present in config file!\n");
      return FALSE;
    }
  balsa_app.real_name = g_strdup (PLGetString (temp_elem));

  /* user name */
  temp_str = PLMakeString ("UserName");
  temp_elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      fprintf (stderr, "UserName not present in config file!\n");
      return FALSE;
    }
  balsa_app.username = g_strdup (PLGetString (temp_elem));

  /* hostname */
  temp_str = PLMakeString ("HostName");
  temp_elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      fprintf (stderr, "HostName not present in config file!\n");
      return FALSE;
    }
  balsa_app.hostname = g_strdup (PLGetString (temp_elem));

  /* directory */
  temp_str = PLMakeString ("LocalMailDir");
  temp_elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      fprintf (stderr, "LocalMailDir not present in config file!\n");
      return FALSE;
    }
  balsa_app.local_mail_directory = g_strdup (PLGetString (temp_elem));

  /* smtp server */
  temp_str = PLMakeString ("SMTPServer");
  temp_elem = PLGetDictionaryEntry (dict, temp_str);
  PLRelease (temp_str);
  if (temp_elem == NULL)
    {
      fprintf (stderr, "SMTPServer not present in config file!\n");
      return FALSE;
    }
  balsa_app.smtp_server = g_strdup (PLGetString (temp_elem));

#if 0
  /* toolbar style */
  balsa_app.toolbar_style = get_int_set_default ("toolbar style", (gint) balsa_app.toolbar_style);

  /* debuging */
  balsa_app.debug = get_int_set_default ("debug", (gint) balsa_app.debug);

  /* mdi style */
  balsa_app.mdi_style = get_int_set_default ("mdi style", (gint) balsa_app.mdi_style);
#endif

  /* save changes */
}


void
save_global_settings (void)
{
#if 0
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
#endif
}
