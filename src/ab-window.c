/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2019 Stuart Parmenter and others,
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "ab-window.h"

#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include "address-view.h"
#include "balsa-app.h"
#include "sendmsg-window.h"
#include "save-restore.h"

enum {
    LIST_COLUMN_NAME,
    LIST_COLUMN_ADDRESS_STRING,
    LIST_COLUMN_ADDRESS,
    LIST_COLUMN_WHICH,
    N_COLUMNS
};

/* Loading ... */
static void balsa_ab_window_load_cb(LibBalsaAddressBook *libbalsa_ab,
				       LibBalsaAddress *address,
				       BalsaAbWindow *ab);
static void balsa_ab_window_load(BalsaAbWindow *ab);
static void balsa_ab_window_reload(GtkWidget *w, BalsaAbWindow *av);

/* Callbacks ... */
static void balsa_ab_window_dist_mode_toggled(GtkWidget * w,
						 BalsaAbWindow *ab);
static void balsa_ab_window_menu_changed(GtkWidget * widget,
					    BalsaAbWindow *ab);
static void balsa_ab_window_run_editor(GtkWidget * widget, gpointer data);
static void balsa_ab_window_response_cb(BalsaAbWindow *ab, gint resp);
static void balsa_ab_window_find(GtkWidget * group_entry,
                                    BalsaAbWindow *ab);

/* address and recipient list management ... */
static void balsa_ab_window_swap_list_entry(GtkTreeView * src,
                                               GtkTreeView * dst);
static void balsa_ab_window_activate_address(GtkTreeView * view,
                                                GtkTreePath * path,
                                                GtkTreeViewColumn * column,
                                                gpointer data);
static void balsa_ab_window_select_recipient(GtkTreeView * view,
                                                GtkTreePath * path,
                                                GtkTreeViewColumn * column,
                                                gpointer data);
static void balsa_ab_window_move_to_recipient_list(GtkWidget *widget,
						      BalsaAbWindow *ab);
static void balsa_ab_window_remove_from_recipient_list(GtkWidget *widget,
							  BalsaAbWindow *ab);

/* Utility ... */
static gint balsa_ab_window_compare_entries(GtkTreeModel * model,
                                               GtkTreeIter * iter1,
                                               GtkTreeIter * iter2,
                                               gpointer data);

struct _BalsaAbWindow
{
    GtkDialog parent;

    /* Are we composing? */
    gboolean composing;

    /* The current address book */
    LibBalsaAddressBook *current_address_book;

    /* the filter entry */
    GtkWidget *filter_entry;

    /* The address list */
    GtkWidget *address_list;

    /* The send to list */
    GtkWidget *recipient_list;

    /* Radio buttons for dist list mode */
    GtkWidget *single_address_mode_radio;
    GtkWidget *dist_address_mode_radio;
    guint      toggle_handler_id;

    /* Stuff to hide when not in compose mode */
    GtkWidget *send_to_label;
    GtkWidget *send_to_list;
    GtkWidget *arrow_box;

    /* The address book list */
    GtkWidget *combo_box;
};

G_DEFINE_TYPE(BalsaAbWindow, balsa_ab_window, GTK_TYPE_DIALOG)

GtkWidget *
balsa_ab_window_new(gboolean composing, GtkWindow* parent)
{
    GtkWidget *ret;
    BalsaAbWindow *ab;
    gint use_headerbar = 1;
    const gchar *dialog_env;

    /* header bar configuration */
	dialog_env = g_getenv("BALSA_DIALOG_HEADERBAR");
	if ((dialog_env != NULL) && (atoi(dialog_env) == 0)) {
		use_headerbar = 0;
	}

    ret = g_object_new(BALSA_TYPE_AB_WINDOW,
                       "use-header-bar", use_headerbar,
                       "transient-for", parent,
                       NULL);
    g_return_val_if_fail(ret != NULL, NULL);
    ab = (BalsaAbWindow *) ret;

    ab->composing = composing;

    if ( composing ) {
	gtk_dialog_add_buttons(GTK_DIALOG(ret),
                               _("_OK"),     GTK_RESPONSE_OK,
                               _("_Cancel"), GTK_RESPONSE_CANCEL,
                               NULL);
	gtk_widget_show(ab->send_to_label);
	gtk_widget_show(ab->send_to_list);
	gtk_widget_show(ab->arrow_box);
    } else {
	gtk_dialog_add_buttons(GTK_DIALOG(ret),
                               _("_Close"), GTK_RESPONSE_CLOSE,
                               NULL);
	gtk_widget_hide(ab->send_to_label);
	gtk_widget_hide(ab->send_to_list);
	gtk_widget_hide(ab->arrow_box);
    }

    return ret;
}

