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

  gnome_config_pop_prefix ();
  gnome_config_sync ();
}
