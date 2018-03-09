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
 *     A GtkGrid with one row per address
 */


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
#   include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "address-view.h"

#include <string.h>
#include <glib/gi18n.h>
#include <gdk/gdkkeysyms.h>

/*
 * LibBalsa includes.
 */
#include "address-book.h"
#include "misc.h"

/*************************************************************
 *
 * LibBalsaAddressViewEntry
 *
 * Subclass of GtkEntry with a key-binding for GDK_KEY_Escape
 *
 ************************************************************/
typedef struct {
    GtkEntry parent;

    LibBalsaAddressView *address_view;
} LibBalsaAddressViewEntry;

typedef struct {
    GtkEntryClass parent_class;
} LibBalsaAddressViewEntryClass;

static void lbav_popup_completions(LibBalsaAddressViewEntry *entry);

/*
 *     GObject class boilerplate
 */

enum {
    POPUP_COMPLETIONS,
    ENTRY_LAST_SIGNAL
};

static guint address_view_entry_signals[ENTRY_LAST_SIGNAL] = {
    0
};

static void
libbalsa_address_view_entry_init(LibBalsaAddressViewEntry *entry)
{
}


static GType libbalsa_address_view_entry_get_type(void);

G_DEFINE_TYPE(LibBalsaAddressViewEntry, libbalsa_address_view_entry, GTK_TYPE_ENTRY)

static void
libbalsa_address_view_entry_class_init(LibBalsaAddressViewEntryClass *klass)
{
    GtkBindingSet *binding_set;

    /**
     * LibBalsaAddressViewEntry::popup-completions:
     * @entry: the object which received the signal
     *
     * The ::popup-completions signal is bound to the Esc key.
     **/
    address_view_entry_signals[POPUP_COMPLETIONS] =
        g_signal_new_class_handler("popup-completions",
                                   G_OBJECT_CLASS_TYPE(klass),
                                   G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
                                   G_CALLBACK(lbav_popup_completions),
                                   NULL, NULL, NULL,
                                   G_TYPE_NONE, 0);

    binding_set = gtk_binding_set_by_class(klass);
    gtk_binding_entry_add_signal(binding_set,
                                 GDK_KEY_Escape, 0,
                                 "popup-completions", 0);
}


static GtkWidget *
libbalsa_address_view_entry_new(LibBalsaAddressView *address_view)
{
    LibBalsaAddressViewEntry *entry;

    entry = g_object_new(libbalsa_address_view_entry_get_type(), NULL);
    entry->address_view = address_view;

    return (GtkWidget *) entry;
}


/*************************************************************
 *
 * LibBalsaAddressView
 *
 ************************************************************/
struct _LibBalsaAddressView {
    GtkGrid parent;

    /*
     * Permanent data
     */
    const gchar *const *types;
    gint n_types;
    gboolean fallback;
    GList *address_book_list;

    gchar *domain;

    /*
     * Ephemera
     */
    GtkWidget *changed_combo;
    gboolean last_was_escape;   /* keystroke    */
};

struct _LibBalsaAddressViewClass {
    GtkGridClass parent_class;
};

/*
 *     GObject class boilerplate
 */

enum {
    OPEN_ADDRESS_BOOK,
    VIEW_CHANGED,
    LAST_SIGNAL
};

static guint address_view_signals[LAST_SIGNAL] = {
    0
};

G_DEFINE_TYPE(LibBalsaAddressView, libbalsa_address_view, GTK_TYPE_GRID)

static void
libbalsa_address_view_init(LibBalsaAddressView *address_view)
{
}


static void
libbalsa_address_view_finalize(GObject *object)
{
    LibBalsaAddressView *address_view = LIBBALSA_ADDRESS_VIEW(object);

    g_free(address_view->domain);

    G_OBJECT_CLASS(libbalsa_address_view_parent_class)->finalize(object);
}


