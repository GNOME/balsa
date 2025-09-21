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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */


#include "libbalsa-gpgme-keys.h"
#include <glib/gi18n.h>
#include "libbalsa.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


/* key server thread data */
typedef struct _keyserver_op_t {
	gpgme_ctx_t gpgme_ctx;
	gchar *fingerprint;
	gchar *email_address;
	gpgme_key_t imported_key;
	GtkWindow *parent;
	GtkMessageType msg_type;
	gchar *message;
} keyserver_op_t;


static gboolean list_keys_real(gpgme_ctx_t            ctx,
							   GList                **keys,
							   guint                 *bad_keys,
							   const gchar           *pattern,
							   gboolean               secret,
							   gpgme_keylist_mode_t   keylist_mode,
							   gboolean	              list_bad_keys,
							   GError               **error);
static gboolean list_local_pubkeys_real(gpgme_ctx_t           ctx,
										GList               **keys,
										InternetAddressList  *addresses,
										GError              **error);
static gboolean import_key_real(gpgme_ctx_t     ctx,
								gconstpointer   key_buf,
								gsize		    buf_len,
								gchar         **import_info,
								GError        **error);
static inline gboolean check_key(const gpgme_key_t key,
								 gboolean          secret,
								 gboolean          on_keyserver);
static gpointer gpgme_keyserver_run(gpointer user_data);
static gboolean gpgme_locate_wkd_key(keyserver_op_t  *keyserver_op,
									 GError         **error);
static gboolean gpgme_copy_key(gpgme_ctx_t   dst_ctx,
							   gpgme_ctx_t   src_ctx,
							   const gchar  *fingerprint,
							   GError      **error);
static gboolean gpgme_import_key(gpgme_ctx_t   ctx,
								 gpgme_key_t   key,
								 gchar       **import_info,
								 gpgme_key_t  *imported_key,
								 GError      **error);
static gchar *gpgme_import_res_to_gchar(gpgme_import_result_t import_result)
	G_GNUC_WARN_UNUSED_RESULT;
static gboolean show_keyserver_dialog(gpointer user_data);
static gint keyserver_import_mailbox(gpgme_ctx_t ctx,
									 InternetAddressMailbox *address,
									 GError **error);


