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


#include <gtk/gtk.h>
#include <gnome.h>
#include <stdio.h>
#include <errno.h>
#include "address-book.h"

static GtkWidget *book_clist;
static GtkWidget *add_clist;
static GtkWidget *ab_entry;
gint            composing;

gint address_book_cb(GtkWidget * widget, gpointer data);

static gint ab_gnomecard_cb(GtkWidget * widget, gpointer data);
static gint ab_cancel_cb(GtkWidget * widget, gpointer data);
static gint ab_okay_cb(GtkWidget * widget, gpointer data);
static void ab_clear_clist(GtkCList * clist);
static gint ab_delete_compare(gconstpointer a, gconstpointer b);
static gint ab_switch_cb(GtkWidget * widget, gpointer data);
/*#define AB_ADD_CB_USED*/
#ifdef AB_ADD_CB_USED
static gint ab_add_cb(GtkWidget * widget, gpointer data);
#endif
static void ab_load(GtkWidget * widget, gpointer data);
static void ab_find(GtkWidget * group_entry) ;


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
	gtk_widget_destroy(GTK_WIDGET(dialog));

	return FALSE;
}

static gint
ab_okay_cb(GtkWidget * widget, gpointer data)
{
	gpointer        row;
	gchar          *text;
	gchar           new[512];

	if (composing) {

		text = gtk_entry_get_text(GTK_ENTRY(ab_entry));
		strcpy(new, text);

		while ((row = gtk_clist_get_row_data(GTK_CLIST(add_clist), 0))) {
			AddressData    *addy = (AddressData *) row;
			sprintf(new, "%s%s %s <%s>", new, ((*new != '\0') ? ", " : ""), addy->name, addy->addy);
			free(addy->name);
			free(addy->addy);
			g_free(addy);
			gtk_clist_remove(GTK_CLIST(add_clist), 0);
		}

		gtk_entry_set_text(GTK_ENTRY(ab_entry), new);
	}
	ab_cancel_cb(widget, data);
	
	return FALSE;
}

static void 
ab_clear_clist(GtkCList * clist)
{
	gpointer        row;
	while ((row = gtk_clist_get_row_data(clist, 0))) {
		AddressData    *addy = (AddressData *) row;
		free(addy->name);
		free(addy->addy);
		g_free(addy);
		gtk_clist_remove(GTK_CLIST(clist), 0);
	}
}

static gint
ab_delete_compare(gconstpointer a, gconstpointer b)
{
	if (GPOINTER_TO_INT(a) > GPOINTER_TO_INT(b))
		return 1;
	else if (GPOINTER_TO_INT(a) == GPOINTER_TO_INT(b))
		return 0;
	else
		return -1;
}

static gint
ab_switch_cb(GtkWidget * widget, gpointer data)
{
	GtkWidget      *from = GTK_WIDGET(data);
	GtkWidget      *to = (data == book_clist) ? add_clist : book_clist;
	GList          *glist = GTK_CLIST(from)->selection;
	GList          *deletelist = NULL, *pointer;

	for (pointer = g_list_first(glist); pointer != NULL; pointer = g_list_next(pointer)) {
		gint            num;
		gchar          *listdata[2];
		AddressData    *addy_data;
		
		num = GPOINTER_TO_INT(pointer->data);
		
		addy_data = gtk_clist_get_row_data(GTK_CLIST(from), num);
		listdata[0] = addy_data->name;
		listdata[1] = addy_data->addy;
		
		deletelist = g_list_append(deletelist, GINT_TO_POINTER(num));

		num = gtk_clist_append(GTK_CLIST(to), listdata);
		gtk_clist_set_row_data(GTK_CLIST(to), num, (gpointer) addy_data);
	}

	deletelist = g_list_sort(deletelist, (GCompareFunc) ab_delete_compare);

	for (pointer = g_list_last(deletelist); pointer != NULL; pointer = g_list_previous(pointer)) {
		gtk_clist_remove(GTK_CLIST(from), GPOINTER_TO_INT(pointer->data));
	}

	g_list_free(deletelist);

	return FALSE;
}

