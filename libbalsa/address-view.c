/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2007 Stuart Parmenter and others,
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


/*
 *     Address entry widget
 *     A GtkTreeView with one line per address
 */


#include "config.h"
#include <string.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

/*
 * LibBalsa includes.
 */
#include "address-view.h"
#include "address-book.h"
#include "cell-renderer-button.h"
#include "misc.h"

/*
 *     GObject class boilerplate
 */

enum {
    OPEN_ADDRESS_BOOK,
    LAST_SIGNAL
};

static guint address_view_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE(LibBalsaAddressView, libbalsa_address_view,
              GTK_TYPE_TREE_VIEW)

static void
libbalsa_address_view_init(LibBalsaAddressView * address_view)
{
}

static void
libbalsa_address_view_finalize(GObject * object)
{
    LibBalsaAddressView *address_view = LIBBALSA_ADDRESS_VIEW(object);

    g_free(address_view->address_book_stock_id);
    g_free(address_view->remove_stock_id);
    g_free(address_view->domain);
    g_free(address_view->path_string);

    if (address_view->focus_row)
        gtk_tree_row_reference_free(address_view->focus_row);

    if (address_view->focus_idle_id)
        g_source_remove(address_view->focus_idle_id);

    (*G_OBJECT_CLASS(libbalsa_address_view_parent_class)->
     finalize) (object);
}

static void
libbalsa_address_view_class_init(LibBalsaAddressViewClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = libbalsa_address_view_finalize;

  /**
   * LibBalsaAddressView::open-address-book:
   * @address_view: the object which received the signal
   * @row_ref:      a #GtkTreeRowReference to the row in @address_view
   *                that will contain the address
   *
   * The ::open-address-book signal is emitted when the address book
   * button is clicked.
   **/
    address_view_signals[OPEN_ADDRESS_BOOK] =
        g_signal_new("open-address-book",
                     G_OBJECT_CLASS_TYPE(object_class),
                     0, 0, NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
                     G_TYPE_NONE, 1, G_TYPE_POINTER);
}

/*
 *     Static data.
 */

static GList *lbav_address_book_list;
enum {
    COMPLETION_NAME_COL,
};
enum {
    ADDRESS_TYPE_COL,
    ADDRESS_TYPESTRING_COL,
    ADDRESS_NAME_COL,
    ADDRESS_STOCK_ID_COL
};

typedef enum LibBalsaAddressViewMatchType_ LibBalsaAddressViewMatchType;
enum LibBalsaAddressViewMatchType_ {
    LIBBALSA_ADDRESS_VIEW_MATCH_FAST,
    LIBBALSA_ADDRESS_VIEW_MATCH_ALL
};

/* Must be consistent with LibBalsaAddressType enum: */
const gchar *const libbalsa_address_view_types[] = {
    N_("To:"),
    N_("Cc:"),
    N_("Bcc:"),
#if !defined(ENABLE_TOUCH_UI)
    N_("Reply To:"),
#endif                          /* ENABLE_TOUCH_UI */
};

/*
 *     Helpers
 */

/*
 *     Create a GList of addresses matching the prefix.
 */
