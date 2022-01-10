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


typedef struct {
    GCond cond;
    const gchar *uid_hint;
    const gchar *passphrase_info;
    gint was_bad;
    GtkWindow *parent;
    GtkWidget *entry;
    gchar *res;
    gboolean done;
} ask_passphrase_data_t;


static void key_selection_changed_cb(GtkTreeSelection * selection, gpointer user_data);
static int sort_iter_cmp_fn(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer data);
static gboolean get_passphrase_idle(gpointer user_data);


gpgme_error_t
lb_gpgme_passphrase(void *hook, const gchar * uid_hint,
		    const gchar * passphrase_info, int prev_was_bad,
		    int fd)
{
    int foo, bar;
    char *passwd;
    char *p;
    GtkWindow *parent;
    static GMutex get_passphrase_lock;
    ask_passphrase_data_t apd;

    g_return_val_if_fail(libbalsa_am_i_subthread(), 0);

    if (hook && GTK_IS_WINDOW(hook))
	parent = (GtkWindow *) hook;
    else
	parent = NULL;

    g_mutex_lock(&get_passphrase_lock);
    g_cond_init(&apd.cond);

    apd.uid_hint = uid_hint;
    apd.was_bad = prev_was_bad;
    apd.passphrase_info = passphrase_info;
    apd.parent = parent;
    apd.done = FALSE;
    g_idle_add(get_passphrase_idle, &apd);

    while (!apd.done)
    	g_cond_wait(&apd.cond, &get_passphrase_lock);

    g_cond_clear(&apd.cond);
    g_mutex_unlock(&get_passphrase_lock);
    passwd = apd.res;

    if (passwd == NULL) {
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
        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_widget_show(dialog);
    }
}

typedef struct {
    GMutex          lock;
    GCond           cond;
    gboolean        done;
    const char     *user_name;
    lb_key_sel_md_t mode;
    GList          *keys;
    GtkWindow      *parent;
    gpgme_key_t     use_key;
    GtkDialog      *dialog;
    gboolean        first;
} select_key_data_t;

