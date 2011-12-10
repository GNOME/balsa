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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "misc.h"
#endif

#include <gpgme.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include "rfc3156.h"
#include "libbalsa-gpgme-cb.h"


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_USER_ID_COLUMN = 0,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_PTR_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

#ifdef BALSA_USE_THREADS
/* FIXME: is this really necessary? */
typedef struct {
    pthread_cond_t cond;
    const gchar *uid_hint;
    const gchar *passphrase_info;
    gint was_bad;
    GtkWindow *parent;
    gchar *res;
} ask_passphrase_data_t;
#endif


static void key_selection_changed_cb(GtkTreeSelection * selection,
				     gpgme_key_t * key);
static gchar *get_passphrase_real(const gchar * uid_hint,
				  const gchar * passphrase_info,
				  int prev_was_bad, GtkWindow * parent);

#ifdef BALSA_USE_THREADS
static gboolean get_passphrase_idle(gpointer data);
#endif


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

#ifdef BALSA_USE_THREADS
    if (!libbalsa_am_i_subthread())
	passwd =
	    get_passphrase_real(uid_hint, passphrase_info, prev_was_bad,
				parent);
    else {
	static pthread_mutex_t get_passphrase_lock =
	    PTHREAD_MUTEX_INITIALIZER;
	ask_passphrase_data_t apd;

	pthread_mutex_lock(&get_passphrase_lock);
	pthread_cond_init(&apd.cond, NULL);
	apd.uid_hint = uid_hint;
	apd.was_bad = prev_was_bad;
	apd.passphrase_info = passphrase_info;
	apd.parent = parent;
	g_idle_add(get_passphrase_idle, &apd);
	pthread_cond_wait(&apd.cond, &get_passphrase_lock);
	pthread_cond_destroy(&apd.cond);
	pthread_mutex_unlock(&get_passphrase_lock);
	passwd = apd.res;
    }
#else
    passwd = get_passphrase_real(context, uid_hint, prev_was_bad);
#endif				/* BALSA_USE_THREADS */

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


gpgme_key_t
lb_gpgme_select_key(const gchar * user_name, gboolean secret, GList * keys,
		    gpgme_protocol_t protocol, GtkWindow * parent)
{
    static const gchar *col_titles[] =
	{ N_("User ID"), N_("Key ID"), N_("Length"), N_("Validity") };
    GtkWidget *dialog;
    GtkWidget *vbox;
    GtkWidget *label;
    GtkWidget *scrolled_window;
    GtkWidget *tree_view;
    GtkTreeStore *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;
    gint i, last_col;
    gchar *prompt;
    gchar *upcase_name;
    gpgme_key_t use_key = NULL;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_CANCEL, NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER
		      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
		      vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    if (secret)
	prompt =
	    g_strdup_printf(_("Select the private key for the signer %s"),
			    user_name);
    else
	prompt = g_strdup_printf(_
				 ("Select the public key for the recipient %s"),
				 user_name);
    label = gtk_label_new(prompt);
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    g_free(prompt);
    gtk_box_pack_start(GTK_BOX(vbox), label, FALSE, TRUE, 0);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW
					(scrolled_window),
					GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
				   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(vbox), scrolled_window, TRUE, TRUE, 0);

    model = gtk_tree_store_new(GPG_KEY_NUM_COLUMNS, G_TYPE_STRING,	/* user ID */
			       G_TYPE_STRING,	/* key ID */
			       G_TYPE_INT,	/* length */
			       G_TYPE_STRING,	/* validity (gpg encrypt only) */
			       G_TYPE_POINTER);	/* key */

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(key_selection_changed_cb), &use_key);

    /* add the keys */
    upcase_name = g_ascii_strup(user_name, -1);
    while (keys) {
	gpgme_key_t key = (gpgme_key_t) keys->data;
	gpgme_subkey_t subkey = key->subkeys;
	gpgme_user_id_t uid = key->uids;
	gchar *uid_info = NULL;
	gboolean uid_found;

	/* find the relevant subkey */
	while (subkey && ((secret && !subkey->can_sign) ||
			  (!secret && !subkey->can_encrypt)))
	    subkey = subkey->next;

	/* find the relevant uid */
	uid_found = FALSE;
	while (uid && !uid_found) {
	    g_free(uid_info);
	    uid_info = libbalsa_cert_subject_readable(uid->uid);

	    /* check the email field which may or may not be present */
	    if (uid->email && !g_ascii_strcasecmp(uid->email, user_name))
		uid_found = TRUE;
	    else {
		/* no email or no match, check the uid */
		gchar *upcase_uid = g_ascii_strup(uid_info, -1);

		if (strstr(upcase_uid, upcase_name))
		    uid_found = TRUE;
		else
		    uid = uid->next;
		g_free(upcase_uid);
	    }
	}

	/* append the element */
	if (subkey && uid) {
	    gtk_tree_store_append(GTK_TREE_STORE(model), &iter, NULL);
	    gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
			       GPG_KEY_USER_ID_COLUMN, uid_info,
			       GPG_KEY_ID_COLUMN, subkey->keyid,
			       GPG_KEY_LENGTH_COLUMN, subkey->length,
			       GPG_KEY_VALIDITY_COLUMN,
			       libbalsa_gpgme_validity_to_gchar_short(uid->
								      validity),
			       GPG_KEY_PTR_COLUMN, key, -1);
	}
	g_free(uid_info);
	keys = g_list_next(keys);
    }
    g_free(upcase_name);

    g_object_unref(G_OBJECT(model));
    /* show the validity only if we are asking for a gpg public key */
    last_col = (protocol == GPGME_PROTOCOL_CMS || secret) ?
	GPG_KEY_LENGTH_COLUMN : GPG_KEY_VALIDITY_COLUMN;
    for (i = 0; i <= last_col; i++) {
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();
	column =
	    gtk_tree_view_column_new_with_attributes(_(col_titles[i]),
						     renderer, "text", i,
						     NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
    }

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 300);
    gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
	use_key = NULL;
    gtk_widget_destroy(dialog);

    return use_key;
}


