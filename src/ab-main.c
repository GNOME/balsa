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

#include <stdlib.h>
#include <string.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#if HAVE_MACOSX_DESKTOP
#  include <ige-mac-integration.h>
#endif

#include "address-book.h"
#include "address-book-vcard.h"
#include "address-book-extern.h"
#include "address-book-ldif.h"
#if ENABLE_LDAP
#include "address-book-ldap.h"
#endif /* ENABLE_LDAP */
#if HAVE_SQLITE
#include "address-book-gpe.h"
#endif /* HAVE_SQLITE */
#if HAVE_RUBRICA
#include "address-book-rubrica.h"
#endif /* HAVE_RUBRICA */
#include "address-book-config.h"
#include "application-helpers.h"
#include "libbalsa-conf.h"
#include "libbalsa.h"
#include <glib/gi18n.h>

struct ABMainWindow {
    GtkWindow *window;
    GtkWidget *notebook; /**< notebook containing browse and edit pages. */
    GtkWidget *entry_list; /* GtkTreeView widget */
    GtkWidget *apply_button, *remove_button, *cancel_button;
    GtkWidget *edit_widget;
    GtkWidget *entries[NUM_FIELDS];

    GList *address_book_list;
    gchar *default_address_book_prefix;
    LibBalsaAddressBook *default_address_book;
    LibBalsaAddressBook* address_book;
    LibBalsaAddress *displayed_address;

    GMenu *file_menu;
} contacts_app;


static void
bab_cleanup(void)
{
    gtk_main_quit();
}

static void ab_set_edit_widget(LibBalsaAddress * address,
                               gboolean can_remove);
static void ab_clear_edit_widget(void);

#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
static gboolean
bab_config_init(const gchar * group, const gchar * value, gpointer data)
{
    LibBalsaAddressBook *address_book;

    address_book = libbalsa_address_book_new_from_config(group);
    if (address_book) {
        contacts_app.address_book_list =
            g_list_append(contacts_app.address_book_list, address_book);

        if (contacts_app.default_address_book_prefix
            && strcmp(group,
                      contacts_app.default_address_book_prefix) == 0)
            contacts_app.default_address_book = address_book;
    }

    return FALSE;
}

static void ab_warning(const char *fmt, ...)
	G_GNUC_PRINTF(1, 2);

enum {
    LIST_COLUMN_NAME,
    LIST_COLUMN_ADDRSPEC,
    LIST_COLUMN_ADDRESS,
    N_COLUMNS
};


/*
  The address load callback. Adds a single address to the address list.

  If the current address book is in dist list mode then create a
  single entry, or else create an entry for each address in the book.
 */
static void
bab_load_cb(LibBalsaAddressBook *libbalsa_ab,
            LibBalsaAddress *address, GtkTreeModel *model)
{
    GtkTreeIter iter;

    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(libbalsa_ab));

    if (address == NULL)
	return;

    if (libbalsa_address_is_dist_list(libbalsa_ab, address)) {
        gchar *address_string = libbalsa_address_to_gchar(address, -1);

        gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
        /* GtkListStore refs address, and unrefs it when cleared  */
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           LIST_COLUMN_NAME, address->full_name,
                           LIST_COLUMN_ADDRSPEC, address_string,
                           LIST_COLUMN_ADDRESS, address,
                           -1);

	g_free(address_string);
    } else {
        GList *l;

	for (l = address->address_list; l; l = l->next) {
            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            /* GtkListStore refs address once for each address in
             * the list, and unrefs it the same number of times when
             * cleared */
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               LIST_COLUMN_NAME, address->full_name,
                               LIST_COLUMN_ADDRSPEC, l->data,
                               LIST_COLUMN_ADDRESS, address,
                               -1);
	}
    }
}

