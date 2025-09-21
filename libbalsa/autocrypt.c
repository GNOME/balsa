/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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
#	include "config.h"
#endif                          /* HAVE_CONFIG_H */

#if defined ENABLE_AUTOCRYPT

#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <sqlite3.h>
#include "libbalsa-gpgme.h"
#include "libbalsa-gpgme-keys.h"
#include "libbalsa-gpgme-widgets.h"
#include "identity.h"
#include "geometry-manager.h"
#include "autocrypt.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "autocrypt"


/* the autocrypt SQL table contains the following data:
 * addr: email address
 * last_seen: time_t value when the last message from addr has been seen
 * ac_timestamp: time_t value when the last message with a valid Autocrypt header from addr has been seen
 * pubkey: raw (binary) public key data
 * fingerprint: the fingerprint of pubkey, stored to avoid frequently importing pubkey into a temporary context
 * expires: the expiry time of pubkey (0 for never), stored to avoid frequently importing pubkey into a temporary context
 * prefer_encrypt: TRUE (1) if the prefer-encrypt=mutual attribute was given in the latest Autocrypt header
 *
 * notes: SQLite stores BOOLEAN as INTEGER
 *        We do not support key gossip, so storing everything in a flat table is sufficient */
#define DB_SCHEMA								\
	"PRAGMA auto_vacuum = 1;"					\
	"CREATE TABLE autocrypt("					\
		"addr TEXT PRIMARY KEY NOT NULL, "		\
		"last_seen BIGINT, "					\
		"ac_timestamp BIGINT, "					\
		"pubkey BLOB NOT NULL, "				\
		"fingerprint TEXT NOT NULL, "			\
		"expires BIGINT NOT NULL, "				\
		"prefer_encrypt BOOLEAN DEFAULT 0);"


#define NUM_QUERIES								8U


struct _AutocryptData {
	gchar *addr;
	time_t last_seen;
	time_t ac_timestamp;
	GBytes *keydata;
	gchar *fingerprint;
	time_t expires;
	gboolean prefer_encrypt;
};

typedef struct _AutocryptData AutocryptData;


enum {
	AC_ADDRESS_COLUMN = 0,
	AC_LAST_SEEN_INT_COLUMN,
	AC_LAST_SEEN_COLUMN,
	AC_TIMESTAMP_INT_COLUMN,
	AC_TIMESTAMP_COLUMN,
	AC_PREFER_ENCRYPT_COLUMN,
	AC_KEY_PTR_COLUMN,
	AC_DB_VIEW_COLUMNS
};


typedef struct {
	gconstpointer keydata;
	gsize keysize;
	gchar *fingerprint;
	gint64 expires;
} ac_key_data_t;


static void autocrypt_close(void);
static gboolean extract_ac_keydata(GMimeAutocryptHeader  *autocrypt_header,
								   ac_key_data_t         *dest);
static void add_or_update_user_info(GMimeAutocryptHeader    *autocrypt_header,
									const ac_key_data_t     *ac_key_data,
									gboolean                 update,
									GError                  **error);
static void update_last_seen(GMimeAutocryptHeader  *autocrypt_header,
							 GError      		  **error);
static AutocryptData *autocrypt_user_info(const gchar  *mailbox,
										  GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;
static void autocrypt_free(AutocryptData *data);
static AutocryptRecommend autocrypt_check_ia_list(gpgme_ctx_t           gpgme_ctx,
												  InternetAddressList  *recipients,
												  time_t                ref_time,
												  GList               **missing_keys,
												  GError              **error);

static gboolean popup_menu_cb(GtkWidget *widget,
							  gpointer   user_data);
static void button_press_cb(GtkGestureMultiPress *multi_press_gesture,
							gint                  n_press,
							gdouble               x,
							gdouble               y,
							gpointer              user_data);
static void popup_menu_real(GtkWidget      *widget,
							const GdkEvent *event);
static void show_key_details_cb(GtkMenuItem *menuitem,
								gpointer     user_data);
static void remove_key_cb(GtkMenuItem *menuitem,
						  gpointer     user_data);
static gint get_keys_real(gpgme_ctx_t           gpgme_ctx,
						  InternetAddressList  *addresses,
						  time_t                now,
						  GError              **error);


static sqlite3 *autocrypt_db = NULL;
static sqlite3_stmt *query[NUM_QUERIES] = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };
G_LOCK_DEFINE_STATIC(db_mutex);


