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

	ab_clear_clist( GTK_CLIST(book_clist) );
	ab_clear_clist( GTK_CLIST(add_clist) );

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
			sprintf(new, "%s%s%s <%s>", new, 
				((*new != '\0') ? ", " : ""), 
				addy->name, addy->addy);
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


static gchar * 
extract_name(const gchar *string)
/* Extract full name in order from <string> that has GnomeCard format
   and returns the pointer to the allocated memory chunk.
*/
{
   enum GCardFieldOrder { LAST=0, FIRST, MIDDLE, PREFIX, SUFFIX };
   gint cpt, j;
   gchar ** fld, **name_arr;
   gchar * res = NULL;

   fld = g_strsplit(string, ";",5);

   cpt = 0;
   while(fld[cpt] != NULL)
      cpt++;

   if(cpt==0) /* insane empty name */ return NULL;

   name_arr = g_malloc((cpt+1)*sizeof(gchar*));

   j = 0;
   if(cpt>PREFIX && fld[PREFIX] != '\0')
      name_arr[j++] = g_strdup(fld[PREFIX]);
      
   if(cpt>FIRST && fld[FIRST] != '\0')
      name_arr[j++] = g_strdup(fld[FIRST]);

   if(cpt>MIDDLE && fld[MIDDLE] != '\0')
      name_arr[j++] = g_strdup(fld[MIDDLE]);

   if(cpt>LAST && fld[LAST] != '\0')
      name_arr[j++] = g_strdup(fld[LAST]);

   if(cpt>SUFFIX && fld[SUFFIX] != '\0')
      name_arr[j++] = g_strdup(fld[SUFFIX]);

   name_arr[j] = NULL;

   g_strfreev(fld);

   /* collect the data to one string */
   res = g_strjoinv(" ", name_arr);
   while(j-- > 0)
      g_free(name_arr[j]);
   g_free(name_arr);

   return res;
}

#define LINE_LEN 256
static void 
ab_load(GtkWidget * widget, gpointer data) 
{ 
   FILE *gc; 
   gchar string[LINE_LEN];
   gchar * name = NULL, *email = NULL, *listdata[2];
   gint in_vcard = FALSE;

   ab_clear_clist(GTK_CLIST(book_clist)); 
   if (composing) 
      ab_clear_clist(GTK_CLIST(add_clist)); 
   gc = fopen(gnome_util_prepend_user_home(".gnome/GnomeCard.gcrd"),"r"); 
   if (!gc) 
   { 
      GtkWidget *box;
      char * msg  = g_strdup_printf(
	 N_("Unable to open ~/.gnome/GnomeCard.gcrd for read.\n - %s\n"), 
	 g_unix_error_string(errno)); 
      box = gnome_message_box_new(msg,
				  GNOME_MESSAGE_BOX_ERROR, _("OK"), NULL );
      gtk_window_set_modal( GTK_WINDOW( box ), TRUE );
      gnome_dialog_run( GNOME_DIALOG( box ) );
      gtk_widget_destroy( GTK_WIDGET( box ) );
      g_free(msg);
      return; 
   } 
   while ( fgets(string, sizeof(string), gc)) 
   { 
      if ( strncasecmp(string, "BEGIN:VCARD", 11) == 0 ) {
	 in_vcard = TRUE;
	 continue;
      }

      if ( strncasecmp(string, "END:VCARD", 9) == 0) {
	 int rownum; 
	 AddressData *data;
	 if(email) {
	    data = g_malloc( sizeof(AddressData) ); 
	    data->name = name ? name : g_strdup( N_("No-Name") );
	    data->addy = email;
	    listdata[0] = name;
	    listdata[1] = email;
	    rownum = gtk_clist_append(GTK_CLIST(book_clist), listdata); 
	    gtk_clist_set_row_data(GTK_CLIST( book_clist),
				   rownum, (gpointer) data); 
	    name  = NULL;
	    email = NULL;
	 } else g_free(name);

	 in_vcard = FALSE;
	 continue;
      }

      if (!in_vcard) continue;
      g_strchomp(string);

      if(strncasecmp(string, "N:", 2) == 0) {
	 name = extract_name(string+2);
	 /* printf("name : %s\n", name); */
	 continue;
      }

      /* fetch only first internet e-mail field */
      if(!email && strncasecmp(string, "EMAIL;INTERNET:",15) == 0)
	 email = g_strdup(string+15);
   }	 

   gtk_clist_set_column_width(GTK_CLIST(book_clist), 0, 
	  gtk_clist_optimal_column_width(GTK_CLIST(book_clist),0)); 
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

	static gchar *titles[2] = {N_("Name"), N_("E-Mail Address")}; 
	
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