static gboolean
bab_set_address_book(LibBalsaAddressBook * ab,
                     GtkWidget           * list,
                     const gchar         * filter)
{
    GtkTreeModel *model;
    LibBalsaABErr ab_err;

    g_return_val_if_fail(ab, FALSE);

    contacts_app.address_book = ab;

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    gtk_list_store_clear(GTK_LIST_STORE(model));
    if ((ab_err =
         libbalsa_address_book_load(ab, filter,
                                    (LibBalsaAddressBookLoadFunc)
                                    bab_load_cb, model)) != LBABERR_OK) {
        printf("error loading address book from %s: %d\n", ab->name,
               ab_err);
    }

    return TRUE;
}

static void
bab_window_set_title(LibBalsaAddressBook * address_book)
{
    const gchar *type = "";
    gchar *title;

    if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book))
        type = "vCard";
    else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERN(address_book))
        type = "External query";
    else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book))
        type = "LDIF";
#if ENABLE_LDAP
    else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
        type = "LDAP";
#endif
#if HAVE_SQLITE
    else if (LIBBALSA_IS_ADDRESS_BOOK_GPE(address_book))
        type = "GPE";
#endif
#if HAVE_RUBRICA
    else if (LIBBALSA_IS_ADDRESS_BOOK_RUBRICA(address_book))
        type = "Rubrica";
#endif /* HAVE_RUBRICA */

    title =
        g_strconcat(type, _(" address book: "), address_book->name, NULL);
    gtk_window_set_title(contacts_app.window, title);
    g_free(title);
}

static void
address_book_change_state(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    const gchar *value;
    GList *l;
    LibBalsaAddressBook *address_book;

    value = g_variant_get_string(state, NULL);
    for (l = contacts_app.address_book_list; l; l = l->next) {
        address_book = l->data;
        if (address_book && strcmp(value, address_book->name) == 0)
            break;
    }

    if (!l || !(address_book = l->data))
        return;

    ab_clear_edit_widget();
    bab_set_address_book(address_book, contacts_app.entry_list, NULL);
    bab_window_set_title(address_book);

    g_simple_action_set_state(action, state);
}

static void
address_changed_cb(struct ABMainWindow *aw)
{
    gtk_widget_set_sensitive(aw->apply_button, TRUE);
    gtk_widget_set_sensitive(aw->cancel_button, TRUE);
}

/* File menu callback helpers */

static void
set_address_book_menu_items(void)
{
    GString *string;
    GList *l;
    guint pos;
    gchar *s;
    GtkBuilder *builder;
    GMenuModel *menu_model;

    pos = g_menu_model_get_n_items(G_MENU_MODEL (contacts_app.file_menu));
    g_menu_remove(contacts_app.file_menu, --pos);

    pos = 0;
    string = g_string_new(NULL);
    g_string_append(string, "<interface>");
    g_string_append(string, "<menu id='address-book-menu'>");
    g_string_append(string, "<section>");
    for (l = contacts_app.address_book_list; l; l = l->next) {
        LibBalsaAddressBook *address_book = l->data;

        if (!address_book)
            continue;

        g_string_append(string, "<item>");

        g_string_append(string, "<attribute name='label'>");
        g_string_append_printf(string, "_%d:%s", ++pos,
                               address_book->name);
        g_string_append(string, "</attribute>");

        g_string_append(string, "<attribute name='action'>");
        g_string_append(string, "win.address-book");
        g_string_append(string, "</attribute>");

        g_string_append(string, "<attribute name='target'>");
        g_string_append(string, address_book->name);
        g_string_append(string, "</attribute>");

        g_string_append(string, "<attribute name='accel'>");
        g_string_append_printf(string, "&lt;Primary&gt;%d", pos);
        g_string_append(string, "</attribute>");

        g_string_append(string, "</item>");
    }
    g_string_append(string, "</section>");
    g_string_append(string, "</menu>");
    g_string_append(string, "</interface>");
    s = g_string_free(string, FALSE);

    builder = gtk_builder_new_from_string(s, -1);
    g_free(s);

    menu_model =
        G_MENU_MODEL(gtk_builder_get_object(builder, "address-book-menu"));
    g_menu_append_section(contacts_app.file_menu, NULL, menu_model);
    libbalsa_window_set_accels(GTK_APPLICATION_WINDOW(contacts_app.window),
                               menu_model);

    g_object_unref(builder);
}

