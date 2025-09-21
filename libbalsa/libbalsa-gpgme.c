/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gpgme low-level stuff for gmime/balsa
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

#include <string.h>
#include <gpgme.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "gmime-gpgme-signature.h"
#include "libbalsa-gpgme-keys.h"
#include "libbalsa-gpgme.h"
#include "libbalsa.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"

static void check_gpg_agent_version(void);

static gboolean gpgme_add_signer(gpgme_ctx_t ctx, const gchar * signer,
				 GtkWindow * parent, GError ** error);
static gpgme_key_t *gpgme_build_recipients(gpgme_ctx_t ctx,
					   GPtrArray * rcpt_list,
					   gboolean accept_low_trust,
					   GtkWindow * parent,
					   GError ** error);
static gpgme_error_t get_key_from_name(gpgme_ctx_t   ctx,
				  	  	  	  	  	   gpgme_key_t  *key,
									   const gchar  *name,
									   gboolean      secret,
									   gboolean      accept_all,
									   GtkWindow    *parent,
									   GError      **error);
static void release_keylist(gpgme_key_t * keylist);

/* callbacks for gpgme file handling */
static ssize_t g_mime_gpgme_stream_rd(GMimeStream * stream, void *buffer,
				      size_t size);
static ssize_t g_mime_gpgme_stream_wr(GMimeStream * stream, void *buffer,
				      size_t size);
static void cb_data_release(void *handle);

static gchar *utf8_valid_str(const char *gpgme_str)
	G_GNUC_WARN_UNUSED_RESULT;

#if defined(ENABLE_NLS)
static const gchar *get_utf8_locale(int category);
#endif

static void gpg_check_capas(const gchar *gpg_path,
							const gchar *version);


static gboolean has_proto_openpgp = FALSE;
static gboolean has_proto_cms = FALSE;
static gpg_capabilities gpg_capas;

static lbgpgme_select_key_cb select_key_cb = NULL;
static lbgpgme_accept_low_trust_cb accept_low_trust_cb = NULL;


/** \brief Initialise GpgME
 *
 * \param select_key Callback function to let the user select a key from a
 *        list if more than one is available.
 * \param accept_low_trust Callback function to ask the user whether a low
 *	  trust key shall be accepted.
 *
 * Initialise the GpgME backend and remember the callback functions.
 *
 * \note This function \em must be called before using any other function
 *       from this module.
 */
void
libbalsa_gpgme_init(lbgpgme_select_key_cb select_key,
		    lbgpgme_accept_low_trust_cb accept_low_trust)
{
    gpgme_engine_info_t e;

    /* initialise the gpgme library */
    g_debug("init gpgme version %s", gpgme_check_version(NULL));

#ifdef ENABLE_NLS
    gpgme_set_locale(NULL, LC_CTYPE, get_utf8_locale(LC_CTYPE));
    gpgme_set_locale(NULL, LC_MESSAGES, get_utf8_locale(LC_MESSAGES));
#endif				/* ENABLE_NLS */

    /* dump the available engines */
    if (gpgme_get_engine_info(&e) == GPG_ERR_NO_ERROR) {
	while (e) {
		g_debug("protocol %s: engine %s (home %s, version %s)",
		      gpgme_get_protocol_name(e->protocol),
		      e->file_name, e->home_dir, e->version);
		if (e->protocol == GPGME_PROTOCOL_OpenPGP) {
			gpg_check_capas(e->file_name, e->version);
		}
	    e = e->next;
	}
    }

	/* check for gpg-agent */
	check_gpg_agent_version();

    /* verify that the engines we need are there */
    if (gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) ==
	GPG_ERR_NO_ERROR) {
    	g_debug("OpenPGP protocol supported");
    	has_proto_openpgp = TRUE;
    } else {
    	g_warning("OpenPGP protocol not supported, basic crypto will not work!");
    	has_proto_openpgp = FALSE;
    }

    if (gpgme_engine_check_version(GPGME_PROTOCOL_CMS) == GPG_ERR_NO_ERROR) {
    	g_debug("CMS (aka S/MIME) protocol supported");
    	has_proto_cms = TRUE;
    } else {
    	g_warning("CMS protocol not supported, S/MIME will not work!");
    	has_proto_cms = FALSE;
    }

    /* remember callbacks */
    select_key_cb = select_key;
    accept_low_trust_cb = accept_low_trust;
}


/** \brief Return the protocol as human-readable string
 *
 * \param protocol Protocol to return as string.
 * \return a static string denoting the passed protocol, or "unknown".
 *
 * Note that this function differs from gpgme_get_protocol_name, as it returns "S/MIME" instead of "CMS" for GPGME_PROTOCOL_CMS,
 * and "unknown" for all other protocols but GPGME_PROTOCOL_OpenPGP (which is returned as "OpenPGP").
 */
const gchar *
libbalsa_gpgme_protocol_name(gpgme_protocol_t protocol)
{
    switch (protocol) {
    case GPGME_PROTOCOL_OpenPGP:
    	return _("OpenPGP");
    case GPGME_PROTOCOL_CMS:
    	return _("S/MIME");
    default:
    	return _("unknown");
    }
}