static void
libbalsa_address_view_class_init(LibBalsaAddressViewClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = libbalsa_address_view_finalize;

    /**
     * LibBalsaAddressView::open-address-book:
     * @address_view: the object which received the signal
     * @widget:       a #GtkWidget in the row in @address_view
     *                that will contain the address
     *
     * The ::open-address-book signal is emitted when the address book
     * button is clicked.
     **/
    address_view_signals[OPEN_ADDRESS_BOOK] =
        g_signal_new("open-address-book",
                     G_OBJECT_CLASS_TYPE(object_class),
                     0, 0, NULL, NULL,
                     g_cclosure_marshal_VOID__OBJECT,
                     G_TYPE_NONE, 1, G_TYPE_OBJECT);

    /**
     * LibBalsaAddressView::view-changed:
     * @address_view: the object which received the signal
     *
     * The ::view-changed signal is emitted when an address in the view
     * has been edited or removed.
     **/
    address_view_signals[VIEW_CHANGED] =
        g_signal_new("view-changed",
                     G_OBJECT_CLASS_TYPE(object_class),
                     0, 0, NULL, NULL,
                     g_cclosure_marshal_VOID__VOID,
                     G_TYPE_NONE, 0);
}


/*
 *     Static data.
 */

enum {
    COMPLETION_NAME_COL
};

typedef enum {
    LIBBALSA_ADDRESS_VIEW_MATCH_FAST,
    LIBBALSA_ADDRESS_VIEW_MATCH_ALL
} LibBalsaAddressViewMatchType;

/* Must be consistent with LibBalsaAddressType enum: */
const gchar *const libbalsa_address_view_types[] = {
    N_("To:"),
    N_("CC:"),
    N_("BCC:"),
    N_("Reply To:"),
};

enum {
    LIBBALSA_ADDRESS_VIEW_BUTTON_COLUMN,
    LIBBALSA_ADDRESS_VIEW_COMBO_COLUMN,
    LIBBALSA_ADDRESS_VIEW_ENTRY_COLUMN
};

#define lbav_get_button(a, r) \
    gtk_grid_get_child_at((GtkGrid *)(a), LIBBALSA_ADDRESS_VIEW_BUTTON_COLUMN, (r))
#define lbav_get_combo(a, r) \
    gtk_grid_get_child_at((GtkGrid *)(a), LIBBALSA_ADDRESS_VIEW_COMBO_COLUMN, (r))
#define lbav_get_entry(a, r) \
    gtk_grid_get_child_at((GtkGrid *)(a), LIBBALSA_ADDRESS_VIEW_ENTRY_COLUMN, (r))
#define lbav_remove_row(a, r) \
    gtk_grid_remove_row((GtkGrid *)(a), (r))

typedef enum {
    WITH_BOOK_ICON,
    WITH_CLOSE_ICON
} LibbalsaAddressViewIcon;

/* Pixbufs */
static GdkPixbuf *lbav_book_icon, *lbav_close_icon, *lbav_drop_down_icon;

/*
 * Forward references
 */
static gboolean lbav_completion_match_func(GtkEntryCompletion  *completion,
                                           const gchar         *key,
                                           GtkTreeIter         *iter,
                                           LibBalsaAddressView *address_view);

static gboolean lbav_completion_match_selected_cb(GtkEntryCompletion  *completion,
                                                  GtkTreeModel        *model,
                                                  GtkTreeIter         *iter,
                                                  LibBalsaAddressView *address_view);

static void lbav_entry_changed_cb(GtkEntry            *entry,
                                  LibBalsaAddressView *address_view);

static void lbav_insert_text_cb(GtkEditable         *editable,
                                const gchar         *text,
                                gint                 length,
                                gint                *position,
                                LibBalsaAddressView *address_view);

static void lbav_notify_has_focus_cb(GtkEntry            *entry,
                                     GParamSpec          *pspec,
                                     LibBalsaAddressView *address_view);

static void lbav_insert_row(LibBalsaAddressView    *address_view,
                            gint                    row,
                            gint                    type,
                            LibbalsaAddressViewIcon icon);

static gint lbav_ensure_blank_row(LibBalsaAddressView *address_view,
                                  gint                 type);

/*
 *     Helpers
 */

/*
 *     Create a GList of addresses matching the prefix.
 */
