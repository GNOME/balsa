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



GtkWidget *
new_icon (gchar ** xpm, GtkWidget * window)
{
  GdkPixmap *pixmap;
  GtkWidget *pixmapwid;
  GdkBitmap *mask;

  pixmap = gdk_pixmap_create_from_xpm_d (window->window, &mask, 0, xpm);

  pixmapwid = gtk_pixmap_new (pixmap, mask);
  return pixmapwid;
}



gint
g_list_index (GList * list, gpointer data)
{
  gint index;

  if (list)
    {
      index = 0;

      while (list)
	{
	  if (list->data == data)
	    return index;

	  list = list->next;
	  index++;
	}
    }

  return -1;
}

gchar *
make_string_from_list (GList * the_list)
{
  GList *list = NULL;
  GString *gs;
  gchar *str;

  gs = g_string_new (NULL);

  list = g_list_first (the_list);

  while (list)
    {
      gs = g_string_append (gs, list->data);
      list = list->next;
      if (list)
	gs = g_string_append (gs, ",");
    }
  str = g_strdup (gs->str);
  g_string_free (gs, 1);
  return str;
}

GList *
make_list_from_string (gchar * the_str)
{
  GList *list = NULL;
  gchar *str;
  char *token;

  if (!the_str)
    return NULL;
  if (strlen (the_str) < 3)
    return NULL;

  str = g_strdup (the_str);
  token = strtok (str, ",");
  while (token)
    {
      list = g_list_append (list, token);
      g_print ("\"%s\"\n", token);
      token = strtok (NULL, ",");
    }
  g_free (str);
  return list;
}