/* documentation: see header file */
gboolean
libbalsa_gpgme_list_keys(gpgme_ctx_t   ctx,
						 GList       **keys,
						 guint        *bad_keys,
						 const gchar  *pattern,
						 gboolean      secret,
						 gboolean	   list_bad_keys,
						 GError      **error)
{
	g_return_val_if_fail((ctx != NULL) && (keys != NULL), FALSE);

	return list_keys_real(ctx, keys, bad_keys, pattern, secret, GPGME_KEYLIST_MODE_LOCAL, list_bad_keys, error);
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_list_local_pubkeys(gpgme_ctx_t           ctx,
								  GList               **keys,
								  InternetAddressList  *addresses,
								  GError              **error)
{
	gboolean result;

	g_return_val_if_fail((ctx != NULL) && IS_INTERNET_ADDRESS_LIST(addresses) && (keys != NULL), FALSE);

	result = list_local_pubkeys_real(ctx, keys, addresses, error);
	if (result) {
		if (*keys != NULL) {
			GList * p;

			for (p = *keys; (p != NULL) && (p->next != NULL); p = p->next) {
				GList *q;

				q = p->next;
				while (q != NULL) {
					GList *next = q->next;

					if (strcmp(((gpgme_key_t) (p->data))->fpr, ((gpgme_key_t) (q->data))->fpr) == 0) {
						gpgme_key_unref((gpgme_key_t) (q->data));
						*keys = g_list_delete_link(*keys, q);
					}
					q = next;
				}
			}
		}
	} else {
		g_list_free_full(*keys, (GDestroyNotify) gpgme_key_unref);
		*keys = NULL;
	}
	return result;
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_have_all_keys(gpgme_ctx_t           ctx,
							 InternetAddressList  *addresses,
							 GError              **error)
{
	int i;
	gboolean result = TRUE;

	g_return_val_if_fail((ctx != NULL) && IS_INTERNET_ADDRESS_LIST(addresses), FALSE);

	for (i = 0; result && (i < internet_address_list_length(addresses)); i++) {
		InternetAddress *this_addr;

		this_addr = internet_address_list_get_address(addresses, i);
		if (INTERNET_ADDRESS_IS_GROUP(this_addr)) {
			result = libbalsa_gpgme_have_all_keys(ctx, INTERNET_ADDRESS_GROUP(this_addr)->members, error);
		} else {
			result = libbalsa_gpgme_have_key(ctx, INTERNET_ADDRESS_MAILBOX(this_addr), error);
		}
	}
	return result;
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_have_key(gpgme_ctx_t              ctx,
						InternetAddressMailbox  *mailbox,
						GError                 **error)
{
	gchar *mailbox_full;
	GList *keys = NULL;
	gboolean result;

	g_return_val_if_fail((ctx != NULL) && INTERNET_ADDRESS_IS_MAILBOX(mailbox), FALSE);

	mailbox_full = g_strconcat("<", mailbox->addr, ">", NULL);
	result = list_keys_real(ctx, &keys, NULL, mailbox_full, FALSE, GPGME_KEYLIST_MODE_LOCAL, FALSE, error);
	g_free(mailbox_full);
	if (result) {
		if (keys == NULL) {
			result = FALSE;
		} else {
			g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
		}
	}
	return result;
}


/* documentation: see header file */
gpgme_key_t
libbalsa_gpgme_load_key(gpgme_ctx_t   ctx,
						const gchar  *fingerprint,
						GError      **error)
{
	gpgme_key_t key = NULL;
	gpgme_error_t gpgme_err;
	gpgme_keylist_mode_t kl_mode;

	g_return_val_if_fail((ctx != NULL) && (fingerprint != NULL), NULL);

	/* only use the local key ring */
	kl_mode = gpgme_get_keylist_mode(ctx);
	gpgme_err = gpgme_set_keylist_mode(ctx, (kl_mode & ~GPGME_KEYLIST_MODE_EXTERN) | GPGME_KEYLIST_MODE_LOCAL);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("error setting key list mode"));
	}

	if (gpgme_err == GPG_ERR_NO_ERROR) {
		gpgme_err = gpgme_op_keylist_start(ctx, fingerprint, 0);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("could not list keys for “%s”"), fingerprint);
		} else {
			gpgme_err = gpgme_op_keylist_next(ctx, &key);
			if (gpgme_err != GPG_ERR_NO_ERROR) {
				libbalsa_gpgme_set_error(error, gpgme_err, _("could not list keys for “%s”"), fingerprint);
			} else {
				gpgme_key_t next_key;

				/* verify this is the only one */
				gpgme_err = gpgme_op_keylist_next(ctx, &next_key);
				if (gpgme_err == GPG_ERR_NO_ERROR) {
					libbalsa_gpgme_set_error(error, GPG_ERR_AMBIGUOUS, _("ambiguous keys for “%s”"), fingerprint);
					gpgme_key_unref(next_key);
					gpgme_key_unref(key);
					key = NULL;
				}
			}
		}
	}
	gpgme_set_keylist_mode(ctx, kl_mode);

	return key;
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_keyserver_op(const gchar *fingerprint,
							const gchar *email_address,
							GtkWindow   *parent,
							GError      **error)
{
	keyserver_op_t *keyserver_op;
	gboolean result = FALSE;

	g_return_val_if_fail(fingerprint != NULL, FALSE);

	keyserver_op = g_new0(keyserver_op_t, 1U);
	keyserver_op->gpgme_ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, error);
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
		keyserver_op->email_address = g_strdup(email_address);
		keyserver_op->parent = parent;

		/* launch thread which takes ownership of the control data structure */
		keyserver_th = g_thread_new("keyserver", gpgme_keyserver_run, keyserver_op);
		g_thread_unref(keyserver_th);
		result = TRUE;
	} else {
		g_free(keyserver_op);
		result = FALSE;
	}

	return result;
}


/* documentation: see header file */
gint
libbalsa_gpgme_keyserver_import(gpgme_ctx_t           ctx,
								InternetAddressList  *addresses,
								GError              **error)
{
	gint n;
	gint result = 0;

	for (n = 0; (result >= 0) && (n < internet_address_list_length(addresses)); n++) {
		InternetAddress *this_addr;
		gint sub_count;

		this_addr = internet_address_list_get_address(addresses, n);
		if (INTERNET_ADDRESS_IS_MAILBOX(this_addr)) {
			sub_count = keyserver_import_mailbox(ctx, INTERNET_ADDRESS_MAILBOX(this_addr), error);
		} else if (INTERNET_ADDRESS_IS_GROUP(this_addr)) {
			sub_count = libbalsa_gpgme_keyserver_import(ctx, INTERNET_ADDRESS_GROUP(this_addr)->members, error);
		} else {
			g_assert_not_reached();		/* should never happen */
		}
		if (sub_count >= 0) {
			result += sub_count;
		} else {
			result = sub_count;
		}
	}

	return result;
}


