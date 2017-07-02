/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Balsa E-Mail Client
 *
 * gpgme key listing and key server operations
 * Copyright (C) 2017 Albrecht Dreß <albrecht.dress@arcor.de>
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


#include "libbalsa-gpgme-keys.h"
#include <glib/gi18n.h>
#include "libbalsa.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme.h"


/* key server thread data */
typedef struct _keyserver_op_t {
	gpgme_ctx_t gpgme_ctx;
	gchar *fingerprint;
	GtkWindow *parent;
} keyserver_op_t;


static inline gboolean check_key(const gpgme_key_t key,
		  	  	  	  	  	  	 gboolean          secret,
								 gboolean          on_keyserver,
								 gint64            now);
static gpointer gpgme_keyserver_run(gpointer user_data);
static GtkWidget *gpgme_keyserver_do_import(keyserver_op_t *keyserver_op,
	  	  	  	  	  	  	  	  	  	    gpgme_key_t     key)
	G_GNUC_WARN_UNUSED_RESULT;
static gboolean gpgme_import_key(gpgme_ctx_t   ctx,
								 gpgme_key_t   key,
								 gchar       **import_info,
								 gpgme_key_t  *imported_key,
								 GError      **error);
static gboolean show_keyserver_dialog(gpointer user_data);
static void keyserver_op_free(keyserver_op_t *keyserver_op);


/* documentation: see header file */
gboolean
libbalsa_gpgme_list_keys(gpgme_ctx_t   ctx,
						 GList       **keys,
						 guint        *bad_keys,
						 const gchar  *pattern,
						 gboolean      secret,
						 gboolean      on_keyserver,
						 GError      **error)
{
	gpgme_error_t gpgme_err = GPG_ERR_NO_ERROR;
	gpgme_keylist_mode_t kl_mode;

	g_return_val_if_fail((ctx != NULL) && (keys != NULL), FALSE);

	/* set key list mode to external if we want to search a remote key server, or to the local key ring */
	kl_mode = gpgme_get_keylist_mode(ctx);
	if (on_keyserver) {
		kl_mode &= ~GPGME_KEYLIST_MODE_LOCAL;
		kl_mode |= GPGME_KEYLIST_MODE_EXTERN;
	} else {
		kl_mode &= ~GPGME_KEYLIST_MODE_EXTERN;
		kl_mode |= GPGME_KEYLIST_MODE_LOCAL;
	}
	gpgme_err = gpgme_set_keylist_mode(ctx, kl_mode);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("error setting key list mode"));
	}

	/* list keys */
	if (gpgme_err == GPG_ERR_NO_ERROR) {
		gpgme_err = gpgme_op_keylist_start(ctx, pattern, (int) secret);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("could not list keys for “%s”"), pattern);
		} else {
			GDateTime *current_time;
			gint64 now;
			guint bad = 0U;

			/* get the current time so we can check for expired keys */
			current_time = g_date_time_new_now_utc();
			now = g_date_time_to_unix(current_time);
			g_date_time_unref(current_time);

			/* loop over all keys */
			// FIXME - this may be /very/ slow, show a spinner?
			do {
				gpgme_key_t key;

				gpgme_err = gpgme_op_keylist_next(ctx, &key);
				if (gpgme_err == GPG_ERR_NO_ERROR) {
					if (check_key(key, secret, on_keyserver, now)) {
						*keys = g_list_prepend(*keys, key);
					} else {
						bad++;
						gpgme_key_unref(key);
					}
				} else if (gpgme_err_code(gpgme_err) != GPG_ERR_EOF) {
					libbalsa_gpgme_set_error(error, gpgme_err, _("could not list keys for “%s”"), pattern);
				} else {
					/* nothing to do, see MISRA C:2012, Rule 15.7 */
				}
			} while (gpgme_err == GPG_ERR_NO_ERROR);
			gpgme_op_keylist_end(ctx);

			if (*keys != NULL) {
				*keys = g_list_reverse(*keys);
			}
			if (bad_keys != NULL) {
				*bad_keys = bad;
			}
		}
	}

	return (gpgme_err_code(gpgme_err) == GPG_ERR_EOF);
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_keyserver_op(const gchar *fingerprint,
							GtkWindow   *parent,
							GError      **error)
{
	keyserver_op_t *keyserver_op;
	gboolean result = FALSE;

	g_return_val_if_fail(fingerprint != NULL, FALSE);

	keyserver_op = g_new0(keyserver_op_t, 1U);
	keyserver_op->gpgme_ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, NULL, NULL, error);
	if (keyserver_op->gpgme_ctx != NULL) {
		size_t fp_len;
		GThread *keyserver_th;

		/* apparently it is not possible to search a key server for fingerprints longer than 16 hex chars (64 bit)... */
		fp_len = strlen(fingerprint);
		if (fp_len > 16U) {
			keyserver_op->fingerprint = g_strdup(&fingerprint[fp_len - 16U]);
		} else {
			keyserver_op->fingerprint = g_strdup(fingerprint);
		}
		keyserver_op->parent = parent;

		/* launch thread which takes ownership of the control data structure */
		keyserver_th = g_thread_new("keyserver", gpgme_keyserver_run, keyserver_op);
		g_thread_unref(keyserver_th);
		result = TRUE;
	} else {
    	keyserver_op_free(keyserver_op);
    	result = FALSE;
	}

    return result;
}

