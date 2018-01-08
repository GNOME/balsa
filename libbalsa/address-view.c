/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
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


/*
 *     Address entry widget
 *     A GtkTreeView with one line per address
 */


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-view.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

/*
 * LibBalsa includes.
 */
#include "address-book.h"
#include "cell-renderer-button.h"
#include "misc.h"

struct _LibBalsaAddressView {
    GtkTreeView parent;

    /*
     * Permanent data
     */
    const gchar *const *types;
    guint n_types;
    gboolean fallback;
    GList *address_book_list;

    gchar *domain;

    GtkTreeViewColumn *type_column;
    GtkTreeViewColumn *focus_column;
    GtkCellRenderer   *renderer_combo;

    /*
     * Ephemera
     */
    gboolean last_was_escape;   /* keystroke    */

    GtkTreeRowReference *focus_row;     /* set cursor   */
    guint focus_idle_id;        /* ditto        */

    GtkCellEditable *editable;  /* cell editing */
    gchar *path_string;         /* ditto        */
};

struct _LibBalsaAddressViewClass {
    GtkTreeViewClass parent_class;
};

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

enum {
    COMPLETION_NAME_COL,
};
enum {
    ADDRESS_TYPE_COL,
    ADDRESS_TYPESTRING_COL,
    ADDRESS_NAME_COL,
    ADDRESS_ICON_COL
};

typedef enum LibBalsaAddressViewMatchType_ LibBalsaAddressViewMatchType;
enum LibBalsaAddressViewMatchType_ {
    LIBBALSA_ADDRESS_VIEW_MATCH_FAST,
    LIBBALSA_ADDRESS_VIEW_MATCH_ALL
};

/* Must be consistent with LibBalsaAddressType enum: */
const gchar *const libbalsa_address_view_types[] = {
    N_("To:"),
    N_("CC:"),
    N_("BCC:"),
    N_("Reply To:"),
};

/* Pixbufs */
static GdkPixbuf *lbav_book_icon, *lbav_close_icon, *lbav_drop_down_icon;

/*
 *     Helpers
 */

/*
 *     Create a GList of addresses matching the prefix.
 */
static GList *
lbav_get_matching_addresses(LibBalsaAddressView * address_view,
                            const gchar * prefix,
                            LibBalsaAddressViewMatchType type)
{
    GList *match = NULL, *list;
    gchar *prefix_n;
    gchar *prefix_f;

    prefix_n = g_utf8_normalize(prefix, -1, G_NORMALIZE_ALL);
    prefix_f = g_utf8_casefold(prefix_n, -1);
    g_free(prefix_n);
    for (list = address_view->address_book_list; list; list = list->next) {
        LibBalsaAddressBook *ab;

        ab = LIBBALSA_ADDRESS_BOOK(list->data);
        if (type == LIBBALSA_ADDRESS_VIEW_MATCH_FAST
            && (!ab->expand_aliases || ab->is_expensive))
            continue;

        match =
            g_list_concat(match,
                          libbalsa_address_book_alias_complete(ab,
                                                               prefix_f));
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
        match = lbav_get_matching_addresses(address_view, prefix, type);
    lbav_append_addresses(address_view, completion, match, prefix);
    g_list_foreach(match, (GFunc) g_object_unref, NULL);
    g_list_free(match);
}

/*
 *     Idle callback to set the GtkTreeView's cursor.
 */
static gboolean
lbav_ensure_blank_line_idle_cb(LibBalsaAddressView * address_view)
{
    GtkTreePath *focus_path;

    focus_path = gtk_tree_row_reference_get_path(address_view->focus_row);
    gtk_tree_row_reference_free(address_view->focus_row);
    address_view->focus_row = NULL;

    /* This will open the entry for editing */
    gtk_tree_view_set_cursor(GTK_TREE_VIEW(address_view), focus_path,
                             address_view->focus_column, TRUE);
    gtk_tree_path_free(focus_path);

    address_view->focus_idle_id = 0;

    return FALSE;
}

/*
 *     Get a type-string
 */
static const gchar *
lbav_type_string(LibBalsaAddressView * address_view, guint type)
{
    return address_view->n_types > 0 ? address_view->types[type] : "";
}

/*
 *     Make sure we have a blank line; use the specified type.
 *     On return, if iter != NULL, iter points to the blank line.
 */
static void
lbav_ensure_blank_line(LibBalsaAddressView * address_view,
                       GtkTreeIter * iter, guint type)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkTreeIter tmp_iter;
    GtkListStore *address_store = GTK_LIST_STORE(model);
    guint this_type;
    gchar *name;
    gboolean valid;
    GtkTreePath *path;

    g_assert(address_view->n_types == 0 || type < address_view->n_types);

    if (!iter)
        iter = &tmp_iter;

    this_type = 0;
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
                           _(lbav_type_string(address_view, type)),
                           ADDRESS_ICON_COL, lbav_book_icon,
                           -1);

    if (address_view->focus_row)
        gtk_tree_row_reference_free(address_view->focus_row);
    path = gtk_tree_model_get_path(model, iter);
    address_view->focus_row = gtk_tree_row_reference_new(model, path);
    gtk_tree_path_free(path);

    if (!address_view->focus_idle_id)
        address_view->focus_idle_id =
        	g_idle_add((GSourceFunc) lbav_ensure_blank_line_idle_cb, address_view);
}