static gboolean
get_used_group(const gchar * key, const gchar * value, gpointer data)
{
    gint *max = data;
    gint curr;

    if (*value && (curr = atoi(value)) > *max)
        *max = curr;

    return FALSE;
}

static gchar *
get_unused_group(const gchar * prefix)
{
    gint max = 0;

    libbalsa_conf_foreach_group(prefix, get_used_group, &max);

    return g_strdup_printf("%s%d", prefix, ++max);
}

#define BAB_PREFIX_LEN 3        /* strlen("_1:") */

static void
address_book_change(LibBalsaAddressBook * address_book, gboolean append)
{
    gchar *group;

    if (append)
        contacts_app.address_book_list =
            g_list_append(contacts_app.address_book_list, address_book);

    set_address_book_menu_items();
    bab_window_set_title(address_book);

    group = address_book->config_prefix ?
        g_strdup(address_book->config_prefix) :
        get_unused_group(ADDRESS_BOOK_SECTION_PREFIX);
    libbalsa_address_book_save_config(address_book, group);
    g_free(group);

    libbalsa_conf_queue_sync();
}

static void
file_new_vcard_activated(GSimpleAction * action,
                         GVariant      * state,
                         gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD, address_book_change,
         contacts_app.window);
}

static void
file_new_extern_activated(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN, address_book_change,
         contacts_app.window);
}

static void
file_new_ldif_activated(GSimpleAction * action,
                        GVariant      * state,
                        gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_LDIF, address_book_change,
         contacts_app.window);
}

#if ENABLE_LDAP
static void
file_new_ldap_activated(GSimpleAction * action,
                        GVariant      * state,
                        gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_LDAP, address_book_change,
         contacts_app.window);
}
#endif /* ENABLE_LDAP */

#if HAVE_SQLITE
static void
file_new_gpe_activated(GSimpleAction * action,
                       GVariant      * state,
                       gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_GPE, address_book_change,
         contacts_app.window);
}
#endif /* HAVE_SQLITE */

#if HAVE_RUBRICA
static void
file_new_rubrica_activated(GSimpleAction * action,
                           GVariant      * state,
                           gpointer        user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA, address_book_change,
         contacts_app.window);
}
#endif /* HAVE_RUBRICA */

static void
file_properties_activated(GSimpleAction * action,
                          GVariant      * state,
                          gpointer        user_data)
{
    LibBalsaAddressBook *address_book;

    if (!(address_book = contacts_app.address_book))
        return;

    balsa_address_book_config_new(address_book, address_book_change,
                                  contacts_app.window);
}

static void
file_delete_activated(GSimpleAction * action,
                      GVariant      * state,
                      gpointer        user_data)
{
    LibBalsaAddressBook *address_book;
    GList *list;

    if ((address_book = contacts_app.address_book) == NULL
        || contacts_app.address_book_list->next == NULL)
        return;

    if (address_book->config_prefix) {
        libbalsa_conf_remove_group(address_book->config_prefix);
        libbalsa_conf_private_remove_group(address_book->config_prefix);
        libbalsa_conf_queue_sync();
    }

    /* Leave a NULL item in the address book list, to avoid changing the
     * positions of the other books. */
    list = g_list_find(contacts_app.address_book_list, address_book);
    list->data = NULL;
    g_object_unref(address_book);

    for (list = contacts_app.address_book_list; list; list = list->next)
        if ((address_book = list->data) != NULL)
            break;

    if (!list)
        return;

    contacts_app.address_book = list->data;
    set_address_book_menu_items();
}

static void
file_quit_activated(GSimpleAction * action,
                    GVariant      * state,
                    gpointer        user_data)
{
    gtk_main_quit();
}

