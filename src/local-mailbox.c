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
#include "c-client.h"
#include "local-mailbox.h"
#include "mailbox.h"


void
load_local_mailboxes ()
{
  GList *list;
  DIR *dp;
  struct dirent *d;
  struct stat st;
  char filename[PATH_MAX + 1];
  DRIVER *drv = NIL;
  MailboxMBX *mbx;
  MailboxMBox *mbox;
  MailboxUNIX *unixmb;
  gint i = 0;


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
		  if (balsa_app.debug)
		    printf ("%s - %s\n", d->d_name, drv->name);

		  if (!strcmp (drv->name, "mbx"))
		    {
		      mbx = (MailboxMBX *) mailbox_new (MAILBOX_MBX);
		      mbx->name = g_strdup (d->d_name);
		      mbx->path = g_strdup (filename);

		      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbx);
		    }

		  else if (!strcmp (drv->name, "mbox"))
		    {
		      mbox = (MailboxMBox *) mailbox_new (MAILBOX_MBOX);
		      mbox->name = g_strdup (d->d_name);
		      mbox->path = g_strdup (filename);

		      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, mbox);
		    }

		  else if (!strcmp (drv->name, "unix"))
		    {
		      unixmb = (MailboxUNIX *) mailbox_new (MAILBOX_UNIX);
		      unixmb->name = g_strdup (d->d_name);
		      unixmb->path = g_strdup (filename);

		      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, unixmb);
		    }
		  i++;
		}
	    }
	}
    }
}