static GList *
lbav_get_matching_addresses(const gchar * prefix,
                            LibBalsaAddressViewMatchType type)
{
    GList *match = NULL, *list;
    gchar *prefix_n;
    gchar *prefix_f;

    prefix_n = g_utf8_normalize(prefix, -1, G_NORMALIZE_ALL);
    prefix_f = g_utf8_casefold(prefix_n, -1);
    g_free(prefix_n);
    for (list = lbav_address_book_list; list; list = list->next) {
        LibBalsaAddressBook *ab;

        ab = LIBBALSA_ADDRESS_BOOK(list->data);
        if (type == LIBBALSA_ADDRESS_VIEW_MATCH_FAST
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

/*
 *     Update the GtkEntryCompletion's GtkTreeModel with
 *     the list of addresses.
 */
static void
lbav_append_addresses(LibBalsaAddressView * address_view,
                      GtkEntryCompletion * completion,
                      GList * match, const gchar * prefix)
{
    GtkListStore *store;
    GtkTreeIter iter;
    gchar *name;

    store = GTK_LIST_STORE(gtk_entry_completion_get_model(completion));
    gtk_list_store_clear(store);
    /* Synchronize the filtered model. */
    gtk_entry_completion_complete(completion);

    for (; match; match = match->next) {
        InternetAddress *ia = match->data;
        name = internet_address_to_string(ia, FALSE);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COMPLETION_NAME_COL, name, -1);
        g_free(name);
    }

    if (address_view->domain && *address_view->domain
        && !strpbrk(prefix, "@%!")) {
        /* No domain in the user's entry, and the current identity has a
         * default domain, so we'll add user@domain as a possible
         * autocompletion. */
        name = g_strconcat(prefix, "@", address_view->domain, NULL);
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, COMPLETION_NAME_COL, name, -1);
        g_free(name);
    }
}

/*
 *     Fetch a GList of addresses matching the active entry and update
 *     the GtkEntryCompletion's list.
 */
static void
lbav_entry_setup_matches(LibBalsaAddressView * address_view,
                         GtkEntry * entry,
                         GtkEntryCompletion * completion,
                         LibBalsaAddressViewMatchType type)
{
    const gchar *prefix;
    GList *match = NULL;

    prefix = gtk_entry_get_text(entry);
    if (*prefix)
        match = lbav_get_matching_addresses(prefix, type);
    lbav_append_addresses(address_view, completion, match, prefix);
    g_list_foreach(match, (GFunc) internet_address_unref, NULL);
    g_list_free(match);
}

/*
 *     Idle callback to set the GtkTreeView's cursor.
 */
static gboolean
lbav_ensure_blank_line_idle_cb(LibBalsaAddressView * address_view)
{
    GtkTreePath *focus_path;

    gdk_threads_enter();

    focus_path = gtk_tree_row_reference_get_path(address_view->focus_row);
    gtk_tree_row_reference_free(address_view->focus_row);
    address_view->focus_row = NULL;

    /* This will open the entry for editing;
     * NOTE: the GtkTreeView documentation states that:
     *  "This function is often followed by
     *   gtk_widget_grab_focus(tree_view) in order to give keyboard
     *   focus to the widget."
     * but in fact, that leaves the entry /not/ open for editing. */
    gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(address_view),
                                     focus_path,
                                     address_view->focus_column,
                                     address_view->focus_cell, TRUE);
    gtk_tree_path_free(focus_path);

    address_view->focus_idle_id = 0;

    gdk_threads_leave();

    return FALSE;
}

/*
 *     Make sure we have a blank line; use the specified type.
 *     On return, iter points to the blank line.
 */
static void
lbav_ensure_blank_line(LibBalsaAddressView * address_view,
                       GtkTreeIter * iter, LibBalsaAddressType type)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkListStore *address_store = GTK_LIST_STORE(model);
    LibBalsaAddressType this_type;
    gchar *name;
    gboolean valid;
    GtkTreePath *path;

    g_assert(type < LIBBALSA_ADDRESS_N_TYPES);

    this_type = address_view->default_type;
    name = NULL;
    for (valid = gtk_tree_model_get_iter_first(model, iter);
         valid; valid = gtk_tree_model_iter_next(model, iter)) {
        g_free(name);
        gtk_tree_model_get(model, iter,
                           ADDRESS_TYPE_COL, &this_type,
                           ADDRESS_NAME_COL, &name, -1);
        if (!name || !*name)
            break;
    }
    g_free(name);

    if (!valid)
        gtk_list_store_append(address_store, iter);
    if (!valid || this_type != type)
        gtk_list_store_set(address_store, iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(libbalsa_address_view_types[type]),
                           ADDRESS_STOCK_ID_COL,
                           address_view->address_book_stock_id, -1);

    if (address_view->focus_row)
        gtk_tree_row_reference_free(address_view->focus_row);
    path = gtk_tree_model_get_path(model, iter);
    address_view->focus_row = gtk_tree_row_reference_new(model, path);
    gtk_tree_path_free(path);

    if (!address_view->focus_idle_id)
        address_view->focus_idle_id =
            g_idle_add((GSourceFunc) lbav_ensure_blank_line_idle_cb,
                       address_view);
}