/** \brief Check if a crypto engine is available
 *
 * \param protocol Protocol for which the engine is checked.
 * \return TRUE is the engine for the passed protocol is available.
 *
 * Check the availability of the crypto engine for a specific protocol.
 */
gboolean
libbalsa_gpgme_check_crypto_engine(gpgme_protocol_t protocol)
{
    switch (protocol) {
    case GPGME_PROTOCOL_OpenPGP:
	return has_proto_openpgp;
    case GPGME_PROTOCOL_CMS:
	return has_proto_cms;
    default:
	return FALSE;
    }
}


/** \brief Get capabilities of the gpg engine
 *
 * \return a pointer to the capabilities of the GnuPG engine, or NULL if it is not supported
 *
 * If an engine for the OpenPGP protocol is available, return a structure containing the path of the executable, and information if
 * some \em export-filter options are available.  This information is needed to export a minimal Autocrypt key, but unfortunately
 * cannot be determined from the engine version.
 *
 * \sa libbalsa_gpgme_export_autocrypt_key(), gpg_check_capas()
 * \todo Actually, gpgme should provide a minimalistic key export.
 */
const gpg_capabilities *
libbalsa_gpgme_gpg_capabilities(void)
{
	return has_proto_openpgp ? &gpg_capas : NULL;
}


/** \brief Create a new GpgME context for a protocol
 *
 * \param protocol requested protocol
 * \param parent parent window, passed to the callback function
 * \param error Filled with error information on error.
 * \return the new gpgme context on success, or NULL on error
 *
 * This helper function creates a new GpgME context for the specified protocol.
 */
gpgme_ctx_t
libbalsa_gpgme_new_with_proto(gpgme_protocol_t        protocol,
							  GError                **error)
{
	gpgme_error_t err;
	gpgme_ctx_t ctx = NULL;

    /* create the GpgME context */
	err = gpgme_new(&ctx);
	if (err != GPG_ERR_NO_ERROR) {
		libbalsa_gpgme_set_error(error, err, _("could not create context"));
	} else {
		err = gpgme_set_protocol(ctx, protocol);
		if (err != GPG_ERR_NO_ERROR) {
			libbalsa_gpgme_set_error(error, err, _("could not set protocol “%s”"), libbalsa_gpgme_protocol_name(protocol));
		    gpgme_release(ctx);
		    ctx = NULL;
		} else {
			gpgme_set_passphrase_cb(ctx, NULL, NULL);
			if (protocol == GPGME_PROTOCOL_CMS) {
				/* make sure the user certificate is always included when signing */
				gpgme_set_include_certs(ctx, 1);
			}
		}
	}

	return ctx;
}


/** \brief Create a temporary GpgME context for a protocol
 *
 * \param protocol requested protocol
 * \param home_dir filled with the path of the temporary folder of the GpgME context on success, \em MUST be freed by the caller
 * \param error filled with error information on error
 * \return the new gpgme context on success, or NULL on error
 *
 * This helper function creates a new GpgME context using a temporary folder for the specified protocol.
 *
 * \note The caller \em SHOULD erase the temporary folder after releasing the context
 */
gpgme_ctx_t
libbalsa_gpgme_temp_with_proto(gpgme_protocol_t   protocol,
							   gchar            **home_dir,
							   GError           **error)
{
	gpgme_ctx_t ctx = NULL;

	g_return_val_if_fail(home_dir != NULL, NULL);

	*home_dir = NULL;
	ctx = libbalsa_gpgme_new_with_proto(protocol, error);
	if (ctx != NULL) {
		char *temp_dir;

		if (!libbalsa_mktempdir(&temp_dir)) {
			g_set_error(error, GPGME_ERROR_QUARK, -1, _("failed to create a temporary folder"));
			gpgme_release(ctx);
			ctx = NULL;
		} else {
			gpgme_engine_info_t this_engine;

			/* get the engine info */
			for (this_engine = gpgme_ctx_get_engine_info(ctx);
				(this_engine != NULL) && (this_engine->protocol != protocol);
				this_engine = this_engine->next) {
				/* nothing to do */
			}

			if (this_engine != NULL) {
				gpgme_error_t err;

				err = gpgme_ctx_set_engine_info(ctx, protocol, this_engine->file_name, temp_dir);
				if (err == GPG_ERR_NO_ERROR) {
					*home_dir = temp_dir;
				} else {
					libbalsa_gpgme_set_error(error, err, _("could not set folder “%s” for engine “%s”"), temp_dir,
						libbalsa_gpgme_protocol_name(protocol));
				}
			} else {
				/* paranoid - should *never* happen */
				libbalsa_gpgme_set_error(error, -1, _("no crypto engine for “%s” available"),
					libbalsa_gpgme_protocol_name(protocol));
			}

			/* unset home_dir indicates error condition */
			if (*home_dir == NULL) {
				gpgme_release(ctx);
				ctx = NULL;
				libbalsa_delete_directory(temp_dir, NULL);
				g_free(temp_dir);
			}
		}
	}

	return ctx;
}