static GList *
lbav_get_matching_addresses(LibBalsaAddressView         *address_view,
                            const gchar                 *prefix,
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
        if ((type == LIBBALSA_ADDRESS_VIEW_MATCH_FAST)
            && (!libbalsa_address_book_get_expand_aliases(ab) ||
                libbalsa_address_book_get_is_expensive(ab))) {
            continue;
        }

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
lbav_append_addresses(LibBalsaAddressView *address_view,
                      GtkEntryCompletion  *completion,
                      GList               *match,
                      const gchar         *prefix)
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
lbav_entry_setup_matches(LibBalsaAddressView         *address_view,
                         GtkEntry                    *entry,
                         GtkEntryCompletion          *completion,
                         LibBalsaAddressViewMatchType type)
{
    const gchar *prefix;
    GList *match = NULL;

    prefix = gtk_entry_get_text(entry);
    if (*prefix) {
        match = lbav_get_matching_addresses(address_view, prefix, type);
    }
    lbav_append_addresses(address_view, completion, match, prefix);
    g_list_free_full(match, g_object_unref);
}


/*
 *     Get a type-string
 */
static const gchar *
lbav_type_string(LibBalsaAddressView *address_view,
                 gint                 type)
{
    return address_view->n_types > 0 ? address_view->types[type] : "";
}


/*
 * Callback for the address-book button
 */
static void
lbav_book_button_clicked(GtkButton           *button,
                         LibBalsaAddressView *address_view)
{
    g_signal_emit(address_view,
                  address_view_signals[OPEN_ADDRESS_BOOK], 0, button);
}


/*
 * Callback for the close button
 */

static void
lbav_close_button_clicked(GtkWidget           *button,
                          LibBalsaAddressView *address_view)
{
    gint row;
    GtkWidget *child;
    gint type;

    for (row = 0; (child = lbav_get_button(address_view, row)) != NULL; row++) {
        if (child == button) {
            break;
        }
    }

    child = lbav_get_combo(address_view, row);
    type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));

    lbav_remove_row(address_view, row);

    /* Make sure the view has at least one row: */
    lbav_ensure_blank_row(address_view, type);
    g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
}


/*
 * Create an action button for a row
 */

static void
lbav_set_button(LibBalsaAddressView    *address_view,
                gint                    row,
                LibbalsaAddressViewIcon icon)
{
    GtkGrid *grid = (GtkGrid *) address_view;
    GtkWidget *button;
    GtkWidget *image;

    /*
     * Attach a button with the requested icon
     */
    button = gtk_button_new();

    if (icon == WITH_BOOK_ICON) {
        image = gtk_image_new_from_pixbuf(lbav_book_icon);
        g_signal_connect(button, "clicked",
                         G_CALLBACK(lbav_book_button_clicked), address_view);
    } else { /*icon == WITH_CLOSE_ICON */
        image = gtk_image_new_from_pixbuf(lbav_close_icon);
        g_signal_connect(button, "clicked",
                         G_CALLBACK(lbav_close_button_clicked), address_view);
    }

    gtk_container_add(GTK_CONTAINER(button), image);
    gtk_grid_attach(grid, button,
                    LIBBALSA_ADDRESS_VIEW_BUTTON_COLUMN, row, 1, 1);
}


/*
 * Callback for the combo_box "changed"signal
 */
static gboolean
lbav_combo_changed_idle(LibBalsaAddressView *address_view)
{
    /* The type of an address was changed. We make it the last address
     * of the new type, moving the row if necessary. */
    GtkWidget *combo_box = address_view->changed_combo;
    gint row;
    GtkWidget *child;
    gint old_row;
    const gchar *name;
    gint new_type;

    /* Find the row of the address whose type was changed */
    for (row = 0; (child = lbav_get_combo(address_view, row)) != NULL; row++) {
        if (child == combo_box) {
            break;
        }
    }
    old_row = row;

    /* Save the address */
    child = lbav_get_entry(address_view, row);
    name = gtk_entry_get_text(GTK_ENTRY(child));

    /* Find the new row for the addrress */
    new_type = gtk_combo_box_get_active(GTK_COMBO_BOX(combo_box));
    for (row = 0; (child = lbav_get_combo(address_view, row)) != NULL; row++) {
        gint type;

        if (row == old_row) {
            continue;
        }

        type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));
        if (type > new_type) {
            break;
        }
    }

    if (row != old_row) {
        /* Move the address to the new row */
        LibbalsaAddressViewIcon icon;

        icon = (name == NULL || name[0] == '\0') ? WITH_BOOK_ICON : WITH_CLOSE_ICON;
        lbav_insert_row(address_view, row, new_type, icon);
        child = lbav_get_entry(address_view, row);
        gtk_entry_set_text(GTK_ENTRY(child), name);

        if (old_row > row) {
            ++old_row;
        }
        lbav_remove_row(address_view, old_row);

        lbav_ensure_blank_row(address_view, new_type);
        g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
    }

    g_object_unref(combo_box);
    g_object_unref(address_view);

    return G_SOURCE_REMOVE;
}


