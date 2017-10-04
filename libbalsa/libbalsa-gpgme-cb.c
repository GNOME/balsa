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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif

#if HAVE_MACOSX_DESKTOP
#include "macosx-helpers.h"
#endif

#include <gpgme.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include "rfc3156.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme-cb.h"


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_USER_ID_COLUMN = 0,
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
static gchar *get_passphrase_real(const gchar * uid_hint,
				  const gchar * passphrase_info,
				  int prev_was_bad, GtkWindow * parent);

static gboolean get_passphrase_idle(gpointer data);


gpgme_error_t
lb_gpgme_passphrase(void *hook, const gchar * uid_hint,
		    const gchar * passphrase_info, int prev_was_bad,
		    int fd)
{
    int foo, bar;
    gchar *passwd;
    gchar *p;
    GtkWindow *parent;

    if (hook && GTK_IS_WINDOW(hook))
	parent = (GtkWindow *) hook;
    else
	parent = NULL;

    if (!libbalsa_am_i_subthread())
	passwd =
	    get_passphrase_real(uid_hint, passphrase_info, prev_was_bad,
				parent);
    else {
	static GMutex get_passphrase_lock;
	ask_passphrase_data_t apd;

	g_mutex_lock(&get_passphrase_lock);
	g_cond_init(&apd.cond);
	apd.uid_hint = uid_hint;
	apd.was_bad = prev_was_bad;
	apd.passphrase_info = passphrase_info;
	apd.parent = parent;
	apd.done = FALSE;
	g_idle_add(get_passphrase_idle, &apd);
	while (!apd.done) {
		g_cond_wait(&apd.cond, &get_passphrase_lock);
	}
	g_cond_clear(&apd.cond);
	g_mutex_unlock(&get_passphrase_lock);
	passwd = apd.res;
    }

    if (!passwd) {
	foo = write(fd, "\n", 1);
	return foo > 0 ? GPG_ERR_CANCELED : GPG_ERR_EIO;
    }

    /* send the passphrase and erase the string */
    foo = write(fd, passwd, strlen(passwd));
    for (p = passwd; *p; p++)
	*p = random();
    g_free(passwd);
    bar = write(fd, "\n", 1);
    return foo > 0 && bar > 0 ? GPG_ERR_NO_ERROR : GPG_ERR_EIO;
}


static void
tree_view_multi_press_cb(GtkGestureMultiPress * gesture,
                         gint                   n_press,
                         gdouble                x,
                         gdouble                y,
                         gpointer               user_data)
{
    GtkTreeView *tree_view = g_object_get_data(G_OBJECT(gesture), "tree-view-gesture");
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
    GtkTreePath *path;
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (n_press != 2) {
        return;
    }
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);

    if (gtk_tree_view_get_path_at_pos(tree_view, (gint) x, (gint) y, &path, NULL, NULL, NULL)) {
        if (!gtk_tree_selection_path_is_selected(selection, path)) {
            gtk_tree_view_set_cursor(tree_view, path, NULL, FALSE);
            gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0, 0);
        }
        gtk_tree_path_free(path);
    }

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    	gpgme_key_t key;
    	GtkWidget *dialog;

        gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, &key, -1);
        dialog = libbalsa_key_dialog(GTK_WINDOW(user_data), GTK_BUTTONS_CLOSE, key, GPG_SUBKEY_CAP_ALL, NULL, NULL);
        (void) gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }

    return;
}


