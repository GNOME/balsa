/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
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

#include <string.h>
#include <gnome.h>
#ifdef GTKHTML_HAVE_GCONF
# include <gconf/gconf.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
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
#include "address-book-config.h"
#include "libbalsa-conf.h"
#include "libbalsa.h"
#include "i18n.h"

struct ABMainWindow {
    GtkWindow *window;
    GtkWidget *entry_list; /* GtkTreeView widget */
    GtkWidget *apply_button, *remove_button, *cancel_button;
    GtkWidget *edit_widget;
    GtkWidget *entries[NUM_FIELDS];
    GtkRadioAction *first_radio_action;

    GList *address_book_list;
    gchar *default_address_book_prefix;
    LibBalsaAddressBook *default_address_book;
    LibBalsaAddressBook* address_book;
    LibBalsaAddress *displayed_address;
    GtkActionGroup *action_group;
    GtkUIManager *ui_manager;
} contacts_app;
    

static void bab_cleanup(void);

static gint bab_save_session(GnomeClient * client, gint phase,
                             GnomeSaveStyle save_style, gint is_shutdown,
                             GnomeInteractStyle interact_style, gint is_fast,
                             gpointer client_data);
static gint bab_kill_session(GnomeClient * client, gpointer client_data);

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
#ifdef __GNUC__
    __attribute__ ((format (printf, 1, 2)))
#endif
;

enum {
    LIST_COLUMN_NAME,
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
    GList *address_list;

    g_return_if_fail ( LIBBALSA_IS_ADDRESS_BOOK(libbalsa_ab) );

    if ( address == NULL )
	return;

    if ( libbalsa_address_is_dist_list(libbalsa_ab, address) ) {
        gchar *address_string = libbalsa_address_to_gchar(address, -1);

        gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
        /* GtkListStore refs address, and unrefs it when cleared  */
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           LIST_COLUMN_NAME, address->full_name,
                           LIST_COLUMN_ADDRESS, address,
                           -1);

	g_free(address_string);
    } else {
	address_list = address->address_list;
	while ( address_list ) {
            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            /* GtkListStore refs address once for each address in
             * the list, and unrefs it the same number of times when
             * cleared */
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               LIST_COLUMN_NAME, address->full_name,
                               LIST_COLUMN_ADDRESS, address,
                               -1);

	    address_list = g_list_next(address_list);
	}
    }
}

