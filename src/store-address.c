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
#include "option_menu.h"

/* global data */
typedef struct _StoreAddressInfo StoreAddressInfo;
struct _StoreAddressInfo {
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
static gboolean store_address_from_entries(GtkWindow *window,
                                           StoreAddressInfo * info,
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
        gboolean successful = 
            store_address_from_entries(GTK_WINDOW(dialog), info, list->data);
        if (response == SA_RESPONSE_SAVE || !successful)
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
static gboolean
store_address_from_entries(GtkWindow *window, StoreAddressInfo * info,
                           GtkWidget ** entries)
{
    LibBalsaAddress *address;
    LibBalsaABErr rc;

    if (info->current_address_book == NULL) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
    		      _("No address book selected...."));
        return FALSE;
    }

    address = libbalsa_address_new_from_edit_entries(entries);
    rc = libbalsa_address_book_add_address(info->current_address_book,
                                           address);
    if(rc != LBABERR_OK) {
        const gchar *msg =
            libbalsa_address_book_strerror(info->current_address_book, rc);
        if(!msg) {
            switch(rc) {
            case LBABERR_CANNOT_WRITE: 
                msg = _("Address could not be written to this address book.");
                break;
            case LBABERR_CANNOT_CONNECT:
                msg = _("Address book could not be accessed."); break;
            case LBABERR_DUPLICATE:
                msg = _("This mail address is already in this address book.");
                break;
            default:
                msg = _("Unexpected address book error. Report it."); break;
            }
        }
        balsa_information_parented(window, LIBBALSA_INFORMATION_ERROR, msg);
    }
    
    g_object_unref(address);
    return rc == LBABERR_OK;
}

/* store_address_book_frame:
 * create the frame containing the address book menu */
static GtkWidget *
store_address_book_frame(StoreAddressInfo * info)
{
    GList *ab_list;
    GtkWidget *frame = gtk_frame_new(_("Choose Address Book"));
    GtkWidget *ab_option;
    GObject *ab_menu;
    LibBalsaAddressBook *address_book;

    ab_menu = libbalsa_option_menu_new();
    if (balsa_app.address_book_list) {
        guint active_idx, curr_idx;
	info->current_address_book = balsa_app.default_address_book;

	curr_idx = active_idx = 0;
	for(ab_list = balsa_app.address_book_list;
            ab_list; 
            ab_list = g_list_next(ab_list)) {
	    address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
#if 1
	    if (address_book == balsa_app.default_address_book)
                active_idx = curr_idx;
            libbalsa_option_menu_append(ab_menu, address_book->name, 
                                        address_book);
#else
	    menu_item = gtk_menu_item_new_with_label(address_book->name);
	    gtk_menu_shell_append(GTK_MENU_SHELL(ab_menu), menu_item);

	    g_signal_connect(G_OBJECT(menu_item), "activate",
			     G_CALLBACK(store_address_book_menu_cb), info);
            g_object_set_data(G_OBJECT(menu_item),"address-book",
                              GUINT_TO_POINTER(default_ab_offset));

	    if (address_book == balsa_app.default_address_book)
		gtk_menu_set_active(GTK_MENU(ab_menu), default_ab_offset);
#endif
	    curr_idx++;

	}
    }
    info->current_address_book = balsa_app.default_address_book;
    ab_option = libbalsa_option_menu_get_widget
        (ab_menu, G_CALLBACK(store_address_book_menu_cb), info);
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
    LibBalsaAddressBook *addr;
    addr = LIBBALSA_ADDRESS_BOOK(libbalsa_option_menu_get_active_data(widget));
    g_return_if_fail(addr);
    info->current_address_book = addr;
}

/* store_address_add_address:
 * make a new page in the notebook */
static void
store_address_add_address(StoreAddressInfo * info,
                          const gchar * lab, LibBalsaAddress * address)
{
    gchar *text;
    gchar *label_text;
    GtkWidget **entries, *ew;

    if (address == NULL)
        return;

    entries = g_new(GtkWidget *, NUM_FIELDS);
    info->entries_list = g_list_append(info->entries_list, entries);
    ew = libbalsa_address_get_edit_widget(address, entries);

    text = libbalsa_address_to_gchar(address, 0);
    label_text = g_strconcat(lab, text, NULL);
    g_free(text);
    if (g_utf8_strlen(label_text, -1) > 10)
        /* truncate to an arbitrary length: */
        *g_utf8_offset_to_pointer(label_text, 10) = '\0';
    gtk_notebook_append_page(GTK_NOTEBOOK(info->notebook), ew,
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