/*
 *     Add the addresses in an InternetAddressList, starting at iter and
 *     inserting lines of the same type as necessary;
 *     on return, iter points to the last line inserted.
 */
static void
lbav_add_from_list(LibBalsaAddressView * address_view,
                   GtkTreeIter * iter, InternetAddressList * list)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    GtkListStore *address_store = GTK_LIST_STORE(model);
    LibBalsaAddressType type;

    gtk_tree_model_get(model, iter, ADDRESS_TYPE_COL, &type, -1);

    while (list) {
        InternetAddress *ia = list->address;

        if (ia) {
            gchar *name = internet_address_to_string(ia, FALSE);

            libbalsa_utf8_sanitize(&name, address_view->fallback, NULL);

            gtk_list_store_set(address_store, iter,
                               ADDRESS_TYPE_COL, type,
                               ADDRESS_TYPESTRING_COL,
                               _(libbalsa_address_view_types[type]),
                               ADDRESS_NAME_COL, name,
                               ADDRESS_STOCK_ID_COL,
                               address_view->remove_stock_id, -1);
            g_free(name);
        }

        if ((list = list->next))
            gtk_list_store_insert_after(address_store, iter, iter);
    }
}

/*
 *     Add the addresses in a string, starting at iter and
 *     inserting lines of the same type as necessary;
 *     on return, iter points to the last line inserted;
 *     returns TRUE if any valid addresses were found.
 */
static gboolean
lbav_add_from_string(LibBalsaAddressView * address_view,
                     GtkTreeIter * iter, const gchar * string)
{
    InternetAddressList *list = internet_address_parse_string(string);

    lbav_add_from_list(address_view, iter, list);
    internet_address_list_destroy(list);

    return list != NULL;
}

/*
 *     Remove addresses of the given type
 */
static void
lbav_remove(LibBalsaAddressView * address_view, LibBalsaAddressType type)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    GtkListStore *address_store = GTK_LIST_STORE(model);
    GtkTreeIter iter;
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        LibBalsaAddressType this_type;

        gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &this_type, -1);
        valid = this_type == type ?
            gtk_list_store_remove(address_store, &iter) :
            gtk_tree_model_iter_next(model, &iter);
    }
}

/*
 *     Set (clear == TRUE) or add (clear == FALSE) the address
 */
static void
lbav_set_or_add(LibBalsaAddressView * address_view,
                LibBalsaAddressType type,
                const gchar * address, gboolean clear)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    GtkTreeIter iter;

    if (clear)
        lbav_remove(address_view, type);

    if (address && *address) {
        GtkListStore *address_store = GTK_LIST_STORE(model);

        lbav_ensure_blank_line(address_view, &iter, type);
        gtk_list_store_set(address_store, &iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(libbalsa_address_view_types[type]), -1);
        lbav_add_from_string(address_view, &iter, address);
        lbav_ensure_blank_line(address_view, &iter, type);
    } else
        lbav_ensure_blank_line(address_view, &iter,
                               address_view->default_type);
}

/*
 * Parse new text and set it in the address store.
 */