/* documentation: see header file */
gboolean
autocrypt_init(GError **error)
{
	static const gchar * const prepare_statements[NUM_QUERIES] = {
		"SELECT * FROM autocrypt WHERE addr = LOWER(?)",
		"INSERT INTO autocrypt VALUES (LOWER(?1), ?2, ?2, ?3, ?4, ?5, ?6)",
		"UPDATE autocrypt SET last_seen = MAX(?2, last_seen), ac_timestamp = ?2, pubkey = ?3, fingerprint = ?4,"
		" expires = ?5, prefer_encrypt = ?6 WHERE addr = LOWER(?1)",
		"UPDATE autocrypt SET last_seen = ?2 WHERE addr = LOWER(?1) AND last_seen < ?2 AND ac_timestamp < ?2",
		"SELECT pubkey FROM autocrypt WHERE fingerprint LIKE ?",
		"SELECT addr, last_seen, ac_timestamp, prefer_encrypt, pubkey FROM autocrypt ORDER BY addr ASC",
		"DELETE FROM autocrypt WHERE addr = LOWER(?1)",
		"SELECT pubkey FROM autocrypt WHERE addr = LOWER(?1) AND (expires = 0 OR expires > ?2)"
	};
	gboolean result;

	g_debug("open Autocrypt database");
	G_LOCK(db_mutex);
	if (autocrypt_db == NULL) {
		gchar *db_path;
		gboolean require_init;
		int sqlite_res;

		db_path = g_build_filename(g_get_user_config_dir(), "balsa", "autocrypt.db", NULL);
		require_init = (g_access(db_path, R_OK + W_OK) != 0);
		sqlite_res = sqlite3_open(db_path, &autocrypt_db);
		if (sqlite_res == SQLITE_OK) {
			guint n;

			/* write the schema if the database is new */
			if (require_init) {
				sqlite_res = sqlite3_exec(autocrypt_db, DB_SCHEMA, NULL, NULL, NULL);
			}

			/* always vacuum the database */
			if (sqlite_res == SQLITE_OK) {
				sqlite_res = sqlite3_exec(autocrypt_db, "VACUUM", NULL, NULL, NULL);
			}

			/* prepare statements */
			for (n = 0U; (sqlite_res == SQLITE_OK) && (n < NUM_QUERIES); n++) {
				sqlite_res = sqlite3_prepare_v2(autocrypt_db, prepare_statements[n], -1, &query[n], NULL);
			}
		}
		G_UNLOCK(db_mutex);

		/* error checks... */
		if (sqlite_res != SQLITE_OK) {
			/* Translators: #1 database file; #2 error message */
			g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("cannot initialise Autocrypt database “%s”: %s"), db_path,
				sqlite3_errmsg(autocrypt_db));
			autocrypt_close();
			result = FALSE;
		} else {
			atexit(autocrypt_close);
			result = TRUE;
		}
		g_free(db_path);
	} else {
		G_UNLOCK(db_mutex);
		result = TRUE;
	}

	return result;
}


/* documentation: see header file */
void
autocrypt_from_message(LibBalsaMessage  *message,
					   GError          **error)
{
	LibBalsaMessageHeaders *headers;
	ac_key_data_t ac_key_data;
	time_t ac_header_time;

	g_return_if_fail(LIBBALSA_IS_MESSAGE(message));
	headers = libbalsa_message_get_headers(message);
	g_return_if_fail(headers != NULL);
	g_return_if_fail(autocrypt_db != NULL);

	/* return silently if there is no gmime autocrypt header
	 * note that it will *always* contain a valid sender address and effective date if it exists, so there is no need to validate
	 * the data returned from the g_mime_autocrypt_header_get_* accessor functions, except for the key data */
	if (headers->autocrypt_hdr == NULL) {
		return;
	}

	// FIXME - we should ignore spam - how can we detect it?

	/* check for content types which shall be ignored
	 * Note: see Autocrypt Level 1 standard, section 2.3 (https://autocrypt.org/level1.html#updating-autocrypt-peer-state) for
	 *       details about this and the following checks which may result in completely ignoring the message. */
	if (autocrypt_ignore(headers->content_type)) {
		g_debug("ignore %s/%s", g_mime_content_type_get_media_type(headers->content_type),
			g_mime_content_type_get_media_subtype(headers->content_type));
		return;
	}

    /* ignore messages without a Date: header or with a date in the future */
	ac_header_time = g_date_time_to_unix(g_mime_autocrypt_header_get_effective_date(headers->autocrypt_hdr));
    if (ac_header_time > time(NULL)) {
    	g_debug("no Date: header or value in the future, ignored");
    	return;
    }

    /* update the database */
    G_LOCK(db_mutex);
    if (extract_ac_keydata(headers->autocrypt_hdr, &ac_key_data)) {
    	AutocryptData *db_info;

    	db_info = autocrypt_user_info(g_mime_autocrypt_header_get_address_as_string(headers->autocrypt_hdr), error);
    	if (db_info != NULL) {
    		if (ac_header_time > db_info->ac_timestamp) {
    			add_or_update_user_info(headers->autocrypt_hdr, &ac_key_data, TRUE, error);
    		} else {
    			g_info("message timestamp %ld not newer than autocrypt db timestamp %ld, ignore message",
    				(long) headers->date, (long) db_info->ac_timestamp);
    		}
    		autocrypt_free(db_info);
    	} else {
    		add_or_update_user_info(headers->autocrypt_hdr, &ac_key_data, FALSE, error);
    	}
    	g_free(ac_key_data.fingerprint);
    } else {
    	/* note: we update the last seen db field if there is no key (i.e. the message did not contain an Autocrypt: header) *and*
    	 * if the key data is broken, or gpgme failed to handle it for some other reason.  We /might/ want to distinguish between
    	 * these two cases. */
        update_last_seen(headers->autocrypt_hdr, error);
    }
    G_UNLOCK(db_mutex);
}


