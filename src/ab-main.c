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

LibBalsaAddressBook* address_book = NULL;

static void bab_cleanup(void);

static gint bab_save_session(GnomeClient * client, gint phase,
                             GnomeSaveStyle save_style, gint is_shutdown,
                             GnomeInteractStyle interact_style, gint is_fast,
                             gpointer client_data);
static gint bab_kill_session(GnomeClient * client, gpointer client_data);

static void
bab_config_init(void)
{
}

static GtkItemFactoryEntry menu_items[] = {
  { "/_File",         NULL,         NULL, 0, "<Branch>" },
  { "/File/_New",     "<control>N", NULL, 0, NULL },
  { "/File/_Open",    "<control>O", (GtkItemFactoryCallback)NULL,
0, NULL },
  { "/File/_Save",    "<control>S", NULL, 0, NULL },
  { "/File/Save _As", NULL,         NULL, 0, NULL },
  { "/File/sep1",     NULL,         NULL, 0, "<Separator>" },
  { "/File/Open Balsa Address Book", NULL,         NULL, 0, NULL },
  { "/File/sep2",     NULL,         NULL, 0, "<Separator>" },
  { "/File/_Quit",     "<control>Q", (GtkItemFactoryCallback)gtk_main_quit, 
    0, NULL },
  { "/_Entry",         NULL,        NULL, 0, "<Branch>" },
  { "/Entry/_New Person", NULL,   (GtkItemFactoryCallback)NULL, 0, NULL },
  { "/Entry/_New Group",  NULL,   (GtkItemFactoryCallback)NULL, 0, NULL },
  { "/Entry/_Delete",  NULL,   (GtkItemFactoryCallback)NULL, 0, NULL },
  { "/Entry/_Edit...", NULL,   (GtkItemFactoryCallback)NULL, 0, NULL },
  { "/_Help",          NULL,         NULL, 0, "<LastBranch>" },
  { "/_Help/_About",   NULL,         NULL, 0, NULL },
};

static void
get_main_menu(GtkWidget  *window, GtkWidget **menubar)
{
    GtkItemFactory *item_factory;
    GtkAccelGroup *accel_group;
    gint nmenu_items = sizeof(menu_items)/sizeof(menu_items[0]);
    
    accel_group = gtk_accel_group_new();
    item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
                                       accel_group);
    gtk_item_factory_create_items (item_factory, nmenu_items,
                                   menu_items, window);

    /* Attach the new accelerator group to the window. */
    gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);
    
    if (menubar)
        /* Finally, return the actual menu bar created by the item factory. */
        *menubar = gtk_item_factory_get_widget (item_factory, "<main>");
}


enum {
    LIST_COLUMN_NAME,
    LIST_COLUMN_ADDRESS_STRING,
    LIST_COLUMN_ADDRESS,
    LIST_COLUMN_WHICH,
    N_COLUMNS
};
static GtkWidget *
bab_window_list_new(GCallback row_activated_cb)
{
    GtkListStore *store;
    GtkWidget *tree;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    store =
        gtk_list_store_new(N_COLUMNS,
                           G_TYPE_STRING,   /* LIST_COLUMN_NAME           */
                           G_TYPE_STRING,   /* LIST_COLUMN_ADDRESS_STRING */
                           G_TYPE_OBJECT,   /* LIST_COLUMN_ADDRESS        */
                           G_TYPE_INT);     /* LIST_COLUMN_WHICH          */
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

    renderer = gtk_cell_renderer_text_new();
    column =
        gtk_tree_view_column_new_with_attributes(_("E-Mail Address"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_ADDRESS_STRING,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    gtk_widget_show(tree);
    /*
    g_signal_connect(G_OBJECT(tree), "row-activated", row_activated_cb,
                     ab);
    */
    return tree;
}

static GtkWidget*
bab_window_entry_new(gpointer d)
{
    GtkWidget* table = gtk_table_new(4,2,FALSE);
    return table;
}

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
    gint count;

    g_return_if_fail ( LIBBALSA_IS_ADDRESS_BOOK(libbalsa_ab) );

    if ( address == NULL )
	return;

    if ( libbalsa_address_is_dist_list(libbalsa_ab, address) ) {
        gchar *address_string = libbalsa_address_to_gchar(address, -1);
        printf("adding '%s'\n", address_string);

        gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
        /* GtkListStore refs address, and unrefs it when cleared  */
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           LIST_COLUMN_NAME, address->full_name,
                           LIST_COLUMN_ADDRESS_STRING, address_string,
                           LIST_COLUMN_ADDRESS, address,
                           LIST_COLUMN_WHICH, -1,
                           -1);

	g_free(address_string);
    } else {
	address_list = address->address_list;
	count = 0;
	while ( address_list ) {
            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            /* GtkListStore refs address once for each address in
             * the list, and unrefs it the same number of times when
             * cleared */
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               LIST_COLUMN_NAME, address->full_name,
                               LIST_COLUMN_ADDRESS_STRING,
                               address_list->data,
                               LIST_COLUMN_ADDRESS, address,
                               LIST_COLUMN_WHICH, count,
                               -1);

	    address_list = g_list_next(address_list);
	    count++;
	}
    }
}