static void
entry_new_activated(GSimpleAction * action,
                    GVariant      * state,
                    gpointer        user_data)
{
    GtkTreeSelection *selection;

    contacts_app.displayed_address = NULL;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(contacts_app.notebook), 1);
    ab_set_edit_widget(NULL, FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, FALSE);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW
                                            (contacts_app.entry_list));
    gtk_tree_selection_unselect_all(selection);
    gtk_widget_grab_focus(contacts_app.edit_widget);
}

static LibBalsaABErr
ab_remove_address(LibBalsaAddress* address)
{
    LibBalsaABErr err = LBABERR_OK;

    g_return_val_if_fail(address, err);

    libbalsa_address_book_remove_address(contacts_app.address_book, address);

    if(err == LBABERR_OK) {
        GtkTreeIter       iter;
        GtkTreeSelection *selection;
        GtkTreeView  *v = GTK_TREE_VIEW(contacts_app.entry_list);
        GtkTreeModel *m;
        selection       = gtk_tree_view_get_selection(v);
        if(gtk_tree_selection_get_selected(selection, &m, &iter))
            gtk_list_store_remove(GTK_LIST_STORE(m), &iter);
	if(address == contacts_app.displayed_address) {
	    ab_clear_edit_widget();
	    contacts_app.displayed_address = NULL;
	}
    } else
        ab_warning("Cannot remove: %s\n",
                   libbalsa_address_book_strerror(contacts_app.address_book,
                                                  err));
    return err;
}

static void
entry_delete_activated(GSimpleAction * action,
                       GVariant      * state,
                       gpointer        user_data)
{
    GtkTreeView  *v = GTK_TREE_VIEW(contacts_app.entry_list);
    GtkTreeSelection *selection = gtk_tree_view_get_selection(v);
    GtkTreeModel *model;
    GtkTreeIter   iter;

    if(gtk_tree_selection_get_selected(selection, &model, &iter)) {
	GValue gv = {0,};
	LibBalsaAddress *address;

	gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
	address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
	if (address)
	    ab_remove_address(address);
    }
}

static void
help_about_activated(GSimpleAction * action,
                     GVariant      * state,
                     gpointer        user_data)
{
    /* Help? */
}

static void
get_main_menu(GtkApplication * application)
{
    static GActionEntry win_entries[] = {
        {"file-new-vcard",      file_new_vcard_activated},
        {"file-new-external",   file_new_extern_activated},
        {"file-new-ldif",       file_new_ldif_activated},
#if ENABLE_LDAP
        {"file-new-ldap",       file_new_ldap_activated},
#endif /* ENABLE_LDAP */
#if HAVE_SQLITE
        {"file-new-gpe",        file_new_gpe_activated},
#endif /* HAVE_SQLITE */
#if HAVE_RUBRICA
        {"file-new-rubrica",    file_new_rubrica_activated},
#endif /* HAVE_RUBRICA */
        {"file-properties",     file_properties_activated},
        {"file-delete",         file_delete_activated},
        {"file-quit",           file_quit_activated},
        {"entry-new",           entry_new_activated},
        {"entry-delete",        entry_delete_activated},
        {"help-about",          help_about_activated},
        {"address-book",        libbalsa_radio_activated, "s", "''",
            address_book_change_state},
    };
    GtkBuilder *builder;
    gchar *ui_file;
    GError *err = NULL;

    builder = gtk_builder_new();
    ui_file = g_build_filename(BALSA_DATA_PREFIX, "ui", "ab-main.ui",
                               NULL);
    if (gtk_builder_add_from_file(builder, ui_file, &err)) {
        gtk_application_set_menubar(application,
                                    G_MENU_MODEL(gtk_builder_get_object
                                                 (builder, "menubar")));
        contacts_app.file_menu =
            G_MENU(gtk_builder_get_object(builder, "file-menu"));
    } else {
        g_print("%s error: %s\n", __func__, err->message);
        g_error_free(err);
    }
    g_free(ui_file);
    g_object_unref(builder);

    g_action_map_add_action_entries(G_ACTION_MAP(contacts_app.window),
                                    win_entries, G_N_ELEMENTS(win_entries),
                                    contacts_app.window);

    set_address_book_menu_items();
}