/*
 *     Replace non-printable chars by spaces
 */
static void
lbav_clean_text(gchar * text)
{
    gchar *p;
    gboolean was_graph = FALSE;

    p = text;
    while (*p) {
        gunichar c  = g_utf8_get_char(p);
        gchar *next = g_utf8_next_char(p);

        if (g_unichar_isprint(c)) {
            was_graph = g_unichar_isgraph(c);
            p = next;
        } else {
            if (was_graph)
                *p++ = ' ';
            was_graph = FALSE;
            if (p != next)
                memmove(p, next, strlen(next) + 1);
        }
    }
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
    guint type;
    int i;

    gtk_tree_model_get(model, iter, ADDRESS_TYPE_COL, &type, -1);

    for (i = 0; i < internet_address_list_length(list); i++) {
        InternetAddress *ia = internet_address_list_get_address(list, i);
        gchar *name = internet_address_to_string(ia, FALSE);

        libbalsa_utf8_sanitize(&name, address_view->fallback, NULL);
        lbav_clean_text(name);

        if (i > 0)
            gtk_list_store_insert_after(address_store, iter, iter);
        gtk_list_store_set(address_store, iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(lbav_type_string(address_view, type)),
                           ADDRESS_NAME_COL, name,
                           ADDRESS_ICON_COL, lbav_close_icon, -1);
        g_free(name);
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
    InternetAddressList *list = internet_address_list_parse_string(string);
    gboolean retval = FALSE;

    if (list) {
        if ((retval = (internet_address_list_length(list) > 0)))
            lbav_add_from_list(address_view, iter, list);
        g_object_unref(list);
    }

    return retval;
}

/*
 *     Remove addresses of the given type
 */
static void
lbav_remove(LibBalsaAddressView * address_view, guint type)
{
    GtkTreeModel *model =
        gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    GtkListStore *address_store = GTK_LIST_STORE(model);
    GtkTreeIter iter;
    gboolean valid;

    valid = gtk_tree_model_get_iter_first(model, &iter);
    while (valid) {
        guint this_type;

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
lbav_set_or_add(LibBalsaAddressView * address_view, guint type,
                const gchar * address, gboolean clear)
{
    if (clear)
        lbav_remove(address_view, type);

    if (address && *address) {
        GtkTreeModel *model =
            gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
        GtkListStore *address_store = GTK_LIST_STORE(model);
        GtkTreeIter iter;

        lbav_ensure_blank_line(address_view, &iter, type);
        gtk_list_store_set(address_store, &iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(libbalsa_address_view_types[type]), -1);
        lbav_add_from_string(address_view, &iter, address);
        lbav_ensure_blank_line(address_view, &iter, type);
    } else
        lbav_ensure_blank_line(address_view, NULL, 0);
}

/*
 * Parse new text and set it in the address store.
 */
static void
lbav_set_text(LibBalsaAddressView * address_view, const gchar * text)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(address_view);
    GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
    GtkListStore *address_store = GTK_LIST_STORE(model);
    GtkTreePath *path;
    GtkTreeIter iter, tmp_iter;
    guint type;
    gchar *name;
    gboolean valid;
    guint count;

    path = gtk_tree_path_new_from_string(address_view->path_string);
    valid = gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);
    if (!valid)
        return;

    gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);

    if (text && *text) {
        /* Parse the new text and store it in this line. */
        if (lbav_add_from_string(address_view, &iter, text))
            lbav_ensure_blank_line(address_view, &iter, type);
        else {
            /* No valid addresses found; just set the new text and keep
             * the focus in this line. */
            gchar *text_dup = g_strdup(text);
            lbav_clean_text(text_dup);
            gtk_list_store_set(address_store, &iter,
                               ADDRESS_NAME_COL, text_dup, -1);
            g_free(text_dup);
        }
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
 *     Find an address_type
 */
static guint
lbav_get_type(LibBalsaAddressView * address_view,
              const gchar * address_type)
{
    guint type;

    for (type = 0; type < address_view->n_types; type++)
        if (strcmp(address_type, lbav_type_string(address_view, type))
            == 0)
            break;

    return type;
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

    if (gtk_widget_get_window(GTK_WIDGET(entry)))
        lbav_entry_setup_matches(address_view, entry, completion,
                                 LIBBALSA_ADDRESS_VIEW_MATCH_FAST);
}

/*
 *     Callback for the entry's "key-pressed" event
 */
static gboolean
lbav_key_pressed_cb(GtkEntry * entry,
                    GdkEvent * event,
                    LibBalsaAddressView * address_view)
{
    GtkEntryCompletion *completion;
    guint keyval;

    if (!gdk_event_get_keyval(event, &keyval) || keyval != GDK_KEY_Escape)
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
    guint type;
    gboolean valid;

    for (type = 0; type < address_view->n_types; type++)
        if (strcmp(new_text, _(lbav_type_string(address_view, type)))
            == 0)
            break;

    path = gtk_tree_path_new_from_string(path_string);
    valid = gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);
    if (!valid)
        return;

    gtk_list_store_set(address_store, &iter,
                       ADDRESS_TYPE_COL, type,
                       ADDRESS_TYPESTRING_COL, new_text, -1);

    gtk_widget_grab_focus(GTK_WIDGET(address_view));
}

/*
 *     Callback for the cell-editable's "editing-done" signal
 *     Store the new text.
 */
static void
lbav_editing_done(GtkCellEditable * cell_editable,
                  LibBalsaAddressView * address_view)
{
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(cell_editable));

    lbav_set_text(address_view, text);
}


