/* Balsa E-Mail Client
 * Copyright (C) 1997-1999 Jay Painter and Stuart Parmenter
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
#include <gtk/gtk.h>
#include <stdio.h>
#include <errno.h>
#include "balsa-app.h"

#include "address-book.h"

static GtkWidget *book_clist;
static GtkWidget *add_clist;
static GtkWidget *ab_entry;
gint            composing;
static LibBalsaAddressBook *current_address_book;

gint address_book_cb(GtkWidget * widget, gpointer data);

static gint ab_gnomecard_cb(GtkWidget * widget, gpointer data);
static gint ab_cancel_cb(GtkWidget * widget, gpointer data);
static gint ab_okay_cb(GtkWidget * widget, gpointer data);
static void ab_clear_clist(GtkCList * clist);
static void ab_switch_cb(GtkWidget * widget, gpointer data);
static void ab_select_row_event(GtkWidget *widget, gint row, gint column, GdkEventButton *event, gpointer data);
static void swap_clist_entry (gint row, GtkWidget *src, GtkWidget *dst);
/*#define AB_ADD_CB_USED*/
#ifdef AB_ADD_CB_USED
static gint ab_add_cb(GtkWidget * widget, gpointer data);
#endif
static void ab_load(GtkWidget * widget, gpointer data);
static void ab_find(GtkWidget * group_entry) ;
static gint ab_compare(GtkCList *clist, gconstpointer a, gconstpointer b);

static void address_book_menu_cb(GtkWidget *widget, gpointer data);

static gint
ab_gnomecard_cb(GtkWidget * widget, gpointer data)
{
	char *argv[] = { "gnomecard" };

	gnome_execute_async (NULL, 1, argv);
	return FALSE;
}

static gint
ab_cancel_cb(GtkWidget * widget, gpointer data)
{
	GnomeDialog    *dialog = (GnomeDialog *) data;
	
	g_assert(dialog != NULL);

	ab_clear_clist( GTK_CLIST(book_clist) );
	ab_clear_clist( GTK_CLIST(add_clist) );

	gtk_widget_destroy(GTK_WIDGET(dialog));

	return FALSE;
}

static void
address_list_to_string(gchar *addr, gchar **string)
{
	gchar *tmp;

	if ( strlen(*string) > 0 )
		tmp = g_strconcat(*string, ", ", addr, NULL);
	else 
		tmp = g_strconcat(*string, addr, NULL);
	g_print ("Add to str: %s (result %s)\n", addr, tmp);
	g_free(*string);
	*string = tmp;
}

/* ab_okay_cb:
   processes the OK button pressed event. 
   keep watch on memory leaks.
*/
static gint
ab_okay_cb(GtkWidget * widget, gpointer data)
{
    gpointer        row;
    gchar          *addr_str, *str, *new_addr;
    
    if (composing) {
	
	addr_str = g_strdup( gtk_entry_get_text(GTK_ENTRY(ab_entry)) );
	g_strchomp(addr_str);
	
	while ((row = gtk_clist_get_row_data(GTK_CLIST(add_clist), 0))) {
	    LibBalsaAddress    *addy = LIBBALSA_ADDRESS(row);
	
	    if(g_list_length(addy->address_list) > 1) {
		    str = g_strdup("");
		    g_list_foreach(addy->address_list, (GFunc)address_list_to_string, &str);
	    } else {
		str = g_strdup_printf("%s <%s>", addy->full_name, (gchar*)addy->address_list->data);
	    }

	    gtk_object_unref(GTK_OBJECT(addy));
	    gtk_clist_remove(GTK_CLIST(add_clist), 0);
	    
	    if(*addr_str) {
		new_addr = g_strconcat(addr_str,", ", str, NULL);
		g_free(str);
	    }
	    else
		new_addr = str;

	    g_free(addr_str);
	    addr_str = new_addr;
	}
	
	gtk_entry_set_text(GTK_ENTRY(ab_entry), addr_str);
	g_free(addr_str);
    }
    ab_cancel_cb(widget, data);
    
    return FALSE;
}

