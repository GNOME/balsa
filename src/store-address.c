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
#include "balsa-index.h"
#include "store-address.h"

#include "libbalsa.h"

/* global data */
struct store_address_info {
    LibBalsaAddressBook *address_book;
    LibBalsaAddressBook *current_address_book;
    GList *entries_list;
    GtkWidget *notebook;
    BalsaIndex *index;
};

/* statics */
GtkWidget *store_address_dialog(struct store_address_info * info);
static void store_address_from_entries(struct store_address_info * info,
                                       GtkWidget ** entries);
static GtkWidget *store_address_book_frame(struct store_address_info * info);
static GtkWidget *store_address_note_frame(struct store_address_info * info);
static void store_address_book_menu_cb(GtkWidget * widget, 
                                       struct store_address_info * info);
static void store_address_add_address(struct store_address_info * info,
                                      const gchar * label,
                                      LibBalsaAddress * address);
static void store_address_add_glist(struct store_address_info * info,
                                    const gchar * label, GList * list);

/* 
 * public interface: balsa_store_address
 */
void
balsa_store_address(GtkWidget * widget, gpointer user_data)
{
    GtkWidget *dialog;
    struct store_address_info *info;

    g_return_if_fail(widget != NULL);
    g_return_if_fail(user_data != NULL);


    info = g_new(struct store_address_info, 1);
    info->index = BALSA_INDEX(user_data);
    info->entries_list = NULL;
    info->current_address_book = NULL;
    dialog = store_address_dialog(info);

    gnome_dialog_close_hides(GNOME_DIALOG(dialog), TRUE);
    if (info->entries_list) {
        GtkNotebook *notebook = GTK_NOTEBOOK(info->notebook);
        while (gnome_dialog_run(GNOME_DIALOG(dialog)) == 0) {
            gint page = gtk_notebook_get_current_page(notebook);
            GList *list = g_list_nth(info->entries_list, page);
            store_address_from_entries(info, list->data);
        }
        g_list_foreach(info->entries_list, (GFunc) g_free, NULL);
        g_list_free(info->entries_list);
    } else
        gnome_appbar_set_status(balsa_app.appbar,
                                _("Store address: no addresses"));
    gtk_widget_destroy(dialog);
    g_free(info);
}

/* store_address_dialog:
 * create the main dialog */
GtkWidget *
store_address_dialog(struct store_address_info * info)
{
    GtkWidget *dialog =
        gnome_dialog_new(_("Store Address"), _("Save in address book"),
                         GNOME_STOCK_BUTTON_CLOSE, NULL);
    GtkWidget *vbox = GNOME_DIALOG(dialog)->vbox;
    GtkWidget *frame;

    frame = store_address_book_frame(info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
    frame = store_address_note_frame(info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    return dialog;
}

/* store_address_from_entries:
 * make the actual address book entry */
static void
store_address_from_entries(struct store_address_info * info,
                           GtkWidget ** entries)
{
    LibBalsaAddress *address = NULL;
    GtkWidget *box = NULL;
    gchar *msg = NULL;
    gint cnt = 0;
    gint cnt2 = 0;
    gchar *entry_str = NULL;
    gint entry_str_len = 0;

    if (info->current_address_book == NULL) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
    		      _("No address book selected...."));
        return;
    }

    /* FIXME: This problem should be solved in the VCard implementation in libbalsa */
    /* semicolons mess up how GnomeCard processes the fields, so disallow them */
    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
        entry_str = gtk_editable_get_chars(GTK_EDITABLE(entries[cnt]), 0, -1);
        entry_str_len = strlen(entry_str);

        for (cnt2 = 0; cnt2 < entry_str_len; cnt2++) {
    	if (entry_str[cnt2] == ';') {
    	    msg = _("Sorry, no semicolons are allowed in the name!\n");

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
    address->middle_name =
        g_strstrip(gtk_editable_get_chars
    	       (GTK_EDITABLE(entries[MIDDLE_NAME]), 0, -1));
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

    libbalsa_address_book_store_address(info->current_address_book,
                                        address);

    gtk_object_destroy(GTK_OBJECT(address));
}

/* store_address_book_frame:
 * create the frame containing the address book menu */
static GtkWidget *
store_address_book_frame(struct store_address_info * info)
{
    GList *ab_list;
    GtkWidget *frame = gtk_frame_new(_("Choose Address Book"));
    GtkWidget *ab_option, *menu_item, *ab_menu;
    LibBalsaAddressBook *address_book;
    guint default_ab_offset = 0;

    ab_menu = gtk_menu_new();
    if (balsa_app.address_book_list) {
	info->current_address_book = balsa_app.default_address_book;

	ab_list = balsa_app.address_book_list;
	while (ab_list) {
	    address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
	    if (info->current_address_book == NULL)
		info->current_address_book = address_book;

	    menu_item = gtk_menu_item_new_with_label(address_book->name);
	    gtk_widget_show(menu_item);
	    gtk_menu_append(GTK_MENU(ab_menu), menu_item);

            info->address_book = address_book;
	    gtk_signal_connect(GTK_OBJECT(menu_item), "activate",
			       store_address_book_menu_cb, info);

	    if (address_book == balsa_app.default_address_book)
		gtk_menu_set_active(GTK_MENU(ab_menu), default_ab_offset);

	    default_ab_offset++;

	    ab_list = g_list_next(ab_list);
	}
	gtk_widget_show(ab_menu);
    }
    ab_option = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(ab_option), ab_menu);
    gtk_widget_show(ab_option);
    gtk_container_add(GTK_CONTAINER(frame), ab_option);
    return frame;
}

/* store_address_note_frame:
 * create the frame containing the notebook with address information */
static GtkWidget *
store_address_note_frame(struct store_address_info *info)
{
    GtkWidget *frame = gtk_frame_new(_("Choose Address"));
    LibBalsaMessage *message;
    GList *list = GTK_CLIST(info->index->ctree)->selection;

    info->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(info->notebook), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(info->notebook), 5);
    gtk_container_add(GTK_CONTAINER(frame), info->notebook);

    list = g_list_last(list);
    while (list) {
        message = gtk_ctree_node_get_row_data(info->index->ctree,
                                              list->data);
        if (message->from)
            store_address_add_address(info, _("From:"), message->from);
        store_address_add_glist(info, _("To:"), message->to_list);
        store_address_add_glist(info, _("Cc:"), message->cc_list);
        store_address_add_glist(info, _("Bcc:"), message->bcc_list);
        list = g_list_previous(list);
    }
    return frame;
}