#ifdef AB_ADD_CB_USED
static gint
ab_add_cb(GtkWidget * widget, gpointer data)
{
	GtkWidget      *dialog,
		*vbox,
		*w,
		*hbox;
	
	dialog = gnome_dialog_new(N_("Add New Address"), GNOME_STOCK_BUTTON_CANCEL, GNOME_STOCK_BUTTON_OK, NULL);
	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, GTK_SIGNAL_FUNC(ab_cancel_cb), (gpointer) dialog);
	vbox = GNOME_DIALOG(dialog)->vbox;

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(N_("Name:")), FALSE, FALSE, 0);
	w = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);

	hbox = gtk_hbox_new(FALSE, 5);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(N_("E-Mail Address:")), FALSE, FALSE, 0);
	w = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), w, FALSE, FALSE, 0);

	gtk_widget_show_all(dialog);

	return FALSE;
}
#endif

static void 
ab_load(GtkWidget * widget, gpointer data) 
{ 
	FILE *gc; 
	gchar name[256], 
		email[256], 
		string[256], 
		*listdata[2]; 
	gint got_name = FALSE; 
	gint in_vcard = FALSE;
	gint i = -1;

	ab_clear_clist(GTK_CLIST(book_clist)); 
	if (composing) 
		ab_clear_clist(GTK_CLIST(add_clist)); 
	
	gc = fopen(gnome_util_prepend_user_home(".gnome/GnomeCard.gcrd"), "r"); 
	if (!gc) { 
		g_print(N_("Unable to open ~/.gnome/GnomeCard.gcrd for read.\n - %s\n"), g_unix_error_string(errno)); 
		return; 
	} 

	while ( fgets(string, 255, gc)) { 

		if ( strncasecmp(string, "BEGIN:VCARD", strlen("BEGIN:VCARD")) == 0 ) {
			in_vcard = TRUE;
		}

		if (in_vcard) {
			if (string[0] == 'F' && string[1] == 'N' && string[2] == ':' && string[3] != '\0') { 
				got_name = TRUE; 
				while (string[++i] != '\n' && string[i] != '\0');
				strncpy(name, &string[3], i-4);
				name[i-4] = '\0';
				i = -1;
			} 

			if (sscanf(string, N_("EMAIL;INTERNET:%s\n"), email)) { 
				int rownum; 
				AddressData *data = g_malloc(sizeof(AddressData)); 
			
				listdata[0] = got_name ? strdup(name) : strdup(N_("No-Name")); 
				listdata[1] = strdup(email); 

				data->name = listdata[0]; 
				data->addy = listdata[1]; 
				rownum = gtk_clist_append(GTK_CLIST(book_clist), listdata); 
				gtk_clist_set_row_data(GTK_CLIST(book_clist), rownum, (gpointer) data); 
			} 

			if ( strncasecmp(string, "END:VCARD", strlen("END:VCARD")) == 0) {
				in_vcard = got_name = FALSE;
			}
		}
	} 

	gtk_clist_set_column_width(GTK_CLIST(book_clist), 0, gtk_clist_optimal_column_width(GTK_CLIST(book_clist), 0)); 

	fclose(gc); 
} 