static void
lbav_combo_changed(GtkComboBox         *combo_box,
                   LibBalsaAddressView *address_view)
{
    address_view->changed_combo = (GtkWidget *) g_object_ref(combo_box);
    g_idle_add((GSourceFunc) lbav_combo_changed_idle, g_object_ref(address_view));
}


/*
 * Callback for the entry "activate" signal
 */
static void
lbav_entry_activated(GtkEntry            *entry,
                     LibBalsaAddressView *address_view)
{
    gint row;
    GtkWidget *child;
    gint type;

    for (row = 0; (child = lbav_get_entry(address_view, row)) != NULL; row++) {
        if (child == (GtkWidget *) entry) {
            break;
        }
    }

    child = lbav_get_button(address_view, row);
    gtk_widget_destroy(child);
    lbav_set_button(address_view, row, WITH_CLOSE_ICON);

    child = lbav_get_combo(address_view, row);
    type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));
    lbav_ensure_blank_row(address_view, type);

    g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
}


/*
 * Insert and populate a row
 */

static void
lbav_insert_row(LibBalsaAddressView    *address_view,
                gint                    row,
                gint                    type,
                LibbalsaAddressViewIcon icon)
{
    GtkGrid *grid = (GtkGrid *) address_view;
    GtkWidget *entry;
    GtkEntryCompletion *completion;
    GtkListStore *store;

    gtk_grid_insert_row(grid, row);

    /*
     * Attach a button with the requested icon
     */
    lbav_set_button(address_view, row, icon);

    /*
     * Attach a combo-box for choosing the type of address, if there are
     * more than one type
     */
    if (address_view->n_types > 0) {
        GtkWidget *combo_box;
        gint i;

        combo_box = gtk_combo_box_text_new();
        for (i = 0; i < address_view->n_types; i++) {
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo_box),
                                      NULL, _(address_view->types[i]));
        }

        gtk_combo_box_set_active(GTK_COMBO_BOX(combo_box), type);
        g_signal_connect(combo_box, "changed",
                         G_CALLBACK(lbav_combo_changed), address_view);
        gtk_grid_attach(grid, combo_box,
                        LIBBALSA_ADDRESS_VIEW_COMBO_COLUMN, row, 1, 1);
    }

    /*
     * Attach an entry for the address
     */
    entry = libbalsa_address_view_entry_new(address_view);
    gtk_widget_set_hexpand(entry, TRUE);

    completion = gtk_entry_completion_new();
    store = gtk_list_store_new(1, G_TYPE_STRING);
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

    gtk_entry_set_completion(GTK_ENTRY(entry), completion);
    g_object_unref(completion);

    g_signal_connect(entry, "activate",
                     G_CALLBACK(lbav_entry_activated), address_view);
    g_signal_connect(entry, "changed",
                     G_CALLBACK(lbav_entry_changed_cb), address_view);
    g_signal_connect(entry, "insert-text",
                     G_CALLBACK(lbav_insert_text_cb), address_view);
    g_signal_connect_after(entry, "notify::has-focus",
                           G_CALLBACK(lbav_notify_has_focus_cb), address_view);

    gtk_grid_attach(grid, entry,
                    LIBBALSA_ADDRESS_VIEW_ENTRY_COLUMN, row, 1, 1);
    if (icon == WITH_BOOK_ICON) {
        gtk_widget_grab_focus(entry);
    }
}


/*
 *     Make sure we have a blank line; use the specified type.
 */

static gint
lbav_ensure_blank_row(LibBalsaAddressView *address_view,
                      gint                 type)
{
    gint row;
    GtkWidget *child;

    g_assert(address_view->n_types == 0 || type < address_view->n_types);

    /* Remove all existing blank rows */
    for (row = 0; (child = lbav_get_entry(address_view, row)) != NULL; /* nothing */) {
        const gchar *name;

        name = gtk_entry_get_text(GTK_ENTRY(child));
        if ((name == NULL) || (name[0] == '\0')) {
            lbav_remove_row(address_view, row);
        } else {
            /* Make sure the row has a close button. */
            child = lbav_get_button(address_view, row);
            gtk_widget_destroy(child);
            lbav_set_button(address_view, row, WITH_CLOSE_ICON);
            ++row;
        }
    }

    /* Find the last row matching type */
    for (row = 0; (child = lbav_get_combo(address_view, row)) != NULL; row++) {
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(child)) > type) {
            break;
        }
    }

    lbav_insert_row(address_view, row, type, WITH_BOOK_ICON);

    address_view->last_was_escape = FALSE;

    return row;
}


