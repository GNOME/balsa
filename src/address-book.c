/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include <gnome.h>
#include "balsa-app.h"
#include "sendmsg-window.h"
#include "address-book.h"

#define LIST_COLUMN_NAME        0
#define LIST_COLUMN_ADDRESS     1

/*
  This struct holds the information associated with each row of a clist:

  The LibBalsaAddress object, and which address within that the row contains.

  I wish there was a better way to do this, but a CListRow can only
  have a single data reference attached.
 */
typedef struct _AddressBookEntry AddressBookEntry;

struct _AddressBookEntry
{
    gint ref_count;
    LibBalsaAddress *address;
    gint which_multiple;
};

/* Object system functions ... */
static void balsa_address_book_init(BalsaAddressBook *ab);
static void balsa_address_book_class_init(BalsaAddressBookClass *klass);

/* CListRow data ... */
static AddressBookEntry *address_book_entry_new(LibBalsaAddress *address, 
						gint which_multiple);
static void address_book_entry_unref(AddressBookEntry *entry);

/* Loading ... */
static void balsa_address_book_load_cb(LibBalsaAddressBook *libbalsa_ab, 
				       LibBalsaAddress *address, 
				       BalsaAddressBook *ab);
static void balsa_address_book_load(BalsaAddressBook *ab);
static void balsa_address_book_reload(GtkWidget *w, BalsaAddressBook *av);

/* Callbacks ... */
static void balsa_address_book_dist_mode_toggled(GtkWidget * w,
						 BalsaAddressBook *ab);
static void balsa_address_book_menu_changed(GtkWidget * widget, 
					    BalsaAddressBook *ab);
static void balsa_address_book_run_gnomecard(GtkWidget * widget, gpointer data);
static void balsa_address_book_find(GtkWidget * group_entry, BalsaAddressBook *ab);
static void balsa_address_book_button_clicked (BalsaAddressBook *ab, 
					       gint button_number, 
					       gpointer data);

/* address and recipient list management ... */
static void balsa_address_book_swap_clist_entry(GtkCList * src, GtkCList * dst);
static void balsa_address_book_select_address(GtkWidget *widget, 
					      gint row, gint column,
					      GdkEventButton *event, 
					      BalsaAddressBook *ab);
static void balsa_address_book_select_recipient(GtkWidget *widget, 
						gint row, gint column,
						GdkEventButton *event, 
						BalsaAddressBook *ab);
static void balsa_address_book_move_to_recipient_list(GtkWidget *widget,
						      BalsaAddressBook *ab);
static void balsa_address_book_remove_from_recipient_list(GtkWidget *widget, 
							  BalsaAddressBook *ab);

/* Utility ... */
static gint balsa_address_book_compare_entries(GtkCList * clist, 
					       gconstpointer a, 
					       gconstpointer b);

GtkType
balsa_address_book_get_type(void)
{
    static GtkType ab_type = 0;

    if ( !ab_type ) {
	GtkTypeInfo ab_info = {
	    "BalsaAddressBook",
	    sizeof(BalsaAddressBook),
	    sizeof(BalsaAddressBookClass),
	    (GtkClassInitFunc) balsa_address_book_class_init,
	    (GtkObjectInitFunc) balsa_address_book_init,
	    NULL, /*reserved*/
	    NULL, /*reserved*/
	};
	ab_type = gtk_type_unique(GNOME_TYPE_DIALOG, &ab_info);
    }
    return ab_type;
}

GtkWidget *
balsa_address_book_new(gboolean composing)
{
    GtkWidget *ret;

    ret = gtk_type_new(BALSA_TYPE_ADDRESS_BOOK);
    g_return_val_if_fail(ret, NULL);

    BALSA_ADDRESS_BOOK(ret)->composing = composing;

    if ( composing ) { 
	gnome_dialog_append_buttons(GNOME_DIALOG(ret), 
				    GNOME_STOCK_BUTTON_OK,
				    GNOME_STOCK_BUTTON_CANCEL,
				    NULL);
	gtk_widget_show(GTK_WIDGET(BALSA_ADDRESS_BOOK(ret)->send_to_box));
	gtk_widget_show(GTK_WIDGET(BALSA_ADDRESS_BOOK(ret)->arrow_box));
    } else {
	gnome_dialog_append_buttons(GNOME_DIALOG(ret),
				    GNOME_STOCK_BUTTON_CLOSE,
				    NULL);
	gtk_widget_hide(GTK_WIDGET(BALSA_ADDRESS_BOOK(ret)->send_to_box));
	gtk_widget_hide(GTK_WIDGET(BALSA_ADDRESS_BOOK(ret)->arrow_box));
    }

    return ret;

}