/*
 * Focus Out callback
 * If only one completion matches, fill it into the entry
 */
static gboolean
lbav_focus_out_cb(GtkEntry * entry, GdkEventFocus * event,
                  LibBalsaAddressView * address_view)
{
    const gchar *the_entry = gtk_entry_get_text(entry);

    if (the_entry && *the_entry) {
        GList *match;

        match = lbav_get_matching_addresses(address_view,
                                            the_entry,
                                            LIBBALSA_ADDRESS_VIEW_MATCH_ALL);

        if (match) {
            if (!match->next) {
                gchar *the_addr =
                    internet_address_to_string((InternetAddress *) match->
                                               data, FALSE);

                g_signal_handlers_block_by_func(entry,
                                                lbav_entry_changed_cb,
                                                address_view);
                gtk_entry_set_text(entry, the_addr);
                g_signal_handlers_unblock_by_func(entry,
                                                  lbav_entry_changed_cb,
                                                  address_view);
                gtk_cell_editable_editing_done(GTK_CELL_EDITABLE(entry));
                g_free(the_addr);
            }
            g_list_foreach(match, (GFunc) g_object_unref, NULL);
            g_list_free(match);
        }
    }

    return FALSE;
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
    g_signal_connect(editable, "editing-done",
                     G_CALLBACK(lbav_editing_done), address_view);
    g_signal_connect_after(GTK_ENTRY(editable), "focus-out-event",
			   G_CALLBACK(lbav_focus_out_cb), address_view);
    gtk_entry_set_completion(GTK_ENTRY(editable), completion);
    g_object_unref(completion);

    address_view->last_was_escape = FALSE;
    address_view->editable = editable;
    g_free(address_view->path_string);
    address_view->path_string = g_strdup(path_string);
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
    GdkPixbuf *pixbuf;

    path = gtk_tree_path_new_from_string(path_string);
    if (!gtk_tree_model_get_iter(model, &iter, path)) {
        gtk_tree_path_free(path);
        return;
    }

    gtk_tree_model_get(model, &iter, ADDRESS_ICON_COL, &pixbuf, -1);

    if (pixbuf == lbav_close_icon) {
        /* User clicked a remove button. */
        GtkListStore *address_store = GTK_LIST_STORE(model);
        guint type;

        gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);
        gtk_list_store_remove(address_store, &iter);

        /* Make sure the view has at least one row: */
        lbav_ensure_blank_line(address_view, NULL, type);
    } else {
        /* User clicked the address book button. */
        GtkTreeRowReference *row_ref =
            gtk_tree_row_reference_new(model, path);

        g_signal_emit(address_view,
                      address_view_signals[OPEN_ADDRESS_BOOK], 0, row_ref);
        gtk_tree_row_reference_free(row_ref);
    }

    g_object_unref(pixbuf);
    gtk_tree_path_free(path);
}