/* ---- local functions ------------------------------------------------------ */

/** \brief Check if a key is usable
 *
 * \param key GpgME key
 * \param secret TRUE for a private key, FALSE for a public key
 * \param on_keyserver TRUE for a key on a key server, FALSE for a key in the local key ring
 * \param now current time stamp in UTC
 * \return TRUE if the key is usable, FALSE if it is expired, disabled, revoked or invalid
 *
 * Note that GpgME provides less information for keys on a key server, in particular regarding the sub-keys, so the check has to be
 * relaxed for this case.
 */
static inline gboolean
check_key(const gpgme_key_t key,
		  gboolean          secret,
		  gboolean          on_keyserver,
		  gint64            now)
{
	gboolean result = FALSE;

	if ((key->expired == 0U) && (key->revoked == 0U) && (key->disabled == 0U) && (key->invalid == 0U)) {
		gpgme_subkey_t subkey = key->subkeys;

		while (!result && (subkey != NULL)) {
			if ((on_keyserver || (secret && (subkey->can_sign != 0U)) || (!secret && (subkey->can_encrypt != 0U))) &&
				(subkey->expired == 0U) && (subkey->revoked == 0U) && (subkey->disabled == 0U) && (subkey->invalid == 0U) &&
				((subkey->expires == 0) || (subkey->expires > (long int) now))) {
				result = TRUE;
			} else {
				subkey = subkey->next;
			}
		}
	}

	return result;
}


/** \brief Key server query thread
 *
 * \param user_data thread data, cast'ed to \ref keyserver_op_t *
 * \return always NULL
 *
 * Use the passed key server thread data to call libbalsa_gpgme_list_keys().  On success, check if exactly \em one key has been
 * returned and call gpgme_keyserver_do_import() as to import or update it in this case.  Call show_keyserver_dialog() as idle
 * callback to present the user the results.
 */
static gpointer
gpgme_keyserver_run(gpointer user_data)
{
	keyserver_op_t *keyserver_op = (keyserver_op_t *) user_data;
	GList *keys = NULL;
	gboolean result;
	GError *error = NULL;

	result = libbalsa_gpgme_list_keys(keyserver_op->gpgme_ctx, &keys, NULL, keyserver_op->fingerprint, FALSE, TRUE, &error);
	if (result) {
		GtkWidget *dialog;

		if (keys == NULL) {
			dialog = gtk_message_dialog_new(keyserver_op->parent,
				GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(), GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				_("Cannot find a key with fingerprint %s on the key server."), keyserver_op->fingerprint);
		} else if (keys->next != NULL) {
			dialog = gtk_message_dialog_new(keyserver_op->parent,
				GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(), GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
				_("Found %u keys with fingerprint %s on the key server. Please check and import the proper key manually"),
				g_list_length(keys), keyserver_op->fingerprint);
		} else {
			dialog = gpgme_keyserver_do_import(keyserver_op, (gpgme_key_t) keys->data);
		}
		g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
		g_idle_add(show_keyserver_dialog, dialog);
	} else {
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("Searching the key server failed: %s"), error->message);
		g_error_free(error);
	}
	keyserver_op_free(keyserver_op);

	return NULL;
}


/** \brief Import a key
 *
 * \param keyserver_op key server thread data
 * \param key the key which shall be imported
 * \return a GtkDialog with information about the import
 *
 * Run gpgme_import_key() and create a dialogue either containing an error message on error, or the import data and the key details
 * on success.
 */