static void
list_row_activated_cb(GtkTreeView *tview, gpointer data);

static void
ab_set_edit_widget(LibBalsaAddress * address, gboolean can_remove)
{
    libbalsa_address_set_edit_entries(address, contacts_app.entries);
    gtk_widget_show(contacts_app.edit_widget);
    gtk_widget_set_sensitive(contacts_app.apply_button, FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, can_remove);
    gtk_widget_set_sensitive(contacts_app.cancel_button, TRUE);
}

static void
ab_clear_edit_widget(void)
{
    gtk_widget_hide(contacts_app.edit_widget);
    gtk_widget_set_sensitive(contacts_app.apply_button,  FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, FALSE);
    gtk_widget_set_sensitive(contacts_app.cancel_button, FALSE);
    contacts_app.displayed_address = NULL;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(contacts_app.notebook), 0);
}

static void
list_selection_changed_cb(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GValue gv = {0,};
    LibBalsaAddress *address;

    if(!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
    address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
    if (address)
        ab_set_edit_widget(address, TRUE);
    else
        ab_clear_edit_widget();
    g_value_unset(&gv);
    contacts_app.displayed_address = address;
}

static void
list_row_activated_cb(GtkTreeView *tree, gpointer data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GValue gv = {0,};
    LibBalsaAddress *address;

    selection = gtk_tree_view_get_selection(tree);
    if(!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
    address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
    if (address) {
        ab_set_edit_widget(address, TRUE);
	printf("Switch page..\n");
	gtk_notebook_set_current_page(GTK_NOTEBOOK(contacts_app.notebook), 1);
    } else
        ab_clear_edit_widget();
    g_value_unset(&gv);
    contacts_app.displayed_address = address;
}

static void
addrlist_drag_get_cb(GtkWidget        * widget,
                     GdkDragContext   * drag_context,
                     GtkSelectionData * sel_data,
                     guint              time,
                     gpointer           user_data)
{
    const gchar *target;

    target = gtk_selection_data_get_target(sel_data);

    if (target == g_intern_static_string("x-application/x-addr")) {
        GtkTreeView *addrlist;
        GtkTreeSelection *selection;
        GtkTreeModel *model;
        GtkTreeIter iter;
        GValue gv = {0,};
        LibBalsaAddress *address;

        addrlist = GTK_TREE_VIEW(widget);
        selection = gtk_tree_view_get_selection(addrlist);
        if(!gtk_tree_selection_get_selected(selection, &model, &iter))
            return;
        gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
        address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
        gtk_selection_data_set(sel_data,
                               target,
                               8, (const guchar *) &address,
                               sizeof(LibBalsaAddress*));
    } else if (target == g_intern_static_string("text/plain") ||
               target == g_intern_static_string("STRING")) {
        g_print("Text/plain cannot be sent.\n");
    } else {
        g_print("Do not know what to do!\n");
    }
}

static GtkWidget *
bab_window_list_new(void)
{
    GtkListStore *store;
    GtkWidget *tree;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    GdkContentFormats *formats;

    store =
        gtk_list_store_new(N_COLUMNS,
                           G_TYPE_STRING,   /* LIST_COLUMN_NAME           */
                           G_TYPE_STRING,   /* LIST_COLUMN_ADDRSPEC       */
                           G_TYPE_OBJECT);  /* LIST_COLUMN_ADDRESS        */
    /*
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0,
                                    balsa_ab_window_compare_entries,
                                    GINT_TO_POINTER(0), NULL);
    */
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(store), 0,
                                         GTK_SORT_ASCENDING);

    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(selection), "changed",
                     G_CALLBACK(list_selection_changed_cb), NULL);
    g_signal_connect(G_OBJECT(tree), "row-activated",
                     G_CALLBACK(list_row_activated_cb), NULL);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("_Name"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);


    formats = gdk_content_formats_new(libbalsa_address_target_list,
                                      G_N_ELEMENTS(libbalsa_address_target_list));
    gtk_drag_source_set(GTK_WIDGET(tree),
                        GDK_BUTTON1_MASK,
                        formats,
                        GDK_ACTION_COPY);
    gdk_content_formats_unref(formats);

    g_signal_connect(G_OBJECT(tree), "drag-data-get",
                     G_CALLBACK(addrlist_drag_get_cb), NULL);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("_Address"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_ADDRSPEC, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    gtk_widget_show(tree);
    return tree;
}

static void
apply_button_cb(GtkWidget * w, gpointer data)
{
    LibBalsaAddress *newval;
    LibBalsaABErr err;

    newval = libbalsa_address_new_from_edit_entries(contacts_app.entries);
    if (!newval) {
        /* Some error--no full_name? */
        ab_warning(contacts_app.displayed_address ?
                   "Cannot modify: %s\n" : "Cannot add: %s\n",
                   "no displayed name");
        gtk_widget_set_sensitive(contacts_app.apply_button,  FALSE);
        return;
    }

    err = contacts_app.displayed_address ?
        libbalsa_address_book_modify_address(contacts_app.address_book,
                                             contacts_app.displayed_address,
                                             newval) :
        libbalsa_address_book_add_address(contacts_app.address_book,
                                          newval);

    if(err == LBABERR_OK) {
        GtkTreeSelection *selection;
        GtkTreeModel *model;
        GtkTreeIter iter;
        GtkTreePath *path = NULL;

        gtk_widget_set_sensitive(contacts_app.apply_button,  FALSE);
        gtk_widget_set_sensitive(contacts_app.remove_button, TRUE);
        gtk_widget_set_sensitive(contacts_app.cancel_button, TRUE);

        /* We need to reload the book; if we modified an existing
         * address, we really need only to reload that one address, but
         * we have no method for that. */
        selection =
            gtk_tree_view_get_selection(GTK_TREE_VIEW
                                        (contacts_app.entry_list));
        if (gtk_tree_selection_get_selected(selection, &model, &iter))
            path = gtk_tree_model_get_path(model, &iter);
        bab_set_address_book(contacts_app.address_book,
                             contacts_app.entry_list, NULL);
        if (path) {
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);
        }
        if (!gtk_tree_selection_get_selected(selection, NULL, NULL))
            ab_clear_edit_widget();
	gtk_notebook_set_current_page(GTK_NOTEBOOK(contacts_app.notebook), 0);

    } else
        ab_warning(contacts_app.displayed_address ?
                   "Cannot modify: %s\n" : "Cannot add: %s\n",
                   libbalsa_address_book_strerror(contacts_app.address_book,
                                                  err));
    g_object_unref(newval);
}

