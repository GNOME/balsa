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

#ifndef __MISC_H__
#define __MISC_H__


GtkWidget * append_menuitem_connect (GtkMenu * menu,
				     gchar * text,
				     GtkSignalFunc func,
				     gpointer data,
				     gpointer user_data);

gchar * get_string_set_default (const char * path,
				const char * value);

gint get_int_set_default (const char *path,
			  const gint value);

gint g_list_index (GList * list, gpointer data);


gchar *make_string_from_list (GList *);
GList *make_list_from_string (gchar *);

int readfile(char *name,char **buf);

#endif /* __MISC_H__ */