/* documentation: see header file */
gchar *
libbalsa_gpgme_export_key(gpgme_ctx_t   ctx,
						  gpgme_key_t   key,
						  const gchar  *name,
						  GError      **error)
{
	gpgme_error_t gpgme_err;
	gpgme_data_t buffer;
	gchar *result = NULL;

	g_return_val_if_fail((ctx != NULL) && (key != NULL), FALSE);

	gpgme_set_armor(ctx, 1);
	gpgme_err = gpgme_data_new(&buffer);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("cannot create data buffer"));
	} else {
		gpgme_key_t keys[2];

		keys[0] = key;
		keys[1] = NULL;
		gpgme_err = gpgme_op_export_keys(ctx, keys, 0, buffer);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("exporting key for “%s” failed"), name);
		} else {
			off_t key_size;

			/* as we are working on a memory buffer, we can omit error checking... */
			key_size = gpgme_data_seek(buffer, 0, SEEK_END);
			result = g_malloc0(key_size + 1);
			(void) gpgme_data_seek(buffer, 0, SEEK_SET);
			(void) gpgme_data_read(buffer, result, key_size);
		}
		gpgme_data_release(buffer);
	}

	return result;
}


/* documentation: see header file */
GBytes *
libbalsa_gpgme_export_autocrypt_key(const gchar  *fingerprint,
									const gchar  *mailbox,
									GError      **error)
{
	gchar *export_args[10] = { "", "--export", "--export-options", "export-minimal,no-export-attributes",
		NULL, NULL, NULL, NULL, NULL, NULL };
	gpgme_ctx_t ctx;
	GBytes *result = NULL;

	g_return_val_if_fail((fingerprint != NULL) && (mailbox != NULL), NULL);

	ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_SPAWN, error);
	if (ctx != NULL) {
		gpgme_data_t keybuf;
		gpgme_error_t gpgme_err;

		gpgme_err = gpgme_data_new(&keybuf);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("cannot create data buffer"));
		} else {
			const gpg_capabilities *gpg_capas;
			guint param_idx;

			gpg_capas = libbalsa_gpgme_gpg_capabilities();
			g_assert(gpg_capas != NULL);		/* paranoid - we're called for OpenPGP, so the info /should/ be there... */
			param_idx = 4U;
			if (gpg_capas->export_filter_subkey) {
				export_args[param_idx++] = g_strdup("--export-filter");
				export_args[param_idx++] = g_strdup("drop-subkey=usage!~e && usage!~s");

			}
			if (gpg_capas->export_filter_uid) {
				export_args[param_idx++] = g_strdup("--export-filter");
				export_args[param_idx++] = g_strdup_printf("keep-uid=mbox=%s", mailbox);
			}
			export_args[param_idx] = g_strdup(fingerprint);

			/* run... */
			gpgme_err = gpgme_op_spawn(ctx, gpg_capas->gpg_path, (const gchar **) export_args, NULL, keybuf, NULL, 0);
			for (param_idx = 4U; export_args[param_idx] != NULL; param_idx++) {
				g_free(export_args[param_idx]);
			}
			if (gpgme_err != GPG_ERR_NO_ERROR) {
				libbalsa_gpgme_set_error(error, gpgme_err, _("cannot export minimal key for “%s”"), mailbox);
				gpgme_data_release(keybuf);
			} else {
				size_t keysize;
				void *keydata;

				keydata = gpgme_data_release_and_get_mem(keybuf, &keysize);
				if ((keydata == NULL) || (keysize == 0U)) {
					g_set_error(error, GPGME_ERROR_QUARK, -1, _("cannot export minimal key for “%s”"), mailbox);
				} else {
					result = g_bytes_new(keydata, keysize);
				}
				gpgme_free(keydata);
			}
		}

		gpgme_release(ctx);
	}

	return result;
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_import_ascii_key(gpgme_ctx_t   ctx,
								const gchar  *key_buf,
								gchar       **import_info,
								GError      **error)
{
	g_return_val_if_fail((ctx != NULL) && (key_buf != NULL), FALSE);

	return import_key_real(ctx, key_buf, strlen(key_buf), import_info, error);
}


