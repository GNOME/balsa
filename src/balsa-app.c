/* Balsa E-Mail Client
 * Copyright (C) 1997-98 Jay Painter
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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include "balsa-app.h"
#include "mailbox.h"


static void setup_mailboxes ();


void
init_balsa_app (int argc, char *argv[])
{
  struct passwd *pw;

  balsa_app.main_window = create_main_window ();


  /* include linkage for the c-client library */
#include "linkage.c"


  /* set user information */
  pw = getpwuid (getuid ());
  balsa_app.user_name = g_strdup (pw->pw_name);
  
  balsa_app.local_mail_directory = g_malloc (strlen (pw->pw_dir) + 
					     strlen (DEFAULT_MAIL_SUBDIR) + 2);
  sprintf (balsa_app.local_mail_directory, "%s/%s", pw->pw_dir, DEFAULT_MAIL_SUBDIR);


  balsa_app.current_mailbox = NULL;
  balsa_app.mailbox_list = NULL;
  setup_mailboxes ();
}


static void
setup_mailboxes ()
{
  GList *list;
  DIR *dp;
  struct dirent *d;
  struct stat st;
  char filename[PATH_MAX + 1];
  Mailbox *mailbox;


  /* check the MAIL environment variable for a spool directory */
  if (getenv ("MAIL"))
    {
      mailbox = mailbox_new ("INBOX", getenv ("MAIL"));
      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
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
              mailbox = mailbox_new (d->d_name, filename);
              balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mailbox);
            }
        }
    }

}