static void
lbav_set_text_at_path(LibBalsaAddressView * address_view,
                      const gchar * text, const gchar * path_string)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkListStore *address_store = GTK_LIST_STORE(model);
    GtkTreePath *path;
    GtkTreeIter iter, tmp_iter;
    LibBalsaAddressType type;
    gchar *name;
    gboolean valid;
    guint count;

    path = gtk_tree_path_new_from_string(path_string);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);

    if (text && *text) {
        /* Parse the new text and store it in this line. */
        if (lbav_add_from_string(address_view, &iter, text))
            lbav_ensure_blank_line(address_view, &iter, type);
        else
            /* No valid addresses found; just set the new text and keep
             * the focus in this line. */
            gtk_list_store_set(address_store, &iter,
                               ADDRESS_NAME_COL, text, -1);
        return;
    }

    gtk_tree_model_get(model, &iter, ADDRESS_NAME_COL, &name, -1);
    if (!name || !*name) {
        /* The line was already blank--just return. */
        g_free(name);
        return;
    }
    g_free(name);

    /* Clear the text. */
    gtk_list_store_set(address_store, &iter, ADDRESS_NAME_COL, NULL, -1);

    /* If this is not the only blank line, remove it. */
    count = 0;
    for (valid = gtk_tree_model_get_iter_first(model, &tmp_iter);
         valid; valid = gtk_tree_model_iter_next(model, &tmp_iter)) {
        gtk_tree_model_get(model, &iter, ADDRESS_NAME_COL, &name, -1);
        if (!name || !*name)
            ++count;
        g_free(name);
    }

    if (count > 1) {
        gtk_list_store_remove(address_store, &iter);
        lbav_ensure_blank_line(address_view, &iter, type);
    }
}

/*
 *     Count addresses in an InternetAddressList
 */
static void
lbav_count_addresses_in_list(InternetAddressList * list, gint * addresses)
{
    for (; list && *addresses >= 0; list = list->next) {
        InternetAddress *ia = list->address;
        if (ia->type == INTERNET_ADDRESS_NAME) {
            if (strpbrk(ia->value.addr, "@%!"))
                ++(*addresses);
            else
                *addresses = -1;
        } else if (ia->type == INTERNET_ADDRESS_GROUP)
            lbav_count_addresses_in_list(ia->value.members, addresses);
    }
}

/*
 * Callbacks.
 */

/*
 *     Callback for the entry's "changed" signal
 */
static void
lbav_entry_changed_cb(GtkEntry * entry, LibBalsaAddressView * address_view)
{
    GtkEntryCompletion *completion;

    completion = gtk_entry_get_completion(entry);

    if (GTK_WIDGET_REALIZED(GTK_WIDGET(entry)))
        lbav_entry_setup_matches(address_view, entry, completion,
                                 LIBBALSA_ADDRESS_VIEW_MATCH_FAST);
}

/*
 *     Callback for the entry's "key-pressed" event
 */
static gboolean
lbav_key_pressed_cb(GtkEntry * entry,
                    GdkEventKey * event,
                    LibBalsaAddressView * address_view)
{
    GtkEntryCompletion *completion;

    if (event->keyval != GDK_Escape)
        return FALSE;

    if (address_view->last_was_escape) {
        address_view->last_was_escape = FALSE;
        return FALSE;
    }
    address_view->last_was_escape = TRUE;

    completion = gtk_entry_get_completion(entry);
    g_signal_handlers_block_by_func(entry, lbav_entry_changed_cb,
                                    address_view);
    lbav_entry_setup_matches(address_view, entry, completion,
                             LIBBALSA_ADDRESS_VIEW_MATCH_ALL);
    g_signal_emit_by_name(entry, "changed");
    g_signal_handlers_unblock_by_func(entry, lbav_entry_changed_cb,
                                      address_view);

    return TRUE;
}

/*
 *     Callback for the entry's "insert_text" event -
 *     replace control chars by spaces
 */