/* documentation: see header file */
gchar *
autocrypt_header(LibBalsaIdentity *identity, GError **error)
{
	const gchar *mailbox;
	gchar *use_fpr = NULL;
	gchar *result = NULL;
	InternetAddress *ia;
	const gchar *force_gpg_key_id;
	AutocryptMode autocrypt_mode;

	g_return_val_if_fail(identity != NULL, NULL);
	autocrypt_mode = libbalsa_identity_get_autocrypt_mode(identity);
	g_return_val_if_fail(autocrypt_mode != AUTOCRYPT_DISABLE, NULL);

	ia = libbalsa_identity_get_address(identity);
	mailbox = internet_address_mailbox_get_addr(INTERNET_ADDRESS_MAILBOX(ia));

	/* no key fingerprint has been passed - try to find the fingerprint of a secret key matching the passed mailbox */
	force_gpg_key_id = libbalsa_identity_get_force_gpg_key_id(identity);
	if ((force_gpg_key_id == NULL) || (force_gpg_key_id[0] == '\0')) {
		gpgme_ctx_t ctx;

		ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, error);
		if (ctx != NULL) {
			GList *keys = NULL;

			libbalsa_gpgme_list_keys(ctx, &keys, NULL, mailbox, TRUE, FALSE, error);
			if (keys != NULL) {
				gpgme_key_t key = (gpgme_key_t) keys->data;

				if ((key != NULL) && (key->subkeys != NULL)) {
					use_fpr = g_strdup(key->subkeys->fpr);
				}
				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
			}
			gpgme_release(ctx);
		}

		if (use_fpr == NULL) {
			g_set_error(error, AUTOCRYPT_ERROR_QUARK, -1,
				/* Translators: #1 sender's email address */
				_("No usable private key for “%s” found! Please create a key or disable Autocrypt."), mailbox);
		} else {
			g_debug("found fingerprint %s for '%s'", use_fpr, mailbox);
		}
	} else {
		use_fpr = g_strdup(force_gpg_key_id);
	}

	if (use_fpr != NULL) {
		GBytes *keydata;

		keydata = libbalsa_gpgme_export_autocrypt_key(use_fpr, mailbox, error);
		g_free(use_fpr);
		if (keydata != NULL) {
			GMimeAutocryptHeader *header;

			header = g_mime_autocrypt_header_new();
			g_mime_autocrypt_header_set_address_from_string(header, mailbox);
			if (autocrypt_mode == AUTOCRYPT_PREFER_ENCRYPT) {
				g_mime_autocrypt_header_set_prefer_encrypt(header, GMIME_AUTOCRYPT_PREFER_ENCRYPT_MUTUAL);
			} else {
				g_mime_autocrypt_header_set_prefer_encrypt(header, GMIME_AUTOCRYPT_PREFER_ENCRYPT_NONE);
			}
			g_mime_autocrypt_header_set_keydata(header, keydata);
			g_bytes_unref(keydata);
			result = g_mime_autocrypt_header_to_string(header, FALSE);
			g_object_unref(header);
		}
	}

	return result;
}


/* documentation: see header file */
gboolean
autocrypt_ignore(GMimeContentType *content_type)
{
	g_return_val_if_fail(GMIME_IS_CONTENT_TYPE(content_type), TRUE);

	return g_mime_content_type_is_type(content_type, "multipart", "report") ||
		g_mime_content_type_is_type(content_type, "text", "calendar");
}


/* documentation: see header file */
GBytes *
autocrypt_get_key(const gchar *fingerprint, GError **error)
{
	gchar *param;
	int sqlite_res;
	GBytes *result = NULL;

	g_return_val_if_fail(fingerprint != NULL, NULL);

	/* prepend SQL "LIKE" wildcard */
	param = g_strconcat("%", fingerprint, NULL);

	G_LOCK(db_mutex);
	sqlite_res = sqlite3_bind_text(query[4], 1, param, -1, SQLITE_STATIC);
	if (sqlite_res == SQLITE_OK) {
		sqlite_res = sqlite3_step(query[4]);
		if (sqlite_res == SQLITE_ROW) {
			result = g_bytes_new(sqlite3_column_blob(query[4], 0), sqlite3_column_bytes(query[4], 0));
			sqlite_res = sqlite3_step(query[4]);
		}

		if (sqlite_res != SQLITE_DONE) {
			/* Translators: #1 GPG key fingerprint; #2 error message */
			g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("error reading Autocrypt data for “%s”: %s"), fingerprint,
				sqlite3_errmsg(autocrypt_db));
			if (result != NULL) {
				g_bytes_unref(result);
				result = NULL;
			}
		}
	} else {
		/* Translators: #1 GPG key fingerprint; #2 error message */
		g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("error reading Autocrypt data for “%s”: %s"), fingerprint,
			sqlite3_errmsg(autocrypt_db));
	}
	sqlite3_reset(query[4]);
	G_UNLOCK(db_mutex);
	g_free(param);

	return result;
}


/* documentation: see header file */
gint autocrypt_import_keys(InternetAddressList *addresses, GError **error)
{
	gpgme_ctx_t gpgme_ctx;
	gint result;

	g_return_val_if_fail(IS_INTERNET_ADDRESS_LIST(addresses), -1);

	/* create the gpgme context and set the protocol */
	gpgme_ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, error);
	if (gpgme_ctx == NULL) {
		result = -1;
	} else {
		result = get_keys_real(gpgme_ctx, addresses, time(NULL), error);
		gpgme_release(gpgme_ctx);
	}
	return result;
}


/* documentation: see header file */
AutocryptRecommend
autocrypt_recommendation(InternetAddressList *recipients, GList **missing_keys, GError **error)
{
	AutocryptRecommend result;
	gpgme_ctx_t gpgme_ctx;

	g_return_val_if_fail(IS_INTERNET_ADDRESS_LIST(recipients), AUTOCRYPT_ENCR_DISABLE);

    /* create the gpgme context and set the protocol */
    gpgme_ctx = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_OpenPGP, error);
    if (gpgme_ctx == NULL) {
    	result = AUTOCRYPT_ENCR_ERROR;
    } else {
    	result = autocrypt_check_ia_list(gpgme_ctx, recipients, time(NULL), missing_keys, error);
    	gpgme_release(gpgme_ctx);

    	if ((result == AUTOCRYPT_ENCR_ERROR) && (missing_keys != NULL) && (*missing_keys != NULL)) {
    		g_list_free_full(*missing_keys, (GDestroyNotify) g_bytes_unref);
    		*missing_keys = NULL;
    	}
    }

	return result;
}


static void
autocrypt_db_dialog_run_response(GtkDialog *self,
                                 gint       response,
                                 gpointer   user_data)
{
    GList *keys = user_data;

    gtk_widget_destroy((GtkWidget *) self);
    g_list_free_full(keys, (GDestroyNotify) g_bytes_unref);
}

