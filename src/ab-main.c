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
#include "address-book-extern.h"
#include "address-book-ldif.h"
#if ENABLE_LDAP
#include "address-book-ldap.h"
#endif
#include "address-book-vcard.h"
#include "address-book-gpe.h"

struct ABMainWindow {
    GtkWidget *entry_list; /* GtkListView widget */
    GtkWidget *edit_box, *apply_button, *remove_button, *cancel_button;
    GtkWidget *edit_widget;
    GtkWidget *entries[NUM_FIELDS];

    GList *address_book_list;
    LibBalsaAddressBook* address_book;
    LibBalsaAddress *displayed_address;
} contacts_app;
    

static void bab_cleanup(void);

static gint bab_save_session(GnomeClient * client, gint phase,
                             GnomeSaveStyle save_style, gint is_shutdown,
                             GnomeInteractStyle interact_style, gint is_fast,
                             gpointer client_data);
static gint bab_kill_session(GnomeClient * client, gpointer client_data);

static void ab_set_edit_widget(GtkWidget *w, gboolean can_remove);

#define BALSA_CONFIG_PREFIX "balsa/"
#define ADDRESS_BOOK_SECTION_PREFIX "address-book-"
static void
bab_config_init(void)
{
    LibBalsaAddressBook *address_book;
    void *iterator;
    gchar *key, *val, *tmp;
    int pref_len = strlen(ADDRESS_BOOK_SECTION_PREFIX);

    iterator = gnome_config_init_iterator_sections(BALSA_CONFIG_PREFIX);
    while ((iterator = gnome_config_iterator_next(iterator, &key, &val))) {

	if (strncmp(key, ADDRESS_BOOK_SECTION_PREFIX, pref_len) == 0) {
	    tmp = g_strconcat(BALSA_CONFIG_PREFIX, key, "/", NULL);

	    address_book = libbalsa_address_book_new_from_config(tmp);
	    if (address_book)
		contacts_app.address_book_list =
		    g_list_append(contacts_app.address_book_list,
				  address_book);
	    g_free(tmp);
	}
	g_free(key);
	g_free(val);
    }

}

static void ab_warning(const char *fmt, ...);

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

#if GTK_CHECK_VERSION(2, 4, 0)
static void
select_address_book_cb(GtkRadioAction * action, GtkRadioAction * current,
                       gpointer callback_data)
{
    GList *l;

    if (action != current)
        return;
    l = g_list_nth(contacts_app.address_book_list,
                   GPOINTER_TO_INT(callback_data));
    if (!l)
        return;
    ab_set_edit_widget(NULL, FALSE);
    bab_set_address_book(LIBBALSA_ADDRESS_BOOK(l->data),
                         contacts_app.entry_list, NULL);
}
#else /* GTK_CHECK_VERSION(2, 4, 0) */
static void
select_address_book_cb(gpointer callback_data, guint callback_action,
                       GtkWidget *w)
{
    GList *l;

    if(!GTK_CHECK_MENU_ITEM(w)->active) return;
    l = g_list_nth(contacts_app.address_book_list,
                          GPOINTER_TO_INT(callback_data));
    if(!l) return;
    ab_set_edit_widget(NULL, FALSE);
    bab_set_address_book(LIBBALSA_ADDRESS_BOOK(l->data),
                         contacts_app.entry_list, NULL);
}
#endif /* GTK_CHECK_VERSION(2, 4, 0) */

static void
address_changed_cb(GtkWidget *w, gpointer data)
{
    struct ABMainWindow * aw = (struct ABMainWindow*)data;
    gtk_widget_set_sensitive(aw->apply_button,  TRUE);
    gtk_widget_set_sensitive(aw->cancel_button, TRUE);
}


static void
#if GTK_CHECK_VERSION(2, 4, 0)
edit_new_person_cb(GtkAction * action, gpointer user_data)
#else /* GTK_CHECK_VERSION(2, 4, 0) */
edit_new_person_cb(gpointer callback_data, guint callback_action, GtkWidget *w)
#endif /* GTK_CHECK_VERSION(2, 4, 0) */
{
    GtkWidget *ew;
    contacts_app.displayed_address = NULL;
    ew = libbalsa_address_get_edit_widget(NULL, contacts_app.entries,
                                          G_CALLBACK(address_changed_cb),
                                          &contacts_app);
    ab_set_edit_widget(ew, FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, FALSE);
}

