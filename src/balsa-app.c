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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include "balsa-app.h"
#include "mailbox.h"


static void load_global_settings ();
static int options_init ();
static void setup_local_mailboxes ();
static void my_special_mailbox ();
void mailbox_add_gnome_config (gint, gchar *, gchar *, gint);

void
init_balsa_app (int argc, char *argv[])
{
  /* include linkage for the c-client library */
#include "linkage.c"

  /* initalize application structure before ALL ELSE */
  balsa_app.user_name = NULL;
  balsa_app.email = NULL;
  balsa_app.organization = NULL;
  balsa_app.local_mail_directory = NULL;
  balsa_app.smtp_server = NULL;
  balsa_app.auth_mailbox = NULL;
  balsa_app.current_mailbox = NULL;
  balsa_app.mailbox_list = NULL;
  balsa_app.main_window = NULL;
  balsa_app.addressbook_list = NULL;

  load_global_settings ();
  options_init ();
  setup_local_mailboxes ();
  my_special_mailbox ();

  /* create main window */
  balsa_app.main_window = create_main_window ();
}

static gint
options_init (void)
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
  gchar *name;

  GString *gstring, *buffer;

  gstring = g_string_new (NULL);
  buffer = g_string_new (NULL);

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

	  name = gnome_config_get_string ("Name");
	  type = gnome_config_get_int ("Type=0");

	  switch (type)
	    {
	    case 0:		/*  MBX  */
	      mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
	      mbx->name = gnome_config_get_string ("Name");
	      mbx->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);
	      break;
	    case 1:		/*  MTX  */
	      mtx = (MailboxMTX *) mailbox_new (MAILBOX_MTX);
	      mtx->name = gnome_config_get_string ("Name");
	      mtx->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mtx);
	      break;
	    case 2:		/*  TENEX  */
	      tenex = (MailboxTENEX *) mailbox_new (MAILBOX_TENEX);
	      tenex->name = gnome_config_get_string ("Name");
	      tenex->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, tenex);
	      break;
	    case 3:		/*  MBox  */
	      mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
	      mbox->name = gnome_config_get_string ("Name");
	      mbox->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
	      break;
	    case 4:		/*  MMDF  */
	      mmdf = (MailboxMMDF *) mailbox_new (MAILBOX_MMDF);
	      mmdf->name = gnome_config_get_string ("Name");
	      mmdf->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mmdf);
	      break;
	    case 5:		/*  UNIX  */
	      unixmb = (MailboxUNIX *) mailbox_new (MAILBOX_UNIX);
	      unixmb->name = gnome_config_get_string ("Name");
	      unixmb->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, unixmb);
	      break;
	    case 6:		/*  MH  */
	      mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
	      mh->name = gnome_config_get_string ("Name");
	      mh->path = gnome_config_get_string ("Path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);
	      break;
	    case 7:		/*  POP3  */
	      pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
	      pop3->name = gnome_config_get_string ("Name");
	      pop3->user = gnome_config_get_string ("username");
	      pop3->passwd = gnome_config_get_string ("password");
	      pop3->server = gnome_config_get_string ("server");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);
	      break;
	    case 8:		/*  IMAP  */
	      imap = (MailboxIMAP *) mailbox_new (MAILBOX_IMAP);
	      imap->name = gnome_config_get_string ("Name");
	      imap->user = gnome_config_get_string ("username");
	      imap->passwd = gnome_config_get_string ("password");
	      imap->server = gnome_config_get_string ("server");
	      imap->path = gnome_config_get_string ("path");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, imap);
	      break;
	    case 9:		/*  NNTP  */
	      nntp = (MailboxNNTP *) mailbox_new (MAILBOX_NNTP);
	      nntp->name = gnome_config_get_string ("Name");
	      nntp->user = gnome_config_get_string ("username");
	      nntp->passwd = gnome_config_get_string ("password");
	      nntp->server = gnome_config_get_string ("server");
	      nntp->newsgroup = gnome_config_get_string ("newsgroup");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, nntp);
	      break;
	    }
	  gnome_config_pop_prefix ();
	}
      else
	{
	  if (i == 0)
	    {
	      mailbox_add_gnome_config (0, "Default", getenv ("MAIL"), 0);
	      mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
	      mbx->name = "Default";
	      mbx->path = getenv ("MAIL");
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);
	      g_string_free (gstring, 1);
	      g_string_free (buffer, 1);
	      return -1;
	    }
	  break;
	}
    }
  gnome_config_pop_prefix ();
  gnome_config_sync ();
  g_string_free (gstring, 1);
  g_string_free (buffer, 1);
  return i;
}


