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

    store = GTK_LIST_STORE(gtk_entry_completion_get_model(completion));
    gtk_list_store_clear(store);
    /* Synchronize the filtered model. */
    gtk_entry_completion_complete(completion);

    for (; match; match = match->next) {
        LibBalsaAddress *address = match->data;

	name = libbalsa_address_to_gchar(address, -1);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, NAME_COL, name, ADDRESS_COL,
                           address, -1);
        g_free(name);
    }

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    if (info->domain && *info->domain && !strpbrk(prefix, "@%!")) {
        /* No domain in the user's entry, and the current identity has a
         * default domain, so we'll add user@domain as a possible
         * autocompletion. */
        name = g_strconcat(prefix, "@", info->domain, NULL);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, NAME_COL, name, -1);
        g_free(name);
    }
}

static void
lbae_entry_setup_matches(GtkEntry * entry, GtkEntryCompletion * completion,
                         LibBalsaAddressEntryInfo * info,
                         LibBalsaAddressEntryMatchType type)
{
    const gchar *prefix;
    GList *match;

    prefix = info->active->data;
    if (!*prefix)
        return;

    match = lbae_get_matching_addresses(prefix, type);
    lbae_append_addresses(completion, match, prefix);
    g_list_foreach(match, (GFunc) g_object_unref, NULL);
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
    LibBalsaAddress *address;
    gchar *name;
    GSList *list;
    GtkEditable *editable;
    gint position, cursor;

    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);

    /* Replace the partial address with the selected one. */
    gtk_tree_model_get(model, iter, NAME_COL, &name, ADDRESS_COL, &address,
                       -1);
    g_free(info->active->data);
    info->active->data = name;
    g_hash_table_insert(info->table, g_strdup(name), address);

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

    store = gtk_list_store_new(2, G_TYPE_STRING, LIBBALSA_TYPE_ADDRESS);

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
                                        g_object_unref);
    g_object_set_data_full(G_OBJECT(completion),
                           LIBBALSA_ADDRESS_ENTRY_INFO, info,
                           (GDestroyNotify) lbae_info_free);

    entry = gtk_entry_new();
    g_signal_connect(entry, "changed", G_CALLBACK(lbae_entry_changed),
                     NULL);
    g_signal_connect(entry, "notify::cursor-position",
                     G_CALLBACK(lbae_entry_notify), NULL);
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
 *     Create list of LibBalsaAddress objects corresponding to the entry
 *     content. If possible, references objects from the address books.
 *     if not, creates new ones.  The objects must be dereferenced later
 *     and the list disposed, eg.  g_list_foreach(l, g_object_unref,
 *     NULL); g_list_free(l);
 *************************************************************/
GList *
libbalsa_address_entry_get_list(GtkEntry * address_entry)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;
    GtkTreeModel *model;
    GSList *list;
    GList *res;

    g_return_val_if_fail(GTK_IS_ENTRY(address_entry), NULL);
    completion = gtk_entry_get_completion(address_entry);
    g_return_val_if_fail(GTK_IS_ENTRY_COMPLETION(completion), NULL);
    info = g_object_get_data(G_OBJECT(completion),
                             LIBBALSA_ADDRESS_ENTRY_INFO);
    g_return_val_if_fail(info != NULL, NULL);

    model = gtk_entry_completion_get_model(completion);

    for (list = info->list, res = NULL; list; list = list->next) {
	const gchar *name = list->data;
        LibBalsaAddress *address;

	address = g_hash_table_lookup(info->table, name);
        if (address)
	    g_object_ref(address);
	else
            address = libbalsa_address_new_from_string(name);
        if (address)
            res = g_list_prepend(res, address);
    }

    return g_list_reverse(res);
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
 *     Show all matches, even from expensive address books; caller
 *     should check that entry is a GtkEntry, but can use this method to
 *     check that it is an address entry.
 *************************************************************/
gboolean
libbalsa_address_entry_show_matches(GtkEntry * entry)
{
    GtkEntryCompletion *completion;
    LibBalsaAddressEntryInfo *info;

    g_return_val_if_fail(GTK_IS_ENTRY(entry), FALSE);

    completion = gtk_entry_get_completion(entry);
    if (!completion
        || !(info = g_object_get_data(G_OBJECT(completion),
                                      LIBBALSA_ADDRESS_ENTRY_INFO)))
        return FALSE;

    g_signal_handlers_block_by_func(entry, lbae_entry_changed, NULL);
    lbae_entry_setup_matches(entry, completion, info,
                             LIBBALSA_ADDRESS_ENTRY_MATCH_ALL);
    g_signal_emit_by_name(entry, "changed");
    g_signal_handlers_unblock_by_func(entry, lbae_entry_changed, NULL);

    return TRUE;
}

/*************************************************************
 *     Number of complete addresses.
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
    gint addresses = 0;
    InternetAddressList *list =
	internet_address_parse_string(gtk_entry_get_text(entry));
    lbae_count_addresses(list, &addresses);
    internet_address_list_destroy(list);

    return addresses;
}
