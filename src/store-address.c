/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2000 Stuart Parmenter and others,
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

#include <gnome.h>

#include "balsa-app.h"
#include "store-address.h"

#include "libbalsa.h"

static LibBalsaAddressBook *current_address_book;

static void store_address_dialog_button_clicked_cb(GtkWidget * widget,
						   gint which,
						   GtkWidget ** entries);
static gint store_address_dialog_close(GtkWidget * widget,
				       GtkWidget ** entries);

static void address_book_menu_cb(GtkWidget * widget, gpointer data);

void
balsa_store_address(GtkWidget * widget, gpointer index)
{
    GList *list = NULL;
    LibBalsaMessage *message = NULL;
    GtkWidget *dialog = NULL;
    GtkWidget *frame = NULL;
    GtkWidget *table = NULL;
    GtkWidget *label = NULL;
    GtkWidget **entries = NULL;
    gint cnt = 0;
    gchar *labels[NUM_FIELDS] = { N_("Card Name:"), N_("First Name:"),
	N_("Last Name:"), N_("Organization:"),
	N_("Email Address:")
    };
    gchar **names;

    gchar *new_name = NULL;
    gchar *new_email = NULL;
    gchar *first_name = NULL;
    gchar *last_name = NULL;
    GtkWidget *ab_option, *menu_item, *ab_menu;
    GList *ab_list;
    LibBalsaAddressBook *address_book;
    guint default_ab_offset = 0;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(index != NULL);

    list = GTK_CLIST(index)->selection;

    if (list == NULL) {
	GtkWidget *box = NULL;
	char *msg =
	    _
	    ("In order to store an address, you must select a message.\n");
	box =
	    gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR,
				  GNOME_STOCK_BUTTON_OK, NULL);
	gtk_window_set_modal(GTK_WINDOW(box), TRUE);
	gnome_dialog_run_and_close(GNOME_DIALOG(box));
	return;
    }

    if (list->next) {
	GtkWidget *box = NULL;
	char *msg = _("You may only store one address at a time.\n");
	box = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR,
				    GNOME_STOCK_BUTTON_OK, NULL);
	gtk_window_set_modal(GTK_WINDOW(box), TRUE);
	gnome_dialog_run_and_close(GNOME_DIALOG(box));
	return;
    }

    message = gtk_clist_get_row_data(GTK_CLIST(index),
				     GPOINTER_TO_INT(list->data));

    if (message->from->address_list == NULL) {
	GtkWidget *box = NULL;
	char *msg = _("This message doesn't contain an e-mail address.\n");
	box = gnome_message_box_new(msg, GNOME_MESSAGE_BOX_ERROR,
				    GNOME_STOCK_BUTTON_OK, NULL);
	gtk_window_set_modal(GTK_WINDOW(box), TRUE);
	gnome_dialog_run_and_close(GNOME_DIALOG(box));
	return;
    }

    /* FIXME: Handle more than just the one address... */
    new_email = g_strdup(message->from->address_list->data);

    if (message->from->full_name == NULL) {
	/* if the message only contains an e-mail address */
	new_name = g_strdup(new_email);
    } else {
	/* make sure message->from->personal is not all whitespace */
	new_name = g_strstrip(g_strdup(message->from->full_name));

	if (strlen(new_name) == 0) {
	    first_name = g_strdup("");
	    last_name = g_strdup("");
	} else {
	    /* guess the first name and last name */
	    names = g_strsplit(new_name, " ", 0);
	    first_name = g_strdup(names[0]);
	    /* get last name */
	    cnt = 0;
	    while (names[cnt])
		cnt++;

	    if (cnt == 1)
		last_name = g_strdup("");
	    else
		last_name = g_strdup(names[cnt - 1]);

	    g_strfreev(names);
	}
    }

    if (!first_name)
	first_name = g_strdup("");
    if (!last_name)
	last_name = g_strdup("");

    entries = g_new(GtkWidget *, NUM_FIELDS);

    dialog = gnome_dialog_new(_("Store Address"), GNOME_STOCK_BUTTON_OK,
			      GNOME_STOCK_BUTTON_CANCEL, NULL);

    frame = gtk_frame_new(_("Contact Information"));
    gtk_box_pack_start(GTK_BOX(GNOME_DIALOG(dialog)->vbox), frame, TRUE,
		       TRUE, 0);

    table = gtk_table_new(5, 2, FALSE);
    gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_container_set_border_width(GTK_CONTAINER(table), 3);

    ab_menu = gtk_menu_new();
    if (balsa_app.address_book_list) {
	current_address_book = balsa_app.default_address_book;

	ab_list = balsa_app.address_book_list;
	while (ab_list) {
	    address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
	    if (current_address_book == NULL)
		current_address_book = address_book;

	    menu_item = gtk_menu_item_new_with_label(address_book->name);
	    gtk_widget_show(menu_item);
	    gtk_menu_append(GTK_MENU(ab_menu), menu_item);

	    gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
			       address_book_menu_cb, address_book);
	    if (address_book == balsa_app.default_address_book) {
		gtk_menu_set_active(GTK_MENU(ab_menu), default_ab_offset);
	    }
	    default_ab_offset++;

	    ab_list = g_list_next(ab_list);
	}
	gtk_widget_show(ab_menu);
    }
    ab_option = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(ab_option), ab_menu);
    gtk_widget_show(ab_option);
    gtk_table_attach(GTK_TABLE(table), ab_option, 0, 2, 0, 1,
		     GTK_FILL | GTK_EXPAND, GTK_FILL, 4, 4);

    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
	label = gtk_label_new(_(labels[cnt]));
	entries[cnt] = gtk_entry_new();

	gtk_table_attach(GTK_TABLE(table), label, 0, 1, cnt + 1, cnt + 2,
			 GTK_FILL, GTK_FILL, 4, 4);

	gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);

	gtk_table_attach(GTK_TABLE(table), entries[cnt], 1, 2, cnt + 1,
			 cnt + 2, GTK_FILL | GTK_EXPAND,
			 GTK_FILL | GTK_EXPAND, 2, 2);
    }

    gtk_entry_set_text(GTK_ENTRY(entries[FULL_NAME]), new_name);
    gtk_entry_set_text(GTK_ENTRY(entries[FIRST_NAME]), first_name);
    gtk_entry_set_text(GTK_ENTRY(entries[LAST_NAME]), last_name);
    gtk_entry_set_text(GTK_ENTRY(entries[EMAIL_ADDRESS]), new_email);

    gtk_editable_select_region(GTK_EDITABLE(entries[FULL_NAME]), 0, -1);

    gnome_dialog_set_default(GNOME_DIALOG(dialog), 0);

    gtk_signal_connect(GTK_OBJECT(dialog), "clicked",
		       GTK_SIGNAL_FUNC
		       (store_address_dialog_button_clicked_cb), entries);
    gtk_signal_connect(GTK_OBJECT(dialog), "close",
		       GTK_SIGNAL_FUNC(store_address_dialog_close),
		       entries);

    gtk_widget_show_all(dialog);

    g_free(new_name);
    g_free(first_name);
    g_free(last_name);
    g_free(new_email);
}