/** \brief Verify a signature
 *
 * \param content GMime stream of the signed matter.
 * \param sig_plain GMime signature stream for a detached signature, or the
 *        output stream for the checked matter in single-part mode.
 * \param protocol GpgME crypto protocol of the signature.
 * \param singlepart_mode TRUE indicates single-part mode (i.e. sig_plain
 *        an output stream).
 * \param error Filled with error information on error.
 * \return A new signature status object on success, or NULL on error.
 *
 * Verify a signature by calling GpgME on the passed streams, and create a
 * new signature object on success.
 */
GMimeGpgmeSigstat *
libbalsa_gpgme_verify(GMimeStream * content, GMimeStream * sig_plain,
		      gpgme_protocol_t protocol, gboolean singlepart_mode,
		      GError ** error)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };
    gpgme_data_t cont_data;
    gpgme_data_t sig_plain_data;
    GMimeGpgmeSigstat *result;

    /* paranoia checks */
    g_return_val_if_fail(GMIME_IS_STREAM(content), NULL);
    g_return_val_if_fail(GMIME_IS_STREAM(sig_plain), NULL);
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP ||
			 protocol == GPGME_PROTOCOL_CMS, NULL);

    /* create the GpgME context */
    ctx = libbalsa_gpgme_new_with_proto(protocol, error);
    if (ctx == NULL) {
    	return NULL;
    }

    /* create the message stream */
    if ((err =
	 gpgme_data_new_from_cbs(&cont_data, &cbs,
				 content)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return NULL;
    }

    /* create data object for the detached signature stream or the
     * "decrypted" plaintext */
    if ((err =
	 gpgme_data_new_from_cbs(&sig_plain_data, &cbs,
				 sig_plain)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not get data from stream"));
	gpgme_data_release(cont_data);
	gpgme_release(ctx);
	return NULL;
    }

    /* verify the signature */
    if (singlepart_mode)
	err = gpgme_op_verify(ctx, cont_data, NULL, sig_plain_data);
    else
	err = gpgme_op_verify(ctx, sig_plain_data, cont_data, NULL);
    if (err != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("signature verification failed"));
	result = g_mime_gpgme_sigstat_new(ctx);
	g_mime_gpgme_sigstat_set_status(result, err);
    } else
	result = g_mime_gpgme_sigstat_new_from_gpgme_ctx(ctx);

    /* release gmgme data buffers, destroy the context and return the
     * signature object */
    gpgme_data_release(cont_data);
    gpgme_data_release(sig_plain_data);
    gpgme_release(ctx);
    return result;
}


/** \brief Sign data
 *
 * \param userid User ID of the signer.
 * \param istream GMime input stream.
 * \param ostream GMime output stream.
 * \param protocol GpgME crypto protocol of the signature.
 * \param singlepart_mode TRUE indicates single-part mode (integrated
 *        signature), FALSE a detached signature.
 * \param parent Parent window to be passed to the passphrase callback
 *        function.
 * \param error Filled with error information on error.
 * \return The hash algorithm used for creating the signature, or
 *         GPGME_MD_NONE on error.
 *
 * Sign the passed matter and write the detached signature or the signed
 * input and the signature, respectively, to the output stream.  The global
 * callback to read the passphrase for the user's private key will be
 * called by GpgME if no GPG Agent is running.
 */
gpgme_hash_algo_t
libbalsa_gpgme_sign(const gchar * userid, GMimeStream * istream,
		    GMimeStream * ostream, gpgme_protocol_t protocol,
		    gboolean singlepart_mode, GtkWindow * parent,
		    GError ** error)
{
    gpgme_error_t err;
    gpgme_ctx_t ctx;
    gpgme_sig_mode_t sig_mode;
    gpgme_data_t in;
    gpgme_data_t out;
    gpgme_hash_algo_t hash_algo;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* paranoia checks */
    g_return_val_if_fail(GMIME_IS_STREAM(istream), GPGME_MD_NONE);
    g_return_val_if_fail(GMIME_IS_STREAM(ostream), GPGME_MD_NONE);
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP ||
			 protocol == GPGME_PROTOCOL_CMS, GPGME_MD_NONE);

    /* create the GpgME context */
    ctx = libbalsa_gpgme_new_with_proto(protocol, error);
    if (ctx == NULL) {
    	return GPGME_MD_NONE;
    }

    /* set the signature mode */
    if (singlepart_mode) {
	if (protocol == GPGME_PROTOCOL_OpenPGP)
	    sig_mode = GPGME_SIG_MODE_CLEAR;
	else
	    sig_mode = GPGME_SIG_MODE_NORMAL;
    } else
	sig_mode = GPGME_SIG_MODE_DETACH;

    /* find the secret key for the "sign_for" address */
    if (!gpgme_add_signer(ctx, userid, parent, error)) {
	gpgme_release(ctx);
	return GPGME_MD_NONE;
    }

    /* OpenPGP signatures are ASCII armored */
    gpgme_set_armor(ctx, protocol == GPGME_PROTOCOL_OpenPGP);

    /* create gpgme data objects */
    if ((err =
	 gpgme_data_new_from_cbs(&in, &cbs,
				 istream)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return GPGME_MD_NONE;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&out, &cbs,
				 ostream)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not create new data object"));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPGME_MD_NONE;
    }

    /* sign and get the used hash algorithm */
    err = gpgme_op_sign(ctx, in, out, sig_mode);
    if (err != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err, _("signing failed"));
	hash_algo = GPGME_MD_NONE;
    } else
	hash_algo = gpgme_op_sign_result(ctx)->signatures->hash_algo;

    /* clean up */
    gpgme_data_release(in);
    gpgme_data_release(out);
    gpgme_release(ctx);
    return hash_algo;
}