static void
select_key_response(GtkDialog *dialog,
                    int        response_id,
                    gpointer   user_data)
{
    select_key_data_t *data = user_data;

    g_mutex_lock(&data->lock);

    if (response_id != GTK_RESPONSE_OK)
        data->use_key = NULL;
    data->done = TRUE;

    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static gboolean
select_key_idle(gpointer user_data)
{
    select_key_data_t *data = user_data;
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
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GList *l;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 data->parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"),     GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_OK, FALSE);
    geometry_manager_attach(GTK_WINDOW(dialog), "KeyList");
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, data->parent);
#endif

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_vexpand (vbox, TRUE);
    gtk_box_append(GTK_BOX(content_area), vbox);

    gtk_widget_set_margin_top(vbox, 12);
    gtk_widget_set_margin_bottom(vbox, 12);
    gtk_widget_set_margin_start(vbox, 12);
    gtk_widget_set_margin_end(vbox, 12);

    switch (data->mode) {
    	case LB_SELECT_PRIVATE_KEY:
    		prompt =
    			g_strdup_printf(_("Select the private key for the signer “%s”"),
    							data->user_name);
    		break;
    	case LB_SELECT_PUBLIC_KEY_USER:
    		prompt =
    			g_strdup_printf(_("Select the public key for the recipient “%s”"),
                         		data->user_name);
    		break;
    	case LB_SELECT_PUBLIC_KEY_ANY:
    		prompt =
    			g_strdup_printf(_("There seems to be no public key for recipient "
    	                          "“%s” in your key ring.\nIf you are sure that the "
    							  "recipient owns a different key, select it from "
    							  "the list."), data->user_name);
    		break;
    	default:
    		g_assert_not_reached();
    }

    label = libbalsa_create_wrap_label(prompt, FALSE);
    g_free(prompt);
    gtk_box_append(GTK_BOX(vbox), label);

    label = gtk_label_new(_("Double-click key to show details"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(vbox), label);

    scrolled_window = gtk_scrolled_window_new();
    gtk_scrolled_window_set_has_frame(GTK_SCROLLED_WINDOW(scrolled_window), TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(vbox), scrolled_window);

    model = gtk_list_store_new(GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,	/* user ID */
			       G_TYPE_STRING,	/* key bits */
			       G_TYPE_STRING,	/* creation date */
			       G_TYPE_POINTER);	/* key */
    sortable = GTK_TREE_SORTABLE(model);
    gtk_tree_sortable_set_sort_func(sortable, 0, sort_iter_cmp_fn, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id(sortable, 0, GTK_SORT_ASCENDING);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed", G_CALLBACK(key_selection_changed_cb), data);

    data->dialog = GTK_DIALOG(dialog);
    data->first = TRUE;

    /* add the keys */
    for (l = data->keys; l != NULL; l = l->next) {
    	gpgme_key_t key = (gpgme_key_t) l->data;

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

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled_window), tree_view);
    g_signal_connect(tree_view, "row-activated", G_CALLBACK(row_activated_cb), dialog);

    g_signal_connect(dialog, "response", G_CALLBACK(select_key_response), data);

    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}

gpgme_key_t
lb_gpgme_select_key(const char * user_name, lb_key_sel_md_t mode, GList * keys,
		    gpgme_protocol_t protocol, GtkWindow * parent)
{
    select_key_data_t data;

    g_return_val_if_fail(libbalsa_am_i_subthread(), NULL);

    data.user_name = user_name;
    data.mode = mode;
    data.keys = keys;
    data.parent = parent;

    g_mutex_init(&data.lock);
    g_cond_init(&data.cond);

    g_mutex_lock(&data.lock);
    data.done = FALSE;
    g_idle_add(select_key_idle, &data);
    while (!data.done)
        g_cond_wait(&data.cond, &data.lock);
    g_mutex_unlock(&data.lock);

    g_mutex_clear(&data.lock);
    g_cond_clear(&data.cond);

    return data.use_key;
}

typedef struct {
    GMutex       lock;
    GCond        cond;
    int          result;
    gpgme_key_t  key;
    GtkWindow   *parent;
} accept_low_trust_key_data_t;

static void
accept_low_trust_key_response(GtkDialog *dialog,
                              int        response_id,
                              gpointer   user_data)
{
    accept_low_trust_key_data_t *data = user_data;

    g_mutex_lock(&data->lock);
    data->result = response_id;
    g_cond_signal(&data->cond);
    g_mutex_unlock(&data->lock);

    gtk_window_destroy(GTK_WINDOW(dialog));
}


static gboolean
accept_low_trust_key_idle(gpointer user_data)
{
    accept_low_trust_key_data_t *data = user_data;
    GtkWidget *dialog;
    char *message2;

    /* create the dialog */
    message2 = g_strdup_printf(_("The owner trust for this key is “%s” only.\nUse this key anyway?"),
    	libbalsa_gpgme_validity_to_gchar_short(data->key->owner_trust));
    dialog = libbalsa_key_dialog(data->parent, GTK_BUTTONS_YES_NO, data->key, GPG_SUBKEY_CAP_ENCRYPT, _("Insufficient key owner trust"),
    	message2);
    g_free(message2);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, data->parent);
#endif

    /* ask the user */
    g_signal_connect(dialog, "response", G_CALLBACK(accept_low_trust_key_response), data);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}

gboolean
lb_gpgme_accept_low_trust_key(const gchar *user_name,
                              gpgme_key_t  key,
                              GtkWindow   *parent)
{
    accept_low_trust_key_data_t data;

    /* paranoia checks */
    g_return_val_if_fail((user_name != NULL) && (key != NULL), FALSE);

    g_mutex_init(&data.lock);
    g_cond_init(&data.cond);

    g_mutex_lock(&data.lock);

    data.key = key;
    data.parent = parent;
    g_idle_add(accept_low_trust_key_idle, &data);

    data.result = 0;
    while (data.result == 0)
        g_cond_wait(&data.cond, &data.lock);

    g_mutex_unlock(&data.lock);

    g_mutex_clear(&data.lock);
    g_cond_clear(&data.cond);

    return data.result == GTK_RESPONSE_YES;
}