/* documentation: see header file */
gboolean
libbalsa_gpgme_import_bin_key(gpgme_ctx_t   ctx,
							  GBytes   	   *key_buf,
							  gchar       **import_info,
							  GError      **error)
{
	gconstpointer key_data;
	gsize key_len;

	g_return_val_if_fail((ctx != NULL) && (key_buf != NULL), FALSE);
	key_data = g_bytes_get_data(key_buf, &key_len);
	return import_key_real(ctx, key_data, key_len, import_info, error);
}


/* ---- local functions ------------------------------------------------------ */

/* See the documentation of libbalsa_gpgme_list_keys() in the header file.
 * The additional parameter keylist_mode shall be one of the following values:
 * - GPGME_KEYLIST_MODE_LOCAL: search the local key ring (gpg --list-keys)
 * - GPGME_KEYLIST_MODE_EXTERN: search external source (gpg --search-keys or gpgsm --list-external-keys)
 * - GPGME_KEYLIST_MODE_LOCATE: search all sources, including WKD (if configured in the config file; gpg --locate-keys).  Note that
 *   in this mode all matching keys are imported into into the key ring related to the context
 */
static gboolean
list_keys_real(gpgme_ctx_t            ctx,
			   GList                **keys,
			   guint                 *bad_keys,
			   const gchar           *pattern,
			   gboolean               secret,
			   gpgme_keylist_mode_t   keylist_mode,
			   gboolean	              list_bad_keys,
			   GError               **error)
{
	gpgme_error_t gpgme_err;
	gpgme_keylist_mode_t kl_save;
	gpgme_keylist_mode_t kl_mode;

	kl_save = gpgme_get_keylist_mode(ctx);
	kl_mode = (kl_save & ~(GPGME_KEYLIST_MODE_LOCAL | GPGME_KEYLIST_MODE_EXTERN)) | keylist_mode;
	gpgme_err = gpgme_set_keylist_mode(ctx, kl_mode);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("error setting key list mode"));
	} else {
		/* list keys */
		gpgme_err = gpgme_op_keylist_start(ctx, pattern, (int) secret);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("could not list keys for “%s”"), pattern);
		} else {
			guint bad = 0U;

			/* loop over all keys */
			// FIXME - this may be /very/ slow, show a spinner?
			do {
				gpgme_key_t key;

				gpgme_err = gpgme_op_keylist_next(ctx, &key);
				if (gpgme_err == GPG_ERR_NO_ERROR) {
					if (list_bad_keys || check_key(key, secret, (keylist_mode & GPGME_KEYLIST_MODE_LOCAL) == 0)) {
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

			if (gpgme_err_code(gpgme_err) != GPG_ERR_EOF) {
				g_list_free_full(*keys, (GDestroyNotify) gpgme_key_unref);
				*keys = NULL;
			} else if (*keys != NULL) {
				*keys = g_list_reverse(*keys);
			}
			if (bad_keys != NULL) {
				*bad_keys = bad;
			}
		}
	}
	gpgme_set_keylist_mode(ctx, kl_save);

	return (gpgme_err_code(gpgme_err) == GPG_ERR_EOF);
}


/** \brief List local public keys for an internet address list
 *
 * \param ctx GpgME context
 * \param keys list of gpgme_key_t items matching the internet address mailboxes in the past list, filled with NULL on error
 * \param addresses list on internet addresses for which all keys shall be returned
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE if any error occurred
 * \note The function is called recursively if an item in the passed internet address list is a group address.
 */
static gboolean
list_local_pubkeys_real(gpgme_ctx_t           ctx,
						GList               **keys,
						InternetAddressList  *addresses,
						GError              **error)
{
	int n;
	gboolean result = TRUE;

	for (n = 0; result && (n < internet_address_list_length(addresses)); n++) {
		InternetAddress *addr;

		addr = internet_address_list_get_address(addresses, n);
		if (INTERNET_ADDRESS_IS_MAILBOX(addr)) {
			gchar *mailbox;
			GList *this_keys = NULL;

			mailbox = g_strconcat("<", INTERNET_ADDRESS_MAILBOX(addr)->addr, ">", NULL);
			result = list_keys_real(ctx, &this_keys, NULL, mailbox, FALSE, GPGME_KEYLIST_MODE_LOCAL, FALSE, error);
			g_free(mailbox);
			*keys = g_list_concat(*keys, this_keys);
		} else if (INTERNET_ADDRESS_IS_GROUP(addr)) {
			result = list_local_pubkeys_real(ctx, keys, INTERNET_ADDRESS_GROUP(addr)->members, error);
		}
	}
	return result;
}


/** \brief Import a binary or ASCII-armoured key
 *
 * \param ctx GpgME context
 * \param key_buf ASCII or binary GnuPG key buffer
 * \param buf_len number of bytes in the GnuPG key buffer
 * \param import_info filled with human-readable information about the import, may be NULL
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE on error
 *
 * Import an ASCII-armoured or binary GnuPG key into the key ring.
 */
static gboolean
import_key_real(gpgme_ctx_t     ctx,
				gconstpointer   key_buf,
				gsize           buf_len,
				gchar         **import_info,
				GError        **error)
{
	gpgme_data_t buffer;
	gpgme_error_t gpgme_err;
	gboolean result = FALSE;

	g_return_val_if_fail(buf_len > 0, FALSE);

	gpgme_err = gpgme_data_new_from_mem(&buffer, key_buf, buf_len, 0);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("cannot create data buffer"));
	} else {
		gpgme_err = gpgme_op_import(ctx, buffer);
		if (gpgme_err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, gpgme_err, _("importing key data failed"));
		} else {
			result = TRUE;
			if (import_info != NULL) {
				*import_info = gpgme_import_res_to_gchar(gpgme_op_import_result(ctx));
			}
		}
		gpgme_data_release(buffer);
	}

	return result;
}