/* documentation: see header file */
void
autocrypt_db_dialog_run(const gchar *date_string, GtkWindow *parent)
{
	GtkWidget *dialog;
	GtkWidget *vbox;
    GtkWidget *scrolled_window;
    GtkWidget *tree_view;
    GtkListStore *model;
    GtkTreeSelection *selection;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkGesture *gesture;
    GList *keys = NULL;
	int sqlite_res;

	dialog = gtk_dialog_new_with_buttons(_("Autocrypt database"), parent,
		GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
                _("_Close"), GTK_RESPONSE_CLOSE, NULL);
	geometry_manager_attach(GTK_WINDOW(dialog), "AutocryptDB");

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), vbox);
    gtk_widget_set_vexpand(vbox, TRUE);

    scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width(GTK_CONTAINER(scrolled_window), 12U);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolled_window), GTK_SHADOW_ETCHED_IN);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled_window, TRUE);
    gtk_widget_set_valign(scrolled_window, GTK_ALIGN_FILL);
    gtk_container_add(GTK_CONTAINER(vbox), scrolled_window);

    model = gtk_list_store_new(AC_DB_VIEW_COLUMNS, G_TYPE_STRING,	/* address */
    	G_TYPE_INT64,												/* last seen timestamp value (for sorting) */
    	G_TYPE_STRING,												/* formatted last seen timestamp */
		G_TYPE_INT64,												/* last Autocrypt message timestamp value (for sorting) */
		G_TYPE_STRING,												/* formatted last Autocrypt message timestamp */
		G_TYPE_BOOLEAN,												/* user prefers encrypted messages */
		G_TYPE_POINTER);											/* key */

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));

    gesture = gtk_gesture_multi_press_new(tree_view);
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), 0);
    g_signal_connect(gesture, "pressed", G_CALLBACK(button_press_cb), NULL);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
    g_signal_connect(tree_view, "popup-menu", G_CALLBACK(popup_menu_cb), NULL);

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    /* add the keys */
    G_LOCK(db_mutex);
    sqlite_res = sqlite3_step(query[5]);
    while (sqlite_res == SQLITE_ROW) {
    	gint64 last_seen_val;
    	gchar *last_seen_buf;
    	gint64 last_ac_val;
    	gchar *last_ac_buf;
    	GBytes *key;
        GtkTreeIter iter;

        last_seen_val = sqlite3_column_int64(query[5], 1);
    	last_seen_buf = libbalsa_date_to_utf8(last_seen_val, date_string);
    	last_ac_val = sqlite3_column_int64(query[5], 2);
    	last_ac_buf = libbalsa_date_to_utf8(last_ac_val, date_string);
    	key = g_bytes_new(sqlite3_column_blob(query[5], 4), sqlite3_column_bytes(query[5], 4));
    	keys = g_list_prepend(keys, key);

		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter,
			AC_ADDRESS_COLUMN, sqlite3_column_text(query[5], 0),
			AC_LAST_SEEN_INT_COLUMN, last_seen_val,
			AC_LAST_SEEN_COLUMN, last_seen_buf,
			AC_TIMESTAMP_INT_COLUMN, last_ac_val,
			AC_TIMESTAMP_COLUMN, last_ac_buf,
			AC_PREFER_ENCRYPT_COLUMN, sqlite3_column_int(query[5], 3),
			AC_KEY_PTR_COLUMN, key,
			-1);
		g_free(last_seen_buf);
		g_free(last_ac_buf);

    	sqlite_res = sqlite3_step(query[5]);
    }
    sqlite3_reset(query[5]);
    G_UNLOCK(db_mutex);

    /* set up the tree view */
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Mailbox"), renderer, "text", AC_ADDRESS_COLUMN, NULL);
	gtk_tree_view_column_set_sort_column_id(column, AC_ADDRESS_COLUMN);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Last seen"), renderer, "text", AC_LAST_SEEN_COLUMN, NULL);
	gtk_tree_view_column_set_sort_column_id(column, AC_LAST_SEEN_INT_COLUMN);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Last Autocrypt message"), renderer, "text", AC_TIMESTAMP_COLUMN, NULL);
	gtk_tree_view_column_set_sort_column_id(column, AC_TIMESTAMP_INT_COLUMN);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);

	renderer = gtk_cell_renderer_toggle_new();
	column = gtk_tree_view_column_new_with_attributes(_("Prefer encryption"), renderer, "active", AC_PREFER_ENCRYPT_COLUMN, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_widget_show_all(vbox);

	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), AC_ADDRESS_COLUMN, GTK_SORT_ASCENDING);
    g_object_unref(model);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(autocrypt_db_dialog_run_response), keys);
    gtk_widget_show_all(dialog);
}