static void
balsa_address_book_init(BalsaAddressBook *ab)
{
    GtkWidget *find_label,
	*find_entry,
	*vbox, *vbox2,
	*w,
	*hbox,
	*box2,
	*scrolled_window,
	*ab_option, *ab_menu, *menu_item,
	*stock_widget, *frame, *label;
    GList *ab_list;
    LibBalsaAddressBook *address_book;
    guint default_offset = 0;

    static gchar *titles[2] = { N_("Name"), N_("E-Mail Address") };
    
    ab->current_address_book = NULL;
    gtk_window_set_title(GTK_WINDOW(ab), _("Address Book"));

#ifdef ENABLE_NLS
    titles[0] = _(titles[0]);
    titles[1] = _(titles[1]);
#endif

    gtk_signal_connect(GTK_OBJECT(ab), "clicked",
		       GTK_SIGNAL_FUNC(balsa_address_book_button_clicked), NULL);
		     
    vbox = GNOME_DIALOG(ab)->vbox;

    gtk_window_set_wmclass(GTK_WINDOW(ab), "addressbook", "Balsa");

    /* The main address list */
    ab->address_clist = gtk_clist_new_with_titles(2, titles);
    gtk_clist_set_selection_mode(GTK_CLIST(ab->address_clist),
				 GTK_SELECTION_MULTIPLE);
    gtk_clist_column_titles_passive(GTK_CLIST(ab->address_clist));
    gtk_clist_set_compare_func(GTK_CLIST(ab->address_clist), balsa_address_book_compare_entries);
    gtk_clist_set_sort_type(GTK_CLIST(ab->address_clist), GTK_SORT_ASCENDING);
    gtk_clist_set_auto_sort(GTK_CLIST(ab->address_clist), TRUE);
    gtk_clist_set_column_auto_resize(GTK_CLIST(ab->address_clist), 0, TRUE);
    gtk_clist_set_column_auto_resize(GTK_CLIST(ab->address_clist), 1, TRUE);
    gtk_widget_show(ab->address_clist);
    gtk_signal_connect(GTK_OBJECT(ab->address_clist), "select_row",
		       GTK_SIGNAL_FUNC(balsa_address_book_select_address), ab);
    
    /* The clist for selected addresses in compose mode */
    ab->recipient_clist = gtk_clist_new_with_titles(2, titles);
    gtk_clist_set_selection_mode(GTK_CLIST(ab->recipient_clist),
				 GTK_SELECTION_MULTIPLE);
    gtk_clist_column_titles_passive(GTK_CLIST(ab->recipient_clist));
    gtk_clist_set_compare_func(GTK_CLIST(ab->recipient_clist), balsa_address_book_compare_entries);
    gtk_clist_set_sort_type(GTK_CLIST(ab->recipient_clist), GTK_SORT_ASCENDING);
    gtk_clist_set_auto_sort(GTK_CLIST(ab->recipient_clist), TRUE);
    gtk_clist_set_column_auto_resize(GTK_CLIST(ab->recipient_clist), 0, TRUE);
    gtk_clist_set_column_auto_resize(GTK_CLIST(ab->recipient_clist), 1, TRUE);
    gtk_widget_show(ab->recipient_clist);
    gtk_signal_connect(GTK_OBJECT(ab->recipient_clist), "select_row",
		       GTK_SIGNAL_FUNC(balsa_address_book_select_recipient),
		       (gpointer) ab);

    /* The address book selection menu */
    ab_menu = gtk_menu_new();

    ab->current_address_book = balsa_app.default_address_book;

    ab_list = balsa_app.address_book_list;
    while (ab_list) {
	address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
	if (ab->current_address_book == NULL)
	    ab->current_address_book = address_book;
	
	menu_item = gtk_menu_item_new_with_label(address_book->name);
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(ab_menu), menu_item);
	
	gtk_object_set_data(GTK_OBJECT(menu_item), "address-book",
			    address_book);
	gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
			   GTK_SIGNAL_FUNC(balsa_address_book_menu_changed), 
                           ab);
	
	if (address_book == balsa_app.default_address_book)
	    gtk_menu_set_active(GTK_MENU(ab_menu), default_offset);
	
	default_offset++;
	
	ab_list = g_list_next(ab_list);
    }
    gtk_widget_show(ab_menu);

    ab_option = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(ab_option), ab_menu);
    gtk_widget_show(ab_option);

    gtk_box_pack_start(GTK_BOX(vbox), ab_option, TRUE, TRUE, 0);

    /* Entry widget for finding an address */
    find_label = gtk_label_new(_("Search for Name:"));
    gtk_widget_show(find_label);

    find_entry = gtk_entry_new();
    gtk_widget_show(find_entry);
    gtk_signal_connect(GTK_OBJECT(find_entry), "changed",
		       GTK_SIGNAL_FUNC(balsa_address_book_find), ab);
    
    /* Horizontal layout */
    hbox = gtk_hbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(hbox);

    /* Column for address list */
    vbox2 = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(hbox), vbox2, FALSE, FALSE, 1);
    gtk_widget_show(vbox2);

    /* Pack the find stuff into a box */
    box2 = gtk_hbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox2), box2, FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(box2), find_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box2), find_entry, TRUE, TRUE, 0);
    gtk_widget_show(GTK_WIDGET(box2));


    /* A scrolled window for the address clist */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox2), scrolled_window, TRUE, TRUE, 0);
    gtk_widget_show(scrolled_window);
    gtk_container_add(GTK_CONTAINER(scrolled_window), ab->address_clist);
    gtk_widget_set_usize(scrolled_window, 300, 250);

    /* Column for arrows in compose mode */
    ab->arrow_box = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX(hbox), ab->arrow_box, FALSE, FALSE, 1);
    gtk_widget_show(ab->arrow_box);
    
    /* FIXME: Can make a stock button in one call... */
    w = gtk_button_new();