static gboolean
bab_set_address_book(LibBalsaAddressBook *ab, GtkWidget* list,
                     const gchar *filter)
{
    LibBalsaABErr ab_err;
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    
    g_return_val_if_fail(ab, FALSE);
    contacts_app.address_book = ab;


    gtk_list_store_clear(GTK_LIST_STORE(model));
    if( (ab_err=libbalsa_address_book_load(ab, filter,
                                           (LibBalsaAddressBookLoadFunc)
                                           bab_load_cb, model))
        != LBABERR_OK) {
        printf("error loading address book from %s: %d\n", 
               ab->name, ab_err);
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

    title =
        g_strconcat(type, _(" address book: "), address_book->name, NULL);
    gtk_window_set_title(contacts_app.window, title);
    g_free(title);
}

static void
select_address_book_cb(GtkRadioAction * action, GtkRadioAction * current,
                       gpointer data)
{
    LibBalsaAddressBook *address_book;

    if (action != current)
        return;

    address_book =
        g_list_nth_data(contacts_app.address_book_list,
                        gtk_radio_action_get_current_value(action));
    if (!address_book)
        return;

    ab_clear_edit_widget();
    bab_set_address_book(address_book, contacts_app.entry_list, NULL);
    bab_window_set_title(address_book);
}

static void
address_changed_cb(struct ABMainWindow *aw)
{
    gtk_widget_set_sensitive(aw->apply_button, TRUE);
    gtk_widget_set_sensitive(aw->cancel_button, TRUE);
}

/* File menu callback helpers */

#define BAB_MERGE_ID "balsa-ab-merge-id-key"

static void
add_address_book(LibBalsaAddressBook * address_book)
{
    static guint pos;
    gchar *label;
    GtkRadioAction *radio_action;
    gchar *accelerator;
    guint merge_id;

    label = g_strdup_printf("_%d:%s", ++pos, address_book->name);
    radio_action = gtk_radio_action_new(address_book->name, label,
                                        NULL, NULL, pos - 1);
    g_free(label);

    if (contacts_app.first_radio_action) {
        GSList *group =
            gtk_radio_action_get_group(contacts_app.first_radio_action);
        gtk_radio_action_set_group(radio_action, group);
        if (address_book == contacts_app.default_address_book)
            contacts_app.first_radio_action = radio_action;
    } else
        contacts_app.first_radio_action = radio_action;

    g_signal_connect(G_OBJECT(radio_action), "changed",
                     G_CALLBACK(select_address_book_cb), NULL);

    accelerator = pos <= 9 ? g_strdup_printf("<control>%d", pos) : NULL;
    gtk_action_group_add_action_with_accel(contacts_app.action_group,
                                           GTK_ACTION(radio_action),
                                           accelerator);
    g_free(accelerator);

    merge_id = gtk_ui_manager_new_merge_id(contacts_app.ui_manager);
    gtk_ui_manager_add_ui(contacts_app.ui_manager, merge_id,
                          "/ui/MainMenu/FileMenu/",
                          address_book->name, address_book->name,
                          GTK_UI_MANAGER_AUTO, FALSE);
    g_object_set_data(G_OBJECT(address_book), BAB_MERGE_ID,
                      GUINT_TO_POINTER(merge_id));
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

#define BAB_RADIO_ACTION "bab-radio-action-key"
#define BAB_PREFIX_LEN 3        /* strlen("_1:") */

static void
address_book_change(LibBalsaAddressBook * address_book, gboolean append)
{
    gchar *group;

    if (append) {
        contacts_app.address_book_list =
            g_list_append(contacts_app.address_book_list, address_book);
        add_address_book(address_book);
    } else {
        GtkRadioAction *radio_action =
            g_object_get_data(G_OBJECT(address_book), BAB_RADIO_ACTION);
        gchar *label;

        g_object_get(G_OBJECT(radio_action), "label", &label, NULL);

        if (strlen(label) <= BAB_PREFIX_LEN
            || strcmp(label + BAB_PREFIX_LEN, address_book->name) != 0) {
            gchar *new_label;

            label[BAB_PREFIX_LEN] = 0;
            new_label = g_strconcat(label, address_book->name, NULL);
            g_object_set(G_OBJECT(radio_action), "label", new_label, NULL);
            g_free(new_label);
            bab_window_set_title(address_book);
        }
        g_free(label);
    }

    group = address_book->config_prefix ?
        g_strdup(address_book->config_prefix) :
        get_unused_group(ADDRESS_BOOK_SECTION_PREFIX);
    libbalsa_address_book_save_config(address_book, group);
    g_free(group);

    libbalsa_conf_sync();
}

static void
file_new_vcard_cb(GtkAction * action, gpointer user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_VCARD, address_book_change,
         contacts_app.window);
}

static void
file_new_extern_cb(GtkAction * action, gpointer user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN, address_book_change,
         contacts_app.window);
}

static void
file_new_ldif_cb(GtkAction * action, gpointer user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_LDIF, address_book_change,
         contacts_app.window);
}

#if ENABLE_LDAP
static void
file_new_ldap_cb(GtkAction * action, gpointer user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_LDAP, address_book_change,
         contacts_app.window);
}
#endif /* ENABLE_LDAP */

#if HAVE_SQLITE
static void
file_new_gpe_cb(GtkAction * action, gpointer user_data)
{
    balsa_address_book_config_new_from_type
        (LIBBALSA_TYPE_ADDRESS_BOOK_GPE, address_book_change,
         contacts_app.window);
}
#endif /* HAVE_SQLITE */

static void
file_properties_cb(GtkAction * action, gpointer user_data)
{
    LibBalsaAddressBook *address_book;
    GtkAction *radio_action;

    if (!(address_book = contacts_app.address_book))
        return;

    radio_action = gtk_action_group_get_action(contacts_app.action_group,
                                               address_book->name);
    g_object_set_data(G_OBJECT(address_book), BAB_RADIO_ACTION,
                      radio_action);
    balsa_address_book_config_new(address_book, address_book_change,
                                  contacts_app.window);
}