static GtkWidget *
balsa_ab_window_list(BalsaAbWindow * ab, GCallback row_activated_cb)
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
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(store), 0,
                                    balsa_ab_window_compare_entries,
                                    GINT_TO_POINTER(0), NULL);
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
        gtk_tree_view_column_new_with_attributes(_("Email Address"),
                                                 renderer,
                                                 "text",
                                                 LIST_COLUMN_ADDRESS_STRING,
                                                 NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

    gtk_widget_show(tree);
    g_signal_connect(tree, "row-activated", row_activated_cb,
                     ab);
    return tree;
}

static void
balsa_ab_window_load_books(BalsaAbWindow * ab)
{
    GList *ab_list;
    guint offset;

    for (ab_list = balsa_app.address_book_list, offset = 0; ab_list;
         ab_list = ab_list->next, ++offset) {
        LibBalsaAddressBook *address_book = ab_list->data;

        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ab->combo_box),
                                       libbalsa_address_book_get_name(address_book));

        if (ab->current_address_book == NULL)
            ab->current_address_book = address_book;
        if (address_book == ab->current_address_book)
            gtk_combo_box_set_active(GTK_COMBO_BOX(ab->combo_box), offset);
    }
}

static void
balsa_ab_window_init(BalsaAbWindow *ab)
{
    GtkWidget *find_label,
	*vbox,
	*w,
	*grid,
	*hbox,
	*box2,
	*scrolled_window,
	*frame;

    ab->current_address_book = NULL;

    g_return_if_fail(balsa_app.address_book_list);
    gtk_window_set_title(GTK_WINDOW(ab), _("Address Book"));

    g_signal_connect(ab, "response",
		     G_CALLBACK(balsa_ab_window_response_cb), NULL);

    vbox = gtk_dialog_get_content_area(GTK_DIALOG(ab));

    gtk_window_set_role(GTK_WINDOW(ab), "addressbook");

    /* hig defaults */
    gtk_container_set_border_width(GTK_CONTAINER(ab), 6);
    gtk_box_set_spacing(GTK_BOX(vbox), 12);


    /* The main address list */
    ab->address_list =
        balsa_ab_window_list(ab,
                                G_CALLBACK
                                (balsa_ab_window_activate_address));

    /* The clist for selected addresses in compose mode */
    ab->recipient_list =
        balsa_ab_window_list(ab,
                                G_CALLBACK
                                (balsa_ab_window_select_recipient));

    /* The address book selection menu */
    ab->combo_box = gtk_combo_box_text_new();

    ab->current_address_book = balsa_app.default_address_book;

    balsa_ab_window_load_books(ab);

    g_signal_connect(ab->combo_box, "changed",
                     G_CALLBACK(balsa_ab_window_menu_changed), ab);
    if (balsa_app.address_book_list->next)
	/* More than one address book. */
	gtk_widget_show(ab->combo_box);

    gtk_container_add(GTK_CONTAINER(vbox), ab->combo_box);

    /* layout grid */
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_vexpand(grid, TRUE);
    gtk_widget_set_valign(grid, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), grid);
    gtk_widget_show(grid);

    /* -- grid column 1 -- */
    /* Entry widget for finding an address */
    find_label = gtk_label_new_with_mnemonic(_("_Search for Name:"));
    gtk_widget_show(find_label);

    ab->filter_entry = gtk_entry_new();
    gtk_widget_show(ab->filter_entry);
    gtk_label_set_mnemonic_widget(GTK_LABEL(find_label), ab->filter_entry);
    g_signal_connect(ab->filter_entry, "changed",
		     G_CALLBACK(balsa_ab_window_find), ab);

    /* Pack the find stuff into the grid */
    box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
    gtk_grid_attach(GTK_GRID(grid), box2, 0, 0, 1, 1);
    gtk_container_add(GTK_CONTAINER(box2), find_label);
    gtk_widget_set_vexpand(ab->filter_entry, TRUE);
    gtk_widget_set_valign(ab->filter_entry, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(box2), ab->filter_entry);
    gtk_widget_show(GTK_WIDGET(box2));


    /* A scrolled window for the address clist */
    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_hexpand(scrolled_window, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				   GTK_POLICY_AUTOMATIC,
				   GTK_POLICY_AUTOMATIC);
    gtk_grid_attach(GTK_GRID(grid), scrolled_window, 0, 1, 1, 1);
    gtk_widget_show(scrolled_window);
    gtk_container_add(GTK_CONTAINER(scrolled_window), ab->address_list);
    gtk_widget_set_size_request(scrolled_window, 300, 250);

    /* Buttons ... */
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_grid_attach(GTK_GRID(grid), hbox, 0, 2, 1, 1);
    gtk_widget_show(GTK_WIDGET(hbox));

    w = libbalsa_add_mnemonic_button_to_box(_("Run _Editor"), hbox, GTK_ALIGN_CENTER);
    g_signal_connect(w, "clicked",
                     G_CALLBACK(balsa_ab_window_run_editor), NULL);

    w = libbalsa_add_mnemonic_button_to_box(_("_Re-import"), hbox, GTK_ALIGN_CENTER);
    g_signal_connect(w, "clicked",
                     G_CALLBACK(balsa_ab_window_reload),
		       ab);

    balsa_ab_window_load(ab);

    /* -- grid column 2 -- */
    /* Column for arrows in compose mode */
    ab->arrow_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_vexpand(ab->arrow_box, TRUE);
    gtk_widget_set_hexpand(ab->arrow_box, FALSE);
    gtk_grid_attach(GTK_GRID(grid), ab->arrow_box, 1, 1, 1, 1);
    gtk_widget_show(ab->arrow_box);

    w = gtk_button_new_from_icon_name("go-next-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_vexpand(w, TRUE);
    gtk_container_add(GTK_CONTAINER(ab->arrow_box), w);
    gtk_widget_show(w);
    g_signal_connect(w, "clicked",
		     G_CALLBACK(balsa_ab_window_move_to_recipient_list),
		       ab);

    w = gtk_button_new_from_icon_name("go-previous-symbolic",
                                      GTK_ICON_SIZE_BUTTON);
    gtk_widget_set_vexpand(w, TRUE);
    gtk_container_add(GTK_CONTAINER(ab->arrow_box), w);
    gtk_widget_show(w);
    g_signal_connect(w, "clicked",
		     G_CALLBACK(balsa_ab_window_remove_from_recipient_list),
		       ab);

    /* -- grid column 3 -- */
    /* label for selected addresses in compose mode */
    ab->send_to_label = gtk_label_new(_("Send-To"));
    gtk_widget_show(ab->send_to_label);
    gtk_grid_attach(GTK_GRID(grid), ab->send_to_label, 2, 0, 1, 1);

    /* list for selected addresses in compose mode */
    ab->send_to_list = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_vexpand(ab->send_to_list, TRUE);
    gtk_widget_set_hexpand(ab->send_to_list, TRUE);
    gtk_widget_show(ab->send_to_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(ab->send_to_list),
				   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_grid_attach(GTK_GRID(grid), ab->send_to_list, 2, 1, 1, 1);
    gtk_container_add(GTK_CONTAINER(ab->send_to_list), ab->recipient_list);
    gtk_widget_set_size_request(ab->send_to_list, 300, 250);

    /* mode switching stuff */
    ab->single_address_mode_radio = gtk_radio_button_new_with_label
	(NULL, _("alternative addresses for the same person"));
    gtk_widget_show(ab->single_address_mode_radio);

    ab->dist_address_mode_radio = gtk_radio_button_new_with_label_from_widget
	(GTK_RADIO_BUTTON(ab->single_address_mode_radio),
	 _("a distribution list"));
    gtk_widget_show(ab->dist_address_mode_radio);
    ab->toggle_handler_id =
        g_signal_connect(ab->single_address_mode_radio,
                         "toggled",
                         G_CALLBACK(balsa_ab_window_dist_mode_toggled),
                         ab);

    if(ab->current_address_book)
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->dist_address_mode_radio),
	    libbalsa_address_book_get_dist_list_mode(ab->current_address_book));

    /* Pack them into a box  */
    box2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_set_homogeneous(GTK_BOX(box2), TRUE);
    gtk_container_add(GTK_CONTAINER(box2), ab->single_address_mode_radio);
    gtk_container_add(GTK_CONTAINER(box2), ab->dist_address_mode_radio);

    frame = gtk_frame_new(_("Treat multiple addresses as:"));
    gtk_container_add(GTK_CONTAINER(frame), box2);

    gtk_container_add(GTK_CONTAINER(vbox), frame);
    gtk_widget_show_all(vbox);

    gtk_widget_grab_focus(ab->filter_entry);
}