static GtkWidget *
gpgme_keyserver_do_import(keyserver_op_t *keyserver_op,
						  gpgme_key_t     key)
{
	GtkWidget *dialog;
	gboolean import_res;
	gchar *import_msg = NULL;
	GError *error = NULL;
	gpgme_key_t imported_key = NULL;

	import_res = gpgme_import_key(keyserver_op->gpgme_ctx, key, &import_msg, &imported_key, &error);
	if (!import_res) {
		dialog = gtk_message_dialog_new(keyserver_op->parent, GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
			GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
		g_error_free(error);
	} else {
		if (imported_key == NULL) {
			dialog = gtk_message_dialog_new(keyserver_op->parent, GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
				GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", import_msg);
		} else {
			dialog = libbalsa_key_dialog(keyserver_op->parent, GTK_BUTTONS_CLOSE, imported_key, GPG_SUBKEY_CAP_ALL,
				_("Key imported"), import_msg);
			gpgme_key_unref(imported_key);
		}
		g_free(import_msg);
	}

	return dialog;
}


/** \brief Import or update a key
 *
 * \param ctx GpgME context
 * \param key key which shall be imported or updated
 * \param import_info filled with a newly allocated string giving more information about a successful operation
 * \param imported_key filled with the imported key on success
 * \param error filled with error information on error, may be NULL
 * \return TRUE if the import operation was successful
 *
 * Try to import or update the passed key, typically returned by libbalsa_gpgme_list_keys() searching a key server, using the
 * passed GpgME context.  On success, fill the information message with a human-readable description.
 *
 * \note As the import operation will retrieve more information from the key server, the returned key will include more information
 *       than the originally passed key.
 */
static gboolean
gpgme_import_key(gpgme_ctx_t   ctx,
				 gpgme_key_t   key,
				 gchar       **import_info,
				 gpgme_key_t  *imported_key,
				 GError      **error)
{
	gpgme_error_t gpgme_err;
	gpgme_key_t keys[2];
	gboolean result;

	keys[0] = key;
	keys[1] = NULL;
	gpgme_err = gpgme_op_import_keys(ctx, keys);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("error importing key"));
		result = FALSE;
		*import_info = NULL;
	} else {
		gpgme_import_result_t import_result;

		import_result = gpgme_op_import_result(ctx);
		if (import_result->considered == 0) {
			*import_info = g_strdup(_("No key was imported or updated."));
		} else {
			if (import_result->imported != 0) {
				*import_info = g_strdup(_("The key was imported into the local key ring."));
			} else if (import_result->unchanged == 0) {
				GString *info;

				info = g_string_new(_("The key was updated in the local key ring:"));
				if (import_result->new_user_ids > 0) {
					g_string_append_printf(info,
						ngettext("\n\342\200\242 %d new user ID", "\n\342\200\242 %d new user ID's", import_result->new_user_ids),
						import_result->new_user_ids);
				}
				if (import_result->new_sub_keys > 0) {
					g_string_append_printf(info,
						ngettext("\n\342\200\242 %d new subkey", "\n\342\200\242 %d new subkeys", import_result->new_sub_keys),
						import_result->new_sub_keys);
				}
				if (import_result->new_signatures > 0) {
					g_string_append_printf(info,
						ngettext("\n\342\200\242 %d new signature", "\n\342\200\242 %d new signatures",
							import_result->new_signatures),
						import_result->new_signatures);
				}
				if (import_result->new_revocations > 0) {
					g_string_append_printf(info,
						ngettext("\n\342\200\242 %d new revocation", "\n\342\200\242 %d new revocations",
							import_result->new_revocations),
							import_result->new_revocations);
				}
				*import_info = g_string_free(info, FALSE);
			} else {
				*import_info = g_strdup(_("No changes for the key were found on the key server."));
			}

			/* load the possibly changed key from the local ring, ignoring any errors */
			if (key->subkeys != NULL) {
				gpgme_keylist_mode_t kl_mode;

				/* ensure local key list mode */
				kl_mode = gpgme_get_keylist_mode(ctx);
				kl_mode &= ~GPGME_KEYLIST_MODE_EXTERN;
				kl_mode |= GPGME_KEYLIST_MODE_LOCAL;
				gpgme_err = gpgme_set_keylist_mode(ctx, kl_mode);
				if (gpgme_err == GPG_ERR_NO_ERROR) {
					gpgme_err = gpgme_get_key(ctx, key->subkeys->fpr, imported_key, 0);
				}
			}
		}
		result = TRUE;
	}

	return result;
}


/** \brief Display a dialogue
 *
 * \param user_data dialogue widget, cast'ed to GtkWidget *
 * \return always FALSE
 *
 * This helper function, called as idle callback, just shows the passed dialogue.
 */
static gboolean
show_keyserver_dialog(gpointer user_data)
{
	GtkWidget *dialog = GTK_WIDGET(user_data);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
	return FALSE;
}


/** \brief Free a key server thread data structure
 *
 * \param keyserver_op key server thread data
 *
 * Destroy the GpgME context and all other allocated data in the passed key server thread data structure and the structure itself.
 */
static void
keyserver_op_free(keyserver_op_t *keyserver_op)
{
	if (keyserver_op != NULL) {
		if (keyserver_op->gpgme_ctx != NULL) {
			gpgme_release(keyserver_op->gpgme_ctx);
		}
		g_free(keyserver_op->fingerprint);
		g_free(keyserver_op);
	}
}