static void
file_delete_cb(GtkAction * action, gpointer user_data)
{
    LibBalsaAddressBook *address_book;
    guint merge_id;
    GtkAction *radio_action;
    GList *list;

    if (!(address_book = contacts_app.address_book)
        || !g_list_next(contacts_app.address_book_list))
        return;

    if (address_book->config_prefix) {
        libbalsa_conf_remove_group(address_book->config_prefix);
        libbalsa_conf_private_remove_group(address_book->config_prefix);
        libbalsa_conf_sync();
    }

    merge_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(address_book),
                                                  BAB_MERGE_ID));
    gtk_ui_manager_remove_ui(contacts_app.ui_manager, merge_id);

    radio_action = gtk_action_group_get_action(contacts_app.action_group,
                                               address_book->name);
    gtk_action_group_remove_action(contacts_app.action_group, radio_action);
    g_object_unref(radio_action);
    if (contacts_app.first_radio_action == (GtkRadioAction*) radio_action)
        contacts_app.first_radio_action = NULL;

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

    radio_action = gtk_action_group_get_action(contacts_app.action_group,
                                               address_book->name);
    if (!contacts_app.first_radio_action)
        contacts_app.first_radio_action = (GtkRadioAction*) radio_action;
    gtk_action_activate(radio_action);
}

static void
edit_new_entry_cb(GtkAction * action, gpointer user_data)
{
    GtkTreeSelection *selection;

    contacts_app.displayed_address = NULL;
    ab_set_edit_widget(NULL, FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, FALSE);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW
                                            (contacts_app.entry_list));
    gtk_tree_selection_unselect_all(selection);
    gtk_widget_grab_focus(GTK_CONTAINER(contacts_app.edit_widget)->
                          focus_child);
}

/* Normal items */
static GtkActionEntry entries[] = {
    {"FileMenu", NULL, "_File"},
    {"EntryMenu", NULL, "_Entry"},
    {"HelpMenu", NULL, "_Help"},
    {"New", GTK_STOCK_NEW, "_New"},
    {"NewVcard", NULL, N_("VCard Address Book (GnomeCard)"), NULL, NULL,
     G_CALLBACK(file_new_vcard_cb)},
    {"NewExtern", NULL, N_("External query (a program)"), NULL, NULL,
     G_CALLBACK(file_new_extern_cb)},
    {"NewLdif", NULL, N_("LDIF Address Book"), NULL, NULL,
     G_CALLBACK(file_new_ldif_cb)},
#if ENABLE_LDAP
    {"NewLdap", NULL, N_("LDAP Address Book"), NULL, NULL,
     G_CALLBACK(file_new_ldap_cb)},
#endif /* ENABLE_LDAP */
#if HAVE_SQLITE
    {"NewGpe", NULL, N_("GPE Address Book"), NULL, NULL,
     G_CALLBACK(file_new_gpe_cb)},
#endif /* HAVE_SQLITE */
    {"Properties", GTK_STOCK_PROPERTIES, "_Properties", NULL,
     "Edit address book properties", G_CALLBACK(file_properties_cb)},
    {"Delete", GTK_STOCK_DELETE, "_Delete", NULL,
     "Delete address book", G_CALLBACK(file_delete_cb)},
    {"Quit", GTK_STOCK_QUIT, "_Quit", NULL, "Exit the program",
     gtk_main_quit},
    {"NewEntry", GTK_STOCK_NEW, "_New Entry", "<shift><control>N",
     "Add new entry", G_CALLBACK(edit_new_entry_cb)},
    {"About",
#if GTK_CHECK_VERSION(2, 6, 0)
     GTK_STOCK_ABOUT,
#else
     GNOME_STOCK_ABOUT,
#endif                          /* GTK_CHECK_VERSION(2, 6, 0) */
     "_About", NULL, NULL, NULL}
};

static const char *ui_description =
"<ui>"
"  <menubar name='MainMenu'>"
"    <menu action='FileMenu'>"
"      <menu action='New'>"
"        <menuitem action='NewVcard'/>"
"        <menuitem action='NewExtern'/>"
"        <menuitem action='NewLdif'/>"
#if ENABLE_LDAP
"        <menuitem action='NewLdap'/>"
#endif /* ENABLE_LDAP */
#if HAVE_SQLITE
"        <menuitem action='NewGpe'/>"
#endif /* HAVE_SQLITE */
"      </menu>"
"      <menuitem action='Properties'/>"
"      <menuitem action='Delete'/>"
"      <separator/>"
"      <menuitem action='Quit'/>"
"      <separator/>"
"    </menu>"
"    <menu action='EntryMenu'>"
"      <menuitem action='NewEntry'/>"
"    </menu>"
"    <menu action='HelpMenu'>"
"      <menuitem action='About'/>"
"    </menu>"
"  </menubar>"
"</ui>";