static void
lbav_insert_text_cb(GtkEditable * editable,
                    const gchar * text,
                    gint length,
                    gint * position, LibBalsaAddressView * address_view)
{
    gchar *p;
    gchar *ins_text = g_strndup(text, length);

    /* replace non-printable chars by spaces */
    p = ins_text;
    while (*p != '\0') {
        gchar *next = g_utf8_next_char(p);

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
                                    (gpointer) lbav_insert_text_cb,
                                    address_view);
    gtk_editable_insert_text(editable, ins_text, length, position);
    g_signal_handlers_unblock_by_func(editable,
                                      (gpointer) lbav_insert_text_cb,
                                      address_view);
    g_signal_stop_emission_by_name(editable, "insert_text");
    g_free(ins_text);
}

/*
 *     The completion's GtkEntryCompletionMatchFunc.
 */
static gboolean
lbav_completion_match_func(GtkEntryCompletion * completion,
                           const gchar * key,
                           GtkTreeIter * iter,
                           LibBalsaAddressView * address_view)
{
    return TRUE;
}

/*
 *     Callback for the completion's "match-selected" signal
 */
static gboolean
lbav_completion_match_selected_cb(GtkEntryCompletion * completion,
                                  GtkTreeModel * model,
                                  GtkTreeIter * iter,
                                  LibBalsaAddressView * address_view)
{
    gchar *name;
    GtkWidget *entry;

    /* Replace the partial address with the selected one. */
    gtk_tree_model_get(model, iter, COMPLETION_NAME_COL, &name, -1);

    /* Rewrite the entry. */
    entry = gtk_entry_completion_get_entry(completion);
    g_signal_handlers_block_by_func(entry, lbav_entry_changed_cb,
                                    address_view);
    gtk_entry_set_text(GTK_ENTRY(entry), name);
    g_signal_handlers_unblock_by_func(entry, lbav_entry_changed_cb,
                                      address_view);

    return TRUE;
}

/*
 *     Callback for the combo's "edited" signal
 *     Store the new text.
 */
static void
lbav_combo_edited_cb(GtkCellRendererText * renderer,
                     const gchar * path_string,
                     const gchar * new_text,
                     LibBalsaAddressView * address_view)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkListStore *address_store = GTK_LIST_STORE(model);
    GtkTreePath *path;
    GtkTreeIter iter;
    LibBalsaAddressType type;

    for (type = 0; type < LIBBALSA_ADDRESS_N_TYPES; type++)
        if (strcmp(new_text, _(libbalsa_address_view_types[type])) == 0)
            break;

    path = gtk_tree_path_new_from_string(path_string);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);
    gtk_list_store_set(address_store, &iter,
                       ADDRESS_TYPE_COL, type,
                       ADDRESS_TYPESTRING_COL, new_text, -1);

    gtk_widget_grab_focus(GTK_WIDGET(address_view));
}

/*
 *     Callback for the tree-view's "editing-started" signal
 *     Set up the GtkEntryCompletion.
 */
static void
lbav_row_editing_cb(GtkCellRenderer * renderer,
                    GtkCellEditable * editable,
                    const gchar * path_string,
                    LibBalsaAddressView * address_view)
{
    GtkEntryCompletion *completion;
    GtkListStore *store;

    if (!GTK_IS_ENTRY(editable))
        return;

    store = gtk_list_store_new(1, G_TYPE_STRING);

    completion = gtk_entry_completion_new();
    gtk_entry_completion_set_model(completion, GTK_TREE_MODEL(store));
    g_object_unref(store);
    gtk_entry_completion_set_match_func(completion,
                                        (GtkEntryCompletionMatchFunc)
                                        lbav_completion_match_func,
                                        address_view, NULL);
    gtk_entry_completion_set_text_column(completion, COMPLETION_NAME_COL);
    g_signal_connect(completion, "match-selected",
                     G_CALLBACK(lbav_completion_match_selected_cb),
                     address_view);

    g_signal_connect(editable, "changed",
                     G_CALLBACK(lbav_entry_changed_cb), address_view);
    g_signal_connect(editable, "key-press-event",
                     G_CALLBACK(lbav_key_pressed_cb), address_view);
    g_signal_connect(editable, "insert-text",
                     G_CALLBACK(lbav_insert_text_cb), address_view);
    gtk_entry_set_completion(GTK_ENTRY(editable), completion);
    g_object_unref(completion);

    address_view->last_was_escape = FALSE;
    address_view->editable = editable;
    g_free(address_view->path_string);
    address_view->path_string = g_strdup(path_string);
}