#if BALSA_MAJOR < 2
    stock_widget = gnome_stock_pixmap_widget(GTK_WIDGET(ab),
					     GNOME_STOCK_PIXMAP_FORWARD);
#else
    stock_widget = gtk_image_new_from_stock(GNOME_STOCK_PIXMAP_FORWARD,
                                            GTK_ICON_SIZE_BUTTON);
#endif                          /* BALSA_MAJOR < 2 */
    gtk_container_add(GTK_CONTAINER(w), stock_widget);
    gtk_box_pack_start(GTK_BOX(ab->arrow_box), w, TRUE, FALSE, 0);
    gtk_widget_show(stock_widget);
    gtk_widget_show(w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
		       GTK_SIGNAL_FUNC(balsa_address_book_move_to_recipient_list),
		       ab);
    
    w = gtk_button_new();
    gtk_box_pack_start(GTK_BOX(ab->arrow_box), w, TRUE, FALSE, 0);
#if BALSA_MAJOR < 2
    stock_widget = gnome_stock_pixmap_widget(GTK_WIDGET(ab),
					     GNOME_STOCK_PIXMAP_BACK);
#else
    stock_widget = gtk_image_new_from_stock(GNOME_STOCK_PIXMAP_BACK,
                                            GTK_ICON_SIZE_BUTTON);