static void
get_main_menu(GtkWidget * window, GtkWidget ** menubar,
              GList * address_books)
{
    GtkActionGroup *action_group;
    GtkUIManager *ui_manager;
    GtkAccelGroup *accel_group;
    GError *error;
    GList *ab;

    contacts_app.action_group = action_group =
        gtk_action_group_new("MenuActions");
    gtk_action_group_set_translation_domain(action_group, NULL);
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), window);

    contacts_app.ui_manager = ui_manager = gtk_ui_manager_new();
    gtk_ui_manager_insert_action_group(ui_manager, action_group, 0);

    accel_group = gtk_ui_manager_get_accel_group(ui_manager);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    error = NULL;
    if (!gtk_ui_manager_add_ui_from_string(ui_manager, ui_description,
                                           -1, &error)) {
        g_message("building menus failed: %s", error->message);
        g_error_free(error);
        return;
    }

    for (ab = address_books; ab; ab = ab->next)
        add_address_book(LIBBALSA_ADDRESS_BOOK(ab->data));

    if (menubar)
        /* Finally, return the actual menu bar created by the UIManager. */
        *menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");
}

static void
list_row_activated_cb(GtkTreeView *tview, gpointer data);

static void
ab_set_edit_widget(LibBalsaAddress * address, gboolean can_remove)
{
    libbalsa_address_set_edit_entries(address, contacts_app.entries);
    gtk_widget_show_all(contacts_app.edit_widget);
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
}

static void
list_selection_changed_cb(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GValue gv = {0,};
    LibBalsaAddress *address;
    if(contacts_app.edit_widget &&
       GTK_WIDGET_VISIBLE(contacts_app.edit_widget) &&
       GTK_WIDGET_IS_SENSITIVE(contacts_app.edit_widget))
	return;
    if(!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
    address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
    if (address) {
        if (address != contacts_app.displayed_address)
            ab_set_edit_widget(address, TRUE);
	gtk_widget_set_sensitive(contacts_app.edit_widget, FALSE);
    } else
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
        if (address != contacts_app.displayed_address)
            ab_set_edit_widget(address, TRUE);
	gtk_widget_set_sensitive(contacts_app.edit_widget, TRUE);
    } else
        ab_clear_edit_widget();
    g_value_unset(&gv);
    contacts_app.displayed_address = address;
}

static void 
addrlist_drag_get_cb(GtkWidget* widget, GdkDragContext* drag_context, 
                     GtkSelectionData* sel_data, guint target_type,
                     guint time, gpointer user_data)
{ 
    GtkTreeView *addrlist;
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    LibBalsaAddress *address;
    GValue gv = {0,};

    g_return_if_fail (widget != NULL);
    addrlist = GTK_TREE_VIEW(widget);

    switch (target_type) {
    case LIBBALSA_ADDRESS_TRG_ADDRESS:
        selection = gtk_tree_view_get_selection(addrlist);
        if(!gtk_tree_selection_get_selected(selection, &model, &iter))
            return;
        gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
        address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
        gtk_selection_data_set(sel_data, sel_data->target, 8,
                               (const guchar *) &address,
                               sizeof(LibBalsaAddress*));
        break;
    case LIBBALSA_ADDRESS_TRG_STRING:
        g_print("Text/plain cannot be sent.\n");
        break;
    default: g_print("Do not know what to do!\n");
    }
}

