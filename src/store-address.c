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
#include "store-address.h"

#include "libbalsa.h"

/* global data */
typedef struct _StoreAddressInfo StoreAddressInfo;
struct _StoreAddressInfo {
    LibBalsaAddressBook *address_book;
    LibBalsaAddressBook *current_address_book;
    GList *message_list;
    GList *entries_list;
    GtkWidget *notebook;
    GtkWidget *dialog;
};
enum StoreAddressResponse {
    SA_RESPONSE_SAVE = 1,
};

/* statics */
GtkWidget *store_address_dialog(StoreAddressInfo * info);
static void store_address_weak_notify(StoreAddressInfo * info,
                                      gpointer message);
static void store_address_response(GtkWidget * dialog, gint response,
                                   StoreAddressInfo *info);
static void store_address_free(StoreAddressInfo * info);
static void store_address_from_entries(StoreAddressInfo * info,
                                       GtkWidget ** entries);
static GtkWidget *store_address_book_frame(StoreAddressInfo * info);
static GtkWidget *store_address_note_frame(StoreAddressInfo * info);
static void store_address_book_menu_cb(GtkWidget * widget, 
                                       StoreAddressInfo * info);
static void store_address_add_address(StoreAddressInfo * info,
                                      const gchar * label,
                                      LibBalsaAddress * address);
static void store_address_add_glist(StoreAddressInfo * info,
                                    const gchar * label, GList * list);

/* 
 * public interface: balsa_store_address
 */
#define BALSA_STORE_ADDRESS_KEY "balsa-store-address"
void
balsa_store_address(GList * messages)
{
    StoreAddressInfo *info = NULL;
    GList *message_list = NULL;
    GList *list;

    for (list = messages; list; list = g_list_next(list)) {
        gpointer data = g_object_get_data(G_OBJECT(list->data),
                                          BALSA_STORE_ADDRESS_KEY);

        if (data)
            info = data;
        else
            message_list = g_list_prepend(message_list, list->data);
    }

    if (!message_list) {
        /* All messages are already showing. */
        if (info)
            gdk_window_raise(info->dialog->window);
        return;
    }

    info = g_new(StoreAddressInfo, 1);
    info->current_address_book = NULL;
    info->message_list = message_list;
    info->entries_list = NULL;
    info->dialog = store_address_dialog(info);

    if (!info->entries_list) {
        gnome_appbar_set_status(balsa_app.appbar,
                                _("Store address: no addresses"));
        store_address_free(info);
        return;
    }

    for (list = message_list; list; list = g_list_next(list)) {
        g_object_set_data(G_OBJECT(list->data),
                          BALSA_STORE_ADDRESS_KEY, info);
        g_object_weak_ref(G_OBJECT(list->data),
                          (GWeakNotify) store_address_weak_notify, info);
    }

    g_signal_connect(G_OBJECT(info->dialog), "response",
                     G_CALLBACK(store_address_response), info);
    gtk_widget_show_all(GTK_WIDGET(info->dialog));
}

/* Weak notify that a message was deleted; remove it from our list. */
static void
store_address_weak_notify(StoreAddressInfo * info, gpointer message)
{
    info->message_list = g_list_remove(info->message_list, message);
    if (!info->message_list)
        gtk_dialog_response(GTK_DIALOG(info->dialog), GTK_RESPONSE_NONE);
}

/* Response signal handler for the dialog. */
static void
store_address_response(GtkWidget * dialog, gint response,
                       StoreAddressInfo * info)
{
    GtkNotebook *notebook = GTK_NOTEBOOK(info->notebook);
    GList *list;

    /* response ==  0 => OK
     * response ==  1 => save
     * response ==  2 => close
     * response == -1    if user closed dialog using the window
     *                   decorations */
    if (response == GTK_RESPONSE_OK || response == SA_RESPONSE_SAVE) {
        /* Save the current address. */
        gint page = gtk_notebook_get_current_page(notebook);
        GList *list = g_list_nth(info->entries_list, page);
        store_address_from_entries(info, list->data);
        if (response == SA_RESPONSE_SAVE)
            /* Keep the dialog open. */
            return;
    }

    /* Let go of remaining messages. */
    for (list = info->message_list; list; list = g_list_next(list)) {
        g_object_set_data(G_OBJECT(list->data), BALSA_STORE_ADDRESS_KEY,
                          NULL);
        g_object_weak_unref(G_OBJECT(list->data),
                            (GWeakNotify) store_address_weak_notify, info);
    }
    g_list_foreach(info->entries_list, (GFunc) g_free, NULL);
    g_list_free(info->entries_list);
    store_address_free(info);
}

/* Clean up when we're done, or if there's nothing to do. */
static void
store_address_free(StoreAddressInfo * info)
{
    g_list_free(info->message_list);
    gtk_widget_destroy(info->dialog);
    g_free(info);
}

/* store_address_dialog:
 * create the main dialog */
GtkWidget *
store_address_dialog(StoreAddressInfo * info)
{
    GtkWidget *dialog =
        gtk_dialog_new_with_buttons(_("Store Address"),
                                    GTK_WINDOW(balsa_app.main_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                    GTK_STOCK_SAVE,  SA_RESPONSE_SAVE,
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
    return dialog;
}

/* store_address_from_entries:
 * make the actual address book entry */
static void
store_address_from_entries(StoreAddressInfo * info,
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
store_address_book_frame(StoreAddressInfo * info)
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
	    gtk_menu_shell_append(GTK_MENU_SHELL(ab_menu), menu_item);

            info->address_book = address_book;
	    g_signal_connect(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(store_address_book_menu_cb), info);

	    if (address_book == balsa_app.default_address_book)
		gtk_menu_set_active(GTK_MENU(ab_menu), default_ab_offset);

	    default_ab_offset++;

	    ab_list = g_list_next(ab_list);
	}
    }
    ab_option = gtk_option_menu_new();
    gtk_option_menu_set_menu(GTK_OPTION_MENU(ab_option), ab_menu);
    gtk_container_add(GTK_CONTAINER(frame), ab_option);
    return frame;
}

/* store_address_note_frame:
 * create the frame containing the notebook with address information */
static GtkWidget *
store_address_note_frame(StoreAddressInfo *info)
{
    GtkWidget *frame = gtk_frame_new(_("Choose Address"));
    LibBalsaMessage *message;
    GList *list;

    info->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(info->notebook), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(info->notebook), 5);
    gtk_container_add(GTK_CONTAINER(frame), info->notebook);

    for (list = info->message_list; list; list = g_list_next(list)) {
        message = LIBBALSA_MESSAGE(list->data);
	if (message->headers) {
	    if (message->headers->from)
		store_address_add_address(info, _("From:"), message->headers->from);
	    store_address_add_glist(info, _("To:"), message->headers->to_list);
	    store_address_add_glist(info, _("Cc:"), message->headers->cc_list);
	    store_address_add_glist(info, _("Bcc:"), message->headers->bcc_list);
	}
    }

    return frame;
}

/* store_address_book_menu_cb:
 * callback for the address book menu */
static void
store_address_book_menu_cb(GtkWidget * widget, 
                           StoreAddressInfo * info)
{
    info->current_address_book = info->address_book;
}

/* store_address_add_address:
 * make a new page in the notebook */
static void
store_address_add_address(StoreAddressInfo * info,
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
store_address_add_glist(StoreAddressInfo * info,
                        const gchar * label, GList * list)
{
    while (list) {
        store_address_add_address(info, label, list->data);
        list = g_list_next(list);
    }
}