#include "padlock-keyhole.xpm"

/*
 * FIXME - usually, the passphrase should /never/ be requested using this function, but directly by GnuPG using pinentry which
 * guarantees, inter alia, the use of safe (unpagable) memory.  For GnuPG >= 2.1 the pinentry mode has to be set to
 * GPGME_PINENTRY_MODE_LOOPBACK to enable the passphrase callback.  Consider to remove this function completely...
 */
static void
get_passphrase_response(GtkDialog *dialog,
                        int        response_id,
                        gpointer   user_data)
{
    ask_passphrase_data_t *apd = user_data;

    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response_id == GTK_RESPONSE_OK)
	apd->res = g_strdup(gtk_editable_get_text(GTK_EDITABLE(apd->entry)));
    else
	apd->res = NULL;

    apd->done = TRUE;
    g_cond_signal(&apd->cond);
}


/* get_passphrase_idle:
   called in MT mode by the main thread.
 */
static gboolean
get_passphrase_idle(gpointer data)
{
    ask_passphrase_data_t *apd = (ask_passphrase_data_t *) data;
    static GdkPixbuf *padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    GtkWidget *content_area;
    gchar *prompt;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Enter Passphrase"), apd->parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT |
                                         libbalsa_dialog_flags(),
                                         _("_OK"),     GTK_RESPONSE_OK,
                                         _("_Cancel"), GTK_RESPONSE_CANCEL,
                                         NULL);
    content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, apd->parent);
#endif

    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);

    gtk_widget_set_margin_top(hbox, 12);
    gtk_widget_set_margin_bottom(hbox, 12);
    gtk_widget_set_margin_start(hbox, 12);
    gtk_widget_set_margin_end(hbox, 12);

    gtk_box_append(GTK_BOX(content_area), hbox);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(hbox), vbox);
    if (!padlock_keyhole)
	padlock_keyhole =
	    gdk_pixbuf_new_from_xpm_data(padlock_keyhole_xpm);
    gtk_box_append(GTK_BOX(vbox), gtk_image_new_from_pixbuf(padlock_keyhole));
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(hbox), vbox);
    if (apd->was_bad)
	prompt =
	    g_strdup_printf(_
			    ("%s\nThe passphrase for this key was bad, please try again!\n\nKey: %s"),
			    apd->passphrase_info, apd->uid_hint);
    else
	prompt =
	    g_strdup_printf(_
			    ("%s\nPlease enter the passphrase for the secret key!\n\nKey: %s"),
			    apd->passphrase_info, apd->uid_hint);
    gtk_box_append(GTK_BOX(vbox), gtk_label_new(prompt));
    g_free(prompt);
    apd->entry = entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(vbox), entry);

    gtk_editable_set_width_chars(GTK_EDITABLE(entry), 40);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(entry);

    g_signal_connect(dialog, "response", G_CALLBACK(get_passphrase_response), apd);
    gtk_widget_show(dialog);

    return G_SOURCE_REMOVE;
}


/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb(GtkTreeSelection * selection, gpointer user_data)
{
    select_key_data_t *data = user_data;

    if (data->first) {
    	gtk_tree_selection_unselect_all(selection);
    	data->first = FALSE;
    } else {
        GtkTreeIter iter;
        GtkTreeModel *model;

        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        	gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, &data->use_key, -1);
        	gtk_dialog_set_response_sensitive(data->dialog, GTK_RESPONSE_OK, TRUE);
        } else {
        	gtk_dialog_set_response_sensitive(data->dialog, GTK_RESPONSE_OK, FALSE);
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
