/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2004 Stuart Parmenter and others,
 *                         See the file AUTHORS for a list.
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

#include <gtk/gtk.h>

#include "option_menu.h"

#ifdef HAVE_GTK24
GObject*
libbalsa_option_menu_new(void)
{
  GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
  return G_OBJECT(store);
}
void
libbalsa_option_menu_append(GObject *w, const char *name, void *data)
{
  GtkTreeIter iter;
  GtkListStore *store = GTK_LIST_STORE(w);
  gtk_list_store_append(store, &iter);
  gtk_list_store_set (store, &iter, 0, name, 1, data, -1);
}

GtkWidget*
libbalsa_option_menu_get_widget(GObject *w, GCallback cb, void *data)
{
  GtkWidget *combo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(w));
  GtkCellRenderer *renderer;
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(combo), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT(combo), renderer,
                                  "text", 0, NULL);
  g_signal_connect(G_OBJECT(combo), "changed", cb, data);
  gtk_widget_show(combo);
  return combo;
}

void
libbalsa_option_menu_set_active(GtkWidget *w, int idx)
{
  gtk_combo_box_set_active(GTK_COMBO_BOX(w), idx);
}

void*
libbalsa_option_menu_get_active_data(GtkWidget *combo_box)
{
  GtkTreeIter iter;
  if(gtk_combo_box_get_active_iter(GTK_COMBO_BOX(combo_box), &iter)) {
      void *res;
      GValue value = {0};
      GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(combo_box));
      gtk_tree_model_get_value(model, &iter, 1, &value);
      res = g_value_get_pointer(&value);
      g_value_unset(&value);
      return res;
  }
  return NULL;
}
#else /* HAVE_GTK24 */
/* =================================================================== */
GObject *
libbalsa_option_menu_new(void)
{
  return G_OBJECT(gtk_option_menu_new());
}

void
libbalsa_option_menu_append(GObject *menu, const char *name, void *data)
{
  GtkWidget *menu_item = gtk_menu_item_new_with_label(name);
  gtk_widget_show(menu_item);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
}

GtkWidget*
libbalsa_option_menu_get_widget(GObject *menu, GCallback cb, void *data)
{
  GtkWidget *option;
  gtk_widget_show(GTK_WIDGET(menu));
  
  option = gtk_option_menu_new();
  gtk_option_menu_set_menu(GTK_OPTION_MENU(option), GTK_WIDGET(menu));
  gtk_widget_show(option);
  g_signal_connect(G_OBJECT(menu), "changed", cb, data);
  return option;
}

void
libbalsa_option_menu_set_active(GtkWidget *menu, int idx)
{
  gtk_menu_set_active(GTK_MENU(menu), idx);
}

void*
libbalsa_option_menu_get_active_data(GtkWidget *widget)
{
  GtkWidget *am = gtk_menu_get_active(GTK_MENU(widget));
  return g_object_get_data (G_OBJECT(am), "address-book");
}

#endif /* HAVE_GTK24 */