static void
remove_button_cb(GtkWidget *w, gpointer data)
{
    ab_remove_address(contacts_app.displayed_address);
}

static void
cancel_button_cb(GtkWidget * w, gpointer data)
{
    ab_clear_edit_widget();
}

static GtkWidget*
bab_get_edit_button_box(struct ABMainWindow *abmw)
{
    GtkWidget *box;
    box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->apply_button =
                      gtk_button_new_with_mnemonic(_("_Apply")));
    g_signal_connect(G_OBJECT(abmw->apply_button), "clicked",
                     G_CALLBACK(apply_button_cb), (gpointer) NULL);
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->remove_button =
                      gtk_button_new_with_mnemonic(_("_Remove")));
    g_signal_connect(G_OBJECT(abmw->remove_button), "clicked",
                     G_CALLBACK(remove_button_cb), (gpointer) NULL);
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->cancel_button =
                      gtk_button_new_with_mnemonic(_("_Cancel")));
    g_signal_connect(G_OBJECT(abmw->cancel_button), "clicked",
                     G_CALLBACK(cancel_button_cb), abmw);
    return box;
}

static void
bab_filter_entry_activate(GtkWidget *entry, GtkWidget *button)
{
    const gchar *filter = gtk_entry_get_text(GTK_ENTRY(entry));
    bab_set_address_book(contacts_app.address_book, contacts_app.entry_list,
                         filter);
    gtk_widget_set_sensitive(button, FALSE);
}

