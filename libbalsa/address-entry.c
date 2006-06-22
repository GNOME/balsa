/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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


#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>

/*
 * LibBalsa includes.
 */
#include "address-entry.h"

/*************************************************************
 *     Address entry widget based on GtkEntryCompletion.
 *************************************************************/

#include "config.h"
#include <stdio.h>
#include <gtk/gtk.h>
#include <gmime/internet-address.h>
#include "address-book.h"

/*************************************************************
 *     Static data.
 *************************************************************/

static GList *lbae_address_book_list;
enum {
    NAME_COL,
    ADDRESS_COL
};

typedef enum LibBalsaAddressEntryMatchType_ LibBalsaAddressEntryMatchType;
enum LibBalsaAddressEntryMatchType_ {
    LIBBALSA_ADDRESS_ENTRY_MATCH_FAST,
    LIBBALSA_ADDRESS_ENTRY_MATCH_ALL
};

/*************************************************************
 *     Data structure attached to the GtkCompletion.
 *************************************************************/
typedef struct {
    GSList *list;
    GSList *active;
    gchar *domain;
    GHashTable *table;
    /* InternetAddress is not a GObject class, so we must keep track of
     * the addresses in the GtkListStore and unref them when we're done.
     */
    InternetAddressList *address_list;
    gboolean last_was_escape;
} LibBalsaAddressEntryInfo;

#define LIBBALSA_ADDRESS_ENTRY_INFO "libbalsa-address-entry-info"

/*************************************************************
 *     Deallocate LibBalsaAddressEntryInfo.
 *************************************************************/

static void
lbae_info_free(LibBalsaAddressEntryInfo * info)
{
    g_slist_foreach(info->list, (GFunc) g_free, NULL);
    g_slist_free(info->list);
    g_free(info->domain);
    g_hash_table_destroy(info->table);
    internet_address_list_destroy(info->address_list);
    g_free(info);
}

/* Helpers. */

/*************************************************************
 *     Parse the entry's text and populate the
 *     LibBalsaAddressEntryInfo.
 *************************************************************/
static void
lbae_parse_entry(GtkEntry * entry, LibBalsaAddressEntryInfo * info)
{
    const gchar *string, *p, *q;
    gint position;
    gboolean quoted;
    gboolean in_group;

    g_slist_foreach(info->list, (GFunc) g_free, NULL);
    g_slist_free(info->list);
    info->list = NULL;

    string = gtk_entry_get_text(entry);
    position = gtk_editable_get_position(GTK_EDITABLE(entry));

    info->active = NULL;
    in_group = quoted = FALSE;
    for (p = string; *p; p = q) {
        gunichar c;

        c = g_utf8_get_char(p);
        q = g_utf8_next_char(p);
        --position;
        /* position is the number of characters between c and the cursor. */

        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted) {
	    if (c == '\\')
		q = g_utf8_next_char(q);
            continue;
	}

        if (in_group) {
	    if (c == ';')
		in_group = FALSE;
            continue;
        }
        if (c == ':') {
            in_group = TRUE;
            continue;
        }

        if (c == ',') {
            info->list =
                g_slist_prepend(info->list,
                                g_strstrip(g_strndup(string, p - string)));
            if (position < 0 && !info->active)
                /* The cursor was in the string we just saved. */
                info->active = info->list;
            string = q;
        }
    }
    info->list =
        g_slist_prepend(info->list,
                        g_strstrip(g_strndup(string, p - string)));
    if (!info->active)
        info->active = info->list;
    info->list = g_slist_reverse(info->list);
}

/*************************************************************
 *     Create a GList of addresses matching the prefix.
 *************************************************************/