/** \brief Encrypt data
 *
 * \param recipients Array of User ID for which the matter shall be
 *        encrypted using their public keys.
 * \param sign_for User ID of the signer or NULL if the matter shall not be
 *        signed.  Note that combined signing and encryption is allowed \em
 *        only in OpenPGP single-part (i.e. RFC 2440) mode.
 * \param istream GMime input stream.
 * \param ostream GMime output stream.
 * \param protocol GpgME crypto protocol to use for encryption.
 * \param singlepart_mode TRUE indicates single-part mode (integrated
 *        signature), FALSE a detached signature.
 * \param trust_all_keys TRUE if all low-truct keys shall be accepted for
 *        encryption.  Otherwise, the function will use the global callback
 *        to ask the user whether a low-trust key shall be accepted.
 * \param parent Parent window to be passed to the callback functions.
 * \param error Filled with error information on error.
 * \return TRUE on success, or FALSE on error.
 *
 * Encrypt the passed matter and write the result to the output stream.
 * Combined signing and encryption is allowed for single-part OpenPGP mode
 * only.
 */
gboolean
libbalsa_gpgme_encrypt(GPtrArray * recipients, const char *sign_for,
		       GMimeStream * istream, GMimeStream * ostream,
		       gpgme_protocol_t protocol, gboolean singlepart_mode,
		       gboolean trust_all_keys, GtkWindow * parent,
		       GError ** error)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_key_t *rcpt_keys;
    gpgme_data_t plain;
    gpgme_data_t crypt;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* paranoia checks */
    g_return_val_if_fail(recipients != NULL, FALSE);
    g_return_val_if_fail(GMIME_IS_STREAM(istream), FALSE);
    g_return_val_if_fail(GMIME_IS_STREAM(ostream), FALSE);
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP ||
			 protocol == GPGME_PROTOCOL_CMS, FALSE);

    /* create the GpgME context */
    ctx = libbalsa_gpgme_new_with_proto(protocol, error);
    if (ctx == NULL) {
    	return FALSE;
    }

    /* sign & encrypt is valid only for single-part OpenPGP */
    if (sign_for != NULL
	&& (!singlepart_mode || protocol != GPGME_PROTOCOL_OpenPGP)) {
	if (error)
	    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_INV_ENGINE,
			_
			("combined signing and encryption is defined only for RFC 2440"));
	gpgme_release(ctx);
	return FALSE;
    }

    /* if requested, find the secret key for "userid" */
    if (sign_for && !gpgme_add_signer(ctx, sign_for, parent, error)) {
	gpgme_release(ctx);
	return FALSE;
    }

    /* build the list of recipients */
    if (!
	(rcpt_keys =
	 gpgme_build_recipients(ctx, recipients, trust_all_keys, parent,
				error))) {
	gpgme_release(ctx);
	return FALSE;
    }

    /* create the data objects */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	gpgme_set_armor(ctx, 1);
	gpgme_set_textmode(ctx, singlepart_mode);
    } else {
	gpgme_set_armor(ctx, 0);
	gpgme_set_textmode(ctx, 0);
    }
    if ((err =
	 gpgme_data_new_from_cbs(&plain, &cbs,
				 istream)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not get data from stream"));
	release_keylist(rcpt_keys);
	gpgme_release(ctx);
	return FALSE;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&crypt, &cbs,
				 ostream)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not create new data object"));
	release_keylist(rcpt_keys);
	gpgme_data_release(plain);
	gpgme_release(ctx);
	return FALSE;
    }

    /* do the encrypt or sign and encrypt operation
     * Note: we set "always trust" here, as if we detected an untrusted key
     * earlier, the user already accepted it */
    if (sign_for)
	err =
	    gpgme_op_encrypt_sign(ctx, rcpt_keys,
				  GPGME_ENCRYPT_ALWAYS_TRUST, plain,
				  crypt);
    else
	err =
	    gpgme_op_encrypt(ctx, rcpt_keys, GPGME_ENCRYPT_ALWAYS_TRUST,
			     plain, crypt);

    release_keylist(rcpt_keys);
    gpgme_data_release(plain);
    gpgme_data_release(crypt);
    gpgme_release(ctx);
    if (err != GPG_ERR_NO_ERROR) {
	if (sign_for)
		libbalsa_gpgme_set_error(error, err,
				   _("signing and encryption failed"));
	else
		libbalsa_gpgme_set_error(error, err, _("encryption failed"));
	return FALSE;
    } else
	return TRUE;
}