static void
balsa_ab_window_class_init(BalsaAbWindowClass *klass)
{
}

/*
  Runs gnome card
*/
static void
balsa_ab_window_run_editor(GtkWidget * widget, gpointer data)
{
    char *argv[] = { "balsa-ab", NULL };
    GError * err = NULL;

    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                       NULL, NULL, NULL, &err))
        balsa_information(LIBBALSA_INFORMATION_WARNING,
                          _("Could not launch %s: %s"), argv[0],
                          err ? err->message : _("Unknown error"));
    g_clear_error(&err);
}

/*
  Returns a string of all the selected recipients.
 */
gchar *
balsa_ab_window_get_recipients(BalsaAbWindow * ab)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean valid;
    GString *str = g_string_new(NULL);

    g_return_val_if_fail(ab->composing, NULL);

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(ab->recipient_list));
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        LibBalsaAddress *address = NULL;
        gint which_multiple = 0;
        gchar *text;

        gtk_tree_model_get(model, &iter,
                           LIST_COLUMN_ADDRESS, &address,
                           LIST_COLUMN_WHICH, &which_multiple, -1);
        text = libbalsa_address_to_gchar(address, which_multiple);
	g_object_unref(address);
        if (text) {
            if (str->len > 0)
                g_string_append(str, ", ");
            g_string_append(str, text);
            g_free(text);
        }
    }

    return g_string_free(str, FALSE);
}

