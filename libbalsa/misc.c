/* Balsa E-Mail Library
 * Copyright (C) 1998 Stuart Parmenter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "config.h"

#include <gnome.h>
#include "mailbox.h"
#include "misc.h"

/* creates one label (text) menuitem, connects the callback,
 * and adds it to the menu */
GtkWidget *
append_menuitem_connect (GtkMenu * menu,
			 gchar * text,
			 GtkSignalFunc func,
			 gpointer data,
			 gpointer user_data)
{
  GtkWidget *menuitem;


  menuitem = gtk_menu_item_new_with_label (text);
  gtk_menu_append (menu, menuitem);
  gtk_widget_show (menuitem);

  gtk_signal_connect (GTK_OBJECT (menuitem),
		      "activate",
		      (GtkSignalFunc) func,
		      data);

  if (user_data)
    gtk_object_set_user_data (GTK_OBJECT (menuitem), user_data);

  return menuitem;
}


gchar *
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


gint
get_int_set_default (const char *path,
		     const gint value)
{
  GString *buffer;
  gboolean unset;
  gint result;

  result = 0;
  buffer = g_string_new (NULL);

  g_string_sprintf (buffer, "%s=%d", path, value);
  result = gnome_config_get_int_with_default (buffer->str, &unset);
  if (unset)
    gnome_config_set_int (path, value);

  g_string_free (buffer, 1);
  return result;
}

gchar *
make_string_from_list (GList * the_list)
{
  gchar *retc;
  GList *list;
  GString *gs = g_string_new (NULL);
  Address *addy;

  list = g_list_first (the_list);

  while (list)
    {
      addy = list->data;
      if (addy->personal)
	{
	  gs = g_string_append (gs, addy->personal);
	  gs = g_string_append_c (gs, ' ');
	}
      if (addy->mailbox)
	{
	  gs = g_string_append_c (gs, '<');
	  gs = g_string_append (gs, addy->mailbox);
	  gs = g_string_append_c (gs, '>');
	}

      if (list->next)
	gs = g_string_append (gs, ", ");

      list = list->next;
    }
  retc = g_strdup (gs->str);
  g_string_free (gs, 1);
  return retc;
}

GList *
make_list_from_string (gchar * the_str)
{
  return NULL;
}

int
readfile (char *filename, char **buf)
{
  size_t size;
  off_t offset;
  int r;
  int fd = open (filename, O_RDONLY);
  struct stat statbuf;

  if (fstat (fd, &statbuf) == -1)
    return -1;

  size = statbuf.st_size;

  if (!size)
    {
      *buf = NULL;
      return size;
    }

  *buf = (char *) malloc (size);
  if ((int) *buf == -1)
    {
      return -1;
    }

  offset = 0;
  while (offset < size)
    {
      r = read (fd, *buf + offset, size - offset);
      if (r > 0)
	{
	  offset += r;
	}
      else if (!r)
	{
	  return offset;
	}
      else if ((errno != EAGAIN) && (errno != EINTR))
	{
	  return -1;
	}
    }

  return size;
}