static AutocryptRecommend
autocrypt_check_ia_list(gpgme_ctx_t           gpgme_ctx,
						InternetAddressList  *recipients,
						time_t                ref_time,
						GList               **missing_keys,
						GError              **error)
{
	AutocryptRecommend result = AUTOCRYPT_ENCR_AVAIL_MUTUAL;
	gint i;

	for (i = 0; (result > AUTOCRYPT_ENCR_DISABLE) && (i < internet_address_list_length(recipients)); i++) {
    	InternetAddress *ia = internet_address_list_get_address(recipients, i);

    	/* check all entries in the list, handle groups recursively */
    	if (INTERNET_ADDRESS_IS_GROUP(ia)) {
    		result = autocrypt_check_ia_list(gpgme_ctx, INTERNET_ADDRESS_GROUP(ia)->members, ref_time, missing_keys, error);
    	} else {
    		AutocryptData *autocrypt_user;
    		const gchar *mailbox;

    		mailbox = INTERNET_ADDRESS_MAILBOX(ia)->addr;
    		G_LOCK(db_mutex);
    		autocrypt_user = autocrypt_user_info(mailbox, NULL);
    		G_UNLOCK(db_mutex);
    		if (autocrypt_user == NULL) {
        		GList *keys = NULL;

    			/* check if we have a public key, keep the state if we found one, disable if not */
        		if (libbalsa_gpgme_list_keys(gpgme_ctx, &keys, NULL, mailbox, FALSE, FALSE, error)) {
        			if (keys != NULL) {
        				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
        				g_debug("'%s': found in public key ring, overall status %d", mailbox, result);
        			} else {
        				result = AUTOCRYPT_ENCR_DISABLE;
        				g_debug("'%s': not in Autocrypt db or public key ring, overall status %d", mailbox, result);
        			}
        		} else {
        			result = AUTOCRYPT_ENCR_ERROR;
        		}
    		} else {
    			/* we found Autocrypt data for this user */
    			if ((autocrypt_user->expires > 0) && (autocrypt_user->expires <= ref_time)) {
    				result = AUTOCRYPT_ENCR_DISABLE;		/* key has expired */
    			} else if (autocrypt_user->ac_timestamp < (autocrypt_user->last_seen - (35 * 24 * 60 * 60))) {
    				result = MIN(result, AUTOCRYPT_ENCR_DISCOURAGE);	/* Autocrypt timestamp > 35 days older than last seen */
    			} else if (autocrypt_user->prefer_encrypt) {
    				result = MIN(result, AUTOCRYPT_ENCR_AVAIL_MUTUAL);	/* user requested "prefer-encrypt=mutual" */
    			} else {
    				result = MIN(result, AUTOCRYPT_ENCR_AVAIL);			/* user did not request "prefer-encrypt=mutual" */
    			}

    			/* check if the Autocrypt key is already in the key ring, add it to the list of missing ones otherwise */
    			if (missing_keys != NULL) {
            		GList *keys = NULL;

            		if (libbalsa_gpgme_list_keys(gpgme_ctx, &keys, NULL, autocrypt_user->fingerprint, FALSE, FALSE, error)) {
            			if (keys != NULL) {
            				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
            			} else {
            				*missing_keys = g_list_prepend(*missing_keys, g_bytes_ref(autocrypt_user->keydata));
            			}
            		} else {
            			result = AUTOCRYPT_ENCR_ERROR;
            		}
    			}
    			autocrypt_free(autocrypt_user);
    			g_debug("'%s': found in Autocrypt db, overall status %d", mailbox, result);
    		}
    	}
	}

	return result;
}


static void
autocrypt_free(AutocryptData *data)
{
	if (data != NULL) {
		g_free(data->addr);
		g_free(data->fingerprint);
		if (data->keydata) {
			g_bytes_unref(data->keydata);
		}
		g_free(data);
	}
}


static void
autocrypt_close(void)
{
	guint n;

	g_debug("closing Autocrypt database");
	G_LOCK(db_mutex);
	for (n = 0U; n < NUM_QUERIES; n++) {
		sqlite3_finalize(query[n]);
		query[n] = NULL;
	}
	sqlite3_close(autocrypt_db);
	autocrypt_db = NULL;
	G_UNLOCK(db_mutex);
}


/* note: this function is called when db_mutex is already locked, so DO NOT lock it again */
static AutocryptData *
autocrypt_user_info(const gchar *mailbox, GError **error)
{
	int sqlite_res;
	AutocryptData *user_info = NULL;

	g_return_val_if_fail((mailbox != NULL) && (autocrypt_db != NULL), NULL);

	sqlite_res = sqlite3_bind_text(query[0], 1, mailbox, -1, SQLITE_STATIC);
	if (sqlite_res == SQLITE_OK) {
		sqlite_res = sqlite3_step(query[0]);
		if (sqlite_res == SQLITE_ROW) {
			user_info = g_new0(AutocryptData, 1U);
			user_info->addr = g_strdup((const gchar *) sqlite3_column_text(query[0], 0));
			user_info->last_seen = sqlite3_column_int64(query[0], 1);
			user_info->ac_timestamp = sqlite3_column_int64(query[0], 2);
			user_info->keydata = g_bytes_new(sqlite3_column_blob(query[0], 3), sqlite3_column_bytes(query[0], 3));
			user_info->fingerprint = g_strdup((const gchar *) sqlite3_column_text(query[0], 4));
			user_info->expires = sqlite3_column_int64(query[0], 5);
			user_info->prefer_encrypt = (sqlite3_column_int(query[0], 6) != 0);
			sqlite_res = sqlite3_step(query[0]);
		}

		if (sqlite_res != SQLITE_DONE) {
			/* Translators: #1 mailbox; #2 error message */
			g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("error reading Autocrypt data for “%s”: %s"), mailbox,
				sqlite3_errmsg(autocrypt_db));
			autocrypt_free(user_info);
			user_info = NULL;
		}
	} else {
		/* Translators: #1 mailbox; #2 error message */
		g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("error reading Autocrypt data for “%s”: %s"), mailbox,
			sqlite3_errmsg(autocrypt_db));
	}
	sqlite3_reset(query[0]);

	return user_info;
}