/*
 *     Replace non-printable chars by spaces
 */
static void
lbav_clean_text(gchar *text)
{
    gchar *p;
    gboolean was_graph = FALSE;

    p = text;
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        gchar *next = g_utf8_next_char(p);

        if (g_unichar_isprint(c)) {
            was_graph = g_unichar_isgraph(c);
            p = next;
        } else {
            if (was_graph) {
                *p++ = ' ';
            }
            was_graph = FALSE;
            if (p != next) {
                memmove(p, next, strlen(next) + 1);
            }
        }
    }
}


/*
 *     Add the addresses in an InternetAddressList, starting at row and
 *     inserting lines of the same type as necessary;
 *     on return, row points to the last line inserted.
 */
static void
lbav_add_from_list(LibBalsaAddressView *address_view,
                   gint                 row,
                   InternetAddressList *list)
{
    gint type;
    int i;

    for (i = 0; i < internet_address_list_length(list); i++) {
        InternetAddress *ia = internet_address_list_get_address(list, i);
        gchar *name = internet_address_to_string(ia, FALSE);
        GtkWidget *child;

        libbalsa_utf8_sanitize(&name, address_view->fallback, NULL);
        lbav_clean_text(name);

        if (i == 0) {
            child = lbav_get_combo(address_view, row);
            type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));

            child = lbav_get_button(address_view, row);
            gtk_widget_destroy(child);
            lbav_set_button(address_view, row, WITH_CLOSE_ICON);
        } else {
            lbav_insert_row(address_view, ++row, type, WITH_CLOSE_ICON);
        }

        child = lbav_get_entry(address_view, row);
        gtk_entry_set_text(GTK_ENTRY(child), name);
        g_free(name);
    }
    g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
}


/*
 *     Add the addresses in a string, starting at row and
 *     inserting lines of the same type as necessary;
 *     returns TRUE if any valid addresses were found.
 */
static gboolean
lbav_add_from_string(LibBalsaAddressView *address_view,
                     gint                 row,
                     const gchar         *string)
{
    InternetAddressList *list;
    gboolean retval = FALSE;

    list = internet_address_list_parse_string(string);
    if (list != NULL) {
        if ((retval = (internet_address_list_length(list) > 0))) {
            lbav_add_from_list(address_view, row, list);
        }
        g_object_unref(list);
    }

    return retval;
}


/*
 *     Remove addresses of the given type
 */
static void
lbav_remove_type(LibBalsaAddressView *address_view,
                 gint                 type)
{
    gint row;
    GtkWidget *child;

    for (row = 0; (child = lbav_get_combo(address_view, row)) != NULL; /* nothing */) {
        gint this_type;

        this_type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));

        if (this_type == type) {
            lbav_remove_row(address_view, row);
        } else {
            ++row;
        }
    }

    g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
}


/*
 *     Add the addresses
 */
static void
lbav_add_addresses(LibBalsaAddressView *address_view,
                   gint                 type,
                   const gchar         *addresses)
{
    if ((addresses != NULL) && (addresses[0] != '\0')) {
        gint row;

        row = lbav_ensure_blank_row(address_view, type);
        lbav_add_from_string(address_view, row, addresses);
        lbav_ensure_blank_row(address_view, type);
        g_signal_emit(address_view, address_view_signals[VIEW_CHANGED], 0);
    } else {
        lbav_ensure_blank_row(address_view, 0);
    }
}


/*
 *     Find an address_type
 */
static guint
lbav_get_type(LibBalsaAddressView *address_view,
              const gchar         *address_type)
{
    gint type;

    for (type = 0; type < address_view->n_types; type++) {
        if (strcmp(address_type, lbav_type_string(address_view, type)) == 0) {
            break;
        }
    }

    return type;
}


/*
 * Callbacks.
 */

/*
 *     Callback for the entry's "changed" signal
 */