static GList *
lbae_get_matching_addresses(const gchar * prefix,
                            LibBalsaAddressEntryMatchType type)
{
    GList *match = NULL, *list;
    gchar *prefix_n;
    gchar *prefix_f;

    prefix_n = g_utf8_normalize(prefix, -1, G_NORMALIZE_ALL);
    prefix_f = g_utf8_casefold(prefix_n, -1);
    g_free(prefix_n);
    for (list = lbae_address_book_list; list; list = list->next) {
        LibBalsaAddressBook *ab;

        ab = LIBBALSA_ADDRESS_BOOK(list->data);
        if (type == LIBBALSA_ADDRESS_ENTRY_MATCH_FAST
            && (!ab->expand_aliases || ab->is_expensive))
            continue;

        match =
            g_list_concat(match,
                          libbalsa_address_book_alias_complete(ab,
                                                               prefix_f,
                                                               NULL));
    }
    g_free(prefix_f);

    return match;
}

/*************************************************************
 *     Update the GtkEntryCompletion's GtkTreeModel with
 *     the list of addresses.
 *************************************************************/
static void
lbae_append_addresses(GtkEntryCompletion * completion, GList * match,
                      const gchar * prefix)
{
    LibBalsaAddressEntryInfo *info;
    GtkListStore *store;
    GtkTreeIter iter;
    gchar *name;
    InternetAddress *ia;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    store = GTK_LIST_STORE(gtk_entry_completion_get_model(completion));
    gtk_list_store_clear(store);
    /* Synchronize the filtered model. */
    gtk_entry_completion_complete(completion);

    internet_address_list_destroy(info->address_list);
    info->address_list = NULL;
    for (; match; match = match->next) {
	ia = match->data;

	name = internet_address_to_string(ia, FALSE);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, NAME_COL, name, ADDRESS_COL, ia, -1);
	info->address_list =
	    internet_address_list_append(info->address_list, ia);
        g_free(name);
    }

    if (info->domain && *info->domain && !strpbrk(prefix, "@%!")) {
        /* No domain in the user's entry, and the current identity has a
         * default domain, so we'll add user@domain as a possible
         * autocompletion. */
        name = g_strconcat(prefix, "@", info->domain, NULL);
        ia = internet_address_new_name("", name);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, NAME_COL, name, ADDRESS_COL, ia, -1);
	info->address_list =
	    internet_address_list_append(info->address_list, ia);
        g_free(name);
    }
}

/*************************************************************
 *     Fetch a GList of addresses matching the active entry and update
 *     the GtkEntryCompletion's list.
 *************************************************************/
static void
lbae_entry_setup_matches(GtkEntry * entry, GtkEntryCompletion * completion,
                         LibBalsaAddressEntryInfo * info,
                         LibBalsaAddressEntryMatchType type)
{
    const gchar *prefix;
    GList *match = NULL;

    prefix = info->active->data;
    if (*prefix)
	match = lbae_get_matching_addresses(prefix, type);
    lbae_append_addresses(completion, match, prefix);
    g_list_foreach(match, (GFunc) internet_address_unref, NULL);
    g_list_free(match);
}

/* Callbacks. */

/*************************************************************
 *     The completion's GtkEntryCompletionMatchFunc.
 *************************************************************/
static gboolean
lbae_completion_match(GtkEntryCompletion * completion, const gchar * key,
                      GtkTreeIter * iter, gpointer user_data)
{
    return TRUE;
}

/*************************************************************
 *     Callback for the entry's "changed" signal
 *************************************************************/
static void
lbae_entry_changed(GtkEntry * entry, gpointer data)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;

    completion = gtk_entry_get_completion(entry);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    lbae_parse_entry(entry, info);

    if (GTK_WIDGET_REALIZED(GTK_WIDGET(entry)))
        lbae_entry_setup_matches(entry, completion, info,
                                 LIBBALSA_ADDRESS_ENTRY_MATCH_FAST);
}

/*************************************************************
 *     Callback for the completion's "match-selected" signal
 *************************************************************/