static GtkWidget *
bab_window_list_new(gpointer cb_data)
{
    GtkListStore *store;
    GtkWidget *tree;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    store =
        gtk_list_store_new(N_COLUMNS,
                           G_TYPE_STRING,   /* LIST_COLUMN_NAME           */
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
                     G_CALLBACK(list_selection_changed_cb), cb_data);
    g_signal_connect(G_OBJECT(tree), "row-activated", 
                     G_CALLBACK(list_row_activated_cb), cb_data);

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("_Name"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);


    gtk_drag_source_set(GTK_WIDGET(tree), 
                        GDK_BUTTON1_MASK,
                        libbalsa_address_target_list, 2,
                        GDK_ACTION_COPY);
    g_signal_connect(G_OBJECT(tree), "drag-data-get",
                     G_CALLBACK(addrlist_drag_get_cb), NULL);

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
    LibBalsaABErr err = LBABERR_OK;

    if (contacts_app.displayed_address)
        libbalsa_address_book_remove_address(contacts_app.address_book,
                                             contacts_app.displayed_address);
    if(err == LBABERR_OK) {
        GtkTreeIter       iter;
        GtkTreeSelection *selection;
        GtkTreeView  *v = GTK_TREE_VIEW(contacts_app.entry_list);
        GtkTreeModel *m;
        selection       = gtk_tree_view_get_selection(v);
        if(gtk_tree_selection_get_selected(selection, &m, &iter))
            gtk_list_store_remove(GTK_LIST_STORE(m), &iter);
        ab_clear_edit_widget();
        contacts_app.displayed_address = NULL;
    } else 
        ab_warning("Cannot remove: %s\n",
                   libbalsa_address_book_strerror(contacts_app.address_book,
                                                  err));
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
    box = gtk_hbutton_box_new();
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->apply_button = 
                      gtk_button_new_from_stock(GTK_STOCK_APPLY));
    g_signal_connect(G_OBJECT(abmw->apply_button), "clicked",
                     G_CALLBACK(apply_button_cb), (gpointer) NULL);
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->remove_button
                      =gtk_button_new_from_stock(GTK_STOCK_REMOVE));
    g_signal_connect(G_OBJECT(abmw->remove_button), "clicked",
                     G_CALLBACK(remove_button_cb), (gpointer) NULL);
    gtk_container_add(GTK_CONTAINER(box),
                      abmw->cancel_button = 
                      gtk_button_new_from_stock(GTK_STOCK_CANCEL));
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
    GtkWidget *search_hbox = gtk_hbox_new(FALSE, 1);
    GtkWidget *find_label, *find_entry, *button;

    gtk_widget_show(search_hbox);
    find_label = gtk_label_new_with_mnemonic(_("F_ilter:"));
    gtk_widget_show(find_label);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_label, FALSE, FALSE, 1);
    find_entry = gtk_entry_new();
    gtk_widget_show(find_entry);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_entry, TRUE, TRUE, 1);
    gtk_widget_show(search_hbox);
    gtk_label_set_mnemonic_widget(GTK_LABEL(find_label), find_entry);
    button = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(button),
                      gtk_image_new_from_stock(GTK_STOCK_OK,
                                               GTK_ICON_SIZE_BUTTON));
    gtk_box_pack_start(GTK_BOX(search_hbox), button, FALSE, FALSE, 1);

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
ew_key_pressed(GtkEntry * entry, GdkEventKey * event, struct ABMainWindow *abmw)
{
    if (event->keyval != GDK_Escape)
	return FALSE;
    gtk_button_clicked(GTK_BUTTON(abmw->cancel_button));
    return TRUE;
}

static GtkWidget*
bab_window_new()
{
    GtkWidget* menubar = NULL, *main_vbox, *cont_box, *vbox, *scroll;
    GtkWidget *wnd = gnome_app_new("Contacts", "Contacts");
    GtkWidget *edit_box;

    get_main_menu(GTK_WIDGET(wnd), &menubar, contacts_app.address_book_list);
    if (menubar)
	gnome_app_set_menus(GNOME_APP(wnd), GTK_MENU_BAR(menubar));
    /* main vbox */
    main_vbox = gtk_vbox_new(FALSE, 1);

    /* Entry widget for finding an address */
    gtk_box_pack_start(GTK_BOX(main_vbox),
                       bab_get_filter_box(), FALSE, FALSE, 1);
 
    cont_box = gtk_hbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(main_vbox), cont_box, TRUE,TRUE, 1);

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(cont_box), scroll, TRUE, TRUE, 1);
    
    contacts_app.entry_list = bab_window_list_new(&contacts_app);
    gtk_container_add(GTK_CONTAINER(scroll), contacts_app.entry_list);

    vbox = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(cont_box), vbox, FALSE, FALSE, 1);
    edit_box = gtk_vbox_new(FALSE, 1);
    contacts_app.edit_widget = 
        libbalsa_address_get_edit_widget(NULL, contacts_app.entries,
                                         G_CALLBACK(address_changed_cb),
                                         &contacts_app);
    gtk_box_pack_start(GTK_BOX(edit_box), contacts_app.edit_widget,
                       FALSE, FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), edit_box, TRUE, TRUE, 1);
    gtk_box_pack_start(GTK_BOX(vbox),
                       bab_get_edit_button_box(&contacts_app),
                       FALSE, FALSE, 1);
    /*
    g_signal_connect(G_OBJECT(find_entry), "changed",
		     G_CALLBACK(balsa_ab_window_find), ab);
    */
    g_signal_connect(wnd, "key-press-event",
		     G_CALLBACK(ew_key_pressed), &contacts_app);
    gtk_window_set_default_size(GTK_WINDOW(wnd), 500, 400);
    gnome_app_set_contents(GNOME_APP(wnd), main_vbox);

    return wnd;
}