/** \brief Decrypt data
 *
 * \param istream GMime input (encrypted) stream.
 * \param ostream GMime output (decrypted) stream.
 * \param protocol GpgME crypto protocol to use.
 * \param parent Parent window to be passed to the passphrase callback
 *        function.
 * \param error Filled with error information on error.
 * \return A new signature status object on success, or NULL on error.
 *
 * Decrypt and -if applicable- verify the signature of the passed data
 * stream.  If the input is not signed the returned signature status will
 * be GPG_ERR_NOT_SIGNED.
 */
GMimeGpgmeSigstat *
libbalsa_gpgme_decrypt(GMimeStream * crypted, GMimeStream * plain,
		       gpgme_protocol_t protocol,
		       GError ** error)
{
    gpgme_ctx_t ctx;
    gpgme_error_t err;
    gpgme_data_t plain_data;
    gpgme_data_t crypt_data;
    GMimeGpgmeSigstat *result;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* paranoia checks */
    g_return_val_if_fail(GMIME_IS_STREAM(crypted), NULL);
    g_return_val_if_fail(GMIME_IS_STREAM(plain), NULL);
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP ||
			 protocol == GPGME_PROTOCOL_CMS, NULL);

    /* create the GpgME context */
    ctx = libbalsa_gpgme_new_with_proto(protocol, error);
    if (ctx == NULL) {
    	return NULL;
    }

    /* create the data streams */
    if ((err =
	 gpgme_data_new_from_cbs(&crypt_data, &cbs,
				 crypted)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&plain_data, &cbs,
				 plain)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err,
			       _("could not create new data object"));
	gpgme_data_release(crypt_data);
	gpgme_release(ctx);
	return NULL;
    }

    /* try to decrypt */
    if ((err =
	 gpgme_op_decrypt_verify(ctx, crypt_data,
				 plain_data)) != GPG_ERR_NO_ERROR) {
    	libbalsa_gpgme_set_error(error, err, _("decryption failed"));
	result = NULL;
    } else {
	/* decryption successful, check for signature */
	result = g_mime_gpgme_sigstat_new_from_gpgme_ctx(ctx);
    }

    /* clean up */
    gpgme_data_release(plain_data);
    gpgme_data_release(crypt_data);
    gpgme_release(ctx);

    return result;
}


/** \brief Export a public key
 *
 * \param protocol GpgME crypto protocol to use
 * \param name pattern (mail address or fingerprint) of the requested key
 * \param parent parent window to be passed to the callback functions
 * \param error Filled with error information on error
 * \return a newly allocated string containing the ASCII-armored public key on success
 *
 * Return the ASCII-armored key matching the passed pattern.  If necessary, the user is asked to select a key from a list of
 * multiple matching keys.
 */
gchar *
libbalsa_gpgme_get_pubkey(gpgme_protocol_t   protocol,
						  const gchar       *name,
						  GtkWindow 		*parent,
						  GError           **error)
{
	gpgme_ctx_t ctx;
	gchar *armored_key = NULL;

	g_return_val_if_fail(name != NULL, NULL);

	ctx = libbalsa_gpgme_new_with_proto(protocol, error);
	if (ctx != NULL) {
		gpgme_error_t gpgme_err;
		gpgme_key_t key = NULL;

		gpgme_err = get_key_from_name(ctx, &key, name, FALSE, FALSE, parent, error);
		if (gpgme_err == GPG_ERR_NO_ERROR) {
			armored_key = libbalsa_gpgme_export_key(ctx, key, name, error);
			gpgme_key_unref(key);
		}
	    gpgme_release(ctx);
	}

	return armored_key;
}


/** \brief Get the key id of a secret key
 *
 * \param protocol GpgME protocol (OpenPGP or CMS)
 * \param name email address for which the key shall be selected
 * \param parent parent window to be passed to the callback functions
 * \param error Filled with error information on error
 * \return a newly allocated string containing the key id key on success, shall be freed by the caller
 *
 * Call libbalsa_gpgme_list_keys() to list all secret keys for the passed protocol, and \em always call \ref select_key_cb to let
 * the user choose the secret key, even if only one is available.
 */
gchar *
libbalsa_gpgme_get_seckey(gpgme_protocol_t   protocol,
	  	  	  	  	  	  const gchar       *name,
						  GtkWindow 		*parent,
						  GError           **error)
{
	gpgme_ctx_t ctx;
	gchar *keyid = NULL;

	ctx = libbalsa_gpgme_new_with_proto(protocol, error);
	if (ctx != NULL) {
		GList *keys = NULL;

		/* Let gpgme list all available secret keys, including those not matching the passed email address.
		 * Rationale: enable selecting a secret key even if the local email address is re-written by the MTA.
		 * See e.g. http://www.postfix.org/ADDRESS_REWRITING_README.html#generic */
		if (libbalsa_gpgme_list_keys(ctx, &keys, NULL, NULL, TRUE, FALSE, error)) {
			if (keys != NULL) {
				gpgme_key_t key;

				/* let the user select a key from the list, even if there is only one */
				if (select_key_cb != NULL) {
					key = select_key_cb(name, LB_SELECT_PRIVATE_KEY, keys, gpgme_get_protocol(ctx), parent);
					if (key != NULL) {
						gpgme_subkey_t subkey;

						for (subkey = key->subkeys; (subkey != NULL) && (keyid == NULL); subkey = subkey->next) {
							if ((subkey->can_sign != 0) && (subkey->expired == 0U) && (subkey->revoked == 0U) &&
								(subkey->disabled == 0U) && (subkey->invalid == 0U)) {
								keyid = g_strdup(subkey->keyid);
							}
						}
					}
				}
				g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
			} else {
				GtkWidget *dialog;

				dialog = gtk_message_dialog_new(parent,
					GTK_DIALOG_DESTROY_WITH_PARENT | libbalsa_dialog_flags(),
					GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
					_("No private key for protocol %s is available for the signer “%s”"),
					libbalsa_gpgme_protocol_name(protocol), name);
				(void) gtk_dialog_run(GTK_DIALOG(dialog));
				gtk_widget_destroy(dialog);
			}
		}
	    gpgme_release(ctx);
	}

	return keyid;
}