#endif                          /* BALSA_MAJOR < 2 */
    gtk_container_add(GTK_CONTAINER(w), stock_widget);
    gtk_widget_show(stock_widget);
    gtk_widget_show(w);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
		       GTK_SIGNAL_FUNC(balsa_address_book_remove_from_recipient_list),
		       ab);
    
    /* Column for selected addresses in compose mode */
    ab->send_to_box = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(hbox), ab->send_to_box, TRUE, TRUE, 1);
    
    label = gtk_label_new(_("Send-To"));
    gtk_widget_show(label);
    gtk_box_pack_start(GTK_BOX(ab->send_to_box), label,
		       FALSE, FALSE, 1);
    
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_show(scrolled_window);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(ab->send_to_box), scrolled_window, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(scrolled_window), ab->recipient_clist);
    gtk_widget_set_usize(scrolled_window, 300, 250);

    /* Buttons ... */
    hbox = gtk_hbutton_box_new();
    gtk_hbutton_box_set_layout_default(GTK_BUTTONBOX_START);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);
    gtk_widget_show(GTK_WIDGET(hbox));
#if BALSA_MAJOR < 2
    stock_widget = gnome_stock_pixmap_widget(GTK_WIDGET(ab),
					     GNOME_STOCK_PIXMAP_OPEN);
#else
    stock_widget = gtk_image_new_from_stock(GNOME_STOCK_PIXMAP_OPEN,
                                            GTK_ICON_SIZE_BUTTON);
#endif                          /* BALSA_MAJOR < 2 */
    w = gtk_button_new_with_label(_("Run GnomeCard"));
    gtk_container_add(GTK_CONTAINER(w), stock_widget);
    gtk_signal_connect(GTK_OBJECT(w), "clicked",
		       GTK_SIGNAL_FUNC(balsa_address_book_run_gnomecard), NULL);
    gtk_container_add(GTK_CONTAINER(hbox), w);
    gtk_widget_show(GTK_WIDGET(w));

    /* FIXME: Should strive to not need this?? */
#if BALSA_MAJOR < 2
    stock_widget = gnome_stock_pixmap_widget(GTK_WIDGET(ab),
					     GNOME_STOCK_PIXMAP_ADD);
#else
    stock_widget = gtk_image_new_from_stock(GNOME_STOCK_PIXMAP_ADD,
                                            GTK_ICON_SIZE_BUTTON);
#endif                          /* BALSA_MAJOR < 2 */
    w = gtk_button_new_with_label(_("Re-Import"));
    gtk_container_add(GTK_CONTAINER(w), stock_widget);
    gtk_signal_connect(GTK_OBJECT(w), "clicked", GTK_SIGNAL_FUNC(balsa_address_book_reload),
		       ab);
    gtk_container_add(GTK_CONTAINER(hbox), w);
    gtk_widget_show(w);

    balsa_address_book_load(ab);

    /* mode switching stuff */
    frame = gtk_frame_new(_("Treat multiple addresses as:"));
    gtk_widget_show(frame);

    ab->single_address_mode_radio = gtk_radio_button_new_with_label
	(NULL, _("alternative addresses for the same person"));
    gtk_widget_show(ab->single_address_mode_radio);

    ab->dist_address_mode_radio = gtk_radio_button_new_with_label_from_widget
	(GTK_RADIO_BUTTON(ab->single_address_mode_radio),
	 _("a distribution list"));
    gtk_widget_show(ab->dist_address_mode_radio);
    ab->toggle_handler_id = gtk_signal_connect(GTK_OBJECT(ab->single_address_mode_radio), "toggled",
					       GTK_SIGNAL_FUNC(balsa_address_book_dist_mode_toggled), ab);

    if(ab->current_address_book)
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->dist_address_mode_radio),
	    ab->current_address_book->dist_list_mode);

    /* Pack them into a box  */
    box2 = gtk_vbox_new(TRUE, 1);
    gtk_container_add(GTK_CONTAINER(frame), box2);
    gtk_box_pack_start(GTK_BOX(box2), ab->single_address_mode_radio, 
		       FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(box2), ab->dist_address_mode_radio, 
		       FALSE, FALSE, 1);
    gtk_widget_show(box2);
    gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 1);
    
    gtk_widget_grab_focus(find_entry);
}

static void
balsa_address_book_class_init(BalsaAddressBookClass *klass)
{
}

/*
  Runs gnome card
*/
static void
balsa_address_book_run_gnomecard(GtkWidget * widget, gpointer data)
{
    char *argv[] = { "gnomecard" };

    gnome_execute_async(NULL, 1, argv);
}

