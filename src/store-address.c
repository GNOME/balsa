/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "store-address.h"

#include <string.h>
#include <glib/gi18n.h>

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#include "balsa-app.h"

/* global data */
typedef struct _StoreAddressInfo StoreAddressInfo;
struct _StoreAddressInfo {
    LibBalsaAddressBook *current_address_book;
    GList *message_list;
    GList *entries_list;
    GtkWidget *notebook;
    GtkWidget *dialog;
};

/* statics */
static GtkWidget *store_address_dialog(StoreAddressInfo * info);
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
                                      InternetAddress * address,
                                      InternetAddress * group);
static void store_address_add_lbaddress(StoreAddressInfo * info,
                                        const LibBalsaAddress *address);
static void store_address_add_list(StoreAddressInfo * info,
                                   const gchar * label,
				   InternetAddressList * list);

/* 
 * public interface: balsa_store_address
 */
#define BALSA_STORE_ADDRESS_KEY "balsa-store-address"
void
balsa_store_address_from_messages(GList * messages)
{
    StoreAddressInfo *info = NULL;
    GList *message_list = NULL;
    GList *list;

    for (list = messages; list; list = list->next) {
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
            gtk_window_present(GTK_WINDOW(info->dialog));
        return;
    }

    info = g_new(StoreAddressInfo, 1);
    info->current_address_book = NULL;
    info->message_list = message_list;
    info->entries_list = NULL;
    info->dialog = store_address_dialog(info);

    if (!info->entries_list) {
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Store address: no addresses"));
        store_address_free(info);
        return;
    }

    for (list = message_list; list; list = list->next) {
        g_object_set_data(G_OBJECT(list->data),
                          BALSA_STORE_ADDRESS_KEY, info);
        g_object_weak_ref(G_OBJECT(list->data),
                          (GWeakNotify) store_address_weak_notify, info);
    }

    g_signal_connect(G_OBJECT(info->dialog), "response",
                     G_CALLBACK(store_address_response), info);
    gtk_widget_show(GTK_WIDGET(info->dialog));
}

void
balsa_store_address(const LibBalsaAddress *address)
{
    StoreAddressInfo *info = NULL;

    info = g_new(StoreAddressInfo, 1);
    info->current_address_book = NULL;
    info->message_list = NULL;
    info->entries_list = NULL;
    info->dialog = store_address_dialog(info);

    store_address_add_lbaddress(info, address);

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
    if (response == GTK_RESPONSE_OK) {
        /* Save the current address. */
        gint page;
        GtkWidget **entries;

        page = gtk_notebook_get_current_page(notebook);
        entries = g_list_nth_data(info->entries_list, page);
        if (!store_address_from_entries(GTK_WINDOW(dialog), info, entries))
            /* Keep the dialog open. */
            return;
    }

    /* Let go of remaining messages. */
    for (list = info->message_list; list; list = list->next) {
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
static GtkWidget *
store_address_dialog(StoreAddressInfo * info)
{
    GtkWidget *dialog;
    GtkWidget *vbox;
    GtkWidget *frame;

    dialog =
        gtk_dialog_new_with_buttons(_("Store Address"),
                                    GTK_WINDOW(balsa_app.main_window),
                                    GTK_DIALOG_DESTROY_WITH_PARENT |
                                    libbalsa_dialog_flags(),
                                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                                    _("_OK"), GTK_RESPONSE_OK,
                                    NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(gtk_dialog_get_widget_for_response
                          (GTK_DIALOG(dialog), GTK_RESPONSE_OK));
    vbox = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, GTK_WINDOW(balsa_app.main_window));
#endif

    info->current_address_book = balsa_app.default_address_book;
    if (balsa_app.address_book_list && balsa_app.address_book_list->next) {
        /* User has more than one address book, so show the options */
        frame = store_address_book_frame(info);
        gtk_widget_show_all(frame);
        gtk_box_pack_start(GTK_BOX(vbox), frame, FALSE, FALSE, 0);
    }

    frame = store_address_note_frame(info);
    gtk_widget_show_all(frame);
    gtk_box_pack_start(GTK_BOX(vbox), frame, TRUE, TRUE, 0);

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
        balsa_information_parented(window, LIBBALSA_INFORMATION_ERROR, "%s", msg);
    }
    
    g_object_unref(address);
    return rc == LBABERR_OK;
}

/* store_address_book_frame:
 * create the frame containing the address book menu */
static GtkWidget *
store_address_book_frame(StoreAddressInfo * info)
{
    guint default_ab_offset = 0;
    GtkWidget *hbox;
    GtkWidget *combo_box;
    guint off;
    GList *ab_list;

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 4);
    gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("Address Book:")),
                       FALSE, FALSE, 0);

    combo_box = gtk_combo_box_text_new();
    g_signal_connect(combo_box, "changed",
                     G_CALLBACK(store_address_book_menu_cb), info);

    /* NOTE: we have to store the default address book index and
       call set_active() after all books are added to the list or
       gtk-2.10.4 will lose the setting. */
    for (off = 0, ab_list = balsa_app.address_book_list;
         ab_list != NULL;
         off++, ab_list = ab_list->next) {
        LibBalsaAddressBook *address_book;

        address_book = LIBBALSA_ADDRESS_BOOK(ab_list->data);
        if (info->current_address_book == NULL)
            info->current_address_book = address_book;

        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo_box),
                                       address_book->name);
        if (address_book == balsa_app.default_address_book)
            default_ab_offset = off;
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), default_ab_offset);

    gtk_box_pack_start(GTK_BOX(hbox), combo_box, TRUE, TRUE, 0);

    return hbox;
}

