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
static void setup_local_mailboxes ();
static void my_special_mailbox ();


void
init_balsa_app (int argc, char *argv[])
{
  /* include linkage for the c-client library */
  #include "linkage.c"

  /* initalize application structure before ALL ELSE */
  balsa_app.user = NULL;
  balsa_app.user_name = NULL;
  balsa_app.local_mail_directory = NULL;
  balsa_app.smtp_server = NULL;
  balsa_app.auth_mailbox = NULL;
  balsa_app.current_mailbox = NULL;
  balsa_app.mailbox_list = NULL;
  balsa_app.main_window = NULL;
  balsa_app.addressbook_list = NULL;

  load_global_settings ();
  setup_local_mailboxes ();
  my_special_mailbox ();

  /* create main window */
  balsa_app.main_window = create_main_window ();
}


static void
setup_local_mailboxes ()
{
  GList *list;
  DIR *dp;
  struct dirent *d;
  struct stat st;
  char filename[PATH_MAX + 1];
  MailboxMBox *mbox;


  /* check the MAIL environment variable for a spool directory */
  if (getenv ("MAIL"))
    {
      mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
      mbox->name = g_strdup ("INBOX");
      mbox->path = g_strdup (getenv ("MAIL"));

      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
    }

  if (dp = opendir (balsa_app.local_mail_directory))
    {
      while ((d = readdir (dp)) != NULL)
        {
          sprintf (filename, "%s/%s", balsa_app.local_mail_directory, d->d_name);

          if (lstat (filename, &st) < 0)
            continue;

          if (S_ISREG (st.st_mode))
            {
              mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
	      mbox->name = g_strdup (d->d_name);
	      mbox->path = g_strdup (filename);

              balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
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
  MailboxPOP3 *pop3;
  MailboxMH *mh;
/*
  pop3 = (MailboxPOP3 *) mailbox_new (MAILBOX_POP3);
  pop3->name = g_strdup ("MyPOP Box");
  pop3->user = g_strdup ("pavlov");
  pop3->passwd = g_strdup ("");
  pop3->server = g_strdup ("venus");

  balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, pop3);
*/
/*
  mh = (MailboxMH *) mailbox_new (MAILBOX_MH);
  mh->name = g_strdup ("GNOME CVS");
  mh->path = g_strdup ("gnomecvs");

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
  
  /* user id */
  balsa_app.user = get_string_set_default ("user", pw->pw_name);

  /* user's text name */
  balsa_app.user_name = get_string_set_default ("user name", pw->pw_gecos);

  /* directory */
  path = g_string_new (NULL);
  g_string_sprintf (path, "%s/mail", pw->pw_dir);
  balsa_app.local_mail_directory= get_string_set_default ("local mail directory", path->str);
  g_string_free (path, 1);

  /* smtp server */
  balsa_app.smtp_server = get_string_set_default ("smtp server", "localhost");

  /* save changes */
  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