/*
  Returns a string of all the selected recipients.
 */
gchar *
balsa_address_book_get_recipients(BalsaAddressBook *ab)
{
    GString *str = NULL;
    AddressBookEntry *entry;
    gchar *text;
    gint i;

    g_return_val_if_fail(ab->composing, NULL);

    for(i = 0; i < GTK_CLIST(ab->recipient_clist)->rows; i++) {
	entry = (AddressBookEntry*)(gtk_clist_get_row_data(GTK_CLIST(ab->recipient_clist), i));

	g_assert (entry != NULL);

	text = libbalsa_address_to_gchar(entry->address, entry->which_multiple);
	if ( text == NULL )
	    continue;

	if ( str )
	    g_string_sprintfa(str, ", %s", text);
	else
	    str = g_string_new(text);
	g_free(text);
    }
    if ( str != NULL ) {
	text = str->str;
	g_string_free(str, FALSE);
	return text;
    } else {
	return NULL;
    }
}

/*
  Moves an entry between two CLists.
*/
/*
  FIXME: Need to only move it back if it belongs in current address book?? 
*/
static void
balsa_address_book_swap_clist_entry(GtkCList * src, GtkCList * dst)
{
    gint num;
    gint row;
    gchar *listdata[2];
    AddressBookEntry *entry;
    gchar *address_string;

    g_return_if_fail(GTK_IS_CLIST(src));
    g_return_if_fail(GTK_IS_CLIST(dst));

    while ( src->selection ) {
	row = GPOINTER_TO_INT(src->selection->data);
	entry = (AddressBookEntry*)(gtk_clist_get_row_data(src, row));
	
	g_return_if_fail(gtk_clist_get_cell_type (src, row, LIST_COLUMN_ADDRESS) == GTK_CELL_TEXT);

 	gtk_clist_get_text (src, row, LIST_COLUMN_ADDRESS, &address_string);

	listdata[LIST_COLUMN_NAME] = entry->address->full_name;
	listdata[LIST_COLUMN_ADDRESS]= address_string;

	num = gtk_clist_append(dst, listdata);

	/* Will be unref'd on remove from source... */
	entry->ref_count++;
	gtk_clist_set_row_data_full(dst, num, entry, 
				    (GtkDestroyNotify)address_book_entry_unref);

	gtk_clist_remove(src, row);
    }
}

/*
  Handle a click on the main address list

  If composing then move the address to the other clist, otherwise
  open a compose window.

*/
static void
balsa_address_book_select_address(GtkWidget *widget, gint row, gint column,
				  GdkEventButton *event, BalsaAddressBook *ab)
{
    g_return_if_fail ( BALSA_IS_ADDRESS_BOOK(ab) );

    if ( event == NULL )
	return;


    if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS) {
	if ( ab->composing ) {
	    balsa_address_book_swap_clist_entry(GTK_CLIST(ab->address_clist),
						GTK_CLIST(ab->recipient_clist));
	} else {
	    BalsaSendmsg *snd;
	    gchar *addr;
	    AddressBookEntry *entry;

	    snd = sendmsg_window_new(GTK_WIDGET(balsa_app.main_window), NULL, 
				     SEND_NORMAL);
	    entry = (AddressBookEntry*)gtk_clist_get_row_data(GTK_CLIST(ab->address_clist), row);
	    
	    addr = libbalsa_address_to_gchar(entry->address, entry->which_multiple);
	    gtk_entry_set_text(GTK_ENTRY(snd->to[1]), addr);
	    g_free(addr);

	    gtk_widget_grab_focus(snd->subject[1]);
	}
    }
}

/*
  Handle a click on the recipient list.

  Only sane if composing. Move to address list.
 */
static void balsa_address_book_select_recipient(GtkWidget *widget, 
						gint row, gint column,
						GdkEventButton *event, 
						BalsaAddressBook *ab)
{
    g_return_if_fail( BALSA_IS_ADDRESS_BOOK(ab) );
    g_return_if_fail( ab->composing );

    if ( event == NULL )
	return;

    if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_3BUTTON_PRESS)
	    balsa_address_book_swap_clist_entry(GTK_CLIST(ab->recipient_clist),
						GTK_CLIST(ab->address_clist));
}