#if GTK_CHECK_VERSION(2, 4, 0)
/* Normal items */
static GtkActionEntry entries[] = {
    {"FileMenu", NULL, "_File"},
    {"EntryMenu", NULL, "_Entry"},
    {"HelpMenu", NULL, "_Help"},
    {"New", GTK_STOCK_NEW, "_New", NULL, "New file", NULL},
    {"Open", GTK_STOCK_OPEN, "_Open", NULL, "Open a file", NULL},
    {"Save", GTK_STOCK_SAVE, "_Save", NULL, "Save file", NULL},
    {"SaveAs", GTK_STOCK_SAVE_AS, "Save _As", "<shift><control>S",
     "Save file as", NULL},
    {"Quit", GTK_STOCK_QUIT, "_Quit", NULL, "Exit the program",
     gtk_main_quit},
    {"NewPerson", GTK_STOCK_NEW, "_New Person", "<shift><control>N",
     "Add new person", G_CALLBACK(edit_new_person_cb)},
    {"NewGroup", GTK_STOCK_NEW, "New _Group", "<control>G",
     "Add new group", NULL},
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
"      <menuitem action='New'/>"
"      <menuitem action='Open'/>"
"      <menuitem action='Save'/>"
"      <menuitem action='SaveAs'/>"
"      <separator/>"
"      <menuitem action='Quit'/>"
"      <separator/>"
"    </menu>"
"    <menu action='EntryMenu'>"
"      <menuitem action='NewPerson'/>"
"      <menuitem action='NewGroup'/>"
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
    int cnt;
    GSList *group = NULL;

    action_group = gtk_action_group_new("MenuActions");
    gtk_action_group_add_actions(action_group, entries,
                                 G_N_ELEMENTS(entries), window);

    ui_manager = gtk_ui_manager_new();
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

    for (cnt = 1, ab = address_books; ab; ab = ab->next, cnt++) {
        LibBalsaAddressBook *address_book;
	gchar *label;
        guint merge_id;
        gchar *accelerator;
        GtkRadioAction *radio_action;

        address_book = LIBBALSA_ADDRESS_BOOK(ab->data);

        label = g_strdup_printf("_%d:%s", cnt, address_book->name);
        radio_action = gtk_radio_action_new(address_book->name, label,
                                            NULL, NULL, cnt - 1);
	g_free(label);
        if (group)
            gtk_radio_action_set_group(radio_action, group);
        else {
            group = gtk_radio_action_get_group(radio_action);
	    gtk_action_activate(GTK_ACTION(radio_action));
	}
        g_signal_connect(G_OBJECT(radio_action), "changed",
                         G_CALLBACK(select_address_book_cb),
                         GINT_TO_POINTER(cnt - 1));

        accelerator =
            cnt <= 9 ? g_strdup_printf("<control>%d", cnt) : NULL;
        gtk_action_group_add_action_with_accel(action_group,
                                               GTK_ACTION(radio_action),
                                               accelerator);
        g_free(accelerator);

        merge_id = gtk_ui_manager_new_merge_id(ui_manager);
        gtk_ui_manager_add_ui(ui_manager, merge_id,
                              "/ui/MainMenu/FileMenu/",
                              address_book->name, address_book->name,
                              GTK_UI_MANAGER_AUTO, FALSE);
    }

    if (menubar)
        /* Finally, return the actual menu bar created by the UIManager. */
        *menubar = gtk_ui_manager_get_widget(ui_manager, "/MainMenu");
}
#else /* GTK_CHECK_VERSION(2, 4, 0) */
static GtkItemFactoryEntry menu_items[] = {
  { "/_File",         NULL,         NULL, 0, "<Branch>" },
  { "/File/_New",     "<control>N", NULL, 0, NULL },
  { "/File/_Open",    "<control>O", (GtkItemFactoryCallback)NULL,
0, NULL },
  { "/File/_Save",    "<control>S", NULL, 0, NULL },
  { "/File/Save _As", NULL,         NULL, 0, NULL },
  { "/File/sep1",     NULL,         NULL, 0, "<Separator>" },
  { "/File/_Quit",     "<control>Q", (GtkItemFactoryCallback)gtk_main_quit, 
    0, NULL },
  { "/File/sep2",     NULL,         NULL, 0, "<Separator>" },
  { "/_Entry",         NULL,        NULL, 0, "<Branch>" },
  { "/Entry/_New Person", NULL,   edit_new_person_cb, 0, NULL },
  { "/Entry/_New Group",  NULL,   (GtkItemFactoryCallback)NULL, 0, NULL },
  { "/_Help",          NULL,         NULL, 0, "<LastBranch>" },
  { "/_Help/_About",   NULL,         NULL, 0, NULL },
};