static void
bab_filter_entry_changed(GtkWidget *entry, GtkWidget *button)
{
    gtk_widget_set_sensitive(button, TRUE);
}

static GtkWidget*
bab_get_filter_box(void)
{
    GtkWidget *search_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget *find_label, *find_entry, *button;

    gtk_widget_show(search_hbox);
    find_label = gtk_label_new_with_mnemonic(_("F_ilter:"));
    gtk_widget_show(find_label);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_label);
    find_entry = gtk_entry_new();
    gtk_widget_show(find_entry);
    gtk_widget_set_hexpand(find_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_entry);
    gtk_widget_show(search_hbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(find_label), find_entry);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_icon_name("gtk-ok"));
    gtk_box_pack_start(GTK_BOX(search_hbox), button);

    g_signal_connect(G_OBJECT(find_entry), "activate",
                     G_CALLBACK(bab_filter_entry_activate),
                     button);
    g_signal_connect_swapped(G_OBJECT(button), "clicked",
                             G_CALLBACK(bab_filter_entry_activate),
                             find_entry);
    g_signal_connect(G_OBJECT(find_entry), "changed",
                             G_CALLBACK(bab_filter_entry_changed),
                             button);
    return search_hbox;
}
static gboolean
ew_key_pressed(GtkEntry * entry, GdkEvent * event, struct ABMainWindow *abmw)
{
    guint keyval;

    if (!(gdk_event_get_keyval(event, &keyval) && keyval == GDK_KEY_Escape))
	return FALSE;

    gtk_button_clicked(GTK_BUTTON(abmw->cancel_button));

    return TRUE;
}

static GtkWidget*
bab_window_new(GtkApplication * application)
{
    GtkWidget *wnd;
    GtkWidget *main_vbox;
    GtkWidget *scroll;
    GtkWidget *browse_widget;
    GtkWidget *edit_widget;

    contacts_app.window =
        GTK_WINDOW(wnd = gtk_application_window_new(application));
    get_main_menu(application);

    gtk_window_set_title(GTK_WINDOW(wnd), "Contacts");

    /* main vbox */
    main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(wnd), main_vbox);

    contacts_app.notebook = gtk_notebook_new();
    gtk_widget_set_hexpand(contacts_app.notebook, TRUE);
    gtk_box_pack_start(GTK_BOX(main_vbox), contacts_app.notebook);

    browse_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);

    /* Entry widget for finding an address */
    gtk_box_pack_start(GTK_BOX(browse_widget), bab_get_filter_box());

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(browse_widget), scroll);

    contacts_app.entry_list = bab_window_list_new();
    gtk_container_add(GTK_CONTAINER(scroll), contacts_app.entry_list);

    gtk_notebook_append_page(GTK_NOTEBOOK(contacts_app.notebook), browse_widget,
			     gtk_label_new(_("Browse")));

    edit_widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 1);
    contacts_app.edit_widget =
        libbalsa_address_get_edit_widget(NULL, contacts_app.entries,
                                         G_CALLBACK(address_changed_cb),
                                         &contacts_app);
    gtk_box_pack_start(GTK_BOX(edit_widget), contacts_app.edit_widget);
    gtk_box_pack_start(GTK_BOX(edit_widget), bab_get_edit_button_box(&contacts_app));

    gtk_notebook_append_page(GTK_NOTEBOOK(contacts_app.notebook), edit_widget,
			     gtk_label_new(_("Edit")));

    /*
    g_signal_connect(G_OBJECT(find_entry), "changed",
		     G_CALLBACK(balsa_ab_window_find), ab);
    */
    g_signal_connect(wnd, "key-press-event",
		     G_CALLBACK(ew_key_pressed), &contacts_app);
    gtk_window_set_default_size(GTK_WINDOW(wnd), 500, 400);

    gtk_widget_show(wnd);
    return wnd;
}