static void
lbav_entry_changed_cb(GtkEntry            *entry,
                      LibBalsaAddressView *address_view)
{
    GtkEntryCompletion *completion;

    completion = gtk_entry_get_completion(entry);

    if (gtk_widget_get_window(GTK_WIDGET(entry))) {
        lbav_entry_setup_matches(address_view, entry, completion,
                                 LIBBALSA_ADDRESS_VIEW_MATCH_FAST);
    }
    address_view->last_was_escape = FALSE;
}


/*
 *     Class method for LibBalsaAddressViewEntry::popup-completions
 */
static void
lbav_popup_completions(LibBalsaAddressViewEntry *view_entry)
{
    LibBalsaAddressView *address_view = view_entry->address_view;
    GtkEntry *entry = (GtkEntry *) view_entry;
    GtkEntryCompletion *completion;

    if (address_view->last_was_escape) {
        address_view->last_was_escape = FALSE;
        return;
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
}


/*
 *     Callback for the entry's "insert_text" event -
 *     replace control chars by spaces
 */
static void
lbav_insert_text_cb(GtkEditable         *editable,
                    const gchar         *text,
                    gint                 length,
                    gint                *position,
                    LibBalsaAddressView *address_view)
{
    gchar *p;
    gchar *ins_text = g_strndup(text, length);

    /* replace non-printable chars by spaces */
    p = ins_text;
    while (*p != '\0') {
        gchar *next = g_utf8_next_char(p);

        if (g_unichar_isprint(g_utf8_get_char(p))) {
            p = next;
        } else {
            *p++ = ' ';
            if (p != next) {
                memmove(p, next, strlen(next) + 1);
            }
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
lbav_completion_match_func(GtkEntryCompletion  *completion,
                           const gchar         *key,
                           GtkTreeIter         *iter,
                           LibBalsaAddressView *address_view)
{
    return TRUE;
}


/*
 *     Callback for the completion's "match-selected" signal
 */
static gboolean
lbav_completion_match_selected_cb(GtkEntryCompletion  *completion,
                                  GtkTreeModel        *model,
                                  GtkTreeIter         *iter,
                                  LibBalsaAddressView *address_view)
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
 * notify::has-focus callback
 * If only one completion matches, fill it into the entry
 */
static void
lbav_notify_has_focus_cb(GtkEntry            *entry,
                         GParamSpec          *pspec,
                         LibBalsaAddressView *address_view)
{
    const gchar *name;
    GList *match;

    if (gtk_widget_has_focus(GTK_WIDGET(entry))) {
        /* Not a focus-out event */
        return;
    }

    name = gtk_entry_get_text(entry);

    if ((name == NULL) || (name[0] == '\0')) {
        /* No text to match */
        return;
    }

    /* If the user touched Esc, all matches were shown, so that is the
     * list that we check; otherwise, the user has seen only fast matches,
     * so that is the list we check. */
    match = lbav_get_matching_addresses(address_view, name,
                                        address_view->last_was_escape ?
                                        LIBBALSA_ADDRESS_VIEW_MATCH_ALL :
                                        LIBBALSA_ADDRESS_VIEW_MATCH_FAST);

    if (match == NULL) {
        /* No matching addresses */
        return;
    }

    if (match->next == NULL) {
        /* Only one match */
        gchar *the_addr;

        the_addr = internet_address_to_string(match->data, FALSE);

        g_signal_handlers_block_by_func(entry,
                                        lbav_entry_changed_cb,
                                        address_view);
        gtk_entry_set_text(entry, the_addr);
        g_signal_handlers_unblock_by_func(entry,
                                          lbav_entry_changed_cb,
                                          address_view);

        g_free(the_addr);

        lbav_entry_activated(entry, address_view);
    }

    g_list_free_full(match, g_object_unref);
}


/*
 *     Public API.
 */

/*
 *     Allocate a new LibBalsaAddressView for use.
 */
LibBalsaAddressView *
libbalsa_address_view_new(const gchar *const *types,
                          guint               n_types,
                          GList              *address_book_list,
                          gboolean            fallback)
{
    LibBalsaAddressView *address_view;

    /* The widget: */
    address_view = g_object_new(LIBBALSA_TYPE_ADDRESS_VIEW, NULL);

    address_view->types = types;
    address_view->n_types = (gint) n_types;
    address_view->address_book_list = address_book_list;
    address_view->fallback = fallback;

    lbav_ensure_blank_row(address_view, 0);

    return address_view;
}


/*
 *     Set default domain.
 */
void
libbalsa_address_view_set_domain(LibBalsaAddressView *address_view,
                                 const gchar         *domain)
{
    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    g_free(address_view->domain);
    address_view->domain = g_strdup(domain);
}


/*
 *     Set the addresses
 */
void
libbalsa_address_view_set_from_string(LibBalsaAddressView *address_view,
                                      const gchar         *address_type,
                                      const gchar         *addresses)
{
    gint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

    lbav_remove_type(address_view, type);
    lbav_add_addresses(address_view, type, addresses);
}


/*
 *     Add addresses
 */
void
libbalsa_address_view_add_from_string(LibBalsaAddressView *address_view,
                                      const gchar         *address_type,
                                      const gchar         *addresses)
{
    gint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

    lbav_add_addresses(address_view, type, addresses);
}


/*
 *     Add addresses to an existing row
 */
void
libbalsa_address_view_add_to_row(LibBalsaAddressView *address_view,
                                 GtkWidget           *button,
                                 const gchar         *addresses)
{
    gint row;
    GtkWidget *child;
    gint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    for (row = 0; (child = lbav_get_button(address_view, row)) != NULL; row++) {
        if (child == button) {
            break;
        }
    }

    lbav_add_from_string(address_view, row, addresses);

    child = lbav_get_combo(address_view, row);
    type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));
    lbav_ensure_blank_row(address_view, type);
}


/*
 *     Set the address from an InternetAddressList
 */
void
libbalsa_address_view_set_from_list(LibBalsaAddressView *address_view,
                                    const gchar         *address_type,
                                    InternetAddressList *list)
{
    gint type;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view));

    type = lbav_get_type(address_view, address_type);
    g_return_if_fail(address_view->n_types == 0
                     || type < address_view->n_types);

    lbav_remove_type(address_view, type);

    if ((list != NULL) && (internet_address_list_length(list) > 0)) {
        gint row;

        row = lbav_ensure_blank_row(address_view, type);
        lbav_add_from_list(address_view, row, list);
        lbav_ensure_blank_row(address_view, type);
    } else {
        lbav_ensure_blank_row(address_view, 0);
    }
}


