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

#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <gnome.h>
#include "balsa-app.h"
#include "addrbook-manager.h"

gint delete_event (GtkWidget *, gpointer);

static GtkWidget *menu_items[9];
extern void close_window (GtkWidget *, gpointer);

static update_addyb_window (GtkWidget *, gpointer);

static GtkWidget *addyb_list;
static GtkWidget *email_list;
static GtkWidget *commentstext;

typedef struct _addyb_item addyb_item;
struct _addyb_item
  {
    gchar *name;
    GList *email;
    gchar *comments;
  };

static addyb_item *
addyb_item_new (gchar * name, gchar * comments)
{
  addyb_item *new_item = g_malloc (sizeof (addyb_item));
  gchar *list_item[1];
  gint row;
  new_item->name = list_item[0] = name;
  new_item->comments = comments;
  new_item->email = NULL;

  row = gtk_clist_append (GTK_CLIST (addyb_list), list_item);
  gtk_clist_set_row_data (GTK_CLIST (addyb_list), row, new_item);
/* should we free the pointer? */
  gtk_signal_connect (GTK_OBJECT (addyb_list),
		      "select_row",
		      GTK_SIGNAL_FUNC (update_addyb_window),
		      NULL);
  balsa_app.addressbook_list = g_list_append (balsa_app.addressbook_list, new_item);

  return (addyb_item *) gtk_clist_get_row_data (GTK_CLIST (addyb_list), row);
}

static void
addyb_email_item_new (addyb_item * ai, gchar * addy)
{
  gchar *list_item[1];
  list_item[0] = addy;
  gtk_clist_set_row_data (GTK_CLIST (email_list),
		       gtk_clist_append (GTK_CLIST (email_list), list_item),
			  ai);
  ai->email = g_list_append (ai->email, addy);
}

static void
addyb_email_item_delete (addyb_item * ai, gchar * addy)
{
  gtk_clist_remove (GTK_CLIST (email_list), gtk_clist_find_row_from_data (GTK_CLIST (email_list), (gpointer) addy));
  ai->email = g_list_remove (ai->email, (gpointer) addy);
}

static void
addyb_compose_update (addyb_item * ai)
{
  gtk_text_freeze (GTK_TEXT (commentstext));
  gtk_editable_delete_text (GTK_EDITABLE (commentstext), 0, gtk_text_get_length (GTK_TEXT (commentstext)));

  gtk_text_insert (GTK_TEXT (commentstext), NULL, NULL, NULL, ai->comments, strlen (ai->comments));
  gtk_text_thaw (GTK_TEXT (commentstext));
}

static void
addyb_emaillist_update (addyb_item * ai)
{
  GList *glist;
  gchar *list_item[1];

  gtk_clist_freeze (GTK_CLIST (email_list));
  gtk_clist_clear (GTK_CLIST (email_list));

  for (glist = g_list_first (ai->email); glist; glist = glist->next)
    {
      list_item[0] = (gchar *) glist->data;
      gtk_clist_append (GTK_CLIST (email_list), list_item);
    }

  gtk_clist_thaw (GTK_CLIST (email_list));
}

static
update_addyb_window (GtkWidget * widget, gpointer data)
{
  addyb_item *ai = (addyb_item *) gtk_clist_get_row_data (GTK_CLIST (widget), ((gint) GTK_CLIST (widget)->selection->data));

  addyb_emaillist_update (ai);
  addyb_compose_update (ai);
}