static void
get_main_menu(GtkWidget  *window, GtkWidget **menubar, GList* address_books)
{
    GtkItemFactory *item_factory;
    GtkAccelGroup *accel_group;
    gint nmenu_items = sizeof(menu_items)/sizeof(menu_items[0]);
    GList *ab;
    int cnt;
    gchar *first_ab_path = NULL;

    accel_group = gtk_accel_group_new();
    item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
                                       accel_group);
    gtk_item_factory_create_items (item_factory, nmenu_items,
                                   menu_items, window);
    
    for(cnt=1, ab= address_books; ab; ab = ab->next, cnt++) {
        LibBalsaAddressBook *address_book = LIBBALSA_ADDRESS_BOOK(ab->data);
        GtkItemFactoryEntry gife;
        gife.path = g_strdup_printf("/File/_%d:%s", cnt,
                                    address_book->name);
        gife.accelerator = cnt<=9 
            ? g_strdup_printf("<control>%d", cnt) : NULL;
        gife.callback = select_address_book_cb;
        gife.callback_action = 0;
        gife.item_type = first_ab_path ? first_ab_path : "<RadioItem>";
        gife.extra_data = NULL;
        gtk_item_factory_create_item(item_factory, &gife, 
                                     GINT_TO_POINTER(cnt-1), 1);
        g_free(gife.accelerator);
        g_free(gife.path);
        if(!first_ab_path)
            first_ab_path = g_strdup_printf("/File/1:%s",
                                    address_book->name);
    }
    g_free(first_ab_path);
    /* Attach the new accelerator group to the window. */
    gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
    
    if (menubar)
        /* Finally, return the actual menu bar created by the item factory. */
        *menubar = gtk_item_factory_get_widget (item_factory, "<main>");
}
#endif /* GTK_CHECK_VERSION(2, 4, 0) */


static void
ab_set_edit_widget(GtkWidget *w, gboolean can_remove)
{
    if(contacts_app.edit_widget)
        gtk_widget_destroy(contacts_app.edit_widget);
    contacts_app.edit_widget = w;
    if(w) {
        gtk_box_pack_start(GTK_BOX(contacts_app.edit_box), w,
                           FALSE, FALSE, 1);
        gtk_widget_show_all(w);
    }
    gtk_widget_set_sensitive(contacts_app.apply_button,  FALSE);
    gtk_widget_set_sensitive(contacts_app.remove_button, can_remove);
    gtk_widget_set_sensitive(contacts_app.cancel_button, FALSE);
}

static void
list_selection_changed_cb(GtkTreeSelection *selection, gpointer data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    GValue gv = {0,};
    GtkWidget *ew;
    LibBalsaAddress *address;

    if(!gtk_tree_selection_get_selected(selection, &model, &iter))
        return;
    gtk_tree_model_get_value(model, &iter, LIST_COLUMN_ADDRESS, &gv);
    address = LIBBALSA_ADDRESS(g_value_get_object(&gv));
    if(address) {
        ew = libbalsa_address_get_edit_widget(address, contacts_app.entries,
                                              G_CALLBACK(address_changed_cb),
                                              data);
        ab_set_edit_widget(ew, TRUE);
    } else ab_set_edit_widget(NULL, FALSE);
    g_value_unset(&gv);
    contacts_app.displayed_address = address;
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

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("Name"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_NAME, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_BROWSE);

    g_signal_connect(G_OBJECT(selection), "changed", 
                     G_CALLBACK(list_selection_changed_cb), cb_data);

    gtk_widget_show(tree);
    return tree;
}

static void
apply_button_cb(GtkWidget *w, gpointer data)
{
    LibBalsaAddress * newval =
        libbalsa_address_new_from_edit_entries(contacts_app.entries);
    LibBalsaABErr err = 
        contacts_app.displayed_address 
        ? libbalsa_address_book_modify_address(contacts_app.address_book,
                                               contacts_app.displayed_address,
                                               newval)
        : libbalsa_address_book_add_address(contacts_app.address_book,
                                            newval);
    if(err == LBABERR_OK) {
        gtk_widget_set_sensitive(contacts_app.apply_button,  FALSE);
        gtk_widget_set_sensitive(contacts_app.remove_button, TRUE);
        gtk_widget_set_sensitive(contacts_app.cancel_button, FALSE);
    } else 
        ab_warning("Cannot add: %s\n",
                   libbalsa_address_book_strerror(contacts_app.address_book,
                                                  err));                   
    g_object_unref(newval);
}