static gboolean
bab_delete_ok(void)
{
    return FALSE;
}
/* -------------------------- main --------------------------------- */
static GtkWidget *ab_window = NULL;
static void
ab_warning(const char *fmt, ...)
{
    GtkWidget *d;
    va_list va_args;
    char *msg;
    va_start(va_args, fmt);
    msg =  g_strdup_vprintf(fmt, va_args);
    va_end(va_args);
    d = gtk_message_dialog_new(GTK_WINDOW(ab_window),
                               GTK_DIALOG_DESTROY_WITH_PARENT,
                               GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, msg);
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
    memset(&contacts_app, 0, sizeof(contacts_app));
}

static void
information_real(void)
{
    /* FIXME */
}

int
main(int argc, char *argv[])
{
    GnomeClient *client;
#ifdef GTKHTML_HAVE_GCONF
    GError *gconf_error;
#endif

#ifdef ENABLE_NLS
    /* Initialize the i18n stuff */
    bindtextdomain(PACKAGE, GNOMELOCALEDIR);
    bind_textdomain_codeset(PACKAGE, "UTF-8");
    textdomain(PACKAGE);
    setlocale(LC_ALL, "");
#endif

    /* FIXME: do we need to allow a non-GUI mode? */
    gtk_init_check(&argc, &argv);
    gnome_program_init(PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
#ifndef GNOME_PARAM_GOPTION_CONTEXT
                       GNOME_PARAM_POPT_TABLE, NULL,
                       GNOME_PARAM_APP_PREFIX,  BALSA_STD_PREFIX,
                       GNOME_PARAM_APP_DATADIR, BALSA_STD_PREFIX "/share",
                       NULL);
#else
                       GNOME_PARAM_GOPTION_CONTEXT, NULL,
                       GNOME_PARAM_NONE);
#endif

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	g_error_free(gconf_error);
    gconf_error = NULL;
#endif

    bab_init();
    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
#if HAVE_SQLITE
    LIBBALSA_TYPE_ADDRESS_BOOK_GPE;
#endif
    libbalsa_real_information_func = (LibBalsaInformationFunc)information_real;
    g_mime_init(0);

    /* load address book data */
    libbalsa_conf_push_group("Globals");
    contacts_app.default_address_book_prefix =
        libbalsa_conf_get_string("DefaultAddressBook");
    libbalsa_conf_pop_group();
    libbalsa_conf_foreach_group(ADDRESS_BOOK_SECTION_PREFIX,
                                bab_config_init, NULL);

    ab_window = bab_window_new();
    contacts_app.window = GTK_WINDOW(ab_window);
    g_signal_connect(G_OBJECT(ab_window), "destroy",
                     G_CALLBACK(bab_cleanup), NULL);
    g_signal_connect(G_OBJECT(ab_window), "delete-event",
                     G_CALLBACK(bab_delete_ok), NULL);

    /* session management */
    client = gnome_master_client();
    g_signal_connect(G_OBJECT(client), "save_yourself",
		     G_CALLBACK(bab_save_session), argv[0]);
    g_signal_connect(G_OBJECT(client), "die",
		     G_CALLBACK(bab_kill_session), NULL);

    gtk_widget_show_all(ab_window);
    gtk_widget_hide(contacts_app.edit_widget);

    if (contacts_app.first_radio_action)
        gtk_action_activate(GTK_ACTION(contacts_app.first_radio_action));

    gdk_threads_enter();
    gtk_main();
    gdk_threads_leave();
    
    return 0;
}


static void
bab_cleanup(void)
{
    gnome_sound_shutdown();
    gtk_main_quit();
}

static gint
bab_kill_session(GnomeClient * client, gpointer client_data)
{
    /* save data here */
    gtk_main_quit(); 
    return TRUE;
}


static gint
bab_save_session(GnomeClient * client, gint phase,
                 GnomeSaveStyle save_style, gint is_shutdown,
                 GnomeInteractStyle interact_style, gint is_fast,
                 gpointer client_data)
{
    gchar **argv;
    guint argc;

    /* allocate 0-filled so it will be NULL terminated */
    argv = g_malloc0(sizeof(gchar *) * 2);

    argc = 1;
    argv[0] = client_data;

    gnome_client_set_clone_command(client, argc, argv);
    gnome_client_set_restart_command(client, argc, argv);

    return TRUE;
}