static GtkWidget *
create_menu (GtkWidget * window)
{
  GtkWidget *menubar, *w, *menu;
  GtkAcceleratorTable *accel;
  int i = 0;

  accel = gtk_accelerator_table_new ();
  menubar = gtk_menu_bar_new ();
  gtk_widget_show (menubar);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_BLANK, _ ("Close"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  gtk_signal_connect_object (GTK_OBJECT (w), "activate",
			     GTK_SIGNAL_FUNC (close_window),
			     GTK_OBJECT (window));
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("File"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu = gtk_menu_new ();

  w = gnome_stock_menu_item (GNOME_STOCK_MENU_ABOUT, _ ("Contents"));
  gtk_widget_show (w);
  gtk_menu_append (GTK_MENU (menu), w);
  menu_items[i++] = w;

  w = gtk_menu_item_new_with_label (_ ("Help"));
  gtk_widget_show (w);
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (w), menu);
  gtk_menu_item_right_justify (GTK_MENU_ITEM (w));
  gtk_menu_bar_append (GTK_MENU_BAR (menubar), w);

  menu_items[i] = NULL;

/*  g_print("%d menu items\n", i); */

  gtk_window_add_accelerator_table (GTK_WINDOW (window), accel);
  return menubar;
}


void
addressbook_window_new (GtkWidget * widget, gpointer data)
{
  addyb_item *addybitem;
  GtkWidget *window;
  GtkWidget *hbox;
  GtkWidget *vbox1;
  GtkWidget *hbox1;
  GtkWidget *label;
  GtkWidget *button;
  GtkWidget *vpane;
  GtkWidget *hpane;
  static char *titles[1];

  window = gnome_app_new ("balsa_addressbook_window", "Address book");
  gtk_widget_set_usize (window, 680, 435);
  gtk_window_set_wmclass (GTK_WINDOW (window), "balsa_app",
			  "Balsa");

  gtk_signal_connect (GTK_OBJECT (window), "destroy",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  gtk_signal_connect (GTK_OBJECT (window), "delete_event",
		      GTK_SIGNAL_FUNC (delete_event), NULL);

  hpane = gtk_hpaned_new ();
  gtk_widget_show (hpane);

/* address book list (show nicknames in this list) */
  titles[0] = "Names";

  vbox1 = gtk_vbox_new (FALSE, 10);
  gtk_widget_show (vbox1);

  addyb_list = gtk_clist_new_with_titles (1, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (addyb_list));
  gtk_clist_set_selection_mode (GTK_CLIST (addyb_list), GTK_SELECTION_BROWSE);

  gtk_clist_set_policy (GTK_CLIST (addyb_list),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (vbox1), addyb_list, TRUE, TRUE, 1);
  gtk_widget_show (addyb_list);

  hbox1 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 3);
  gtk_widget_show (hbox1);

  button = gtk_button_new_with_label ("Add");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Modify");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Remove");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  gtk_paned_add1 (GTK_PANED (hpane), vbox1);

  vpane = gtk_vpaned_new ();
  gtk_widget_show (vpane);

/* email address list for the currently selected nick in the list above */
  vbox1 = gtk_vbox_new (FALSE, 3);
  gtk_widget_show (vbox1);

  titles[0] = "Email address";

  email_list = gtk_clist_new_with_titles (1, titles);
  gtk_clist_column_titles_passive (GTK_CLIST (email_list));
  gtk_clist_set_selection_mode (GTK_CLIST (email_list), GTK_SELECTION_BROWSE);

  gtk_clist_set_policy (GTK_CLIST (email_list),
			GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_box_pack_start (GTK_BOX (vbox1), email_list, TRUE, TRUE, 1);
  gtk_widget_show (email_list);

  hbox1 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, FALSE, 3);
  gtk_widget_show (hbox1);

  button = gtk_button_new_with_label ("Add");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Modify");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  button = gtk_button_new_with_label ("Remove");
  gtk_widget_set_usize (button, 60, 25);
  gtk_box_pack_start (GTK_BOX (hbox1), button, TRUE, FALSE, 0);
  gtk_widget_show (button);

  gtk_paned_add1 (GTK_PANED (vpane), vbox1);


/* comments box */
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);

  label = gtk_label_new ("Comments:");
  gtk_box_pack_start (GTK_BOX (vbox1), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  commentstext = gtk_text_new (NULL, NULL);
  gtk_box_pack_start (GTK_BOX (vbox1), commentstext, TRUE, TRUE, 3);
  gtk_widget_show (commentstext);
  gtk_text_set_editable (GTK_TEXT (commentstext), 1);

  gtk_paned_add2 (GTK_PANED (vpane), vbox1);

  gtk_paned_add2 (GTK_PANED (hpane), vpane);


/* other stuff... */

  addybitem = addyb_item_new ("Pavlov", "tests are fun");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  addyb_email_item_delete (addybitem, "pavlov@alldolls.net");

  addybitem = addyb_item_new ("test1", "tests suck!");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  addybitem = addyb_item_new ("test2", "i like tests damnit!");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  addybitem = addyb_item_new ("test3", "weeee");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  addybitem = addyb_item_new ("test4", "i must be bored");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  addybitem = addyb_item_new ("test5", "jay painter knows what he is doing much more than pav does...");
  addyb_email_item_new (addybitem, "pavlov@innerx.net");
  addyb_email_item_new (addybitem, "pavlov@pavlov.net");
  addyb_email_item_new (addybitem, "pavlov@alldolls.net");

  gnome_app_set_contents (GNOME_APP (window), hpane);

  gnome_app_set_menus (GNOME_APP (window),
		       GTK_MENU_BAR (create_menu (window)));

  gtk_widget_show (window);
}


gint
delete_event (GtkWidget * widget, gpointer data)
{
}