/*
  Moves an entry between two CLists.
*/
/*
  FIXME: Need to only move it back if it belongs in current address book??
*/
/*
 * balsa_ab_window_swap_list_entry is the method.
 *
 * balsa_ab_window_swap_make_gslist, balsa_ab_window_swap_real,
 * and balsa_ab_window_swap_do_swap are callbacks/helpers.
 */

static void
balsa_ab_window_swap_make_gslist(GtkTreeModel * model,
                                    GtkTreePath * path,
                                    GtkTreeIter * iter,
                                    gpointer data)
{
    GtkTreeRowReference *reference =
        gtk_tree_row_reference_new(model, path);
    GSList **selected = data;

    *selected = g_slist_prepend(*selected, reference);
}

static void
balsa_ab_window_swap_real(GtkTreeModel * model, GtkTreePath * path,
                             GtkTreeModel * dst_model)
{
    GtkTreeIter iter;
    GtkTreeIter dst_iter;
    gchar *name;
    gchar *address_string;
    LibBalsaAddress *address;
    gint which_multiple;

    gtk_tree_model_get_iter(model, &iter, path);
    gtk_tree_model_get(model, &iter,
                       LIST_COLUMN_NAME, &name,
                       LIST_COLUMN_ADDRESS_STRING, &address_string,
                       LIST_COLUMN_ADDRESS, &address,
                       LIST_COLUMN_WHICH, &which_multiple, -1);

    gtk_list_store_prepend(GTK_LIST_STORE(dst_model), &dst_iter);
    gtk_list_store_set(GTK_LIST_STORE(dst_model), &dst_iter,
                       LIST_COLUMN_NAME, name,
                       LIST_COLUMN_ADDRESS_STRING, address_string,
                       LIST_COLUMN_ADDRESS, address,
                       LIST_COLUMN_WHICH, which_multiple, -1);

    /* gtk_list_store_remove unrefs address, but gtk_list_store_set has
     * already reffed it, so it won't be finalized, even if there
     * were no other outstanding refs. */
    gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
    g_object_unref(address);
    g_free(name);
    g_free(address_string);
}