static LibBalsaAddressBook*
libbalsa_address_book_from_file(const gchar* fname, GtkWidget* list,
                                LibBalsaABErr* err)
{
    LibBalsaAddressBook* ab;
    GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(list));
    gtk_list_store_clear(GTK_LIST_STORE(model));

    ab = libbalsa_address_book_vcard_new("Address Book", fname);
    if( (*err=libbalsa_address_book_load(ab, (LibBalsaAddressBookLoadFunc)
                                         bab_load_cb, model))
        != LBABERR_OK) {
        g_object_unref(ab); ab = NULL;
        printf("error loading vcard addres book from %s: %d %d\n", fname,
               *err, LBABERR_OK);
    }
    return ab;
}

static gboolean
bab_set_address_book(const gchar* fl, GtkWidget* list)
{
    LibBalsaABErr err;
    LibBalsaAddressBook* new_address_book = 
        libbalsa_address_book_from_file(fl, list, &err);
    if(!new_address_book) {
        printf("Error.\n");
        return FALSE;
    }
    if(address_book) g_object_unref(address_book);
    address_book = new_address_book;

    return TRUE;
}

static GtkWidget*
bab_window_new()
{
    GtkWidget* menubar, *main_vbox, *search_hbox, *cont_hbox, *scroll;
    GtkWidget *find_label, *find_entry, *list;
    GtkWidget *wnd = gnome_app_new("Contacts", "Contacts");
    gchar* fl;

    get_main_menu(GTK_WIDGET(wnd), &menubar);
    gnome_app_set_menus(GNOME_APP(wnd), GTK_MENU_BAR(menubar));
    /* main vbox */
    main_vbox = gtk_vbox_new(FALSE, 1);

    /* Entry widget for finding an address */
    search_hbox = gtk_hbox_new(FALSE, 1);
    gtk_widget_show(search_hbox);
    gtk_box_pack_start(GTK_BOX(main_vbox), search_hbox, FALSE, FALSE, 1);

    find_label = gtk_label_new_with_mnemonic(_("_Search for Name:"));
    gtk_widget_show(find_label);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_label, FALSE, FALSE, 1);
    find_entry = gtk_entry_new();
    gtk_widget_show(find_entry);
    gtk_box_pack_start(GTK_BOX(search_hbox), find_entry, TRUE, TRUE, 1);
    
    cont_hbox = gtk_hbox_new(FALSE, 1);
    gtk_widget_show(search_hbox);
    gtk_box_pack_start(GTK_BOX(main_vbox), cont_hbox, TRUE,TRUE, 1);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_show(scroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(cont_hbox), scroll, TRUE,TRUE, 1);
    gtk_container_add(GTK_CONTAINER(scroll), 
                      list=bab_window_list_new(NULL));
    gtk_box_pack_start(GTK_BOX(cont_hbox), bab_window_entry_new(NULL), 
                       FALSE, FALSE, 1);

    /*
    g_signal_connect(G_OBJECT(find_entry), "changed",
		     G_CALLBACK(balsa_ab_window_find), ab);
    */
    gtk_window_set_default_size(GTK_WINDOW(wnd), 500, 400);
    gnome_app_set_contents(GNOME_APP(wnd), main_vbox);

    fl = g_strconcat(g_get_home_dir(),"/.GnomeCard.gcrd", NULL);
    bab_set_address_book(fl, list);
    g_free(fl);
    return wnd;
}

static gboolean
bab_delete_ok(void)
{
    return FALSE;
}
/* -------------------------- main --------------------------------- */
static void
bab_init(void)
{
    LIBBALSA_TYPE_ADDRESS_BOOK_VCARD;
    LIBBALSA_TYPE_ADDRESS_BOOK_EXTERN;
    LIBBALSA_TYPE_ADDRESS_BOOK_LDIF;
#if ENABLE_LDAP
    LIBBALSA_TYPE_ADDRESS_BOOK_LDAP;
#endif
}

int
main(int argc, char *argv[])
{
    GtkWidget *window;
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
              (const char *) gnome_i18n_get_language_list(LC_CTYPE)->data);
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

    window = bab_window_new();
    g_signal_connect(G_OBJECT(window), "destroy",
                     G_CALLBACK(bab_cleanup), NULL);
    g_signal_connect(G_OBJECT(window), "delete-event",
                     G_CALLBACK(bab_delete_ok), NULL);

    /* session management */
    client = gnome_master_client();
    g_signal_connect(G_OBJECT(client), "save_yourself",
		     G_CALLBACK(bab_save_session), argv[0]);
    g_signal_connect(G_OBJECT(client), "die",
		     G_CALLBACK(bab_kill_session), NULL);

    gtk_widget_show_all(window);

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