static gboolean
extract_ac_keydata(GMimeAutocryptHeader *autocrypt_header, ac_key_data_t *dest)
{
	GBytes *keydata;
	gboolean success = FALSE;

	keydata = g_mime_autocrypt_header_get_keydata(autocrypt_header);
	if (keydata != NULL) {
		gpgme_ctx_t ctx;
		gchar *temp_dir = NULL;

		dest->keydata = g_bytes_get_data(keydata, &dest->keysize);

		/* try to import the key into a temporary context: validate, get fingerprint and expiry date */
		ctx = libbalsa_gpgme_temp_with_proto(GPGME_PROTOCOL_OpenPGP, &temp_dir, NULL);
		if (ctx != NULL) {
			GList *keys = NULL;
			GError *gpg_error = NULL;
			guint bad_keys = 0U;

			success = libbalsa_gpgme_import_bin_key(ctx, keydata, NULL, &gpg_error) &&
				libbalsa_gpgme_list_keys(ctx, &keys, &bad_keys, NULL, FALSE, FALSE, &gpg_error);
			if (success && (keys != NULL) && (keys->next == NULL)) {
				gpgme_key_t key = (gpgme_key_t) keys->data;

				if (key != NULL) {
					gpgme_subkey_t sign_subkey;

					for (sign_subkey = key->subkeys;
						(sign_subkey != NULL) && (sign_subkey->can_sign == 0);
						sign_subkey = sign_subkey->next);
					if (sign_subkey != NULL) {
						dest->fingerprint = g_strdup(sign_subkey->fpr);
						dest->expires = sign_subkey->expires;
					} else {
						g_warning("Autocrypt key for '%s' does not contain a signing-capable subkey",
							g_mime_autocrypt_header_get_address_as_string(autocrypt_header));
						success = FALSE;
					}
				} else {
					success = FALSE;
				}
			} else {
				g_warning("Failed to import or list key data for '%s': %s (%u keys, %u bad)",
					g_mime_autocrypt_header_get_address_as_string(autocrypt_header),
					(gpg_error != NULL) ? gpg_error->message : "unknown", (keys != NULL) ? g_list_length(keys) : 0U, bad_keys);
				success = FALSE;
			}
			g_clear_error(&gpg_error);

			g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
			gpgme_release(ctx);
			libbalsa_delete_directory(temp_dir, NULL);
			g_free(temp_dir);
		}
	}

	return success;
}


/* note: this function is called when db_mutex is already locked, so DO NOT lock it again */
static void
add_or_update_user_info(GMimeAutocryptHeader *autocrypt_header, const ac_key_data_t *ac_key_data, gboolean update, GError **error)
{
	guint query_idx;
	const gchar *addr;
	gint64 date_header;
	gint prefer_encrypt;

	query_idx = update ? 2 : 1;

	addr = g_mime_autocrypt_header_get_address_as_string(autocrypt_header);
	date_header = g_date_time_to_unix(g_mime_autocrypt_header_get_effective_date(autocrypt_header));
	if (g_mime_autocrypt_header_get_prefer_encrypt(autocrypt_header) == GMIME_AUTOCRYPT_PREFER_ENCRYPT_MUTUAL) {
		prefer_encrypt = (gint) AUTOCRYPT_PREFER_ENCRYPT;
	} else {
		prefer_encrypt = (gint) AUTOCRYPT_NOPREFERENCE;
	}

	if ((sqlite3_bind_text(query[query_idx], 1, addr, -1, SQLITE_STATIC) != SQLITE_OK) ||
		(sqlite3_bind_int64(query[query_idx], 2, date_header) != SQLITE_OK) ||
		(sqlite3_bind_blob(query[query_idx], 3, ac_key_data->keydata, ac_key_data->keysize, SQLITE_STATIC) != SQLITE_OK) ||
		(sqlite3_bind_text(query[query_idx], 4, ac_key_data->fingerprint, -1, SQLITE_STATIC) != SQLITE_OK) ||
		(sqlite3_bind_int64(query[query_idx], 5, ac_key_data->expires) != SQLITE_OK) ||
		(sqlite3_bind_int(query[query_idx], 6, prefer_encrypt) != SQLITE_OK) ||
		(sqlite3_step(query[query_idx]) != SQLITE_DONE)) {
		/* Translators: #1 email address; #2 error message */
		g_set_error(error, AUTOCRYPT_ERROR_QUARK, -1, update ? _("update user “%s” failed: %s") : _("insert user “%s” failed: %s"),
			addr, sqlite3_errmsg(autocrypt_db));
	} else {
		g_debug("%s user '%s': %d", update ? "updated" : "inserted", addr, sqlite3_changes(autocrypt_db));
	}
	sqlite3_reset(query[query_idx]);
}


/* note: this function is called when db_mutex is already locked, so DO NOT lock it again */
static void
update_last_seen(GMimeAutocryptHeader *autocrypt_header, GError **error)
{
	const gchar *addr;
	time_t date_header;

	addr = g_mime_autocrypt_header_get_address_as_string(autocrypt_header);
	date_header = g_date_time_to_unix(g_mime_autocrypt_header_get_effective_date(autocrypt_header));
	if ((sqlite3_bind_text(query[3], 1, addr, -1, SQLITE_STATIC) != SQLITE_OK) ||
		(sqlite3_bind_int64(query[3], 2, date_header) != SQLITE_OK) ||
		(sqlite3_step(query[3]) != SQLITE_DONE)) {
		/* Translators: #1 email address; #2 error message */
		g_set_error(error, AUTOCRYPT_ERROR_QUARK, -1, _("update user “%s” failed: %s"), addr, sqlite3_errmsg(autocrypt_db));
	} else {
		g_debug("updated last_seen for '%s': %d", addr, sqlite3_changes(autocrypt_db));
	}
	sqlite3_reset(query[3]);
}


