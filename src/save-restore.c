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
#include "save-restore.h"
#include "balsa-app.h"
#include "misc.h"


void
add_mailbox_config (gchar * name, gchar * path, gint type)
{
  GString *gstring;
  gchar **mblist;

  GList *list;
  Mailbox *mailbox;

  gint i = 0;

  gstring = g_string_new (NULL);

  mblist = g_new (gchar *, g_list_length (balsa_app.mailbox_list));

  list = g_list_first (balsa_app.mailbox_list);
  for (i = 0; list; list = list->next, i++, mailbox = list->data)
    {
      mblist[i] = g_strdup (mailbox->name);
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
  MailboxMBX *mbx;
  MailboxMTX *mtx;
  MailboxTENEX *tenex;
  MailboxMBox *mbox;
  MailboxMMDF *mmdf;
  MailboxUNIX *unixmb;
  MailboxMH *mh;

  MailboxPOP3 *pop3;
  MailboxIMAP *imap;
  MailboxNNTP *nntp;

  gint i = 0;
  gint type;

  GString *gstring;

  DRIVER *drv = NIL;

  gstring = g_string_new (NULL);

  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s", name);

  if (!gnome_config_has_section (gstring->str))
    return FALSE;
  else
    {
      g_string_truncate (gstring, 0);
      g_string_sprintf (gstring, "/balsa/%s/", name);
      gnome_config_pop_prefix ();
      gnome_config_push_prefix (gstring->str);
      type = gnome_config_get_int ("Type=0");

      switch (type)
	{
	case 0:		/* Local mailbox */

	  drv = NIL;

	  if (drv = mail_valid (NIL, g_strdup (name), "error, cannot load. darn"))
	    {
	      if (balsa_app.debug)
		printf ("%s - %s\n", name, drv->name);
	      if (!strcmp (drv->name, "mbx"))
		{
		  mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
		  mbx->name = g_strdup (name);
		  mbx->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);
		}
	      else if (!strcmp (drv->name, "mtx"))
		{
		  mtx = (MailboxMTX *) mailbox_new (MAILBOX_MTX);
		  mtx->name = g_strdup (name);
		  mtx->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mtx);
		}
	      else if (!strcmp (drv->name, "tenex"))
		{
		  tenex = (MailboxTENEX *) mailbox_new (MAILBOX_TENEX);
		  tenex->name = g_strdup (name);
		  tenex->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, tenex);
		}
	      else if (!strcmp (drv->name, "mbox"))
		{
		  mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
		  mbox->name = g_strdup (name);
		  mbox->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
		}
	      else if (!strcmp (drv->name, "mmdf"))
		{
		  mmdf = (MailboxMMDF *) mailbox_new (MAILBOX_MMDF);
		  mmdf->name = g_strdup (name);
		  mmdf->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mmdf);
		}
	      else if (!strcmp (drv->name, "unix"))
		{
		  unixmb = (MailboxUNIX *) mailbox_new (MAILBOX_UNIX);
		  unixmb->name = g_strdup (name);
		  unixmb->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, unixmb);
		}
	      else if (!strcmp (drv->name, "mh"))
		{
		  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
		  mh->name = g_strdup (name);
		  mh->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);
		}
	    }
	  break;

	case 1:		/*  POP3  */
	  pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
	  pop3->name = g_strdup (name);
	  pop3->user = gnome_config_get_string ("username");
	  pop3->passwd = gnome_config_get_string ("password");
	  pop3->server = gnome_config_get_string ("server");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);
	  break;
	case 2:		/*  IMAP  */
	  imap = (MailboxIMAP *) mailbox_new (MAILBOX_IMAP);
	  imap->name = g_strdup (name);
	  imap->user = gnome_config_get_string ("username");
	  imap->passwd = gnome_config_get_string ("password");
	  imap->server = gnome_config_get_string ("server");
	  imap->path = gnome_config_get_string ("Path");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, imap);
	  break;
	case 3:		/*  NNTP  */
	  nntp = (MailboxNNTP *) mailbox_new (MAILBOX_NNTP);
	  nntp->name = g_strdup (name);
	  nntp->user = gnome_config_get_string ("username");
	  nntp->passwd = gnome_config_get_string ("password");
	  nntp->server = gnome_config_get_string ("server");
	  nntp->newsgroup = gnome_config_get_string ("newsgroup");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, nntp);
          break;

/* old */
	case 7:
	  pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
	  pop3->name = g_strdup (name);
	  pop3->user = gnome_config_get_string ("username");
	  pop3->passwd = gnome_config_get_string ("password");
	  pop3->server = gnome_config_get_string ("server");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);
	  gnome_config_set_int ("Type", 1);
	  break;
	case 8:
	  imap = (MailboxIMAP *) mailbox_new (MAILBOX_IMAP);
	  imap->name = g_strdup (name);
	  imap->user = gnome_config_get_string ("username");
	  imap->passwd = gnome_config_get_string ("password");
	  imap->server = gnome_config_get_string ("server");
	  imap->path = gnome_config_get_string ("Path");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, imap);
	  gnome_config_set_int ("Type", 2);
	  break;
	case 9:
	  nntp = (MailboxNNTP *) mailbox_new (MAILBOX_NNTP);
	  nntp->name = g_strdup (name);
	  nntp->user = gnome_config_get_string ("username");
	  nntp->passwd = gnome_config_get_string ("password");
	  nntp->server = gnome_config_get_string ("server");
	  nntp->newsgroup = gnome_config_get_string ("newsgroup");
	  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, nntp);
	  gnome_config_set_int ("Type", 3);
	  break;


	default:
	  drv = NIL;

	  if (drv = mail_valid (NIL, g_strdup (name), "error, cannot load. darn"))
	    {
	      if (balsa_app.debug)
		printf ("%s - %s\n", name, drv->name);
	      if (!strcmp (drv->name, "mbx"))
		{
		  mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
		  mbx->name = g_strdup (name);
		  mbx->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);
		}
	      else if (!strcmp (drv->name, "mtx"))
		{
		  mtx = (MailboxMTX *) mailbox_new (MAILBOX_MTX);
		  mtx->name = g_strdup (name);
		  mtx->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mtx);
		}
	      else if (!strcmp (drv->name, "tenex"))
		{
		  tenex = (MailboxTENEX *) mailbox_new (MAILBOX_TENEX);
		  tenex->name = g_strdup (name);
		  tenex->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, tenex);
		}
	      else if (!strcmp (drv->name, "mbox"))
		{
		  mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
		  mbox->name = g_strdup (name);
		  mbox->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
		}
	      else if (!strcmp (drv->name, "mmdf"))
		{
		  mmdf = (MailboxMMDF *) mailbox_new (MAILBOX_MMDF);
		  mmdf->name = g_strdup (name);
		  mmdf->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mmdf);
		}
	      else if (!strcmp (drv->name, "unix"))
		{
		  unixmb = (MailboxUNIX *) mailbox_new (MAILBOX_UNIX);
		  unixmb->name = g_strdup (name);
		  unixmb->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, unixmb);
		}
	      else if (!strcmp (drv->name, "mh"))
		{
		  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
		  mh->name = g_strdup (name);
		  mh->path = gnome_config_get_string ("Path");
		  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);
		}
	    }
	  gnome_config_set_int ("Type", 0);
	  break;

	}
      gnome_config_pop_prefix ();
    }
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
  gnome_config_set_int ("toolbar style", (gint) balsa_app.toolbar_style);
  gnome_config_set_int ("debug", (gint) balsa_app.debug);

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