static void 
ab_clear_clist(GtkCList * clist)
{
	gpointer        row;
	
	gtk_clist_freeze(GTK_CLIST(clist));
	while ((row = gtk_clist_get_row_data(clist, 0))) {
		
		LibBalsaAddress    *addy = LIBBALSA_ADDRESS(row);
		gtk_object_unref(GTK_OBJECT(addy));
		gtk_clist_remove(GTK_CLIST(clist), 0);
	}
	gtk_clist_thaw(GTK_CLIST(clist));

}

static void
swap_clist_entry (gint row, GtkWidget *src, GtkWidget *dst)
{
    	gint 		num;
	gchar 		*listdata[2];
	LibBalsaAddress	*addy_data;

	if (src != NULL || dst != NULL)
	{
		addy_data = LIBBALSA_ADDRESS(gtk_clist_get_row_data(GTK_CLIST(src), row));
	
		listdata[0] = addy_data->id;
		if ( addy_data->address_list )
			listdata[1] = addy_data->address_list->data;
		else	
			listdata[1] = "";

		gtk_clist_remove(GTK_CLIST(src), row);
		num = gtk_clist_append(GTK_CLIST(dst), listdata);
		gtk_clist_set_row_data(GTK_CLIST(dst), num, (gpointer)addy_data);
	}
}

static void
ab_select_row_event (GtkWidget * widget, gint row, gint column, GdkEventButton *event, gpointer data)
{
  if ( event == NULL )
    return;

  if (event->type==GDK_2BUTTON_PRESS || event->type==GDK_3BUTTON_PRESS)
    swap_clist_entry (row, /* row to swap */
		      GTK_WIDGET(data), /* from */
		      (data == book_clist) ? (add_clist) : (book_clist) /* to */);
}

static void
ab_switch_cb(GtkWidget * widget, gpointer data)
{
    	GtkWidget      *from = GTK_WIDGET(data);

	while ( GTK_CLIST(from)->selection )
	{
	    	swap_clist_entry (GPOINTER_TO_INT(GTK_CLIST(from)->selection->data),
			    from, 
			    (data == book_clist) ? (add_clist) : (book_clist));
	}
}

#ifdef AB_ADD_CB_USED
static gint
ab_add_cb(GtkWidget * widget, gpointer data)
{
	GtkWidget      *dialog,
		*vbox,
		*w,
		*hbox;
	
	dialog = gnome_dialog_new( _("Add New Address"), 
				   GNOME_STOCK_BUTTON_CANCEL, 
				   GNOME_STOCK_BUTTON_OK, NULL);
        gnome_dialog_set_parent (dialog, GTK_WINDOW (balsa_app.main_window));

	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, 
				    GTK_SIGNAL_FUNC(ab_cancel_cb), 
				    (gpointer) dialog);
	vbox = GNOME_DIALOG(dialog)->vbox;

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Name:")), FALSE, 
			   FALSE, 0);
	w = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("E-Mail Address:")),
			   FALSE, FALSE, 0);
	w = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);

	return FALSE;
}
#endif

/*
 * ab_load()
 *
 * Loads the addressbooks into a clist.  This is used by the compose
 * window.
 */
#define LINE_LEN 256
static void 
ab_load(GtkWidget * widget, gpointer data) 
{ 
	gchar *listdata[2];
	GList *list;
	LibBalsaAddress *addr;
	gint rownum; 

	/*
	 * Load the addressbooks
	 */
	ab_clear_clist(GTK_CLIST(book_clist)); 

	if (composing) 
		ab_clear_clist(GTK_CLIST(add_clist)); 

	if (current_address_book == NULL)
		return;

	libbalsa_address_book_load(current_address_book);
	list =  current_address_book->address_list;

	/*
	 * Add the GList to a gtk_clist()
	 */
	gtk_clist_freeze (GTK_CLIST (book_clist)); 
	while (list != NULL)
	{ 
		addr = LIBBALSA_ADDRESS(list->data);
		listdata[0] = addr->id;
		if ( addr->address_list ) 
			listdata[1] = (gchar*)addr->address_list->data;
		else
			listdata[1] = "";

		gtk_object_ref(GTK_OBJECT(addr));

		rownum = gtk_clist_append (GTK_CLIST (book_clist), listdata); 
		gtk_clist_set_row_data (GTK_CLIST (book_clist),
					rownum, addr); 
		list = g_list_next (list);
	}	 

	/*
	 * Show the GList.
	 */
	gtk_clist_set_column_width(GTK_CLIST(book_clist), 0, 
	gtk_clist_optimal_column_width(GTK_CLIST(book_clist),0)); 
	gtk_clist_thaw(GTK_CLIST(book_clist)); 

}