static gboolean
lbae_completion_match_selected(GtkEntryCompletion * completion,
                               GtkTreeModel * model, GtkTreeIter * iter,
                               gpointer user_data)
{
    LibBalsaAddressEntryInfo *info;
    InternetAddress *ia;
    gchar *name;
    GSList *list;
    GtkEditable *editable;
    gint position, cursor;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);

    /* Replace the partial address with the selected one. */
    gtk_tree_model_get(model, iter, NAME_COL, &name, ADDRESS_COL, &ia,
                       -1);
    internet_address_ref(ia);
    g_hash_table_insert(info->table, g_strdup(name), ia);
    g_free(info->active->data);
    info->active->data = name;

    /* Rewrite the entry. */
    editable = GTK_EDITABLE(gtk_entry_completion_get_entry(completion));
    g_signal_handlers_block_by_func(editable, lbae_entry_changed, NULL);
    gtk_editable_delete_text(editable, 0, -1);
    cursor = position = 0;
    for (list = info->list; list; list = list->next) {
        gtk_editable_insert_text(editable, list->data, -1, &position);
        if (list == info->active)
            cursor = position;
        if (list->next)
            gtk_editable_insert_text(editable, ", ", -1, &position);
    }
    gtk_editable_set_position(editable, cursor);
    g_signal_handlers_unblock_by_func(editable, lbae_entry_changed, NULL);
    g_signal_emit_by_name(editable, "changed");

    return TRUE;
}

/*************************************************************
 *     Callback for the entry's "notify" signal
 *************************************************************/
static void
lbae_entry_notify(GtkEntry * entry, GParamSpec * spec, gpointer data)
{
    g_signal_emit_by_name(entry, "changed");
}

/*************************************************************
 *     Callback for the entry's "key-pressed" event
 *************************************************************/
static gboolean
lbae_key_pressed(GtkEntry * entry, GdkEventKey * event, gpointer data)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;

    if (event->keyval != GDK_Escape)
        return FALSE;

    completion = gtk_entry_get_completion(entry);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);

    if (info->last_was_escape) {
        info->last_was_escape = FALSE;
        return FALSE;
    }
    info->last_was_escape = TRUE;

    g_signal_handlers_block_by_func(entry, lbae_entry_changed, NULL);
    lbae_entry_setup_matches(entry, completion, info,
                             LIBBALSA_ADDRESS_ENTRY_MATCH_ALL);
    g_signal_emit_by_name(entry, "changed");
    g_signal_handlers_unblock_by_func(entry, lbae_entry_changed, NULL);

    return TRUE;
}

/*************************************************************
 *     Callback for the entry's "insert_text" event -
 *     replace control chars by spaces
 *************************************************************/
static void
lbae_insert_handler(GtkEditable *editable, const gchar *text, gint length,
		    gint *position, gpointer data)
{
    gchar * p;
    gchar * ins_text = g_strndup(text, length);

    /* replace non-printable chars by spaces */
    p = ins_text;
    while (*p != '\0') { 
	gchar * next = g_utf8_next_char(p);

	if (g_unichar_isprint(g_utf8_get_char(p)))
	    p = next;
	else {
	    *p++ = ' ';
	    if (p != next)
		memmove(p, next, strlen(next) + 1);
	}
    }

    /* insert */
    g_signal_handlers_block_by_func(editable,
				    (gpointer)lbae_insert_handler, data);
    gtk_editable_insert_text(editable, ins_text, length, position);
    g_signal_handlers_unblock_by_func(editable,
				      (gpointer)lbae_insert_handler, data);
    g_signal_stop_emission_by_name(editable, "insert_text"); 
    g_free(ins_text);
}

/* Public API. */

/*************************************************************
 *     Allocate a new LibBalsaAddressEntry for use.
 *************************************************************/
GtkWidget *
libbalsa_address_entry_new()
{
    GtkWidget *entry;
    GtkEntryCompletion *completion;
    GtkListStore *store;
    LibBalsaAddressEntryInfo *info;

    store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);

    completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_entry_completion_set_match_func(completion,
                                        (GtkEntryCompletionMatchFunc)
                                        lbae_completion_match, NULL, NULL);
    gtk_entry_completion_set_text_column(completion, NAME_COL);
    g_signal_connect(completion, "match-selected",
                     G_CALLBACK(lbae_completion_match_selected), NULL);

    info = g_new0(LibBalsaAddressEntryInfo, 1);
    info->table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                        (GDestroyNotify)
					internet_address_unref);
    g_object_set_data_full(G_OBJECT(completion),
                           LIBBALSA_ADDRESS_ENTRY_INFO, info,
                           (GDestroyNotify) lbae_info_free);

    entry = gtk_entry_new();
    g_signal_connect(entry, "changed", G_CALLBACK(lbae_entry_changed),
                     NULL);
    g_signal_connect(entry, "notify::cursor-position",
                     G_CALLBACK(lbae_entry_notify), NULL);
    g_signal_connect(entry, "key-press-event", G_CALLBACK(lbae_key_pressed),
                     NULL);
    g_signal_connect(entry, "insert-text", G_CALLBACK(lbae_insert_handler),
		     NULL);
    gtk_entry_set_completion(GTK_ENTRY(entry), completion);
    g_object_unref(completion);

    return entry;
}