/* callback: popup menu key in autocrypt database dialogue activated */
static gboolean
popup_menu_cb(GtkWidget *widget, gpointer G_GNUC_UNUSED user_data)
{
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		GtkTreePath *path;

		path = gtk_tree_model_get_path(model, &iter);
		gtk_tree_view_scroll_to_cell(tree_view, path, NULL, FALSE, 0.0, 0.0);
		gtk_tree_path_free(path);
		popup_menu_real(widget, NULL);
	}

	return TRUE;
}


/* callback: mouse click in autocrypt database dialogue activated */
static void
button_press_cb(GtkGestureMultiPress *multi_press_gesture, gint G_GNUC_UNUSED n_press, gdouble x, gdouble y,
	gpointer G_GNUC_UNUSED user_data)
{
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(multi_press_gesture));
    GtkTreeView *tree_view = GTK_TREE_VIEW(widget);
    GtkGesture *gesture;
    GdkEventSequence *sequence;
    const GdkEvent *event;

    gesture = GTK_GESTURE(multi_press_gesture);
    sequence = gtk_gesture_single_get_current_sequence(GTK_GESTURE_SINGLE(multi_press_gesture));
    event = gtk_gesture_get_last_event(gesture, sequence);
    if (gdk_event_triggers_context_menu(event) && (gdk_event_get_window(event) == gtk_tree_view_get_bin_window(tree_view))) {
        gint bx;
        gint by;
        GtkTreePath *path;

        gtk_tree_view_convert_widget_to_bin_window_coords(tree_view, (gint) x, (gint) y, &bx, &by);
        if (gtk_tree_view_get_path_at_pos(tree_view, bx, by, &path, NULL, NULL, NULL)) {
        	GtkTreeSelection *selection = gtk_tree_view_get_selection(tree_view);
            GtkTreeModel *model = gtk_tree_view_get_model(tree_view);
            GtkTreeIter iter;

            gtk_tree_selection_unselect_all(selection);
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree_view), path, NULL, FALSE);
            if (gtk_tree_model_get_iter(model, &iter, path)) {
            	popup_menu_real(GTK_WIDGET(tree_view), event);
            }
            gtk_tree_path_free(path);
        }
    }
}


/* autocrypt database dialogue context menu */
static void
popup_menu_real(GtkWidget *widget, const GdkEvent *event)
{
    GtkWidget *popup_menu;
    GtkWidget* menu_item;

	popup_menu = gtk_menu_new();
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Show details…"));
	g_signal_connect(menu_item, "activate", G_CALLBACK(show_key_details_cb), widget);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup_menu), menu_item);
    menu_item = gtk_menu_item_new_with_mnemonic(_("_Delete"));
    g_signal_connect(menu_item, "activate", G_CALLBACK(remove_key_cb), widget);
    gtk_menu_shell_append(GTK_MENU_SHELL(popup_menu), menu_item);
    gtk_widget_show_all(popup_menu);
    if (event != NULL) {
    	gtk_menu_popup_at_pointer(GTK_MENU(popup_menu), event);
    } else {
        gtk_menu_popup_at_widget(GTK_MENU(popup_menu), widget, GDK_GRAVITY_CENTER, GDK_GRAVITY_CENTER, NULL);
    }
}

typedef struct {
    gchar *temp_dir;
    gpgme_ctx_t ctx;
    GList *keys;
    gchar *mail_addr;
} show_key_details_data_t;

static void
show_key_details_response(GtkDialog *self,
                          gint       response,
                          gpointer   user_data)
{
    show_key_details_data_t *data = user_data;

    gtk_widget_destroy((GtkWidget *) self);

    g_list_free_full(data->keys, (GDestroyNotify) gpgme_key_unref);
    gpgme_release(data->ctx);
    libbalsa_delete_directory(data->temp_dir, NULL);
    g_free(data->mail_addr);
    g_free(data);
}

/* key context menu callback: show key details */
static void
show_key_details_cb(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
	GtkTreeModel *model;
	GtkTreeSelection *selection;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(user_data));
	if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
		gpgme_ctx_t ctx;
		gchar *temp_dir = NULL;
		GError *error = NULL;

		ctx = libbalsa_gpgme_temp_with_proto(GPGME_PROTOCOL_OpenPGP, &temp_dir, &error);
		if (ctx != NULL) {
			GBytes *key;
			gchar *mail_addr;
			GList *keys = NULL;
			gboolean success;

			gtk_tree_model_get(model, &iter, AC_KEY_PTR_COLUMN, &key, AC_ADDRESS_COLUMN, &mail_addr, -1);
			success = libbalsa_gpgme_import_bin_key(ctx, key, NULL, &error) &&
				libbalsa_gpgme_list_keys(ctx, &keys, NULL, NULL, FALSE, TRUE, &error);
			if (success) {
				GtkWidget *toplevel;
				GtkWindow *window;
				GtkWidget *dialog;
                                show_key_details_data_t *data;

				toplevel = gtk_widget_get_toplevel(GTK_WIDGET(user_data));
				window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
				if (keys != NULL) {
					dialog = libbalsa_key_dialog(window, GTK_BUTTONS_CLOSE, (gpgme_key_t) keys->data, GPG_SUBKEY_CAP_ALL,
						NULL, NULL);
                                        gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
				} else {
                                    dialog =
                                        gtk_message_dialog_new(window,
                                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
						GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
						/* Translators: #1 email address */
						_("The database entry for “%s” does not contain a key."),
						mail_addr);
				}

                                data = g_new(show_key_details_data_t, 1);
                                data->keys = keys;
                                data->ctx = ctx;
                                data->temp_dir = temp_dir;
                                data->mail_addr = mail_addr;

                                g_signal_connect(dialog, "response",
                                                 G_CALLBACK(show_key_details_response), data);
                                gtk_widget_show_all(dialog);
                                return;
			}
			gpgme_release(ctx);
			libbalsa_delete_directory(temp_dir, NULL);
			g_free(temp_dir);
			g_free(mail_addr);
		}

		if (error != NULL) {
			libbalsa_information(LIBBALSA_INFORMATION_ERROR, _("cannot show key details: %s"), error->message);
			g_error_free(error);
		}
	}
}