static void
remove_button_cb(GtkWidget *w, gpointer data)
{
    LibBalsaABErr err = 
        libbalsa_address_book_remove_address(contacts_app.address_book,
                                             contacts_app.displayed_address);
    if(err == LBABERR_OK) {
        GtkTreeIter       iter;
        GtkTreeSelection *selection;
        GtkTreeView  *v = GTK_TREE_VIEW(contacts_app.entry_list);
        GtkTreeModel *m = gtk_tree_view_get_model(v);
        selection       = gtk_tree_view_get_selection(v);
        if(gtk_tree_selection_get_selected(selection, &m, &iter))
            gtk_list_store_remove(GTK_LIST_STORE(m), &iter);
        ab_set_edit_widget(NULL, FALSE);
        contacts_app.displayed_address = NULL;
    } else 
        ab_warning("Cannot remove: %s\n",
                   libbalsa_address_book_strerror(contacts_app.address_book,
                                                  err));
}
static void
cancel_button_cb(GtkWidget *w, gpointer data)
{
    struct ABMainWindow *abmw = (struct ABMainWindow*)data;
    if(abmw->displayed_address) {
        GtkWidget *ew =
            libbalsa_address_get_edit_widget(abmw->displayed_address,
                                             abmw->entries,
                                             G_CALLBACK(address_changed_cb),
                                             data);
        ab_set_edit_widget(ew, TRUE);
    } else ab_set_edit_widget(NULL, FALSE);
}

#define ELEMENTS(x) (sizeof(x)/sizeof((x)[0])) 
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

static GtkWidget*
bab_window_new()
{
    GtkWidget* menubar, *main_vbox, *cont_box, *vbox, *scroll;
    GtkWidget *wnd = gnome_app_new("Contacts", "Contacts");
    GList *first_ab;

    get_main_menu(GTK_WIDGET(wnd), &menubar, contacts_app.address_book_list);
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
    gtk_box_pack_start(GTK_BOX(cont_box), scroll, TRUE,TRUE, 1);
    
    contacts_app.entry_list = bab_window_list_new(&contacts_app);
    gtk_container_add(GTK_CONTAINER(scroll), contacts_app.entry_list);

    vbox = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(cont_box), vbox, TRUE,TRUE, 1);
    contacts_app.edit_box = gtk_vbox_new(FALSE, 1);
    gtk_box_pack_start(GTK_BOX(vbox), contacts_app.edit_box,
                       TRUE,TRUE, 1);
    gtk_box_pack_start(GTK_BOX(vbox),
                       bab_get_edit_button_box(&contacts_app),
                       FALSE, FALSE, 1);
    /*
    g_signal_connect(G_OBJECT(find_entry), "changed",
		     G_CALLBACK(balsa_ab_window_find), ab);
    */
    gtk_window_set_default_size(GTK_WINDOW(wnd), 500, 400);
    gnome_app_set_contents(GNOME_APP(wnd), main_vbox);

    first_ab = g_list_first(contacts_app.address_book_list);
    if(first_ab)
        bab_set_address_book(LIBBALSA_ADDRESS_BOOK(first_ab->data),
                             contacts_app.entry_list, NULL);
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
    /* FIXME: gnome_i18n_get_language seems to have gone away; 
     * is this a reasonable replacement? */
    setlocale(LC_CTYPE,
              (const char *) gnome_i18n_get_language_list("LC_CTYPE")->data);
#endif

    /* FIXME: do we need to allow a non-GUI mode? */
    gtk_init_check(&argc, &argv);
    gnome_program_init(PACKAGE, VERSION, LIBGNOMEUI_MODULE, argc, argv,
                       GNOME_PARAM_POPT_TABLE, NULL,
                       GNOME_PARAM_APP_PREFIX,  BALSA_STD_PREFIX,
                       GNOME_PARAM_APP_DATADIR, BALSA_STD_PREFIX "/share",
                       NULL);

#ifdef GTKHTML_HAVE_GCONF
    if (!gconf_init(argc, argv, &gconf_error))
	g_error_free(gconf_error);
    gconf_error = NULL;
#endif

    bab_init();

    /* load address book data */
    bab_config_init();

    ab_window = bab_window_new();
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