static void 
ab_find(GtkWidget * group_entry) 
{ 
	gchar *entry_text; 
	gpointer row; 
	gchar *new; 
	gint num; 

	g_return_if_fail(book_clist); 
	g_return_if_fail(group_entry);

	entry_text = gtk_entry_get_text(GTK_ENTRY(group_entry)); 
	if (strlen(entry_text) == 0)
	  return;

	gtk_clist_unselect_all(GTK_CLIST(book_clist));
	gtk_clist_freeze(GTK_CLIST(book_clist)); 

	num = 0; 
	while ( (row = gtk_clist_get_row_data(GTK_CLIST(book_clist), num))!= NULL) { 
		gtk_clist_get_text(GTK_CLIST(book_clist), num, 0, &new); 
		if (strncasecmp(new, entry_text,strlen(entry_text)) == 0){ 
			gtk_clist_moveto(GTK_CLIST(book_clist), num, 0, 0, 0); 
			break; 
		} 
		num++; 
	} 
	gtk_clist_thaw(GTK_CLIST(book_clist)); 
	gtk_clist_select_row(GTK_CLIST(book_clist), num, 0);
	return;
} 

static void 
mode_toggled(GtkWidget *w, gpointer data) {
    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data))) {
	balsa_app.ab_dist_list_mode = FALSE;
    } else {
	balsa_app.ab_dist_list_mode = TRUE;
    }
	ab_load(NULL, NULL); 
}