/*
 * set a GError form GpgME information
 */
void
libbalsa_gpgme_set_error(GError        **error,
					     gpgme_error_t   gpgme_err,
						 const gchar    *format,
						 ...)
{
    if (error != NULL) {
    	gchar errbuf[4096];		/* should be large enough... */
        gchar *errstr;
        gchar *srcstr;
        gchar *msgstr;
        va_list ap;

        srcstr = utf8_valid_str(gpgme_strsource(gpgme_err));
        gpgme_strerror_r(gpgme_err, errbuf, sizeof(errbuf));
        errstr = utf8_valid_str(errbuf);
        va_start(ap, format);
        msgstr = g_strdup_vprintf(format, ap);
        va_end(ap);
        g_set_error(error, GPGME_ERROR_QUARK, gpgme_err, "%s: %s: %s", srcstr, msgstr, errstr);
        g_free(msgstr);
        g_free(errstr);
        g_free(srcstr);
    }
}


/* ---- local stuff ---------------------------------------------------- */

/* GpgME callback for gpgme_op_assuan_transact_ext(), see check_gpg_agent_version() */
static gpgme_error_t
assuan_data_cb(void *opaque, const void *data, size_t datalen)
{
	*((gchar **) opaque) = g_strndup(data, datalen);
	return GPG_ERR_NO_ERROR;
}

/** \brief Check the version of the GPG Agent
 *
 * Try to get the version of the GPG agent, and print it as debug message on success.  Warn the user if any error occurred.
 */
static void
check_gpg_agent_version(void)
{
	gchar *agent_ver = NULL;
	gpgme_ctx_t assuan;
	GError *error = NULL;

	assuan = libbalsa_gpgme_new_with_proto(GPGME_PROTOCOL_ASSUAN, &error);
	if (assuan != NULL) {
		gpgme_error_t err;
		gpgme_error_t op_err;

		err = gpgme_op_assuan_transact_ext(assuan, "GETINFO version", assuan_data_cb, &agent_ver, NULL, NULL, NULL, NULL, &op_err);
		if ((err != GPG_ERR_NO_ERROR) || (op_err != GPG_ERR_NO_ERROR)) {
			libbalsa_gpgme_set_error(&error, (err != GPG_ERR_NO_ERROR) ? err : op_err, _("accessing the GPG agent failed"));
		} else {
			g_debug("GPG Agent version %s", agent_ver);
		}
		gpgme_release(assuan);
	}
	if (error != NULL) {
		libbalsa_information(LIBBALSA_INFORMATION_WARNING,
				_("Signing or decrypting GPG or S/MIME messages may fail, please check the GPG agent: %s"), error->message);
		g_error_free(error);
	}
}

static gchar *
utf8_valid_str(const char *gpgme_str)
{
	gchar *result;

	if (gpgme_str != NULL) {
		if (g_utf8_validate(gpgme_str, -1, NULL)) {
			result = g_strdup(gpgme_str);
		} else {
			gsize bytes_written;
			result = g_locale_to_utf8(gpgme_str, -1, NULL, &bytes_written, NULL);
		}
	} else {
		result = NULL;
	}
	return result;
}


/*
 * callback to get data from a stream
 */
static ssize_t
g_mime_gpgme_stream_rd(GMimeStream * stream, void *buffer, size_t size)
{
    ssize_t result;

    result = g_mime_stream_read(stream, buffer, size);
    if (result == -1 && g_mime_stream_eos(stream))
	result = 0;

    return result;
}


/*
 * callback to write data to a stream
 */
static ssize_t
g_mime_gpgme_stream_wr(GMimeStream * stream, void *buffer, size_t size)
{
    return g_mime_stream_write(stream, buffer, size);
}


/*
 * dummy function for callback based gpgme data objects
 */
static void
cb_data_release(void *handle)
{
    /* must just be present... bug or feature?!? */
}