/*
 *     Callback for the drop_down's "activated" signal
 *
 *     Pop up the address type combo-box
 */
static void
lbav_drop_down_activated_cb(LibBalsaCellRendererButton * drop_down,
                            const gchar * path_string,
                            LibBalsaAddressView * address_view)
{
    GtkTreePath *path;

    path = gtk_tree_path_new_from_string(path_string);
    gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(address_view),
                                     path,
                                     address_view->type_column,
                                     address_view->renderer_combo,
                                     TRUE);
    gtk_tree_path_free(path);
}

/*
 * Sort function for the address store
 */
static gint
lbav_sort_func(GtkTreeModel * model, GtkTreeIter * a, GtkTreeIter * b,
               gpointer user_data)
{
    guint type_a, type_b;
    gint retval;
    gchar *name;
    gboolean is_blank_a, is_blank_b;

    gtk_tree_model_get(model, a, ADDRESS_TYPE_COL, &type_a, -1);
    gtk_tree_model_get(model, b, ADDRESS_TYPE_COL, &type_b, -1);

    /* Sort by type. */
    retval = type_a - type_b;
    if (retval)
        return retval;

    /* Within type, make sure a blank line sorts to the bottom. */

    gtk_tree_model_get(model, a, ADDRESS_NAME_COL, &name, -1);
    is_blank_a = !name || !*name;
    g_free(name);

    gtk_tree_model_get(model, b, ADDRESS_NAME_COL, &name, -1);
    is_blank_b = !name || !*name;
    g_free(name);

    return is_blank_a - is_blank_b;
}

/*
 *     Public API.
 */

/*
 *     Allocate a new LibBalsaAddressView for use.
 */
LibBalsaAddressView *
libbalsa_address_view_new(const gchar * const *types,
                          guint n_types,
                          GList * address_book_list, gboolean fallback)
{
    GtkListStore *address_store;
    GtkTreeView *tree_view;
    LibBalsaAddressView *address_view;
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
                                       /* ADDRESS_ICON_COL: */
                                       GDK_TYPE_PIXBUF);

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

    address_view->types = types;
    address_view->n_types = n_types;
    address_view->address_book_list = address_book_list;
    address_view->fallback = fallback;

    tree_view = GTK_TREE_VIEW(address_view);
    gtk_tree_view_set_activate_on_single_click(tree_view, TRUE);

    /* The button: */
    column = gtk_tree_view_column_new();
    renderer = libbalsa_cell_renderer_button_new();
    g_signal_connect(renderer, "activated",
                     G_CALLBACK(lbav_button_activated_cb), address_view);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "pixbuf", ADDRESS_ICON_COL,
                                        NULL);
    gtk_tree_view_append_column(tree_view, column);

    if (n_types > 0) {
        /* List-store for the address type combo: */
        GtkListStore *type_store = gtk_list_store_new(1, G_TYPE_STRING);
        guint i;

        for (i = 0; i < n_types; i++) {
            GtkTreeIter iter;

            gtk_list_store_append(type_store, &iter);
            gtk_list_store_set(type_store, &iter, 0, _(types[i]), -1);
        }

        address_view->type_column = column = gtk_tree_view_column_new();

        /* The address type combo: */
        address_view->renderer_combo = renderer =
            gtk_cell_renderer_combo_new();
        g_object_set(renderer,
                     "editable", TRUE,
                     "has-entry", FALSE,
                     "model", type_store,
                     "text-column", 0,
                     NULL);
        g_object_unref(type_store);

        g_signal_connect(renderer, "edited",
                         G_CALLBACK(lbav_combo_edited_cb), address_view);

        gtk_tree_view_column_pack_start(column, renderer, TRUE);
        gtk_tree_view_column_set_attributes(column, renderer,
                                            "text", ADDRESS_TYPESTRING_COL,
                                            NULL);

        /* Add a drop-down icon to indicate that this is in fact a
         * combo: */
        renderer = libbalsa_cell_renderer_button_new();
        g_signal_connect(renderer, "activated",
                         G_CALLBACK(lbav_drop_down_activated_cb),
                         address_view);
        g_object_set(renderer, "pixbuf", lbav_drop_down_icon, NULL);
        gtk_tree_view_column_pack_start(column, renderer, FALSE);

        gtk_tree_view_append_column(tree_view, column);
    }

    /* Column for the entry widget. */
    address_view->focus_column = column = gtk_tree_view_column_new();

    /* The address entry: */
    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "editable", TRUE, NULL);
    g_signal_connect(renderer, "editing-started",
                     G_CALLBACK(lbav_row_editing_cb), address_view);
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_attributes(column, renderer,
                                        "text", ADDRESS_NAME_COL, NULL);
    gtk_tree_view_append_column(tree_view, column);

    lbav_ensure_blank_line(address_view, NULL, 0);

    return address_view;
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
 *     Set the addresses
 */