/* store_address_note_frame:
 * create the frame containing the notebook with address information */
static GtkWidget *
store_address_note_frame(StoreAddressInfo *info)
{
    LibBalsaMessage *message;
    GList *list;

    info->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(info->notebook), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(info->notebook), 4);

    for (list = info->message_list; list; list = list->next) {
        message = LIBBALSA_MESSAGE(list->data);
	if (message->headers) {
	    store_address_add_list(info, _("From: "), message->headers->from);
	    store_address_add_list(info, _("To: "), message->headers->to_list);
	    store_address_add_list(info, _("Cc: "), message->headers->cc_list);
	    store_address_add_list(info, _("Bcc: "), message->headers->bcc_list);
	}
    }

    return info->notebook;
}

/* store_address_book_menu_cb:
 * callback for the address book menu */
static void
store_address_book_menu_cb(GtkWidget * widget, 
                           StoreAddressInfo * info)
{
    guint i = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    GList *nth = g_list_nth(balsa_app.address_book_list, i);
    if(nth)
        info->current_address_book = LIBBALSA_ADDRESS_BOOK(nth->data);
}

/* store_address_add_address:
 * make a new page in the notebook */
static void
store_address_add_address(StoreAddressInfo * info,
                          const gchar * lab, InternetAddress * ia,
			  InternetAddress * group)
{
    gchar *text;
    LibBalsaAddress *address;
    gchar *label_text;
    GtkWidget **entries, *ew;

    if (ia == NULL)
        return;

    entries = g_new(GtkWidget *, NUM_FIELDS);
    info->entries_list = g_list_append(info->entries_list, entries);

    text = internet_address_to_string(ia, FALSE);
    address = libbalsa_address_new();
    address->full_name =
        g_strdup(ia->name ? ia->name : group ? group->name : NULL);
    if (INTERNET_ADDRESS_IS_GROUP(ia)) {
        InternetAddressList *members;
        int j;

        address->address_list = NULL;
        members = INTERNET_ADDRESS_GROUP(ia)->members;

        for (j = 0; j < internet_address_list_length(members); j++) {
            InternetAddress *member_address =
                internet_address_list_get_address(members, j);
            if (INTERNET_ADDRESS_IS_MAILBOX(member_address))
                address->address_list =
                    g_list_prepend(address->address_list,
                                   g_strdup(INTERNET_ADDRESS_MAILBOX
                                            (member_address)->addr));
        }
        address->address_list = g_list_reverse(address->address_list);
    } else {
        address->address_list =
            g_list_prepend(NULL,
                           g_strdup(INTERNET_ADDRESS_MAILBOX(ia)->addr));
    }
    ew = libbalsa_address_get_edit_widget(address, entries, NULL, NULL);
    g_object_unref(address);

    label_text = g_strconcat(lab, text, NULL);
    g_free(text);
    if (g_utf8_strlen(label_text, -1) > 15)
        /* truncate to an arbitrary length: */
        *g_utf8_offset_to_pointer(label_text, 15) = '\0';
    gtk_notebook_append_page(GTK_NOTEBOOK(info->notebook), ew,
                             gtk_label_new(label_text));
    g_free(label_text);
}

static void
store_address_add_lbaddress(StoreAddressInfo * info,
                            const LibBalsaAddress *address)
{
    gchar *label_text;
    GtkWidget **entries, *ew;

    g_return_if_fail(address->address_list);
    entries = g_new(GtkWidget *, NUM_FIELDS);
    info->entries_list = g_list_append(info->entries_list, entries);

    ew = libbalsa_address_get_edit_widget(address, entries, NULL, NULL);

    label_text = g_strdup(address->full_name ? address->full_name :
                          address->address_list->data);
    if (g_utf8_strlen(label_text, -1) > 15)
        /* truncate to an arbitrary length: */
        *g_utf8_offset_to_pointer(label_text, 15) = '\0';
    gtk_notebook_append_page(GTK_NOTEBOOK(info->notebook), ew,
                             gtk_label_new(label_text));
    g_free(label_text);
}

/* store_address_add_list:
 * take a list of addresses and pass them one at a time to
 * store_address_add_address */
static void
store_address_add_list(StoreAddressInfo    * info,
                       const gchar         * label,
                       InternetAddressList * list)
{
    int i, j;

    if (!list)
        return;

    for (i = 0; i < internet_address_list_length(list); i++) {
        InternetAddress *ia = internet_address_list_get_address(list, i);

        if (INTERNET_ADDRESS_IS_MAILBOX(ia)) {
            store_address_add_address(info, label, ia, NULL);
        } else if (info->current_address_book->dist_list_mode) {
            store_address_add_address(info, label, ia, ia);
        } else {
            InternetAddressList *members =
                INTERNET_ADDRESS_GROUP(ia)->members;

            for (j = 0; j < internet_address_list_length(members); j++) {
                InternetAddress *member_address =
                    internet_address_list_get_address(members, j);

                if (INTERNET_ADDRESS_IS_MAILBOX(member_address))
                    store_address_add_address(info, label, member_address,
                                              ia);
            }
        }
    }
}
