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

gchar *
make_string_from_list (GList * the_list)
{
  GList *list = NULL;
  GString *gs;
  gchar *str;
  Address *addr;

  gs = g_string_new (NULL);

  list = g_list_first (the_list);

  while (list)
    {
      addr = list->data;
      if (addr->personal)
      {
         gs = g_string_append (gs, addr->personal);
         gs = g_string_append (gs, " <");
         gs = g_string_append (gs, addr->user);
         gs = g_string_append_c (gs, '@');
         gs = g_string_append (gs, addr->host);
         gs = g_string_append (gs, "> ");
      }
      else
      {
         gs = g_string_append (gs, addr->user);
         gs = g_string_append_c (gs, '@');
         gs = g_string_append (gs, addr->host);
      }
      list = list->next;
      if (list)
	gs = g_string_append_c (gs, ',');
    }
  str = g_strdup (gs->str);
  g_string_free (gs, 1);
  return str;
}

GList *
make_list_from_string (gchar * the_str)
{
  GList *list = NULL;
  gchar *buff;
  gchar *personal;
  gchar *user;
  gchar *host;
  gint len;
  gint i, y;
  gint gt, lt;
  Address *addr;

  if (!the_str)
    return NULL;

  len = strlen (the_str);

  buff = g_new (gchar, len + 1);
  user = g_new (gchar, 255 + 1);
  buff = g_new (gchar, 255 + 1);

  if (len < 3)
    return NULL;

  addr = address_new();
  for (i = y = 0; i < len; i++)
    {
      switch (the_str[i])
	{
	case ',':
	  buff[y] = '\0';
	  buff[0] = '\0';
	  list = g_list_append (list, addr);
	  addr = address_new();
	  y = 0;
	  break;
	case '<':
	  gt = 1;
	  lt = 0;
	  user[gt-1] = '\0';
	  break;
	case '>':
	  gt = 0;
	  lt = 0;
	  addr->host=g_strdup(host);
	  break;
        case '@':
	  gt = 0;
	  lt = 1;
	  host[lt-1] = '\0';
	  addr->user=g_strdup(user);
	  break;
	default:
	  if (gt > 0)
	  {
            user[gt-1] = the_str[i];
	    gt++;
	  }
	  if (lt > 0)
	  {
            host[lt-1] = the_str[i];
	    lt++;
	  }
	  buff[y] = the_str[i];
	  y++;
	  break;
	}
    }
  buff[y] = '\0';
  list = g_list_append (list, g_strdup(buff));

  g_free (buff);
  return list;
}