gboolean
lb_gpgme_accept_low_trust_key(const gchar * user_name,
			      const gpgme_user_id_t user_id,
			      GtkWindow * parent)
{
    GtkWidget *dialog;
    gint result;
    gchar *message1;
    gchar *message2;

    /* paranoia checks */
    g_return_val_if_fail(user_id != NULL, FALSE);

    /* build the message */
    message1 =
	g_strdup_printf(_("Insufficient trust for recipient %s"),
			user_name);
    message2 =
	g_strdup_printf(_
			("The validity of the key with user ID \"%s\" is \"%s\"."),
			user_id->uid,
			libbalsa_gpgme_validity_to_gchar_short(user_id->
							       validity));
    dialog =
	gtk_message_dialog_new_with_markup(parent,
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_WARNING,
					   GTK_BUTTONS_YES_NO,
					   "<b>%s</b>\n\n%s\n%s", message1,
					   message2,
					   _("Use this key anyway?"));
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif

    /* ask the user */
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result == GTK_RESPONSE_YES;

}


#include "padlock-keyhole.xpm"


static gchar *
get_passphrase_real(const gchar * uid_hint, const gchar * passphrase_info,
		    int prev_was_bad, GtkWindow * parent)
{
    static GdkPixbuf *padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    gchar *prompt, *passwd;

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Enter Passphrase"), parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_CANCEL, NULL);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, parent);
#endif
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
    gtk_container_add(GTK_CONTAINER
		      (gtk_dialog_get_content_area(GTK_DIALOG(dialog))),
		      hbox);

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (!padlock_keyhole)
	padlock_keyhole =
	    gdk_pixbuf_new_from_xpm_data(padlock_keyhole_xpm);
    gtk_box_pack_start(GTK_BOX(vbox),
		       gtk_image_new_from_pixbuf(padlock_keyhole), FALSE,
		       FALSE, 0);
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

    gtk_widget_show_all(gtk_dialog_get_content_area(GTK_DIALOG(dialog)));
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


#ifdef BALSA_USE_THREADS
/* get_passphrase_idle:
   called in MT mode by the main thread.
 */
static gboolean
get_passphrase_idle(gpointer data)
{
    ask_passphrase_data_t *apd = (ask_passphrase_data_t *) data;

    gdk_threads_enter();
    apd->res =
	get_passphrase_real(apd->uid_hint, apd->passphrase_info,
			    apd->was_bad, apd->parent);
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}
#endif


/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb(GtkTreeSelection * selection, gpgme_key_t * key)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
	gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, key, -1);
}