static gboolean
bab_delete_ok(void)
{
    return FALSE;
}
/* -------------------------- main --------------------------------- */
static void
ab_warning(const char *fmt, ...)
{
    GtkWidget *d;
    va_list va_args;
    char *msg;
    va_start(va_args, fmt);
    msg =  g_strdup_vprintf(fmt, va_args);
    va_end(va_args);
    d = gtk_message_dialog_new(contacts_app.window,
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                               "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

static void
bab_init(void)
{
    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
#if HAVE_SQLITE
    LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
#endif
#if HAVE_RUBRICA
    LIBBALSA_TYPE_ADDRESS_BOOK_RUBRICA;
#endif /* HAVE_RUBRICA */
    memset(&contacts_app, 0, sizeof(contacts_app));
}

static void
information_real(void)
{
    /* FIXME */
}

static void
bab_set_intial_address_book(LibBalsaAddressBook * ab,
                            GtkWidget           * window)
{
    GAction *action;

    action =
        g_action_map_lookup_action(G_ACTION_MAP(window), "address-book");
    g_action_change_state(action, g_variant_new_string(ab->name));
}

GtkDialogFlags
libbalsa_dialog_flags(void)
{
	static GtkDialogFlags dialog_flags = GTK_DIALOG_USE_HEADER_BAR;
	static gint check_done = 0;

	if (g_atomic_int_get(&check_done) == 0) {
		const gchar *dialog_env;

		dialog_env = g_getenv("BALSA_DIALOG_HEADERBAR");
		if ((dialog_env != NULL) && (atoi(dialog_env) == 0)) {
			dialog_flags = (GtkDialogFlags) 0;
		}
		g_atomic_int_set(&check_done, 1);
	}
	return dialog_flags;
}

int
main(int argc, char *argv[])
{
    GtkApplication *application;
    LibBalsaAddressBook *ab;
    GtkWidget *ab_window;
    GList *l;

    application =
        gtk_application_new("org.desktop.BalsaAb", G_APPLICATION_FLAGS_NONE);
    if (!g_application_register(G_APPLICATION(application), NULL, NULL))
        g_message("Could not register address book editor");
    if (g_application_get_is_remote(G_APPLICATION(application))) {
        g_object_unref(application);
        return 1;
    }

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);
    setlocale(LC_ALL, "");
#endif

    bab_init();
    libbalsa_real_information_func = (LibBalsaInformationFunc)information_real;
    g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);

    /* load address book data */
    libbalsa_conf_push_group("Globals");
    contacts_app.default_address_book_prefix =
        libbalsa_conf_get_string("DefaultAddressBook");
    libbalsa_conf_pop_group();
    libbalsa_conf_foreach_group(ADDRESS_BOOK_SECTION_PREFIX,
                                bab_config_init, NULL);
    ab = contacts_app.default_address_book ?
        contacts_app.default_address_book :
        contacts_app.address_book_list->data;

    ab_window = bab_window_new(application);

    g_signal_connect(G_OBJECT(ab_window), "destroy",
                     G_CALLBACK(bab_cleanup), NULL);
    g_signal_connect(G_OBJECT(ab_window), "delete-event",
                     G_CALLBACK(bab_delete_ok), NULL);
    bab_set_intial_address_book(ab, ab_window);

    /* session management */

    gtk_widget_show(ab_window);
    gtk_widget_hide(contacts_app.edit_widget);

    gtk_main();

    /* Proper shutdown here */
    for (l = contacts_app.address_book_list; l; l = l->next)
        if (l->data)
            g_object_unref(l->data);
    g_list_free(contacts_app.address_book_list);

    return 0;
}