/** \brief Check if a key is usable
 *
 * \param key GpgME key
 * \param secret TRUE for a private key, FALSE for a public key
 * \param on_keyserver TRUE for a key on a key server, FALSE for a key in the local key ring
 * \return TRUE if the key is usable, FALSE if it is expired, disabled, revoked, invalid or does not have a fingerprint
 *
 * Note that GpgME provides less information for keys on a key server, in particular regarding the sub-keys, so the check has to be
 * relaxed for this case.
 */
static inline gboolean
check_key(const gpgme_key_t key,
		  gboolean          secret,
		  gboolean          on_keyserver)
{
	gboolean result = FALSE;

	if ((key->fpr != NULL) && (key->expired == 0U) && (key->revoked == 0U) && (key->disabled == 0U) && (key->invalid == 0U)) {
		gpgme_subkey_t subkey = key->subkeys;

		while (!result && (subkey != NULL)) {
			if ((on_keyserver || (secret && (subkey->can_sign != 0U)) || (!secret && (subkey->can_encrypt != 0U))) &&
				(subkey->expired == 0U) && (subkey->revoked == 0U) && (subkey->disabled == 0U) && (subkey->invalid == 0U)) {
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
 * returned and call gpgme_import_key() as to import or update it in this case.  Call show_keyserver_dialog() as idle callback to
 * present the user the results.
 */
static gpointer
gpgme_keyserver_run(gpointer user_data)
{
	keyserver_op_t *keyserver_op = (keyserver_op_t *) user_data;
	gboolean result;
	GError *error = NULL;

	/* try WKD if possible */
	if ((keyserver_op->email_address != NULL) && (gpgme_get_protocol(keyserver_op->gpgme_ctx) == GPGME_PROTOCOL_OpenPGP)) {
		result = gpgme_locate_wkd_key(keyserver_op, &error);
	} else {
		result = TRUE;
	}

	/* try keyservers unless we have an error of already found the WKD key */
	if (result && (keyserver_op->imported_key == NULL)) {
		GList *keys = NULL;

		result = list_keys_real(keyserver_op->gpgme_ctx, &keys, NULL, keyserver_op->fingerprint, FALSE, GPGME_KEYLIST_MODE_EXTERN,
			FALSE, &error);

		if (result) {
			if (keys == NULL) {
				keyserver_op->msg_type = GTK_MESSAGE_INFO;
				keyserver_op->message =
					g_strdup_printf(_("Cannot find a key with fingerprint %s on the key server."), keyserver_op->fingerprint);
			} else if (keys->next != NULL) {
				guint key_cnt;

				/* more than one key found for the fingerprint - should never happen */
				key_cnt = g_list_length(keys);
				keyserver_op->msg_type = GTK_MESSAGE_WARNING;
				keyserver_op->message = g_strdup_printf(
					ngettext("Found %u key with fingerprint %s on the key server. "
							 "Please check and import the proper key manually.",
							 "Found %u keys with fingerprint %s on the key server. "
							 "Please check and import the proper key manually.",
							 key_cnt), key_cnt, keyserver_op->fingerprint);
			} else {
				result = gpgme_import_key(keyserver_op->gpgme_ctx, (gpgme_key_t) keys->data, &keyserver_op->message,
					&keyserver_op->imported_key, &error);
				if (result) {
					keyserver_op->msg_type = GTK_MESSAGE_INFO;
				}
			}

			g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
		}
	}

	if (!result) {
		keyserver_op->msg_type = GTK_MESSAGE_ERROR;
		keyserver_op->message = g_strdup_printf(_("Searching the key server failed: %s"), error->message);
		g_error_free(error);
	}
	g_idle_add(show_keyserver_dialog, keyserver_op);	/* idle callback will free keyserver_op */

	return NULL;
}


/** \brief Locate a key on a Web Key Directory server
 *
 * \param user_data keyserver thread data
 * \param error filled with error information on error, may be NULL
 * \return FALSE if an error occurred, TRUE otherwise, including the case that no suitable key has been found
 *
 * Run the "gpg --locate-keys" command for the email address keyserver_op_t::email_address which basically (if configured) includes
 * a query of Web Key Directory (WKD) servers.  As this operation immediately imports all matching keys, it is performed on an
 * ephemeral context.  If the operation loaded a key with the proper fingerprint keyserver_op_t::fingerprint, it is copied into the
 * main context keyserver_op_t::gpgme_ctx.  The function sets keyserver_op_t::message and keyserver_op_t::message and iff a key has
 * been copied keyserver_op_t::imported_key.
 *
 * \note The ephemeral context uses the default settings which include WKD support being enabled.
 */
static gboolean
gpgme_locate_wkd_key(keyserver_op_t  *keyserver_op,
					 GError         **error)
{
	gpgme_ctx_t ctx;
	gchar *temp_dir;
	gboolean result = FALSE;

	ctx = libbalsa_gpgme_temp_with_proto(gpgme_get_protocol(keyserver_op->gpgme_ctx), &temp_dir, error);
	if (ctx != NULL) {
		gchar *address;
		GList *wkd_keys = NULL;

		/* includes checking for Web Key Directory keys, but imports all matching ones into the context */
		address = g_strconcat("<", keyserver_op->email_address, ">", NULL);
		result = list_keys_real(ctx, &wkd_keys, NULL, address, FALSE, GPGME_KEYLIST_MODE_LOCATE, FALSE, error);
		g_free(address);

		/* check if wkd returned a key with the requested fingerprint */
		if (result && (wkd_keys != NULL)) {
			g_list_free_full(wkd_keys, (GDestroyNotify) gpgme_key_unref);
			wkd_keys = NULL;
			result =
				list_keys_real(ctx, &wkd_keys, NULL, keyserver_op->fingerprint, FALSE, GPGME_KEYLIST_MODE_LOCAL, FALSE, error);
		}

		/* copy the key for the requested fingerprint into the main context */
		if (result) {
			if (wkd_keys != NULL) {
				if (gpgme_copy_key(keyserver_op->gpgme_ctx, ctx, keyserver_op->fingerprint, error)) {
					keyserver_op->imported_key = (gpgme_key_t) wkd_keys->data;
					gpgme_key_ref(keyserver_op->imported_key);
					keyserver_op->message = gpgme_import_res_to_gchar(gpgme_op_import_result(keyserver_op->gpgme_ctx));
					keyserver_op->msg_type = GTK_MESSAGE_INFO;
				} else {
					result = FALSE;
				}
			}
		}

		/* clean up keys and temporary context */
		g_list_free_full(wkd_keys, (GDestroyNotify) gpgme_key_unref);
		gpgme_release(ctx);
		libbalsa_delete_directory(temp_dir, NULL);
		g_free(temp_dir);
	}

	return result;
}


/** \brief Copy a key into a different GpgME context
 *
 * \param dst_ctx destination GpgME context
 * \param dst_ctx source GpgME context
 * \param fingerprint key fingerprint
 * \param error filled with error information on error, may be NULL
 * \return TRUE if the operation was successful
 */
static gboolean
gpgme_copy_key(gpgme_ctx_t   dst_ctx,
			   gpgme_ctx_t   src_ctx,
			   const gchar  *fingerprint,
			   GError      **error)
{
	gpgme_error_t gpgme_err;
	gpgme_data_t buffer;
	gboolean result = FALSE;

	gpgme_err = gpgme_data_new(&buffer);
	if (gpgme_err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, gpgme_err, _("cannot create data buffer"));
	} else {
		gpgme_err = gpgme_op_export(src_ctx, fingerprint, 0, buffer);
		if (gpgme_err == GPG_ERR_NO_ERROR) {
			if (gpgme_data_seek(buffer, 0, SEEK_END) <= 0) {
				libbalsa_gpgme_set_error(error, gpgme_err, _("no data for key with fingerprint %s"), fingerprint);
			} else {
				gpgme_data_seek(buffer, 0, SEEK_SET);
				gpgme_err = gpgme_op_import(dst_ctx, buffer);
				if (gpgme_err != GPG_ERR_NO_ERROR) {
					libbalsa_gpgme_set_error(error, gpgme_err, _("importing key data failed"));
				} else {
					result = TRUE;
				}
			}
		} else {
			libbalsa_gpgme_set_error(error, gpgme_err, _("reading key data failed"));
		}
		gpgme_data_release(buffer);
	}

	return result;
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
		*import_info = gpgme_import_res_to_gchar(import_result);

		/* the key has been considered: load the possibly changed key from the local ring, ignoring any errors */
		if ((import_result->considered != 0) && (key->subkeys != NULL)) {
			*imported_key = libbalsa_gpgme_load_key(ctx, key->subkeys->fpr, error);
		}

		result = TRUE;
	}

	return result;
}


/** \brief Create a human-readable import result message
 *
 * \param import_result GpgME import result data
 * \return a newly allocated human-readable string containing the key import results
 *
 * This helper function collects the information about the last import operation using the passed context into a human-readable
 * string.
 */
static gchar *
gpgme_import_res_to_gchar(gpgme_import_result_t import_result)
{
	gchar *import_info;

	if (import_result->considered == 0) {
		import_info = g_strdup(_("No key was imported or updated."));
	} else if (import_result->considered == import_result->no_user_id) {
		import_info = g_strdup(_("The key was ignored because it does not have a user ID."));
	} else {
		if (import_result->imported != 0) {
			import_info = g_strdup(_("The key was imported into the local key ring."));
		} else if (import_result->unchanged == 0) {
			GString *info;

			info = g_string_new(_("The key was updated in the local key ring:"));
			if (import_result->new_user_ids > 0) {
				g_string_append_printf(info,
					ngettext("\n\342\200\242 %d new user ID", "\n\342\200\242 %d new user IDs", import_result->new_user_ids),
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
			import_info = g_string_free(info, FALSE);
		} else {
			import_info = g_strdup(_("The existing key in the key ring was not changed."));
		}
	}

	return import_info;
}


/** \brief Display a dialogue with the result of the key server operation
 *
 * \param user_data key server thread data, cast'ed to \ref keyserver_op_t *
 * \return always FALSE
 *
 * This helper function, called as idle callback, creates either a key dialogue created by calling libbalsa_key_dialog(), or just
 * a message dialogue of type \ref keyserver_op_t::msg_type if \ref keyserver_op_t::imported_key is NULL.  After running the
 * dialogue, the passed key server thread data is freed.
 */
static void
show_keyserver_dialog_response(GtkDialog *self,
                               gint       response_id,
                               gpointer   user_data)
{
    keyserver_op_t *keyserver_op = (keyserver_op_t *) user_data;

    gtk_widget_destroy(GTK_WIDGET(self));

    /* free the remaining keyserver thread data */
    if (keyserver_op->gpgme_ctx != NULL) {
        gpgme_release(keyserver_op->gpgme_ctx);
    }
    g_free(keyserver_op->fingerprint);
    g_free(keyserver_op->email_address);
    g_free(keyserver_op->message);
    g_free(keyserver_op);
}

static gboolean
show_keyserver_dialog(gpointer user_data)
{
	GtkWidget *dialog;
	keyserver_op_t *keyserver_op = (keyserver_op_t *) user_data;

	if (keyserver_op->imported_key != NULL) {
		dialog = libbalsa_key_dialog(keyserver_op->parent, GTK_BUTTONS_CLOSE, keyserver_op->imported_key, GPG_SUBKEY_CAP_ALL, NULL,
			keyserver_op->message);
			gpgme_key_unref(keyserver_op->imported_key);
	} else {
		dialog = gtk_message_dialog_new(keyserver_op->parent, GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
			keyserver_op->msg_type, GTK_BUTTONS_CLOSE, "%s", keyserver_op->message);
	}

        g_signal_connect(dialog, "response",
                         G_CALLBACK(show_keyserver_dialog_response), keyserver_op);
        gtk_widget_show_all(dialog);

	return FALSE;
}


/** \brief Search and import a key for an internet mailbox address
 *
 * \param ctx GpgME context
 * \param addresses internet mailbox address
 * \param error filled with error information on error, may be NULL
 * \return the count (> 0) of imported keys, -1 on error, 0 if no key has been imported
 *
 * Check if a valid public key exists in the local key ring.  If not, search WKD iff protocol is OpenPGP, and the key servers if
 * necessary.
 *
 * \note For WKD the respective option must be enabled in the user's gpg config file (see 'man gpg').
 */
static gint
keyserver_import_mailbox(gpgme_ctx_t              ctx,
						 InternetAddressMailbox  *address,
						 GError                 **error)
{
	gint result = 0;
	gchar *mailbox;
	GList *keys = NULL;

	/* ensure exact match */
	mailbox = g_strconcat("<", address->addr, ">", NULL);

	/* first check if we already have a proper key */
	if (!list_keys_real(ctx, &keys, NULL, mailbox, FALSE, GPGME_KEYLIST_MODE_LOCAL, FALSE, error)) {
		result = -1;
	} else if (keys != NULL) {
		g_debug("%s: local key found for %s", __func__, mailbox);
		g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
	} else {
		/* WKD is supported for OpenPGP only */
		if (gpgme_get_protocol(ctx) == GPGME_PROTOCOL_OpenPGP) {
			g_debug("%s: no local key found for %s, trying WKD...", __func__, mailbox);
			if (!list_keys_real(ctx, &keys, NULL, mailbox, FALSE, GPGME_KEYLIST_MODE_LOCATE, FALSE, error)) {
				result = -1;
			} else if (keys != NULL) {
				/* apparently the WKD search returns only one element, even if more keys have been imported, so we re-check the
				 * local key ring */
				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
				keys = NULL;
				if (!list_keys_real(ctx, &keys, NULL, mailbox, FALSE, GPGME_KEYLIST_MODE_LOCAL, FALSE, error)) {
					result = -1;
				} else {
					g_debug("%s: %u WKD key(s) found for %s", __func__, g_list_length(keys), mailbox);
					result += g_list_length(keys);
					g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
				}
			} else {
				g_debug("%s: no WKD keys found for %s", __func__, mailbox);
			}
		}

		/* try keyservers if we didn't find any key yet */
		if (result == 0) {
			g_debug("%s: searching keyservers for %s", __func__, mailbox);
			if (!list_keys_real(ctx, &keys, NULL, mailbox, FALSE, GPGME_KEYLIST_MODE_EXTERN, FALSE, error)) {
				result = -1;
			} else if (keys == NULL) {
				g_debug("%s: no key found for %s on keyserver", __func__, mailbox);
			} else {
				GList *p;
				gpgme_key_t import_keys[2] = { NULL, NULL };

				g_debug("%s: key(s) found for %s on keyserver", __func__, mailbox);
				for (p = keys; (result >= 0) && (p != NULL); p = p->next) {
					gpgme_error_t gpgme_err;

					import_keys[0] = p->data;
					gpgme_err = gpgme_op_import_keys(ctx, import_keys);
					if (gpgme_err == GPG_ERR_NO_ERROR) {
						gpgme_import_result_t import_res = gpgme_op_import_result(ctx);

						g_debug("considered: %d", import_res->considered);
						result += import_res->considered;
					} else {
						libbalsa_gpgme_set_error(error, gpgme_err, _("error importing key"));
						result = -1;
					}

				}
				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
			}
		}
	}

	g_free(mailbox);
	return result;
}