void
mailbox_add_gnome_config (gint num, gchar * name, gchar * path, gint type)
{
  GString *gstring;

  gstring = g_string_new (NULL);

  gnome_config_push_prefix ("/balsa/Accounts/");
  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "%i", num);
  gnome_config_set_string (gstring->str, name);
  gnome_config_pop_prefix ();

  g_string_truncate (gstring, 0);
  g_string_sprintf (gstring, "/balsa/%s/", name);
  gnome_config_push_prefix (gstring->str);
  gnome_config_set_string ("Name", name);
  gnome_config_set_string ("Path", path);
  gnome_config_set_int ("Type", type);
  gnome_config_pop_prefix ();

  gnome_config_sync ();
  g_string_free (gstring, 1);
}

static void
setup_local_mailboxes ()
{
  GList *list;
  DIR *dp;
  struct dirent *d;
  struct stat st;
  char filename[PATH_MAX + 1];
  DRIVER *drv = NIL;
  MailboxMBox *mbox;
  MailboxUNIX *unixmb;
  gint i = 0;

  /* check the MAIL environment variable for a spool directory */
/* We'll make this the default if no other mailboxes are loaded... */
/*
   if (getenv ("MAIL"))
   {
   mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
   mbox->name = g_strdup ("INBOX");
   mbox->path = g_strdup (getenv ("MAIL"));

   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
   }
 */
  if (dp = opendir (balsa_app.local_mail_directory))
    {
      while ((d = readdir (dp)) != NULL)
	{
	  sprintf (filename, "%s/%s", balsa_app.local_mail_directory, d->d_name);
	  drv = NIL;

	  if (lstat (filename, &st) < 0)
	    continue;

	  if (S_ISREG (st.st_mode))
	    {
	      if (drv = mail_valid (NIL, g_strdup (filename), "error, cannot load. darn"))
		{

		  if (!strcmp (drv->name, "mbox"))
		    {
		      mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
		      mbox->name = g_strdup (d->d_name);
		      mbox->path = g_strdup (filename);

		      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
/*
 *		      mailbox_add_gnome_config (i, mbox->name, mbox->path, 3);
 */
		    }
		  if (!strcmp (drv->name, "unix"))
		    {
		      unixmb = (MailboxUNIX *) mailbox_new (MAILBOX_UNIX);
		      unixmb->name = g_strdup (d->d_name);
		      unixmb->path = g_strdup (filename);

		      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, unixmb);
/*
 * 		      mailbox_add_gnome_config (i, unixmb->name, unixmb->path, 5);
 */
		    }
		  i++;
		}
	    }
	}
    }
}


/* This is where you can hard-code your own
 *  mailbox for testing!
 */
static void
my_special_mailbox ()
{
/*
   MailboxPOP3 *pop3;
   MailboxMBX *mbx;
   MailboxMH *mh;

   pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
   pop3->name = g_strdup ("MyPOP Box");
   pop3->user = g_strdup ("pavlov");
   pop3->passwd = g_strdup ("");
   pop3->server = g_strdup ("venus");

   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);

   mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
   mh->name = g_strdup ("gnome-list");
   mh->path = g_strdup ("gnome");
   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);

   mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
   mbx->name = g_strdup ("gnome-list-mbx");
   mbx->path = g_strdup ("/home/pavlov/gnomecvs.mbx");
   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);

   mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
   mh->name = g_strdup ("Gnome CVS");
   mh->path = g_strdup ("gnomecvs");
   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);

   mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
   mh->name = g_strdup ("gtk-list");
   mh->path = g_strdup ("gtk+");
   balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mh);
 */
}

static gchar *
get_string_set_default (const char *path,
			const char *value)
{
  GString *buffer;
  gboolean unset;
  gchar *result;

  result = NULL;
  buffer = g_string_new (NULL);

  g_string_sprintf (buffer, "%s=%s", path, value);
  result = gnome_config_get_string_with_default (buffer->str, &unset);
  if (unset)
    gnome_config_set_string (path, value);

  g_string_free (buffer, 1);
  return result;
}


static void
load_global_settings ()
{
  GString *path;
  struct passwd *pw;

  pw = getpwuid (getuid ());

  /* set to Global configure section */
  gnome_config_push_prefix ("/balsa/Global/");

  /* user's text name */
  balsa_app.user_name = get_string_set_default ("user name", pw->pw_gecos);

  /* email */
  balsa_app.email = get_string_set_default ("email", pw->pw_name);

  /* organization */
  balsa_app.organization = get_string_set_default ("organization", "None");

  /* directory */
  path = g_string_new (NULL);
  g_string_sprintf (path, "%s/Mail", pw->pw_dir);
  balsa_app.local_mail_directory = get_string_set_default ("local mail directory", path->str);
  g_string_free (path, 1);

  /* smtp server */
  balsa_app.smtp_server = get_string_set_default ("smtp server", "localhost");

  /* save changes */
  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