/*
 *     Number of complete addresses, or -1 if any is incomplete.
 */
gint
libbalsa_address_view_n_addresses(LibBalsaAddressView *address_view)
{
    gint addresses = 0;
    gint type;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), -1);

    for (type = 0; type < address_view->n_types; type++) {
        InternetAddressList *list =
            libbalsa_address_view_get_list(address_view,
                                           lbav_type_string(address_view, type));
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
libbalsa_address_view_get_list(LibBalsaAddressView *address_view,
                               const gchar         *address_type)
{
    gint type;
    InternetAddressList *address_list;
    gint row;
    GtkWidget *child;

    g_return_val_if_fail(LIBBALSA_IS_ADDRESS_VIEW(address_view), NULL);

    type = lbav_get_type(address_view, address_type);
    g_return_val_if_fail(address_view->n_types == 0
                         || type < address_view->n_types, NULL);

    address_list = internet_address_list_new();
    for (row = 0; (child = lbav_get_combo(address_view, row)) != NULL; row++) {
        gint this_type;

        this_type = gtk_combo_box_get_active(GTK_COMBO_BOX(child));

        if (this_type == type) {
            const gchar *name;
            InternetAddressList *tmp_list;

            child = lbav_get_entry(address_view, row);
            name = gtk_entry_get_text(GTK_ENTRY(child));
            tmp_list = internet_address_list_parse_string(name);
            if (tmp_list != NULL) {
                internet_address_list_append(address_list, tmp_list);
                g_object_unref(tmp_list);
            }
        }
    }

    return address_list;
}


void
libbalsa_address_view_set_book_icon(GdkPixbuf *book_icon)
{
    g_set_object(&lbav_book_icon, book_icon);
}


void
libbalsa_address_view_set_close_icon(GdkPixbuf *close_icon)
{
    g_set_object(&lbav_close_icon, close_icon);
}


void
libbalsa_address_view_set_drop_down_icon(GdkPixbuf *drop_down_icon)
{
    g_set_object(&lbav_drop_down_icon, drop_down_icon);
}