#define BALSA_TREE_VIEW "balsa-tree-view"
static void
balsa_ab_window_swap_do_swap(gpointer data, gpointer user_data)
{
    GtkTreeRowReference *reference = data;
    GtkTreePath *path = gtk_tree_row_reference_get_path(reference);

    if (path) {
        GtkTreeView *tree_view =
            g_object_get_data(G_OBJECT(user_data), BALSA_TREE_VIEW);
        GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
        GtkTreeModel *dst_model = gtk_tree_view_get_model(user_data);

        balsa_ab_window_swap_real(model, path, dst_model);
        gtk_tree_path_free(path);
    }

    gtk_tree_row_reference_free(reference);
}

static void
balsa_ab_window_swap_list_entry(GtkTreeView * src, GtkTreeView * dst)
{
    GtkTreeSelection *selection;
    GSList *selected;

    g_return_if_fail(GTK_IS_TREE_VIEW(src));
    g_return_if_fail(GTK_IS_TREE_VIEW(dst));

    selection = gtk_tree_view_get_selection(src);
    selected = NULL;
    gtk_tree_selection_selected_foreach(selection,
                                        balsa_ab_window_swap_make_gslist,
                                        &selected);
    g_object_set_data(G_OBJECT(dst), BALSA_TREE_VIEW, src);
    g_slist_foreach(selected, balsa_ab_window_swap_do_swap, dst);
}

/*
  Handle a click on the main address list

  If composing then move the address to the other clist, otherwise
  open a compose window.

*/
static void
balsa_ab_window_activate_address(GtkTreeView * view,
                                    GtkTreePath * path,
                                    GtkTreeViewColumn * column,
                                    gpointer data)
{
    BalsaAbWindow *ab = data;

    g_return_if_fail(BALSA_IS_AB_WINDOW(ab));

    if (ab->composing) {
        balsa_ab_window_swap_list_entry(GTK_TREE_VIEW(ab->address_list),
                                           GTK_TREE_VIEW(ab->
                                                         recipient_list));
    } else {
        BalsaSendmsg *snd;
        GtkTreeModel *model =
            gtk_tree_view_get_model(GTK_TREE_VIEW(ab->address_list));
        GtkTreeIter iter;
        LibBalsaAddress *address;
        gint which_multiple;
        gchar *addr;

        gtk_tree_model_get_iter(model, &iter, path);
        gtk_tree_model_get(model, &iter,
                           LIST_COLUMN_ADDRESS, &address,
                           LIST_COLUMN_WHICH, &which_multiple, -1);
        addr = libbalsa_address_to_gchar(address, which_multiple);
	g_object_unref(address);
        snd = sendmsg_window_compose_with_address(addr);
        g_free(addr);

        gtk_widget_grab_focus(snd->subject.body);
    }
}

/*
  Handle a click on the recipient list.

  Only sane if composing. Move to address list.
 */
static void
balsa_ab_window_select_recipient(GtkTreeView * view,
                                    GtkTreePath * path,
                                    GtkTreeViewColumn * column,
                                    gpointer data)
{
    BalsaAbWindow *ab = data;

    g_return_if_fail( BALSA_IS_AB_WINDOW(ab) );
    g_return_if_fail( ab->composing );

    balsa_ab_window_swap_list_entry(GTK_TREE_VIEW(ab->recipient_list),
                                       GTK_TREE_VIEW(ab->address_list));
}

/*
  Handle a click on forward button
*/
static void
balsa_ab_window_move_to_recipient_list(GtkWidget *widget, BalsaAbWindow *ab)
{
    g_return_if_fail( BALSA_IS_AB_WINDOW(ab) );
    g_return_if_fail( ab->composing );

    balsa_ab_window_swap_list_entry(GTK_TREE_VIEW(ab->address_list),
				       GTK_TREE_VIEW(ab->recipient_list));
}

/*
  Handle a click on the back button
 */
static void
balsa_ab_window_remove_from_recipient_list(GtkWidget *widget, BalsaAbWindow *ab)
{
    g_return_if_fail( BALSA_IS_AB_WINDOW(ab) );
    g_return_if_fail( ab->composing );

    balsa_ab_window_swap_list_entry(GTK_TREE_VIEW(ab->recipient_list),
				       GTK_TREE_VIEW(ab->address_list));
}

/*
  Handle a click on the reload button.
 */
static void
balsa_ab_window_reload(GtkWidget *w, BalsaAbWindow *ab)
{
    balsa_ab_window_load(ab);
}

/*
 * Load the current addressbook.
 */