/*************************************************************
 *     Must be called before using the widget.
 *************************************************************/
void
libbalsa_address_entry_set_address_book_list(GList * list)
{
    lbae_address_book_list = list;
}

/*************************************************************
 *     Create InternetAddressList corresponding to the entry content. 
 *     The list must be destroyed using internet_address_list_destroy().
 *************************************************************/
InternetAddressList *
libbalsa_address_entry_get_list(GtkEntry * address_entry)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;
    InternetAddressList *address_list = NULL;
    GSList *list;

    g_return_val_if_fail(GTK_IS_ENTRY(address_entry), NULL);

    completion = gtk_entry_get_completion(address_entry);
    g_return_val_if_fail(GTK_IS_ENTRY_COMPLETION(completion), NULL);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    g_return_val_if_fail(info != NULL, NULL);

    for (list = info->list; list; list = list->next) {
	const gchar *name = list->data;
	InternetAddress *ia;
	InternetAddressList *tmp_list = NULL;

	if (!name || !*name)
	    continue;
	ia = g_hash_table_lookup(info->table, name);
        if (!ia) {
	    tmp_list = internet_address_parse_string(name);
	    if(tmp_list) ia = tmp_list->address;
	}
        if (ia)
            address_list = internet_address_list_append(address_list, ia);
	internet_address_list_destroy(tmp_list);
    }

    return address_list;
}

/*************************************************************
 *     Set default domain.
 *************************************************************/
void
libbalsa_address_entry_set_domain(GtkEntry * address_entry, void *domain)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;

    g_return_if_fail(GTK_IS_ENTRY(address_entry));
    completion = gtk_entry_get_completion(address_entry);
    g_return_if_fail(GTK_IS_ENTRY_COMPLETION(completion));
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    g_return_if_fail(info != NULL);

    g_free(info->domain);
    info->domain = g_strdup(domain);
}

/*************************************************************
 *     Number of complete addresses, or -1 if any is incomplete.
 *
 *     First a helper:
 *************************************************************/
static void
lbae_count_addresses(InternetAddressList * list, gint * addresses)
{
    for (; list && *addresses >= 0; list = list->next) {
        InternetAddress *ia = list->address;
        if (ia->type == INTERNET_ADDRESS_NAME) {
            if (strpbrk(ia->value.addr, "@%!"))
                ++(*addresses);
            else
                *addresses = -1;
        } else if (ia->type == INTERNET_ADDRESS_GROUP)
            lbae_count_addresses(ia->value.members, addresses);
    }
}

gint
libbalsa_address_entry_addresses(GtkEntry * entry)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;
    GSList *list;
    gint items;
    InternetAddressList *address_list;
    gint addresses;

    g_return_val_if_fail(GTK_IS_ENTRY(entry), -1);

    completion = gtk_entry_get_completion(entry);
    if (!completion
        || !(info = g_object_get_data(G_OBJECT(completion),
                                      LIBBALSA_ADDRESS_ENTRY_INFO)))
        return -1;

    /* Count non-empty items in the entry. */
    for (list = info->list, items = 0; list; list = list->next)
        if (*(gchar *) list->data)
            ++items;

    address_list = libbalsa_address_entry_get_list(entry);
    if (internet_address_list_length(address_list) == items) {
        addresses = 0;
        lbae_count_addresses(address_list, &addresses);
    } else
        addresses = -1;
    internet_address_list_destroy(address_list);

    return addresses;
}
