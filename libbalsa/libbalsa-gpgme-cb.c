/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gpgme standard callback functions for balsa
 * Copyright (C) 2011 Albrecht Dreß <albrecht.dress@arcor.de>
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


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#include <gpgme.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include "rfc3156.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme-cb.h"
#include "geometry-manager.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_USER_ID_COLUMN = 0,
    GPG_KEY_BITS_COLUMN,
    GPG_KEY_CREATED_COLUMN,
    GPG_KEY_PTR_COLUMN,
    GPG_KEY_NUM_COLUMNS
};


/* FIXME: is this really necessary? */
typedef struct {
    GCond cond;
    const gchar *uid_hint;
    const gchar *passphrase_info;
    gint was_bad;
    GtkWindow *parent;
    gchar *res;
    gboolean done;
} ask_passphrase_data_t;


static void key_selection_changed_cb(GtkTreeSelection * selection,
				     gpgme_key_t * key);
static gint sort_iter_cmp_fn(GtkTreeModel *model, GtkTreeIter *a,
					 GtkTreeIter *b, gpointer data);


static void
row_activated_cb(GtkTreeView       *tree_view,
                 GtkTreePath       *path,
                 GtkTreeViewColumn *column,
                 gpointer           user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    model = gtk_tree_view_get_model(tree_view);
    if (gtk_tree_model_get_iter(model, &iter, path)) {
    	gpgme_key_t key;
        GtkWindow *window = user_data;
    	GtkWidget *dialog;

        gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, &key, -1);
        dialog = libbalsa_key_dialog(window, GTK_BUTTONS_CLOSE, key, GPG_SUBKEY_CAP_ALL, NULL, NULL);
        (void) gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
}


gpgme_key_t
lb_gpgme_select_key(const gchar * user_name, lb_key_sel_md_t mode, GList * keys,
		    gpgme_protocol_t protocol, GtkWindow * parent)
{
    GtkWidget *dialog;
    GtkWidget *content_area;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *scrolled_window;
    GtkWidget *tree_view;
    GtkListStore *model;
	GtkTreeSortable *sortable;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gchar *prompt;
    gpgme_key_t use_key = NULL;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"),     GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);
	geometry_manager_attach(GTK_WINDOW(dialog), "KeyList");
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_vexpand (vbox, TRUE);
    gtk_container_add(GTK_CONTAINER(content_area), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    switch (mode) {
    	case LB_SELECT_PRIVATE_KEY:
    		prompt =
    			g_strdup_printf(_("Select the private key for the signer “%s”"),
    							user_name);
    		break;
    	case LB_SELECT_PUBLIC_KEY_USER:
    		prompt =
    			g_strdup_printf(_("Select the public key for the recipient “%s”"),
                         		user_name);
    		break;
    	case LB_SELECT_PUBLIC_KEY_ANY:
    		prompt =
    			g_strdup_printf(_("There seems to be no public key for recipient "
    	                          "“%s” in your key ring.\nIf you are sure that the "
    							  "recipient owns a different key, select it from "
    							  "the list."), user_name);
    		break;
    	default:
    		g_assert_not_reached();
   	}
    label = libbalsa_create_wrap_label(prompt, FALSE);
    g_free(prompt);
    gtk_container_add(GTK_CONTAINER(vbox), label);

    label = gtk_label_new(_("Double-click key to show details"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_container_add(GTK_CONTAINER(vbox), label);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW
					(scrolled_window),
					GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
    	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), scrolled_window);

    model = gtk_list_store_new(GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,	/* user ID */
			       G_TYPE_STRING,	/* key bits */
			       G_TYPE_STRING,	/* creation date */
			       G_TYPE_POINTER);	/* key */
    sortable = GTK_TREE_SORTABLE(model);
    gtk_tree_sortable_set_sort_func(sortable, 0, sort_iter_cmp_fn, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_object_set_data(G_OBJECT(selection), "dialog", dialog);
    g_object_set_data(G_OBJECT(selection), "first", GUINT_TO_POINTER(1));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
		     G_CALLBACK(key_selection_changed_cb), &use_key);

    /* add the keys */
    while (keys != NULL) {
    	gpgme_key_t key = (gpgme_key_t) keys->data;

    	/* simply add the primary uid -- the user can show the full key details */
    	if ((key->uids != NULL) && (key->uids->uid != NULL) && (key->subkeys != NULL)) {
    		gchar *uid_info;
    		gchar *bits;
    		gchar *created;

    		uid_info = libbalsa_cert_subject_readable(key->uids->uid);
    		bits = g_strdup_printf("%u", key->subkeys->length);
    		created = libbalsa_date_to_utf8(key->subkeys->timestamp, "%x %X");
    		gtk_list_store_append(model, &iter);
    		gtk_list_store_set(model, &iter,
    			GPG_KEY_USER_ID_COLUMN, uid_info,
				GPG_KEY_BITS_COLUMN, bits,
				GPG_KEY_CREATED_COLUMN, created,
				GPG_KEY_PTR_COLUMN, key, -1);
    		g_free(uid_info);
    		g_free(bits);
    		g_free(created);
    	}
    	keys = g_list_next(keys);
    }

    g_object_unref(model);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("User ID"), renderer,
		"text", GPG_KEY_USER_ID_COLUMN, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "xalign", 1.0, NULL);
	column = gtk_tree_view_column_new_with_attributes(_("Size"), renderer,
		"text", GPG_KEY_BITS_COLUMN, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, FALSE);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Created"), renderer,
		"text", GPG_KEY_CREATED_COLUMN, NULL);
    gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, FALSE);

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(row_activated_cb), dialog);

    gtk_widget_show_all(content_area);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK) {
    	use_key = NULL;
    }
    gtk_widget_destroy(dialog);

    return use_key;
}


gboolean
lb_gpgme_accept_low_trust_key(const gchar *user_name,
				  	  	  	  gpgme_key_t  key,
							  GtkWindow   *parent)
{
    GtkWidget *dialog;
    gint result;
    gchar *message2;

    /* paranoia checks */
    g_return_val_if_fail((user_name != NULL) && (key != NULL), FALSE);

    /* create the dialog */
    message2 = g_strdup_printf(_("The owner trust for this key is “%s” only.\nUse this key anyway?"),
    	libbalsa_gpgme_validity_to_gchar_short(key->owner_trust));
    dialog = libbalsa_key_dialog(parent, GTK_BUTTONS_YES_NO, key, GPG_SUBKEY_CAP_ENCRYPT, _("Insufficient key owner trust"),
    	message2);

    /* ask the user */
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result == GTK_RESPONSE_YES;
}


/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb(GtkTreeSelection * selection, gpgme_key_t * key)
{
    GtkDialog *dialog =
    	GTK_DIALOG(g_object_get_data(G_OBJECT(selection), "dialog"));

    if (GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(selection), "first")) != 0) {
    	gtk_tree_selection_unselect_all(selection);
    	g_object_set_data(G_OBJECT(selection), "first", GUINT_TO_POINTER(0));
    } else {
        GtkTreeIter iter;
        GtkTreeModel *model;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        	gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, key, -1);
        	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_OK, TRUE);
        } else {
        	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_OK, FALSE);
        }
    }
}

/* compare function for the key list */
static gint
sort_iter_cmp_fn(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
				 gpointer data)
{
	gchar *name1, *name2;
	gint result;

	gtk_tree_model_get(model, a, 0, &name1, -1);
	gtk_tree_model_get(model, b, 0, &name2, -1);
	result = g_utf8_collate(name1, name2);
	g_free(name1);
	g_free(name2);
	return result;
 }