gint 
address_book_cb(GtkWidget * widget, gpointer data) 
{ 
	GtkWidget *find_label, 
		*find_entry, 
		*dialog, 
		*vbox, 
		*w, 
		*hbox, 
		*box2, 
		*scrolled_window,
	        *radio1, *radio2,
		*ab_option, *ab_menu,
		*menu_item;
	GList *ab_list;
	LibBalsaAddressBook *address_book;
	guint default_offset = 0;

	static gchar *titles[2] = {N_("Name"), N_("E-Mail Address")}; 

#ifdef ENABLE_NLS
	titles[0]=_(titles[0]);
	titles[1]=_(titles[1]);
#endif

	dialog = gnome_dialog_new(_("Address Book"), GNOME_STOCK_BUTTON_OK, GNOME_STOCK_BUTTON_CANCEL, NULL); 

        /* If we have something in the data, then the addressbook was opened
         * from a message window */
	/* FIXME: (widget->parent->parent->parent->parent->parent) could be more elegant ;-) */
        if (GTK_IS_BUTTON (widget))
                gnome_dialog_set_parent (
		    GNOME_DIALOG (dialog), 
		    GTK_WINDOW (widget->parent->parent->parent->parent->parent->parent));
        else
                gnome_dialog_set_parent (GNOME_DIALOG (dialog), 
                                         GTK_WINDOW (widget->parent->parent) );

	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, GTK_SIGNAL_FUNC(ab_okay_cb), (gpointer) dialog); 
	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 1, GTK_SIGNAL_FUNC(ab_cancel_cb), (gpointer) dialog); 
	vbox = GNOME_DIALOG(dialog)->vbox; 

	book_clist = gtk_clist_new_with_titles(2, titles); 
	gtk_clist_set_selection_mode(GTK_CLIST(book_clist), GTK_SELECTION_MULTIPLE); 
	gtk_clist_column_titles_passive(GTK_CLIST(book_clist)); 
	gtk_clist_set_compare_func(GTK_CLIST(book_clist), ab_compare);
	gtk_clist_set_sort_type(GTK_CLIST(book_clist), GTK_SORT_ASCENDING);
	gtk_clist_set_auto_sort(GTK_CLIST(book_clist), TRUE);

	add_clist = gtk_clist_new_with_titles(2, titles); 
	gtk_clist_set_selection_mode(GTK_CLIST(add_clist), GTK_SELECTION_MULTIPLE); 
	gtk_clist_column_titles_passive(GTK_CLIST(add_clist)); 
	gtk_clist_set_compare_func(GTK_CLIST(add_clist), ab_compare);
	gtk_clist_set_sort_type(GTK_CLIST(add_clist), GTK_SORT_ASCENDING);
	gtk_clist_set_auto_sort(GTK_CLIST(add_clist), TRUE);

	ab_menu = gtk_menu_new ();	
	if ( balsa_app.address_book_list ) {
		current_address_book = balsa_app.default_address_book;
		
		ab_list = balsa_app.address_book_list;
		while (ab_list) {
			address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
			if ( current_address_book == NULL )
				current_address_book = address_book;

			menu_item = gtk_menu_item_new_with_label ( address_book->name );
			gtk_widget_show ( menu_item );
			gtk_menu_append(GTK_MENU(ab_menu), menu_item);
			
			gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
					   address_book_menu_cb, address_book);
			if ( address_book == balsa_app.default_address_book) {
				gtk_menu_set_active(GTK_MENU(ab_menu), default_offset);
			}
			default_offset++;
	
			ab_list = g_list_next(ab_list);
		}
		gtk_widget_show(ab_menu);
	}
	ab_option = gtk_option_menu_new();
	gtk_option_menu_set_menu(GTK_OPTION_MENU(ab_option), ab_menu);
	gtk_widget_show(ab_option);
	gtk_box_pack_start( GTK_BOX(vbox), ab_option, TRUE, TRUE, 0);
	
	ab_entry = (GtkWidget *) data; 

	find_entry = gtk_entry_new(); 
	gtk_widget_show(find_entry); 
	gtk_signal_connect(GTK_OBJECT(find_entry), "changed", GTK_SIGNAL_FUNC(ab_find), find_entry); 
	find_label = gtk_label_new(_("Name:")); 
	gtk_widget_show(find_label); 
	composing = FALSE; 

	hbox = gtk_hbox_new(FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0); 

	box2 = gtk_vbox_new(FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(hbox), box2, FALSE, FALSE, 0); 
	/*gtk_box_pack_start(GTK_BOX(box2), gtk_label_new(_("Address Book")), FALSE, FALSE, 0); */
	gtk_box_pack_start(GTK_BOX(box2), find_label, FALSE, FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(box2), find_entry, FALSE, FALSE, 0); 
	
	scrolled_window = gtk_scrolled_window_new(NULL, NULL); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
	gtk_box_pack_start(GTK_BOX(box2), scrolled_window, TRUE, TRUE, 0); 
	gtk_container_add(GTK_CONTAINER(scrolled_window), book_clist); 
	gtk_widget_set_usize(scrolled_window, 300, 250);
	
	gtk_signal_connect(GTK_OBJECT(book_clist), "select_row", GTK_SIGNAL_FUNC(ab_select_row_event), (gpointer) book_clist);

	/* 
	 * Only display this part of * the window when we're adding to a composing 
	 * message. 
	 */ 
	if( GTK_IS_ENTRY( (GtkEntry *) data ) ) {
		composing = TRUE; 
		
		box2 = gtk_vbox_new(FALSE, 5); 
		gtk_box_pack_start(GTK_BOX(hbox), box2, FALSE, FALSE, 0); 
		w = gtk_button_new(); 
		gtk_container_add(GTK_CONTAINER(w), gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_FORWARD)); 
		gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_switch_cb), (gpointer) book_clist); 
		gtk_box_pack_start(GTK_BOX(box2), w, TRUE, FALSE, 0); 
		w = gtk_button_new(); 
		gtk_container_add(GTK_CONTAINER(w), gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_BACK)); 
		gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_switch_cb), (gpointer) add_clist); 
		gtk_box_pack_start(GTK_BOX(box2), w, TRUE, FALSE, 0); 
		
		box2 = gtk_vbox_new(FALSE, 5); 
		gtk_box_pack_start(GTK_BOX(hbox), box2, TRUE, TRUE, 0); 
		gtk_box_pack_start(GTK_BOX(box2), gtk_label_new(_("Send-To")), FALSE, FALSE, 0); 
		scrolled_window = gtk_scrolled_window_new(NULL, NULL); 
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
					       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
		gtk_box_pack_start(GTK_BOX(box2), scrolled_window, FALSE, FALSE, 0); 
		gtk_container_add(GTK_CONTAINER(scrolled_window), add_clist); 
		gtk_clist_set_selection_mode(GTK_CLIST(add_clist), GTK_SELECTION_MULTIPLE); 
		gtk_clist_column_titles_passive(GTK_CLIST(add_clist)); 
		gtk_widget_set_usize(scrolled_window, 300, 250); 

		gtk_signal_connect(GTK_OBJECT(add_clist), "select_row", GTK_SIGNAL_FUNC(ab_select_row_event), (gpointer) add_clist);
	} 

	hbox = gtk_hbutton_box_new(); 
	gtk_hbutton_box_set_layout_default(GTK_BUTTONBOX_START); 
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0); 
	w = gnome_pixmap_button(gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_OPEN), _("Run GnomeCard")); 

	gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_gnomecard_cb), NULL); 
	gtk_container_add(GTK_CONTAINER(hbox), w); 
	gtk_widget_ref (w);

	w = gnome_pixmap_button(gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_ADD), _("Re-Import")); 
	gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_load), NULL); 
	gtk_container_add(GTK_CONTAINER(hbox), w); 
	
	ab_load(NULL, NULL); 

	/* mode switching stuff */
	radio1 = gtk_radio_button_new_with_label(
	    NULL, _("Import first address only"));
	radio2 = gtk_radio_button_new_with_label_from_widget (
	    GTK_RADIO_BUTTON(radio1), _("Import all adresses"));
	gtk_signal_connect(GTK_OBJECT(radio1), "toggled", 
			   GTK_SIGNAL_FUNC(mode_toggled), radio1); 
     

	/* Pack them into a box, then show all the widgets */
	gtk_box_pack_start (GTK_BOX (vbox), radio1, TRUE, TRUE, 1);
	gtk_box_pack_start (GTK_BOX (vbox), radio2, TRUE, TRUE, 1);
	gtk_window_set_wmclass (GTK_WINDOW (dialog), "addressbook", "Balsa");
	gtk_widget_show_all(dialog);
   
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio2), 
				     balsa_app.ab_dist_list_mode);
	gtk_widget_grab_focus(find_entry);

	/* PS: I do not like modal dialog boxes but the only other option
	   is to reference the connected field and  check if it is
	   destroyed in ab_okay_cb before accessing it.
	*/
	if(composing) {
	   gtk_window_set_modal( GTK_WINDOW( dialog ), TRUE );
	   gnome_dialog_run( GNOME_DIALOG( dialog ) );
	}
	
	return FALSE; 
} 

static void address_book_menu_cb(GtkWidget *widget, gpointer data)
{
	current_address_book = LIBBALSA_ADDRESS_BOOK(data);
	ab_load(widget, data);
}

static gint 
ab_compare(GtkCList *clist, gconstpointer a, gconstpointer b)
{
  gchar *c1, *c2;

  GtkCListRow *row1 = (GtkCListRow *) a;
  GtkCListRow *row2 = (GtkCListRow *) b;

  g_assert(row1->cell->type == GTK_CELL_TEXT);
  g_assert(row2->cell->type == GTK_CELL_TEXT);

  c1 = GTK_CELL_TEXT(*row1->cell)->text;
  c2 = GTK_CELL_TEXT(*row2->cell)->text;

  if ( c1 == NULL || c2 == NULL )
    return 0;

  return g_strcasecmp(c1, c2);
}