/*
  Handle a click on forward button
*/
static void
balsa_address_book_move_to_recipient_list(GtkWidget *widget, BalsaAddressBook *ab)
{
    g_return_if_fail( BALSA_IS_ADDRESS_BOOK(ab) );
    g_return_if_fail( ab->composing );

    balsa_address_book_swap_clist_entry(GTK_CLIST(ab->address_clist), 
					GTK_CLIST(ab->recipient_clist));
}

/*
  Handle a click on the back button
 */
static void
balsa_address_book_remove_from_recipient_list(GtkWidget *widget, BalsaAddressBook *ab)
{
    g_return_if_fail( BALSA_IS_ADDRESS_BOOK(ab) );
    g_return_if_fail( ab->composing );

    balsa_address_book_swap_clist_entry(GTK_CLIST(ab->recipient_clist), 
					GTK_CLIST(ab->address_clist));
}

/*
  Handle a click on the reload button.
 */
static void
balsa_address_book_reload(GtkWidget *w, BalsaAddressBook *ab)
{
    balsa_address_book_load(ab);
}


/*
 * Loads the addressbooks into a clist.  
 */
static void
balsa_address_book_load(BalsaAddressBook *ab)
{
    g_return_if_fail(BALSA_IS_ADDRESS_BOOK(ab));
    ab = BALSA_ADDRESS_BOOK(ab);

    gtk_clist_clear(GTK_CLIST(ab->address_clist));

    if (ab->current_address_book == NULL)
	return;

    libbalsa_address_book_load(ab->current_address_book, 
			       (LibBalsaAddressBookLoadFunc)
			       balsa_address_book_load_cb,
			       ab);
}

/*
  The address load callback. Adds a single address to the address list.

  If the current address book is in dist list mode then create a
  single entry, or else create an entry for each address in the book.
 */
static void
balsa_address_book_load_cb(LibBalsaAddressBook *libbalsa_ab, LibBalsaAddress *address, BalsaAddressBook *ab)
{
    gchar *listdata[2];
    gint rownum;
    GList *address_list;
    AddressBookEntry *entry;
    gint count;

    g_return_if_fail ( BALSA_IS_ADDRESS_BOOK(ab));
    g_return_if_fail ( LIBBALSA_IS_ADDRESS_BOOK(libbalsa_ab) );

    if ( address == NULL )
	return;

    if ( libbalsa_address_is_dist_list(libbalsa_ab, address) ) {

	listdata[LIST_COLUMN_NAME] = address->full_name;
	listdata[LIST_COLUMN_ADDRESS] = libbalsa_address_to_gchar(address, -1);
	
	rownum = gtk_clist_append(GTK_CLIST(ab->address_clist), listdata);

	entry = address_book_entry_new(address, -1);
	gtk_clist_set_row_data_full(GTK_CLIST(ab->address_clist), rownum, entry,
				    (GtkDestroyNotify)address_book_entry_unref);
	g_free(listdata[LIST_COLUMN_ADDRESS]);

    } else {
	address_list = address->address_list;
	count = 0;
	while ( address_list ) {
	    listdata[LIST_COLUMN_NAME] = address->full_name;
	    listdata[LIST_COLUMN_ADDRESS] = (gchar *) address_list->data;
	    
	    rownum = gtk_clist_append(GTK_CLIST(ab->address_clist), listdata);
	    
	    entry = address_book_entry_new(address, count);
	    gtk_clist_set_row_data_full(GTK_CLIST(ab->address_clist), rownum, entry,
					(GtkDestroyNotify)address_book_entry_unref);

	    address_list = g_list_next(address_list);
	    count++;
	}
    }
}