/** \brief Get a key for a name or fingerprint
 *
 * \param ctx GpgME context
 * \param key filled with the key on success
 * \param name pattern (mail address or fingerprint) of the requested key
 * \param secret TRUE to select a secret (private) key for signing, FALSE to select a public key for encryption
 * \param accept_all TRUE to accept a low-trust public key without confirmation
 * \param parent transient parent window
 * \param error filled with a human-readable error on error, may be NULL
 * \return GPG_ERR_GENERAL if listing the keys failed, GPG_ERR_NO_KEY if no suitable key is available, GPG_ERR_CANCELED if the user
 *         cancelled the operation, GPG_ERR_AMBIGUOUS if multiple keys exist, or GPG_ERR_NOT_TRUSTED if the key is not trusted
 *
 * Get a key for a name or a fingerprint.  A name will always be enclosed in "<...>" to get an exact match.  If \em secret is set,
 * choose only secret (private) keys (signing).  Otherwise, choose only public keys (encryption).  If multiple keys would match,
 * call the key selection CB \ref select_key_cb (if present).  If no matching key could be found or if any error occurs, return an
 * appropriate error code.
 */
static gpgme_error_t
get_key_from_name(gpgme_ctx_t   ctx,
				  gpgme_key_t  *key,
				  const gchar  *name,
				  gboolean      secret,
				  gboolean      accept_all,
				  GtkWindow    *parent,
				  GError      **error)
{
	gchar *mail_name;
	gboolean list_res;
	GList *keys = NULL;
	gpgme_key_t selected;
	guint bad_keys = 0U;
	gpgme_error_t result;

	/* enclose a mail address into "<...>" to perform an exact search */
	if (strchr(name, '@') != NULL) {
		mail_name = g_strconcat("<", name, ">", NULL);
	} else {
		mail_name = g_strdup(name);
	}

	/* let gpgme list keys */
	list_res = libbalsa_gpgme_list_keys(ctx, &keys, &bad_keys, mail_name, secret, FALSE, error);
	g_free(mail_name);
	if (!list_res) {
		return GPG_ERR_GENERAL;
	}

	if (keys == NULL) {
		if (bad_keys > 0U) {
			g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
				_("A key for “%s” is present, but it is expired, disabled, revoked or invalid"), name);
		} else {
			g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
				_("Could not find a key for “%s”"), name);
		}
		return secret ? GPG_ERR_NO_SECKEY : GPG_ERR_NO_PUBKEY;
	}

	/* let the user select a key from the list if there is more than one */
	result = GPG_ERR_NO_ERROR;
	if (g_list_length(keys) > 1U) {
		if (select_key_cb != NULL) {
			selected = select_key_cb(name, secret ? LB_SELECT_PRIVATE_KEY : LB_SELECT_PUBLIC_KEY_USER,
				keys, gpgme_get_protocol(ctx), parent);
			if (selected == NULL) {
				result = GPG_ERR_CANCELED;
			}
		} else {
			g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION, _("Multiple keys for “%s”"), name);
			selected = NULL;
			result = GPG_ERR_AMBIGUOUS;
		}
	} else {
		selected = (gpgme_key_t) keys->data;
	}

	/* ref the selected key, free all others and the list */
	if (selected != NULL) {
		gpgme_key_ref(selected);
	}
	g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);

	/* OpenPGP: ask the user if a low-validity key should be trusted for encryption (Note: owner_trust is not applicable to
	 * S/MIME certificates) */
	if ((selected != NULL) &&
                (result == GPG_ERR_NO_ERROR) && !secret && !accept_all && (gpgme_get_protocol(ctx) == GPGME_PROTOCOL_OpenPGP) &&
		(selected->owner_trust < GPGME_VALIDITY_FULL)) {
		if ((accept_low_trust_cb == NULL) || !accept_low_trust_cb(name, selected, parent)) {
			gpgme_key_unref(selected);
			selected = NULL;
			g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION, _("Insufficient key validity"));
			result = GPG_ERR_NOT_TRUSTED;
		}
	}

	*key = selected;
	return result;
}


/** \brief Select a public key from all available keys
 *
 * \param ctx GpgME context
 * \param name recipient's mail address, used only for display
 * \param parent transient parent window
 * \param error filled with a human-readable error on error, may be NULL
 * \return the selected key or NULL if the user cancelled the operation
 *
 * This helper function loads all available keys and calls \ref select_key_cb to let the user choose one of them.
 */
static gpgme_key_t
get_pubkey(gpgme_ctx_t   ctx,
		   const gchar  *name,
		   GtkWindow    *parent,
		   GError      **error)
{
	GList *keys = NULL;
	gpgme_key_t key = NULL;

	/* let gpgme list all available keys */
	if (libbalsa_gpgme_list_keys(ctx, &keys, NULL, NULL, FALSE, FALSE, error)) {
		if (keys != NULL) {
			/* let the user select a key from the list, even if there is only one */
			if (select_key_cb != NULL) {
				key = select_key_cb(name, LB_SELECT_PUBLIC_KEY_ANY, keys, gpgme_get_protocol(ctx), parent);
				if (key != NULL) {
					gpgme_key_ref(key);
				}
			}
			g_list_free_full(keys, (GDestroyNotify) gpgme_key_unref);
		}
	}

	return key;
}


/** \brief Add the private key for signing
 *
 * \param ctx GpgME context
 * \param signer sender's (signers) mail address or key fingerprint
 * \param parent transient parent window
 * \param error filled with a human-readable error on error, may be NULL
 * \return TRUE on success or FALSE if no suitable key is available
 *
 * Add the signer's key to the list of signers of the passed context.
 */
