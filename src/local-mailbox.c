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
  MailboxType mailbox_type;
  MailboxLocal *local;
  gint i = 0;


  dp = opendir (balsa_app.local_mail_directory);
  if (!dp)
    return;


  while ((d = readdir (dp)) != NULL)
    {
      sprintf (filename, "%s/%s", balsa_app.local_mail_directory, d->d_name);
      drv = NIL;
      
      if (lstat (filename, &st) < 0)
	continue;
 
      if (!S_ISREG (st.st_mode))
	continue;
          
      if (drv = mail_valid (NIL, g_strdup (filename), "error, cannot load. darn"))
	{
	  if (balsa_app.debug)
	    g_print ("%s - %s\n", d->d_name, drv->name);
	  
	  /*
	   * create and add the mailbox to the mailbox list
	   * XXX: does this need to do more cheking???
	   */
	  mailbox_type = mailbox_type_from_description (drv->name);
	  
	  if (mailbox_type != MAILBOX_UNKNOWN)
	    {
	      local = (MailboxLocal *) mailbox_new (mailbox_type);
	      local->name = g_strdup (d->d_name);
	      local->path = g_strdup (filename);
	      balsa_app.mailbox_list = g_list_append (balsa_app.mailbox_list, local);

	      if (balsa_app.debug)
		g_print ("Local Mailbox Loaded as: %s\n", mailbox_type_description (local->type));
	    }
	}
    }
}