/*
  Search for an address in the address list.
  Attached to the changed signal of the find entry.
*/
static void
balsa_address_book_find(GtkWidget * group_entry, BalsaAddressBook *ab)
{
    const gchar *entry_text;
    gpointer row;
    gchar *new;
    gint num;

    g_return_if_fail(BALSA_IS_ADDRESS_BOOK(ab));

    entry_text = gtk_entry_get_text(GTK_ENTRY(group_entry));

    if (*entry_text == '\0')
	return;

    gtk_clist_unselect_all(GTK_CLIST(ab->address_clist));
    gtk_clist_freeze(GTK_CLIST(ab->address_clist));

    num = 0;
    while ((row = gtk_clist_get_row_data(GTK_CLIST(ab->address_clist), num)) != NULL) {
	gtk_clist_get_text(GTK_CLIST(ab->address_clist), num, 
			   LIST_COLUMN_NAME, &new);

	if (strncasecmp(new, entry_text, strlen(entry_text)) == 0) {
	    gtk_clist_moveto(GTK_CLIST(ab->address_clist), 
			     num, LIST_COLUMN_NAME, 
			     0, 0);
	    break;
	}
	num++;
    }
    gtk_clist_thaw(GTK_CLIST(ab->address_clist));
    gtk_clist_select_row(GTK_CLIST(ab->address_clist), num, 
			 LIST_COLUMN_NAME);
    return;
}

/*
  Handle a change in the dist mode
*/
static void
balsa_address_book_dist_mode_toggled(GtkWidget * w, BalsaAddressBook *ab)
{
    gboolean active;

    g_return_if_fail(BALSA_IS_ADDRESS_BOOK(ab));
    if(ab->current_address_book == NULL) return;

    active = gtk_toggle_button_get_active
	(GTK_TOGGLE_BUTTON(ab->single_address_mode_radio));

    ab->current_address_book->dist_list_mode = !active;

    balsa_address_book_load(ab);
}

/*
  Handle a change in the current address book.
*/
static void
balsa_address_book_menu_changed(GtkWidget * widget, BalsaAddressBook *ab)
{
    LibBalsaAddressBook *addr;

    addr = LIBBALSA_ADDRESS_BOOK(gtk_object_get_data(GTK_OBJECT(widget), "address-book"));
    g_assert(addr != NULL);

    ab->current_address_book = addr;

    gtk_signal_handler_block(GTK_OBJECT(ab->single_address_mode_radio), 
			     ab->toggle_handler_id);
    if ( ab->current_address_book->dist_list_mode )
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->dist_address_mode_radio), TRUE);
    else 
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->single_address_mode_radio), TRUE);
    gtk_signal_handler_unblock(GTK_OBJECT(ab->single_address_mode_radio), 
			       ab->toggle_handler_id);

    balsa_address_book_load(ab);
}

/*
  Compare two rows in a clist.
*/
static gint
balsa_address_book_compare_entries(GtkCList * clist, gconstpointer a, gconstpointer b)
{
    gchar *c1, *c2;

    GtkCListRow *row1 = (GtkCListRow *) a;
    GtkCListRow *row2 = (GtkCListRow *) b;

    g_assert(row1->cell->type == GTK_CELL_TEXT);
    g_assert(row2->cell->type == GTK_CELL_TEXT);

    c1 = GTK_CELL_TEXT(*row1->cell)->text;
    c2 = GTK_CELL_TEXT(*row2->cell)->text;

    if (c1 == NULL || c2 == NULL)
	return 0;

    return g_strcasecmp(c1, c2);
}

/*
  Create the data attached to a clist row
*/

static AddressBookEntry *
address_book_entry_new(LibBalsaAddress *address, gint which_multiple)
{
    AddressBookEntry *abe;

    abe = g_new(AddressBookEntry, 1);

    gtk_object_ref(GTK_OBJECT(address));

    abe->ref_count = 1;
    abe->address = address;
    abe->which_multiple = which_multiple;
    
    return abe;
}

/*
  Unref a row data struct and free if needed.
 */
static void
address_book_entry_unref(AddressBookEntry *entry)
{
    entry->ref_count--;

    if ( entry->ref_count > 0 ) 
	return;

    gtk_object_unref(GTK_OBJECT(entry->address));
    g_free(entry);
}

static void
balsa_address_book_button_clicked (BalsaAddressBook *ab, gint button_number, gpointer data)
{
    if ( !ab->composing )
	gtk_widget_destroy(GTK_WIDGET(ab));
}