static gboolean
gpgme_add_signer(gpgme_ctx_t   ctx,
				 const gchar  *signer,
				 GtkWindow    *parent,
				 GError      **error)
{
	gpgme_error_t result;
	gpgme_key_t key = NULL;

    /* note: private (secret) key has never low trust... */
	result = get_key_from_name(ctx, &key, signer, TRUE, FALSE, parent, error);
	if (result == GPG_ERR_NO_ERROR) {
		/* set the key (the previous operation guaranteed that it exists, no need 2 check return values...) */
		(void) gpgme_signers_add(ctx, key);
		gpgme_key_unref(key);
	}

    return (result == GPG_ERR_NO_ERROR);
}


/** \brief Find public keys for a list of recipients
 *
 * \param ctx GpgME context
 * \param rcpt_list array of <i>gchar *</i> elements, each containing a recipient's mailbox
 * \param accept_low_trust TRUE to accept low-trust keys without confirmation
 * \param parent transient parent window
 * \param error filled with a human-readable error on error, may be NULL
 * \return a newly allocated, NULL-terminated array of keys on success, NULL if any error occurred
 *
 * Build an array of keys for all recipients in rcpt_list and return it.
 *
 * \note The caller shall free the returned list by calling release_keylist().
 */
static gpgme_key_t *
gpgme_build_recipients(gpgme_ctx_t   ctx,
					   GPtrArray    *rcpt_list,
					   gboolean      accept_low_trust,
					   GtkWindow    *parent,
					   GError      **error)
{
	gpgme_key_t *rcpt = g_new0(gpgme_key_t, rcpt_list->len + 1U);
	gpgme_error_t select_res;
	guint num_rcpts;

	/* try to find the public key for every recipient */
	select_res = GPG_ERR_NO_ERROR;
	for (num_rcpts = 0U; (select_res == GPG_ERR_NO_ERROR) && (num_rcpts < rcpt_list->len); num_rcpts++) {
		gchar *name = (gchar *) g_ptr_array_index(rcpt_list, num_rcpts);
		gpgme_key_t key = NULL;

		select_res = get_key_from_name(ctx, &key, name, FALSE, accept_low_trust, parent, error);

		/* if no public key exists for the user, as fallback list all keys so an other one may be selected */
		if (select_res == GPG_ERR_NO_PUBKEY) {
			key = get_pubkey(ctx, name, parent, error);
			if (key != NULL) {
				select_res = GPG_ERR_NO_ERROR;		/* got one, clear error state */
			}
		}

		/* set the recipient */
		rcpt[num_rcpts] = key;
	}

	if (select_res != GPG_ERR_NO_ERROR) {
		release_keylist(rcpt);
		rcpt = NULL;
	}

	return rcpt;
}


/*
 * helper function: unref all keys in the NULL-terminated array keylist and
 * finally release the array itself
 */
static void
release_keylist(gpgme_key_t * keylist)
{
    gpgme_key_t *key = keylist;

    while (*key) {
	gpgme_key_unref(*key);
	key++;
    }
    g_free(keylist);
}


#if defined(ENABLE_NLS)
/*
 * convert a locale name to utf-8
 */
static const gchar *
get_utf8_locale(int category)
{
    gchar *locale;
    static gchar localebuf[64];	/* should be large enough */
    gchar *dot;

    if (!(locale = setlocale(category, NULL)))
	return NULL;
    strncpy(localebuf, locale, 57);
    localebuf[57] = '\0';
    dot = strchr(localebuf, '.');
    if (!dot)
	dot = localebuf + strlen(localebuf);
    strcpy(dot, ".UTF-8");
    return localebuf;
}
#endif

/*
 * Note: this function is a hack to detect if the gpg engine in use support the '--export-filter' options 'keep-uid=...' and
 * 'drop-subkey=...' (since 2.2.9) needed for exporting a minimal Autocrypt key.
 */
static void
gpg_check_capas(const gchar *gpg_path, const gchar *version)
{
	gchar *gpg_args[] = { (gchar *) gpg_path, "--export", "--export-filter", "keep-uid=primary=1", "0000000000000000", NULL };
	gint exit_status;
	guint major;
	guint minor;
	guint release;

	gpg_capas.gpg_path = g_strdup(gpg_path);

	/* check for the "--export-filter keep-uid=..." option */
	if (g_spawn_sync(NULL, gpg_args, NULL, G_SPAWN_STDOUT_TO_DEV_NULL + G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL,
					 &exit_status, NULL)) {
		gpg_capas.export_filter_uid = g_spawn_check_wait_status(exit_status, NULL);
	}
	g_debug("%s supports '--export-filter keep-uid=...': %d", gpg_path, gpg_capas.export_filter_uid);

	/* check for the "--export-filter drop-subkey=usage!~e && usage!~s" option */
	if (sscanf(version, "%u.%u.%u", &major, &minor, &release) == 3) {
		gpg_capas.export_filter_subkey = (major > 2U) ||
			((major == 2U) && (minor > 2U)) ||
			((major == 2U) && (minor == 2U) && (release >= 9U));
	}
	g_debug("%s supports '--export-filter drop-subkey=...': %d", gpg_path, gpg_capas.export_filter_subkey);
}