static void
store_address_dialog_button_clicked_cb(GtkWidget * widget, gint which,
				       GtkWidget ** entries)
{
    if (which == 0) {
	LibBalsaAddress *address = NULL;
	GtkWidget *box = NULL;
	gchar *msg = NULL;
	gint cnt = 0;
	gint cnt2 = 0;
	gchar *entry_str = NULL;
	gint entry_str_len = 0;

	if (current_address_book == NULL) {
	    balsa_information(LIBBALSA_INFORMATION_WARNING,
			      _("No address book selected...."));
	    return;
	}

	/* FIXME: This problem should be solved in the VCard implementation in libbalsa */
	/* semicolons mess up how GnomeCard processes the fields, so disallow them */
	for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
	    entry_str =
		gtk_editable_get_chars(GTK_EDITABLE(entries[cnt]), 0, -1);
	    entry_str_len = strlen(entry_str);

	    for (cnt2 = 0; cnt2 < entry_str_len; cnt2++) {
		if (entry_str[cnt2] == ';') {
		    msg =
			_
			("Sorry, no semicolons are allowed in the name!\n");
		    gtk_editable_select_region(GTK_EDITABLE(entries[cnt]),
					       0, -1);
		    gtk_widget_grab_focus(GTK_WIDGET(entries[cnt]));
		    box = gnome_message_box_new(msg,
						GNOME_MESSAGE_BOX_ERROR,
						GNOME_STOCK_BUTTON_OK,
						NULL);
		    gtk_window_set_modal(GTK_WINDOW(box), TRUE);
		    gnome_dialog_run_and_close(GNOME_DIALOG(box));
		    g_free(entry_str);
		    return;
		}
	    }
	    g_free(entry_str);
	}

	address = libbalsa_address_new();
	address->full_name =
	    g_strstrip(gtk_editable_get_chars
		       (GTK_EDITABLE(entries[FULL_NAME]), 0, -1));
	address->first_name =
	    g_strstrip(gtk_editable_get_chars
		       (GTK_EDITABLE(entries[FIRST_NAME]), 0, -1));
	address->last_name =
	    g_strstrip(gtk_editable_get_chars
		       (GTK_EDITABLE(entries[LAST_NAME]), 0, -1));
	address->organization =
	    g_strstrip(gtk_editable_get_chars
		       (GTK_EDITABLE(entries[ORGANIZATION]), 0, -1));
	address->address_list =
	    g_list_append(address->address_list,
			  g_strstrip(gtk_editable_get_chars
				     (GTK_EDITABLE(entries[EMAIL_ADDRESS]),
				      0, -1)));

	libbalsa_address_book_store_address(current_address_book, address);

	gtk_object_destroy(GTK_OBJECT(address));
    }
    gnome_dialog_close(GNOME_DIALOG(widget));

}


static gint
store_address_dialog_close(GtkWidget * widget, GtkWidget ** entries)
{
    g_free(entries);
    return FALSE;
}

static void
address_book_menu_cb(GtkWidget * widget, gpointer data)
{
    current_address_book = LIBBALSA_ADDRESS_BOOK(data);
}