typedef struct {
    gchar *mail_addr;
    GtkTreeModel *model;
    GtkTreeIter iter;
} remove_key_data_t;

static void
remove_key_response(GtkDialog *self,
                    gint       response,
                    gpointer   user_data)
{
    remove_key_data_t *data = user_data;

    gtk_widget_destroy((GtkWidget *) self);

    if (response == GTK_RESPONSE_YES) {
        G_LOCK(db_mutex);
        if ((sqlite3_bind_text(query[6], 1, data->mail_addr, -1, SQLITE_STATIC) != SQLITE_OK) ||
            (sqlite3_step(query[6]) != SQLITE_DONE)) {
            g_warning("deleting database entry for \"%s\" failed: %s", data->mail_addr, sqlite3_errmsg(autocrypt_db));
        } else {
            g_debug("deleted database entry for \"%s\"", data->mail_addr);
        }
        sqlite3_reset(query[6]);
        G_UNLOCK(db_mutex);
        gtk_list_store_remove(GTK_LIST_STORE(data->model), &data->iter);
    }

    g_free(data->mail_addr);
    g_free(data);
}

/* key context menu callback: remove key from database */
static void
remove_key_cb(GtkMenuItem G_GNUC_UNUSED *menuitem, gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeSelection *selection;
    GtkTreeIter iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(user_data));
    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        GtkWidget *toplevel;
        GtkWindow *window;
        GtkWidget *dialog;
        gchar *mail_addr;
        remove_key_data_t *data;

        toplevel = gtk_widget_get_toplevel(GTK_WIDGET(user_data));
        window = GTK_IS_WINDOW(toplevel) ? GTK_WINDOW(toplevel) : NULL;
        gtk_tree_model_get(model, &iter, AC_ADDRESS_COLUMN, &mail_addr, -1);
        dialog =
            gtk_message_dialog_new(window,
                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
                                   GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
                                   /* Translators: #1 email address */
                                   _("Delete the Autocrypt key for “%s” from the database?"), mail_addr);

        data = g_new(remove_key_data_t, 1);
        data->mail_addr = mail_addr;
        data->model = model;
        data->iter = iter;

        g_signal_connect(dialog, "response",
                         G_CALLBACK(remove_key_response), data);
        gtk_widget_show_all(dialog);
    }
}


/** \brief Import keys from the Autocrypt database into the local key ring
 *
 * \param gpgme_ctx GpgME target context
 * \param addresses internet addresse to check and import
 * \param now current time, used to mask expired keys
 * \param error filled with error information on error, may be NULL
 * \return the count of imported keys (>= 0) on success, -1 on error
 */
static gint
get_keys_real(gpgme_ctx_t gpgme_ctx, InternetAddressList *addresses, time_t now, GError **error)
{
	gint n;
	gint imported = 0;

	for (n = 0; (imported >= 0) && (n < internet_address_list_length(addresses)); n++) {
		InternetAddress *this_addr;

		this_addr = internet_address_list_get_address(addresses, n);
		if (INTERNET_ADDRESS_IS_MAILBOX(this_addr)) {
			if (!libbalsa_gpgme_have_key(gpgme_ctx, INTERNET_ADDRESS_MAILBOX(this_addr), NULL)) {
				const gchar *mailbox = INTERNET_ADDRESS_MAILBOX(this_addr)->addr;

				G_LOCK(db_mutex);
				if ((sqlite3_bind_text(query[7], 1, mailbox, -1, SQLITE_STATIC) != SQLITE_OK) ||
					(sqlite3_bind_int64(query[7], 2, now) != SQLITE_OK)) {
					/* Translators: #1 email address (mailbox); #2 error message */
					g_set_error(error, AUTOCRYPT_ERROR_QUARK, -1, _("error reading Autocrypt data for “%s”: %s"), mailbox,
						sqlite3_errmsg(autocrypt_db));
					imported = -1;
				} else {
					int sqlite_res;

					sqlite_res = sqlite3_step(query[7]);
					if (sqlite_res == SQLITE_ROW) {
						GBytes *keybuf;

						keybuf = g_bytes_new_static(sqlite3_column_blob(query[7], 0), sqlite3_column_bytes(query[7], 0));
						if (libbalsa_gpgme_import_bin_key(gpgme_ctx, keybuf, NULL, error)) {
							imported++;
						} else {
							imported = -1;
						}
						g_bytes_unref(keybuf);
						sqlite_res = sqlite3_step(query[7]);
					}

					if (sqlite_res != SQLITE_DONE) {
						/* Translators: #1 email address (mailbox); #2 error message */
						g_set_error(error, AUTOCRYPT_ERROR_QUARK, sqlite_res, _("error reading Autocrypt data for “%s”: %s"),
							mailbox, sqlite3_errmsg(autocrypt_db));
						imported = -1;
					}
				}
				sqlite3_reset(query[7]);
				G_UNLOCK(db_mutex);
			}
		} else if (INTERNET_ADDRESS_IS_GROUP(this_addr)) {
			gint sub_count;

			sub_count = get_keys_real(gpgme_ctx, INTERNET_ADDRESS_GROUP(this_addr)->members, now, error);
			if (sub_count >= 0) {
				imported += sub_count;
			} else {
				imported = sub_count;
			}
		} else {
			g_assert_not_reached();		/* should never happen */
		}
	}

	return imported;
}


#endif  /* ENABLE_AUTOCRYPT */