/*
 *     Callback for the tree-view's "edited" signal
 */
static void
lbav_row_edited_cb(GtkCellRendererText * renderer,
                   const gchar * path_string,
                   const gchar * new_text,
                   LibBalsaAddressView * address_view)
{
    lbav_set_text_at_path(address_view, new_text, path_string);
}

/*
 *     Callback for the tree-view's "editing-canceled" signal
 *     NOTE: We treat this the same as "edited", to avoid a lot of user
 *     surprises.
 */
static void
lbav_row_editing_canceled_cb(GtkCellRendererText * renderer,
                             LibBalsaAddressView * address_view)
{
    const gchar *text =
        gtk_entry_get_text(GTK_ENTRY(address_view->editable));

    lbav_set_text_at_path(address_view, text, address_view->path_string);
}

/*
 *     Callback for the button's "activated" signal
 */
static void
lbav_button_activated_cb(LibBalsaCellRendererButton * button,
                         const gchar * path_string,
                         LibBalsaAddressView * address_view)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreePath *path;
    GtkTreeIter iter;
    gchar *stock_id;

    path = gtk_tree_path_new_from_string(path_string);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter, ADDRESS_STOCK_ID_COL, &stock_id, -1);

    if (strcmp(stock_id, address_view->remove_stock_id) == 0) {
        /* User clicked a remove button. */
        GtkListStore *address_store = GTK_LIST_STORE(model);
        LibBalsaAddressType type;
        gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);

        gtk_list_store_remove(address_store, &iter);

        /* Make sure the view has at least one row: */
        lbav_ensure_blank_line(address_view, &iter, type);
    } else {
        /* User clicked the address book button. */
        GtkTreeRowReference *row_ref =
            gtk_tree_row_reference_new(model, path);

        g_signal_emit(address_view,
                      address_view_signals[OPEN_ADDRESS_BOOK], 0, row_ref);
        gtk_tree_row_reference_free(row_ref);
    }

    g_free(stock_id);
    gtk_tree_path_free(path);
}

static gint
lbav_sort_func(GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b,
               gpointer user_data)
{
    LibBalsaAddressType type_a, type_b;
    gint retval;

    gtk_tree_model_get(model, a, ADDRESS_TYPE_COL, &type_a, -1);
    gtk_tree_model_get(model, b, ADDRESS_TYPE_COL, &type_b, -1);

    /* Sort by type. */
    retval = type_a - type_b;
    if (retval == 0) {
        /* Within type, make sure a blank line sorts to the bottom. */
        gchar *name;
        gboolean is_blank_a, is_blank_b;

        gtk_tree_model_get(model, a, ADDRESS_NAME_COL, &name, -1);
        is_blank_a = !name || !*name;
        g_free(name);

        gtk_tree_model_get(model, b, ADDRESS_NAME_COL, &name, -1);
        is_blank_b = !name || !*name;
        g_free(name);

        if (is_blank_a && !is_blank_b)
            retval = 1;
        else if (is_blank_b && !is_blank_a)
            retval = -1;
    }

    return retval;
}

/*
 *     Public API.
 */

/*
 *     Allocate a new LibBalsaAddressView for use.
 */