gpgme_key_t
lb_gpgme_select_key(const gchar * user_name, lb_key_sel_md_t mode, GList * keys,
		    gpgme_protocol_t protocol, GtkWindow * parent)
{
    GtkWidget *dialog;
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
    gint width, height;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
    GtkGesture *gesture;

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
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_vexpand (vbox, TRUE);
    gtk_container_add(GTK_CONTAINER
		      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
		      vbox);
    g_object_set(G_OBJECT(vbox), "margin", 12, NULL);
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
    label = gtk_label_new(prompt);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    g_free(prompt);
    gtk_box_pack_start(GTK_BOX(vbox), label);

    label = gtk_label_new(_("Double-click key to show details"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(vbox), label);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW
					(scrolled_window),
					GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
    	GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window);

    model = gtk_list_store_new(GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,	/* user ID */
			       G_TYPE_POINTER);	/* key */
    sortable = GTK_TREE_SORTABLE(model);
    gtk_tree_sortable_set_sort_func(sortable, 0, sort_iter_cmp_fn, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_object_set_data(G_OBJECT(selection), "dialog", dialog);
    g_object_set_data(G_OBJECT(selection), "first", GUINT_TO_POINTER(1));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(key_selection_changed_cb), &use_key);

    /* add the keys */
    while (keys != NULL) {
    	gpgme_key_t key = (gpgme_key_t) keys->data;

    	/* simply add the primary uid -- the user can show the full key details */
    	if ((key->uids != NULL) && (key->uids->uid != NULL)) {
    		gchar *uid_info;

    		uid_info = libbalsa_cert_subject_readable(key->uids->uid);
    		gtk_list_store_append(model, &iter);
    		gtk_list_store_set(model, &iter,
    			GPG_KEY_USER_ID_COLUMN, uid_info,
				GPG_KEY_PTR_COLUMN, key, -1);
    		g_free(uid_info);
    	}
    	keys = g_list_next(keys);
    }

    g_object_unref(G_OBJECT(model));

	renderer = gtk_cell_renderer_text_new();
	column =
	    gtk_tree_view_column_new_with_attributes(_("User ID"),
						     renderer, "text", 0,
						     NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);

    gesture = gtk_gesture_multi_press_new(tree_view);
    g_object_set_data_full(G_OBJECT(tree_view), "tree-view-gesture",
                           gesture, g_object_unref);
    g_object_set_data(G_OBJECT(gesture), "tree-view-gesture", tree_view);
    g_signal_connect(gesture, "pressed", G_CALLBACK(tree_view_multi_press_cb), dialog);

    /* set window size to 2/3 of the parent */
    gtk_window_get_size(parent, &width, &height);
    gtk_window_set_default_size(GTK_WINDOW(dialog), (2 * width) / 3, (2 * height) / 3);
    gtk_widget_show(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

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
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif

    /* ask the user */
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result == GTK_RESPONSE_YES;
}


#include "padlock-keyhole.xpm"

/*
 * FIXME - usually, the passphrase should /never/ be requested using this function, but directly by GnuPG using pinentry which
 * guarantees, inter alia, the use of safe (unpagable) memory.  For GnuPG >= 2.1 the pinentry mode has to be set to
 * GPGME_PINENTRY_MODE_LOOPBACK to enable the passphrase callback.  Consider to remove this function completely...
 */
static gchar *
get_passphrase_real(const gchar * uid_hint, const gchar * passphrase_info,
		    int prev_was_bad, GtkWindow * parent)
{
    static GdkPixbuf *padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    gchar *prompt, *passwd;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Enter Passphrase"), parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"),     GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    g_object_set(G_OBJECT(hbox), "margin", 12, NULL);
    gtk_container_add(GTK_CONTAINER
		      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
		      hbox);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (!padlock_keyhole)
	padlock_keyhole =
	    gdk_pixbuf_new_from_xpm_data(padlock_keyhole_xpm);
    gtk_box_pack_start(GTK_BOX(vbox), gtk_image_new_from_pixbuf(padlock_keyhole));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (prev_was_bad)
	prompt =
	    g_strdup_printf(_
			    ("%s\nThe passphrase for this key was bad, please try again!\n\nKey: %s"),
			    passphrase_info, uid_hint);
    else
	prompt =
	    g_strdup_printf(_
			    ("%s\nPlease enter the passphrase for the secret key!\n\nKey: %s"),
			    passphrase_info, uid_hint);
    gtk_container_add(GTK_CONTAINER(vbox), gtk_label_new(prompt));
    g_free(prompt);
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(vbox), entry);

    gtk_widget_show(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(entry);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
	passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    else
	passwd = NULL;

    gtk_widget_destroy(dialog);

    return passwd;
}


/* get_passphrase_idle:
   called in MT mode by the main thread.
 */
static gboolean
get_passphrase_idle(gpointer data)
{
    ask_passphrase_data_t *apd = (ask_passphrase_data_t *) data;

    apd->res =
	get_passphrase_real(apd->uid_hint, apd->passphrase_info,
			    apd->was_bad, apd->parent);
    apd->done = TRUE;
    g_cond_signal(&apd->cond);
    return FALSE;
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