static void 
ab_find(GtkWidget * group_entry) 
{ 
	gchar *entry_text; 
	gpointer row; 
	gchar *new; 
	gint num; 

	g_return_if_fail(book_clist); 

	entry_text = gtk_entry_get_text(GTK_ENTRY(group_entry)); 
	gtk_clist_freeze(GTK_CLIST(book_clist)); 

	if (strlen(entry_text) == 0)
		return;

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
	return;
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
		*scrolled_window; 

	gchar *titles[2] = {N_("Name"), N_("E-Mail Address")}; 
	
	dialog = gnome_dialog_new(N_("Address Book"), GNOME_STOCK_BUTTON_CANCEL, GNOME_STOCK_BUTTON_OK, NULL); 
	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 0, GTK_SIGNAL_FUNC(ab_cancel_cb), (gpointer) dialog); 
	gnome_dialog_button_connect(GNOME_DIALOG(dialog), 1, GTK_SIGNAL_FUNC(ab_okay_cb), (gpointer) dialog); 
	vbox = GNOME_DIALOG(dialog)->vbox; 

	book_clist = gtk_clist_new_with_titles(2, titles); 
	gtk_clist_set_selection_mode(GTK_CLIST(book_clist), GTK_SELECTION_MULTIPLE); 
	gtk_clist_column_titles_passive(GTK_CLIST(book_clist)); 
	
	add_clist = gtk_clist_new_with_titles(2, titles); 
	gtk_clist_set_selection_mode(GTK_CLIST(add_clist), GTK_SELECTION_MULTIPLE); 
	gtk_clist_column_titles_passive(GTK_CLIST(add_clist)); 

	ab_entry = (GtkWidget *) data; 

	find_entry = gtk_entry_new(); 
	gtk_widget_show(find_entry); 
	gtk_signal_connect(GTK_OBJECT(find_entry), "changed", GTK_SIGNAL_FUNC(ab_find), find_entry); 
	find_label = gtk_label_new(N_("Name:")); 
	gtk_widget_show(find_label); 

	composing = FALSE; 

	hbox = gtk_hbox_new(FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0); 

	box2 = gtk_vbox_new(FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(hbox), box2, FALSE, FALSE, 0); 
	//gtk_box_pack_start(GTK_BOX(box2), gtk_label_new(N_("Address Book")), FALSE, FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(box2), find_label, FALSE, FALSE, 0); 
	gtk_box_pack_start(GTK_BOX(box2), find_entry, FALSE, FALSE, 0); 
	
	scrolled_window = gtk_scrolled_window_new(NULL, NULL); 
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
				       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
	gtk_box_pack_start(GTK_BOX(box2), scrolled_window, TRUE, TRUE, 0); 
	gtk_container_add(GTK_CONTAINER(scrolled_window), book_clist); 
	gtk_widget_set_usize(scrolled_window, 250, 200); 
	
	/* 
	 * Only display this part of * the window when we're adding to a composing 
	 * message. 
	 */ 
	/*if (!GNOME_IS_MDI((GnomeMDI *) data)) { */
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
		gtk_box_pack_start(GTK_BOX(box2), gtk_label_new(N_("Send-To")), FALSE, FALSE, 0); 
		scrolled_window = gtk_scrolled_window_new(NULL, NULL); 
		gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), 
					       GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC); 
		gtk_box_pack_start(GTK_BOX(box2), scrolled_window, FALSE, FALSE, 0); 
		gtk_container_add(GTK_CONTAINER(scrolled_window), add_clist); 
		gtk_clist_set_selection_mode(GTK_CLIST(add_clist), GTK_SELECTION_MULTIPLE); 
		gtk_clist_column_titles_passive(GTK_CLIST(add_clist)); 
		gtk_widget_set_usize(scrolled_window, 250, 200); 
	} 

	hbox = gtk_hbutton_box_new(); 
	gtk_hbutton_box_set_layout_default(GTK_BUTTONBOX_START); 
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0); 
	w = gnome_pixmap_button(gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_OPEN), N_("Run GnomeCard")); 

	gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_gnomecard_cb), NULL); 
	gtk_container_add(GTK_CONTAINER(hbox), w); 
	gtk_widget_ref (w);

	w = gnome_pixmap_button(gnome_stock_pixmap_widget(dialog, GNOME_STOCK_PIXMAP_ADD), N_("Re-Import")); 
	gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(ab_load), NULL); 
	gtk_container_add(GTK_CONTAINER(hbox), w); 
	
	ab_load(NULL, NULL); 
	
	gtk_widget_show_all(dialog); 
	
	return FALSE; 
} 