LibBalsaAddressView *
libbalsa_address_view_new(LibBalsaAddressViewType type,
                          const gchar * address_book_stock_id,
                          const gchar * remove_stock_id)
{
    GtkListStore *address_store;
    GtkTreeIter iter;
    GtkTreeView *tree_view;
    LibBalsaAddressView *address_view;
    guint i;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    /* List store for the widget: */
    address_store = gtk_list_store_new(4,
                                       /* ADDRESS_TYPE_COL: */
                                       G_TYPE_INT,
                                       /* ADDRESS_TYPESTRING_COL: */
                                       G_TYPE_STRING,
                                       /* ADDRESS_NAME_COL: */
                                       G_TYPE_STRING,
                                       /* ADDRESS_STOCK_ID_COL: */
                                       G_TYPE_STRING);

    gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE
                                            (address_store),
                                            lbav_sort_func, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id
        (GTK_TREE_SORTABLE(address_store),
         GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);

    /* The widget: */
    address_view =
        g_object_new(LIBBALSA_TYPE_ADDRESS_VIEW, "model", address_store,
                     "headers-visible", FALSE, NULL);
    g_object_unref(address_store);

    address_view->default_type =
        type == LIBBALSA_ADDRESS_VIEW_TYPE_RECIPIENTS ?
        LIBBALSA_ADDRESS_TYPE_TO : LIBBALSA_ADDRESS_TYPE_REPLYTO;
    address_view->remove_stock_id = g_strdup(remove_stock_id);
    address_view->address_book_stock_id = g_strdup(address_book_stock_id);

    tree_view = GTK_TREE_VIEW(address_view);
    if (type == LIBBALSA_ADDRESS_VIEW_TYPE_RECIPIENTS) {
        /* List-store for the address type combo: */
        GtkListStore *type_store = gtk_list_store_new(1, G_TYPE_STRING);

        for (i = 0; i < LIBBALSA_ADDRESS_TYPE_REPLYTO; i++) {
            gtk_list_store_append(type_store, &iter);
            gtk_list_store_set(type_store, &iter,
                               0, _(libbalsa_address_view_types[i]), -1);
        }

        /* The address type combo: */
        renderer = gtk_cell_renderer_combo_new();
        g_object_set(renderer,
                     "editable", TRUE,
                     "has-entry", FALSE,
                     "model", type_store, "text-column", 0, NULL);
        g_object_unref(type_store);
        g_signal_connect(renderer, "edited",
                         G_CALLBACK(lbav_combo_edited_cb), address_view);
        column =
            gtk_tree_view_column_new_with_attributes(NULL, renderer,
                                                     "text",
                                                     ADDRESS_TYPESTRING_COL,
                                                     NULL);
        gtk_tree_view_append_column(tree_view, column);
    }

    /* Column for the entry widget and the address-book/remove button. */
    address_view->focus_column = column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

    /* The address entry: */
    address_view->focus_cell = renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    g_signal_connect(renderer, "editing-started",
                     G_CALLBACK(lbav_row_editing_cb), address_view);
    g_signal_connect(renderer, "edited",
                     G_CALLBACK(lbav_row_edited_cb), address_view);
    g_signal_connect(renderer, "editing-canceled",
                     G_CALLBACK(lbav_row_editing_canceled_cb),
                     address_view);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", ADDRESS_NAME_COL, NULL);

    /* The button: */
    renderer = libbalsa_cell_renderer_button_new();
    g_signal_connect(renderer, "activated",
                     G_CALLBACK(lbav_button_activated_cb), address_view);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "stock-id", ADDRESS_STOCK_ID_COL,
                                        NULL);

    gtk_tree_view_append_column(tree_view, column);

    lbav_ensure_blank_line(address_view, &iter,
                           address_view->default_type);

    return address_view;
}

/*
 *     Must be called before using the widget.
 */
void
libbalsa_address_view_set_address_book_list(GList * list)
{
    lbav_address_book_list = list;
}

/*
 *     Set default domain.
 */
void
libbalsa_address_view_set_domain(LibBalsaAddressView * address_view,
                                 const gchar * domain)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    g_free(address_view->domain);
    address_view->domain = g_strdup(domain);
}

/*
 *     Set whether to use the fallback codeset.
 */
void
libbalsa_address_view_set_fallback(LibBalsaAddressView * address_view,
                                   gboolean fallback)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    address_view->fallback = fallback;
}

