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
#include "config.h"

#include <gnome.h>

#include "balsa-app.h"
#include "main-window.h"
#include "mailbox.h"
#include "save-restore.h"



int
main (int argc, char *argv[])
{
  gnome_init ("balsa", NULL, argc, argv, 0, NULL);
  init_balsa_app (argc, argv);

  open_main_window ();

  gtk_main ();
  return 0;
}


void
balsa_exit ()
{
  GList *list;
  Mailbox *mailbox;

  list = balsa_app.mailbox_list;
  while (list)
    {
      mailbox = list->data;
      list = list->next;
      if (balsa_app.debug)
	g_print ("Mailbox: %s Ref: %d\n", mailbox->name, mailbox->open_ref);

      while (mailbox->open_ref > 0)
	mailbox_open_unref (mailbox);
    }

#if 0
  gtk_timeout_remove (balsa_app.check_mail_timer);
  gtk_timeout_remove (balsa_app.new_messages_timer);
#endif

  save_global_settings ();
  gtk_exit (0);
}