static void
balsa_ab_window_set_title(BalsaAbWindow *ab)
{
    LibBalsaAddressBook *address_book = ab->current_address_book;
    const gchar *type = "";
    gchar *title;

    if (LIBBALSA_IS_ADDRESS_BOOK_VCARD(address_book))
        type = "vCard";
    else if (LIBBALSA_IS_ADDRESS_BOOK_EXTERNQ(address_book))
        type = "External query";
    else if (LIBBALSA_IS_ADDRESS_BOOK_LDIF(address_book))
        type = "LDIF";
#if ENABLE_LDAP
    else if (LIBBALSA_IS_ADDRESS_BOOK_LDAP(address_book))
        type = "LDAP";
#endif
#if HAVE_GPE
    else if (LIBBALSA_IS_ADDRESS_BOOK_GPE(address_book))
        type = "GPE";
#endif

    title =
        g_strconcat(type, _(" address book: "),
                    libbalsa_address_book_get_name(address_book), NULL);
    gtk_window_set_title(GTK_WINDOW(ab), title);
    g_free(title);
}

static void
balsa_ab_window_load(BalsaAbWindow *ab)
{
    GtkTreeModel *model;
    LibBalsaABErr err;
    const gchar *filter;
    g_return_if_fail(BALSA_IS_AB_WINDOW(ab));

    model = gtk_tree_view_get_model(GTK_TREE_VIEW(ab->address_list));
    gtk_list_store_clear(GTK_LIST_STORE(model));

    if (ab->current_address_book == NULL)
	return;

    filter = gtk_entry_get_text(GTK_ENTRY(ab->filter_entry));
    err = libbalsa_address_book_load(ab->current_address_book, filter,
                                     (LibBalsaAddressBookLoadFunc)
                                     balsa_ab_window_load_cb, ab);
    if (err != LBABERR_OK && err != LBABERR_CANNOT_READ) {
	const gchar *desc =
	    libbalsa_address_book_strerror(ab->current_address_book, err);
        balsa_information_parented(GTK_WINDOW(ab),
				   LIBBALSA_INFORMATION_ERROR,
				   _("Error opening address book “%s”:\n%s"),
				   libbalsa_address_book_get_name(ab->current_address_book),
                                   desc);
    }
    balsa_ab_window_set_title(ab);
}

/*
  The address load callback. Adds a single address to the address list.

  If the current address book is in dist list mode then create a
  single entry, or else create an entry for each address in the book.
 */
static void
balsa_ab_window_load_cb(LibBalsaAddressBook *libbalsa_ab,
                        LibBalsaAddress *address, BalsaAbWindow *ab)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    guint n_addrs;
    const gchar *full_name;

    g_return_if_fail(BALSA_IS_AB_WINDOW(ab));
    g_return_if_fail(LIBBALSA_IS_ADDRESS_BOOK(libbalsa_ab));

    if (address == NULL)
	return;

    full_name = libbalsa_address_get_full_name(address);
    model = gtk_tree_view_get_model(GTK_TREE_VIEW(ab->address_list));

    n_addrs = libbalsa_address_get_n_addrs(address);
    if (libbalsa_address_book_get_dist_list_mode(libbalsa_ab)
        && n_addrs > 1) {
        gchar *address_string = libbalsa_address_to_gchar(address, -1);

        gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
        /* GtkListStore refs address, and unrefs it when cleared  */
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           LIST_COLUMN_NAME, full_name,
                           LIST_COLUMN_ADDRESS_STRING, address_string,
                           LIST_COLUMN_ADDRESS, address,
                           LIST_COLUMN_WHICH, -1,
                           -1);

	g_free(address_string);
    } else {
        guint n;

	for (n = 0; n < n_addrs; ++n) {
            const gchar *addr = libbalsa_address_get_nth_addr(address, n);

            gtk_list_store_prepend(GTK_LIST_STORE(model), &iter);
            /* GtkListStore refs address once for each address in
             * the list, and unrefs it the same number of times when
             * cleared */
            gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                               LIST_COLUMN_NAME, full_name,
                               LIST_COLUMN_ADDRESS_STRING, addr,
                               LIST_COLUMN_ADDRESS, address,
                               LIST_COLUMN_WHICH, n,
                               -1);
	}
    }
}