/*
 *     Set the addresses
 */
void
libbalsa_address_view_set_from_string(LibBalsaAddressView * address_view,
                                      LibBalsaAddressType type,
                                      const gchar * addresses)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));
    g_return_if_fail(type < LIBBALSA_ADDRESS_N_TYPES);

    lbav_set_or_add(address_view, type, addresses, TRUE);
}

/*
 *     Add addresses
 */
void
libbalsa_address_view_add_from_string(LibBalsaAddressView * address_view,
                                      LibBalsaAddressType type,
                                      const gchar * addresses)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));
    g_return_if_fail(type < LIBBALSA_ADDRESS_N_TYPES);

    lbav_set_or_add(address_view, type, addresses, FALSE);
}

/*
 *     Add addresses to an existing row
 */
void
libbalsa_address_view_add_to_row(LibBalsaAddressView * address_view,
                                 GtkTreeRowReference * row_ref,
                                 const gchar * addresses)
{
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeIter iter;
    LibBalsaAddressType type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    path = gtk_tree_row_reference_get_path(row_ref);
    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);

    lbav_add_from_string(address_view, &iter, addresses);

    gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);
    lbav_ensure_blank_line(address_view, &iter, type);
}

/*
 *     Set the address from an InternetAddressList
 */
void
libbalsa_address_view_set_from_list(LibBalsaAddressView * address_view,
                                    LibBalsaAddressType type,
                                    InternetAddressList * list)
{
    GtkTreeIter iter;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));
    g_return_if_fail(type < LIBBALSA_ADDRESS_N_TYPES);

    lbav_remove(address_view, type);

    if (list) {
        GtkTreeModel *model =
            gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
        GtkListStore *address_store = GTK_LIST_STORE(model);

        lbav_ensure_blank_line(address_view, &iter, type);
        gtk_list_store_set(address_store, &iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(libbalsa_address_view_types[type]), -1);
        lbav_add_from_list(address_view, &iter, list);
        lbav_ensure_blank_line(address_view, &iter, type);
    } else
        lbav_ensure_blank_line(address_view, &iter,
                               address_view->default_type);
}

/*
 *     Number of complete addresses, or -1 if any is incomplete.
 */
gint
libbalsa_address_view_n_addresses(LibBalsaAddressView * address_view)
{
    gint addresses;
    LibBalsaAddressType type;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), -1);

    addresses = 0;
    for (type = 0; type < LIBBALSA_ADDRESS_N_TYPES; type++) {
        InternetAddressList *list =
            libbalsa_address_view_get_list(address_view, type);
        lbav_count_addresses_in_list(list, &addresses);
        internet_address_list_destroy(list);
    }

    return addresses;
}

/*
 *     Create InternetAddressList corresponding to the view content.
 *     The list must be destroyed using internet_address_list_destroy().
 */
InternetAddressList *
libbalsa_address_view_get_list(LibBalsaAddressView * address_view,
                               LibBalsaAddressType type)
{
    GtkTreeModel *model;
    InternetAddressList *address_list;
    gboolean valid;
    GtkTreeIter iter;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), NULL);
    g_return_val_if_fail(type < LIBBALSA_ADDRESS_N_TYPES, NULL);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    address_list = NULL;
    for (valid = gtk_tree_model_get_iter_first(model, &iter);
         valid; valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaAddressType this_type;
        gchar *name;

        gtk_tree_model_get(model, &iter,
                           ADDRESS_TYPE_COL, &this_type,
                           ADDRESS_NAME_COL, &name, -1);

        if (this_type == type && name && *name) {
            InternetAddressList *l, *tmp_list =
                internet_address_parse_string(name);
            for (l = tmp_list; l; l = l->next) {
                InternetAddress *ia = l->address;
                if (ia)
                    address_list =
                        internet_address_list_append(address_list, ia);
            }
            internet_address_list_destroy(tmp_list);
        }
        g_free(name);
    }

    return address_list;
}