void
libbalsa_address_view_set_from_string(LibBalsaAddressView * address_view,
                                      const gchar * address_type,
                                      const gchar * addresses)
{
    guint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

    lbav_set_or_add(address_view, type, addresses, TRUE);
}

/*
 *     Add addresses
 */
void
libbalsa_address_view_add_from_string(LibBalsaAddressView * address_view,
                                      const gchar * address_type,
                                      const gchar * addresses)
{
    guint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

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
    guint type;
    gboolean valid;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    path = gtk_tree_row_reference_get_path(row_ref);
    valid = gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_path_free(path);
    if (!valid)
        return;

    lbav_add_from_string(address_view, &iter, addresses);

    gtk_tree_model_get(model, &iter, ADDRESS_TYPE_COL, &type, -1);
    lbav_ensure_blank_line(address_view, &iter, type);
}

/*
 *     Set the address from an InternetAddressList
 */
void
libbalsa_address_view_set_from_list(LibBalsaAddressView * address_view,
                                    const gchar * address_type,
                                    InternetAddressList * list)
{
    guint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

    lbav_remove(address_view, type);

    if (list && internet_address_list_length(list) > 0) {
        GtkTreeModel *model =
            gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
        GtkListStore *address_store = GTK_LIST_STORE(model);
        GtkTreeIter iter;

        lbav_ensure_blank_line(address_view, &iter, type);
        gtk_list_store_set(address_store, &iter,
                           ADDRESS_TYPE_COL, type,
                           ADDRESS_TYPESTRING_COL,
                           _(libbalsa_address_view_types[type]), -1);
        lbav_add_from_list(address_view, &iter, list);
        lbav_ensure_blank_line(address_view, &iter, type);
    } else
        lbav_ensure_blank_line(address_view, NULL, 0);
}

/*
 *     Number of complete addresses, or -1 if any is incomplete.
 */
gint
libbalsa_address_view_n_addresses(LibBalsaAddressView * address_view)
{
    gint addresses = 0;
    guint type;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), -1);

    for (type = 0; type < address_view->n_types; type++) {
        InternetAddressList *list =
            libbalsa_address_view_get_list(address_view,
                                           lbav_type_string(address_view,
                                                            type));
        addresses += libbalsa_address_n_mailboxes_in_list(list);
        g_object_unref(list);
    }

    return addresses;
}

/*
 *     Create InternetAddressList corresponding to the view content.
 *     The list, which is NULL only on error, must be destroyed using
 *     g_object_unref().
 */
InternetAddressList *
libbalsa_address_view_get_list(LibBalsaAddressView * address_view,
                               const gchar * address_type)
{
    guint type;
    GtkTreeModel *model;
    InternetAddressList *address_list;
    gboolean valid;
    GtkTreeIter iter;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), NULL);

    type = lbav_get_type(address_view, address_type);
    g_return_val_if_fail(address_view->n_types == 0
                         || type < address_view->n_types, NULL);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(address_view));
    address_list = internet_address_list_new();
    for (valid = gtk_tree_model_get_iter_first(model, &iter);
         valid; valid = gtk_tree_model_iter_next(model, &iter)) {
        guint this_type;
        gchar *name;

        gtk_tree_model_get(model, &iter,
                           ADDRESS_TYPE_COL, &this_type,
                           ADDRESS_NAME_COL, &name, -1);

        if (this_type == type) {
            InternetAddressList *tmp_list =
                internet_address_list_parse_string(name);
            if (tmp_list) {
                internet_address_list_append(address_list, tmp_list);
                g_object_unref(tmp_list);
            }
        }
        g_free(name);
    }

    return address_list;
}

void
libbalsa_address_view_set_book_icon(GdkPixbuf * book_icon)
{
    g_set_object(&lbav_book_icon, book_icon);
}

void
libbalsa_address_view_set_close_icon(GdkPixbuf * close_icon)
{
    g_set_object(&lbav_close_icon, close_icon);
}

void
libbalsa_address_view_set_drop_down_icon(GdkPixbuf * drop_down_icon)
{
    g_set_object(&lbav_drop_down_icon, drop_down_icon);
}