/*
  Search for an address in the address list.
  Attached to the changed signal of the find entry.
*/
static void
balsa_ab_window_find(GtkWidget * group_entry, BalsaAbWindow * ab)
{
    const gchar *entry_text;
    GtkTreeSelection *selection;
    GtkTreeView *tree_view;
    GtkTreeModel *model;
    GtkTreeIter iter;
    gboolean valid;
    gboolean first_path;

    g_return_if_fail(BALSA_IS_AB_WINDOW(ab));

    entry_text = gtk_entry_get_text(GTK_ENTRY(group_entry));

    if (*entry_text == '\0')
        return;

    tree_view = GTK_TREE_VIEW(ab->address_list);
    selection = gtk_tree_view_get_selection(tree_view);
    gtk_tree_selection_unselect_all(selection);

    model = gtk_tree_view_get_model(tree_view);
    first_path = TRUE;
    for (valid = gtk_tree_model_get_iter_first(model, &iter); valid;
         valid = gtk_tree_model_iter_next(model, &iter)) {
        gchar *new;

        gtk_tree_model_get(model, &iter, LIST_COLUMN_NAME, &new, -1);
        if (g_ascii_strncasecmp(new, entry_text,
                                strlen(entry_text)) == 0) {
            GtkTreePath *path = gtk_tree_model_get_path(model, &iter);

            gtk_tree_selection_select_path(selection, path);
            if (first_path) {
                gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
#if 0
                gtk_tree_view_scroll_to_cell(tree_view, path, NULL,
                                             TRUE, 0.5, 0);
#endif
                first_path = FALSE;
            }
            gtk_tree_path_free(path);
        }
        g_free(new);
    }
}

/*
  Handle a change in the dist mode
*/
static void
balsa_ab_window_dist_mode_toggled(GtkWidget * w, BalsaAbWindow *ab)
{
    gboolean active;

    g_return_if_fail(BALSA_IS_AB_WINDOW(ab));
    if(ab->current_address_book == NULL) return;

    active = gtk_toggle_button_get_active
	(GTK_TOGGLE_BUTTON(ab->single_address_mode_radio));

    libbalsa_address_book_set_dist_list_mode(ab->current_address_book, !active);

    balsa_ab_window_load(ab);
}

/*
  Handle a change in the current address book.
*/
static void
balsa_ab_window_menu_changed(GtkWidget * widget, BalsaAbWindow *ab)
{
    LibBalsaAddressBook *addr;
    int active = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
    addr = LIBBALSA_ADDRESS_BOOK(g_list_nth_data
                                 (balsa_app.address_book_list, active));
    g_assert(addr != NULL);

    ab->current_address_book = addr;

    g_signal_handler_block(ab->single_address_mode_radio,
			   ab->toggle_handler_id);
    if (libbalsa_address_book_get_dist_list_mode(ab->current_address_book))
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->dist_address_mode_radio), TRUE);
    else
	gtk_toggle_button_set_active(
	    GTK_TOGGLE_BUTTON(ab->single_address_mode_radio), TRUE);
    g_signal_handler_unblock(ab->single_address_mode_radio,
			     ab->toggle_handler_id);

    balsa_ab_window_load(ab);
}

/*
 *  Compare two rows in a GtkTreeView.
 */
static gint
balsa_ab_window_compare_entries(GtkTreeModel * model,
                                GtkTreeIter * iter1,
                                GtkTreeIter * iter2,
                                gpointer data)
{
    gchar *c1 = NULL;
    gchar *c2 = NULL;
    gint retval;

    gtk_tree_model_get(model, iter1, LIST_COLUMN_NAME, &c1, -1);
    gtk_tree_model_get(model, iter2, LIST_COLUMN_NAME, &c2, -1);

    /* Non-obvious logic, copied from g_strcmp0 */
    if (c1 == NULL)
	retval = -(c1 != c2);
    else if (c2 == NULL)
	retval = (c1 != c2);
    else
        retval = g_ascii_strcasecmp(c1, c2);

    g_free(c1);
    g_free(c2);

    return retval;
}

/* balsa_ab_window_response_cb:
   Normally, we should not destroy the window in the response callback.
   This time, we can make an exception - nobody is waiting for the result
   anyway.
*/
static void
balsa_ab_window_response_cb(BalsaAbWindow *ab, gint response)
{
    switch(response) {
    case GTK_RESPONSE_CLOSE:
        if ( !ab->composing )
            gtk_widget_destroy(GTK_WIDGET(ab));
        break;
    default: /* nothing */
	break;
    }
}

