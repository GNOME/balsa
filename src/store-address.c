/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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
#include <string.h>

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
enum StoreAddressResponse {
    SA_RESPONSE_SAVE = 1,
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

    if (info->entries_list) {
        GtkNotebook *notebook = GTK_NOTEBOOK(info->notebook);
        gint response;

        /* response ==  0 => OK
         * response ==  1 => save
         * response ==  2 => close
         * response == -1    if user closed dialog using the window
         *                   decorations */
        while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) 
               == GTK_RESPONSE_OK || response == SA_RESPONSE_SAVE) {
            gint page = gtk_notebook_get_current_page(notebook);
            GList *list = g_list_nth(info->entries_list, page);
            store_address_from_entries(info, list->data);
            if (response == GTK_RESPONSE_OK)
                break;
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
        gtk_dialog_new_with_buttons(_("Store Address"),
                                    GTK_WINDOW(balsa_app.main_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    _("_Save"),  SA_RESPONSE_SAVE,
                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                    NULL);
    GtkWidget *vbox = GTK_DIALOG(dialog)->vbox;
    GtkWidget *frame;

    frame = store_address_book_frame(info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
    frame = store_address_note_frame(info);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), 
                       gtk_label_new(_("Save this address "
                                       "and close the dialog?")),
                       TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    return dialog;
}

/* store_address_from_entries:
 * make the actual address book entry */
static void
store_address_from_entries(struct store_address_info * info,
                           GtkWidget ** entries)
{
    LibBalsaAddress *address;
    gint cnt;

    if (info->current_address_book == NULL) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
    		      _("No address book selected...."));
        return;
    }

    /* FIXME: This problem should be solved in the VCard implementation in libbalsa */
    /* semicolons mess up how GnomeCard processes the fields, so disallow them */
    for (cnt = 0; cnt < NUM_FIELDS; cnt++) {
        const gchar *entry_str =
            gtk_entry_get_text(GTK_ENTRY(entries[cnt]));

        if (strchr(entry_str, ';')) {
            balsa_information(LIBBALSA_INFORMATION_ERROR,
                              _("Sorry, no semicolons are allowed "
                                "in the name!\n"));

            gtk_editable_select_region(GTK_EDITABLE(entries[cnt]), 0, -1);

            gtk_widget_grab_focus(GTK_WIDGET(entries[cnt]));

            return;
        }
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
    address->nick_name =
        g_strstrip(gtk_editable_get_chars
    	       (GTK_EDITABLE(entries[NICK_NAME]), 0, -1));
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

    g_object_unref(address);
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
	    gtk_menu_shell_append(GTK_MENU_SHELL(ab_menu), menu_item);

            info->address_book = address_book;
	    g_signal_connect(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(store_address_book_menu_cb), info);

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
    GList *list, *l = balsa_index_selected_list(info->index);

    info->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(info->notebook), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(info->notebook), 5);
    gtk_container_add(GTK_CONTAINER(frame), info->notebook);

    for (list = g_list_last(l); list; list = g_list_previous(list)) {
        message = list->data;
        if (message->from)
            store_address_add_address(info, _("From:"), message->from);
        store_address_add_glist(info, _("To:"), message->to_list);
        store_address_add_glist(info, _("Cc:"), message->cc_list);
        store_address_add_glist(info, _("Bcc:"), message->bcc_list);
    }
    g_list_free(l);
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
	N_("_Displayed Name:"),
	N_("_First Name:"),
	N_("_Middle Name:"),
	N_("_Last Name:"),
	N_("_Nickname:"),
	N_("O_rganization:"),
	N_("_Email Address:")
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
	label = gtk_label_new_with_mnemonic(_(labels[cnt]));
	entries[cnt] = gtk_entry_new();
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entries[cnt]);

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
    if (g_utf8_strlen(label_text, -1) > 10)
        /* truncate to an arbitrary length: */
        *g_utf8_offset_to_pointer(label_text, 10) = '\0';
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