/* store_address_book_menu_cb:
 * callback for the address book menu */
static void
store_address_book_menu_cb(GtkWidget * widget, 
                           struct store_address_info * info)
{
    info->current_address_book = info->address_book;
}

/* store_address_add_address:
 * make a new page in the notebook */
static void
store_address_add_address(struct store_address_info * info,
                          const gchar * lab, LibBalsaAddress * address)
{
    gchar *text;
    gchar *label_text;
    GtkWidget *vbox;
    GtkWidget *table;
    GtkWidget *label;
    GtkWidget **entries = NULL;
    gint cnt;
    gint cnt2;

    gchar *labels[NUM_FIELDS] = {
	N_("Card Name:"),
	N_("First Name:"),
	N_("Middle Name:"),
	N_("Last Name:"),
	N_("Organization:"),
	N_("Email Address:")
    };

    gchar **names;

    gchar *new_name = NULL;
    gchar *new_email = NULL;
    gchar *new_organization = NULL;
    gchar *first_name = NULL;
    gchar *middle_name = NULL;
    gchar *last_name = NULL;
    gchar *carrier = NULL;

    if (address == NULL)
        return;

    vbox = gtk_vbox_new(FALSE, 0);

    new_email = g_strdup(address->address_list->data);

    /* initialize the organization... */
    if (address->organization == NULL)
	new_organization = g_strdup("");
    else
	new_organization = g_strdup(address->organization);

    /* if the message only contains an e-mail address */
    if (address->full_name == NULL)
	new_name = g_strdup(new_email);
    else {
	/* make sure address->personal is not all whitespace */
	new_name = g_strstrip(g_strdup(address->full_name));

	/* guess the first name, middle name and last name */
	if (*new_name != '\0') {
	    names = g_strsplit(new_name, " ", 0);

	    cnt = 0;
	    while (names[cnt])
		cnt++;

	    /* get first name */
	    first_name = g_strdup(names[0]);

	    /* get last name */
	    if (cnt == 1)
		last_name = g_strdup("");
	    else
		last_name = g_strdup(names[cnt - 1]);

	    /* get middle name */
	    middle_name = g_strdup("");

	    cnt2 = 1;
	    if (cnt > 2)
		while (cnt2 != cnt - 1) {
		    carrier = middle_name;
		    middle_name = g_strconcat(middle_name, names[cnt2++], NULL);
		    g_free(carrier);

		    if (cnt2 != cnt - 1) {
			carrier = middle_name;
			middle_name = g_strconcat(middle_name, " ", NULL);
			g_free(carrier);
		    }
		}

	    g_strfreev(names);
	}
    }

    if (first_name == NULL)
	first_name = g_strdup("");
    if (middle_name == NULL)
	middle_name = g_strdup("");
    if (last_name == NULL)
	last_name = g_strdup("");

    entries = g_new(GtkWidget *, NUM_FIELDS);
    info->entries_list = g_list_append(info->entries_list, entries);

    table = gtk_table_new(5, 2, FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(table), 3);
    gtk_box_pack_start(GTK_BOX(vbox), table, TRUE, TRUE, 0);

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
    gtk_entry_set_text(GTK_ENTRY(entries[MIDDLE_NAME]), middle_name);
    gtk_entry_set_text(GTK_ENTRY(entries[LAST_NAME]), last_name);
    gtk_entry_set_text(GTK_ENTRY(entries[EMAIL_ADDRESS]), new_email);
    gtk_entry_set_text(GTK_ENTRY(entries[ORGANIZATION]), new_organization);

    gtk_editable_select_region(GTK_EDITABLE(entries[FULL_NAME]), 0, -1);

    for (cnt = FULL_NAME + 1; cnt < NUM_FIELDS; cnt++)
        gtk_editable_set_position(GTK_EDITABLE(entries[cnt]), 0);

    g_free(new_name);
    g_free(first_name);
    g_free(middle_name);
    g_free(last_name);
    g_free(new_email);
    g_free(new_organization);

    text = libbalsa_address_to_gchar(address, 0);
    label_text = g_strconcat(lab, text, NULL);
    g_free(text);
    if (strlen(label_text) > 10)
        /* truncate to an arbitrary length: */
        label_text[10] = '\0';
    gtk_notebook_append_page(GTK_NOTEBOOK(info->notebook), vbox,
                             gtk_label_new(label_text));
    g_free(label_text);
}

/* store_address_add_glist:
 * take a GList of addresses and pass them one at a time to
 * store_address_add_address */
static void
store_address_add_glist(struct store_address_info * info,
                        const gchar * label, GList * list)
{
    while (list) {
        store_address_add_address(info, label, list->data);
        list = g_list_next(list);
    }
}
