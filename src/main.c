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
#include <gnome.h>

#include "balsa-app.h"
#include "index.h"
#include "mailbox.h"

int
main (int argc, char *argv[])
{
  gnome_init ("balsa", &argc, &argv);

  init_balsa_app (argc, argv);
  options_init ();
  mailbox_menu_update ();

  /* give things a kick start here */
  if (balsa_app.mailbox_list)
    {
      Mailbox *mailbox;

      mailbox = (Mailbox *) balsa_app.mailbox_list->data;
      mailbox_open (mailbox);
    }

  gtk_main ();
  return 0;
}

void
balsa_exit ()
{
  gnome_config_sync ();
  gtk_exit (0);
}
