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

#ifdef BALSA_USE_THREADS
#include <pthread.h>
#include "misc.h"
#endif

#if HAVE_MACOSX_DESKTOP
#include "macosx-helpers.h"
#endif

#include <string.h>
#include <gpgme.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "gmime-gpgme-signature.h"
#include "libbalsa-gpgme.h"


static void g_set_error_from_gpgme(GError ** error,
				   gpgme_error_t gpgme_err,
				   const gchar * message);
static gpgme_error_t gpgme_new_with_protocol(gpgme_ctx_t * ctx,
					     gpgme_protocol_t protocol,
					     GtkWindow * parent,
					     GError ** error);
static gboolean gpgme_add_signer(gpgme_ctx_t ctx, const gchar * signer,
				 GtkWindow * parent, GError ** error);
static gpgme_key_t *gpgme_build_recipients(gpgme_ctx_t ctx,
					   GPtrArray * rcpt_list,
					   gboolean accept_low_trust,
					   GtkWindow * parent,
					   GError ** error);
static void release_keylist(gpgme_key_t * keylist);

/* callbacks for gpgme file handling */
static ssize_t g_mime_gpgme_stream_rd(GMimeStream * stream, void *buffer,
				      size_t size);
static ssize_t g_mime_gpgme_stream_wr(GMimeStream * stream, void *buffer,
				      size_t size);
static void cb_data_release(void *handle);


#if defined(ENABLE_NLS)
static const gchar *get_utf8_locale(int category);
#endif


static gboolean has_proto_openpgp = FALSE;
static gboolean has_proto_cms = FALSE;

static gpgme_passphrase_cb_t gpgme_passphrase_cb = NULL;
static lbgpgme_select_key_cb select_key_cb = NULL;
static lbgpgme_accept_low_trust_cb accept_low_trust_cb = NULL;


/** \brief Initialise GpgME
 *
 * \param get_passphrase Callback function to read a passphrase from the
 *        user.  Note that this function is used \em only for OpenPGP and
 *        \em only if no GPG Agent is running and can therefore usually be
 *        NULL.  The first (HOOK) argument the passed function accepts
 *        shall be the parent GtkWindow.
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
libbalsa_gpgme_init(gpgme_passphrase_cb_t get_passphrase,
		    lbgpgme_select_key_cb select_key,
		    lbgpgme_accept_low_trust_cb accept_low_trust)
{
    gpgme_engine_info_t e;
    const gchar *agent_info;

    /* initialise the gpgme library */
    g_message("init gpgme version %s", gpgme_check_version(NULL));

#ifdef HAVE_GPG
    /* configure the GnuPG engine if a specific gpg path has been
     * detected */
    gpgme_set_engine_info(GPGME_PROTOCOL_OpenPGP, GPG_PATH, NULL);
#endif

#ifdef ENABLE_NLS
    gpgme_set_locale(NULL, LC_CTYPE, get_utf8_locale(LC_CTYPE));
    gpgme_set_locale(NULL, LC_MESSAGES, get_utf8_locale(LC_MESSAGES));
#endif				/* ENABLE_NLS */

    /* dump the available engines */
    if (gpgme_get_engine_info(&e) == GPG_ERR_NO_ERROR) {
	while (e) {
	    g_message("protocol %s: engine %s (home %s, version %s)",
		      gpgme_get_protocol_name(e->protocol),
		      e->file_name, e->home_dir, e->version);
	    e = e->next;
	}
    }

    /* check for gpg-agent */
    agent_info = g_getenv("GPG_AGENT_INFO");
    if (agent_info) {
	g_message("gpg-agent found: %s", agent_info);
	gpgme_passphrase_cb = NULL;
    } else {
	gpgme_passphrase_cb = get_passphrase;
    }

    /* verify that the engines we need are there */
    if (gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) ==
	GPG_ERR_NO_ERROR) {
	g_message("OpenPGP protocol supported");
	has_proto_openpgp = TRUE;
    } else {
	g_message
	    ("OpenPGP protocol not supported, basic crypto will not work!");
	has_proto_openpgp = FALSE;
    }

#ifdef HAVE_SMIME
    if (gpgme_engine_check_version(GPGME_PROTOCOL_CMS) ==
	GPG_ERR_NO_ERROR) {
	g_message("CMS (aka S/MIME) protocol supported");
	has_proto_cms = TRUE;
    } else {
	g_message("CMS protocol not supported, S/MIME will not work!");
	has_proto_cms = FALSE;
    }
#else
    g_message("built without CMS (aka S/MIME) protocol support");
    has_proto_cms = FALSE;
#endif

    /* remember callbacks */
    select_key = select_key_cb;
    accept_low_trust = accept_low_trust_cb;
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
    if ((err =
	 gpgme_new_with_protocol(&ctx, protocol, NULL,
				 error)) != GPG_ERR_NO_ERROR)
	return NULL;

    /* create the message stream */
    if ((err =
	 gpgme_data_new_from_cbs(&cont_data, &cbs,
				 content)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return NULL;
    }

    /* create data object for the detached signature stream or the
     * "decrypted" plaintext */
    if ((err =
	 gpgme_data_new_from_cbs(&sig_plain_data, &cbs,
				 sig_plain)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
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
	g_set_error_from_gpgme(error, err,
			       _("signature verification failed"));
	result = g_mime_gpgme_sigstat_new();
	result->status = err;
	result->protocol = gpgme_get_protocol(ctx);
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
    if ((err =
	 gpgme_new_with_protocol(&ctx, protocol, parent,
				 error)) != GPG_ERR_NO_ERROR)
	return GPGME_MD_NONE;

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
	g_set_error_from_gpgme(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return GPGME_MD_NONE;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&out, &cbs,
				 ostream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
			       _("could not create new data object"));
	gpgme_data_release(in);
	gpgme_release(ctx);
	return GPGME_MD_NONE;
    }

    /* sign and get the used hash algorithm */
    err = gpgme_op_sign(ctx, in, out, sig_mode);
    if (err != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("signing failed"));
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
 * \return 0 on success, or -1 on error.
 *
 * Encrypt the passed matter and write the result to the output stream.
 * Combined signing and encryption is allowed for single-part OpenPGP mode
 * only.
 */
int
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
    g_return_val_if_fail(recipients != NULL, -1);
    g_return_val_if_fail(GMIME_IS_STREAM(istream), -1);
    g_return_val_if_fail(GMIME_IS_STREAM(ostream), GPGME_MD_NONE);
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP ||
			 protocol == GPGME_PROTOCOL_CMS, -1);

    /* create the GpgME context */
    if ((err =
	 gpgme_new_with_protocol(&ctx, protocol, parent,
				 error)) != GPG_ERR_NO_ERROR)
	return -1;

    /* sign & encrypt is valid only for single-part OpenPGP */
    if (sign_for != NULL
	&& (!singlepart_mode || protocol != GPGME_PROTOCOL_OpenPGP)) {
	if (error)
	    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_INV_ENGINE,
			_
			("combined signing and encryption is defined only for RFC 2440"));
	gpgme_release(ctx);
	return -1;
    }

    /* if requested, find the secret key for "userid" */
    if (sign_for && !gpgme_add_signer(ctx, sign_for, parent, error)) {
	gpgme_release(ctx);
	return -1;
    }

    /* build the list of recipients */
    if (!
	(rcpt_keys =
	 gpgme_build_recipients(ctx, recipients, trust_all_keys, parent,
				error))) {
	gpgme_release(ctx);
	return -1;
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
	g_set_error_from_gpgme(error, err,
			       _("could not get data from stream"));
	release_keylist(rcpt_keys);
	gpgme_release(ctx);
	return -1;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&crypt, &cbs,
				 ostream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
			       _("could not create new data object"));
	release_keylist(rcpt_keys);
	gpgme_data_release(plain);
	gpgme_release(ctx);
	return -1;
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
	    g_set_error_from_gpgme(error, err,
				   _("signing and encryption failed"));
	else
	    g_set_error_from_gpgme(error, err, _("encryption failed"));
	return -1;
    } else
	return 0;
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
		       gpgme_protocol_t protocol, GtkWindow * parent,
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
    if ((err =
	 gpgme_new_with_protocol(&ctx, protocol, parent,
				 error)) != GPG_ERR_NO_ERROR)
	return NULL;

    /* create the data streams */
    if ((err =
	 gpgme_data_new_from_cbs(&crypt_data, &cbs,
				 crypted)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
			       _("could not get data from stream"));
	gpgme_release(ctx);
	return NULL;
    }
    if ((err =
	 gpgme_data_new_from_cbs(&plain_data, &cbs,
				 plain)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err,
			       _("could not create new data object"));
	gpgme_data_release(crypt_data);
	gpgme_release(ctx);
	return NULL;
    }

    /* try to decrypt */
    if ((err =
	 gpgme_op_decrypt_verify(ctx, crypt_data,
				 plain_data)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("decryption failed"));
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

/* ---- local stuff ---------------------------------------------------- */

#define UTF8_VALID_STR(s)						\
    do {								\
	if ((s) && !g_utf8_validate(s, -1, NULL)) {			\
	    gsize bwr;							\
	    gchar * newstr = g_locale_to_utf8(s, -1, NULL, &bwr, NULL);	\
									\
	    g_free(s);							\
	    s = newstr;							\
	}								\
    } while (0)


/*
 * set a GError form GpgME information
 */
static void
g_set_error_from_gpgme(GError ** error, gpgme_error_t gpgme_err,
		       const gchar * message)
{
    gchar *errstr;
    gchar *srcstr;

    if (!error)
	return;

    srcstr = g_strdup(gpgme_strsource(gpgme_err));
    UTF8_VALID_STR(srcstr);
    errstr = g_strdup(gpgme_strerror(gpgme_err));
    UTF8_VALID_STR(errstr);
    g_set_error(error, GPGME_ERROR_QUARK, gpgme_err, "%s: %s: %s", srcstr,
		message, errstr);
    g_free(srcstr);
    g_free(errstr);
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


/*
 * create a GpgME context for the passed protocol
 */
static gpgme_error_t
gpgme_new_with_protocol(gpgme_ctx_t * ctx, gpgme_protocol_t protocol,
			GtkWindow * parent, GError ** error)
{
    gpgme_error_t err;


    /* create the GpgME context */
    if ((err = gpgme_new(ctx)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not create context"));
    } else {
	if ((err = gpgme_set_protocol(*ctx, protocol)) != GPG_ERR_NO_ERROR) {
	    gchar *errmsg =
		g_strdup_printf(_("could not set protocol '%s'"),
				gpgme_get_protocol_name(protocol));

	    g_set_error_from_gpgme(error, err, errmsg);
	    g_free(errmsg);
	    gpgme_release(*ctx);
	} else {
	    if (protocol == GPGME_PROTOCOL_OpenPGP)
		gpgme_set_passphrase_cb(*ctx, gpgme_passphrase_cb, parent);
	}
    }

    return err;
}


/*
 * Get a key for name. If secret_only is set, choose only secret (private)
 * keys (signing). Otherwise, choose only public keys (encryption).
 * If multiple keys would match, call the key selection CB (if present). If
 * no matching key could be found or if any error occurs, return NULL and
 * set error.
 */
#define KEY_IS_OK(k)   (!((k)->expired || (k)->revoked || \
                          (k)->disabled || (k)->invalid))
static gpgme_key_t
get_key_from_name(gpgme_ctx_t ctx, const gchar * name, gboolean secret,
		  gboolean accept_all, GtkWindow * parent, GError ** error)
{
    GList *keys = NULL;
    gpgme_key_t key;
    gpgme_error_t err;
    gboolean found_bad;
    time_t now = time(NULL);

    /* let gpgme list keys */
    if ((err =
	 gpgme_op_keylist_start(ctx, name, secret)) != GPG_ERR_NO_ERROR) {
	gchar *msg =
	    g_strdup_printf(_("could not list keys for \"%s\""), name);

	g_set_error_from_gpgme(error, err, msg);
	g_free(msg);
	return NULL;
    }

    found_bad = FALSE;
    while ((err = gpgme_op_keylist_next(ctx, &key)) == GPG_ERR_NO_ERROR) {
	/* check if this key and the relevant subkey are usable */
	if (KEY_IS_OK(key)) {
	    gpgme_subkey_t subkey = key->subkeys;

	    while (subkey && ((secret && !subkey->can_sign) ||
			      (!secret && !subkey->can_encrypt)))
		subkey = subkey->next;

	    if (subkey && KEY_IS_OK(subkey) &&
		(subkey->expires == 0 || subkey->expires > now))
		keys = g_list_append(keys, key);
	    else
		found_bad = TRUE;
	} else
	    found_bad = TRUE;
    }

    if (gpg_err_code(err) != GPG_ERR_EOF) {
	gchar *msg =
	    g_strdup_printf(_("could not list keys for \"%s\""), name);

	g_set_error_from_gpgme(error, err, msg);
	g_free(msg);
	gpgme_op_keylist_end(ctx);
	g_list_foreach(keys, (GFunc) gpgme_key_unref, NULL);
	g_list_free(keys);
	return NULL;
    }
    gpgme_op_keylist_end(ctx);

    if (!keys) {
	if (error) {
	    if (strchr(name, '@')) {
		if (found_bad)
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_
				("%s: a key for %s is present, but it is expired, disabled, revoked or invalid"),
				"gmime-gpgme", name);
		else
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_("%s: could not find a key for %s"),
				"gmime-gpgme", name);
	    } else {
		if (found_bad)
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_
				("%s: a key with id %s is present, but it is expired, disabled, revoked or invalid"),
				"gmime-gpgme", name);
		else
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_("%s: could not find a key with id %s"),
				"gmime-gpgme", name);
	    }
	}
	return NULL;
    }

    /* let the user select a key from the list if there is more than one */
    if (g_list_length(keys) > 1) {
	if (select_key_cb)
	    key =
		select_key_cb(name, secret, keys, gpgme_get_protocol(ctx),
			      parent);
	else {
	    if (error)
		g_set_error(error, GPGME_ERROR_QUARK,
			    GPG_ERR_KEY_SELECTION,
			    _("%s: multiple keys for %s"), "gmime-gpgme",
			    name);
	    key = NULL;
	}
	if (key)
	    gpgme_key_ref(key);
	g_list_foreach(keys, (GFunc) gpgme_key_unref, NULL);
    } else
	key = (gpgme_key_t) keys->data;
    g_list_free(keys);

    /* OpenPGP: ask the user if a low-validity key should be trusted for
     * encryption */
    // FIXME - shouldn't we do the same for S/MIME?
    if (key && !secret && !accept_all
	&& gpgme_get_protocol(ctx) == GPGME_PROTOCOL_OpenPGP) {
	gpgme_user_id_t uid = key->uids;
	gchar *upcase_name = g_ascii_strup(name, -1);
	gboolean found = FALSE;

	while (!found && uid) {
	    /* check the email field which may or may not be present */
	    if (uid->email && !g_ascii_strcasecmp(uid->email, name))
		found = TRUE;
	    else {
		/* no email or no match, check the uid */
		gchar *upcase_uid = g_ascii_strup(uid->uid, -1);

		if (strstr(upcase_uid, upcase_name))
		    found = TRUE;
		else
		    uid = uid->next;
		g_free(upcase_uid);
	    }
	}
	g_free(upcase_name);

	/* ask the user if a low-validity key shall be used */
	if (uid && uid->validity < GPGME_VALIDITY_FULL) {
	    if (!accept_low_trust_cb
		|| !accept_low_trust_cb(name, uid, parent)) {
		gpgme_key_unref(key);
		key = NULL;
		if (error)
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_("%s: insufficient validity for uid %s"),
				"gmime-gpgme", name);
	    }
	}
    }

    return key;
}


/*
 * Add signer to ctx's list of signers and return TRUE on success or FALSE
 * on error.
 */
static gboolean
gpgme_add_signer(gpgme_ctx_t ctx, const gchar * signer, GtkWindow * parent,
		 GError ** error)
{
    gpgme_key_t key = NULL;

    /* note: private (secret) key has never low trust... */
    if (!
	(key = get_key_from_name(ctx, signer, TRUE, FALSE, parent, error)))
	return FALSE;

    /* set the key (the previous operation guaranteed that it exists, no
     * need 2 check return values...) */
    gpgme_signers_add(ctx, key);
    gpgme_key_unref(key);

    return TRUE;
}


/*
 * Build a NULL-terminated array of keys for all recipients in rcpt_list
 * and return it. The caller has to take care that it's released. If
 * something goes wrong, NULL is returned.
 */
static gpgme_key_t *
gpgme_build_recipients(gpgme_ctx_t ctx, GPtrArray * rcpt_list,
		       gboolean accept_low_trust, GtkWindow * parent,
		       GError ** error)
{
    gpgme_key_t *rcpt = g_new0(gpgme_key_t, rcpt_list->len + 1);
    guint num_rcpts;

    /* try to find the public key for every recipient */
    for (num_rcpts = 0; num_rcpts < rcpt_list->len; num_rcpts++) {
	gchar *name = (gchar *) g_ptr_array_index(rcpt_list, num_rcpts);
	gpgme_key_t key;

	if (!
	    (key =
	     get_key_from_name(ctx, name, FALSE, accept_low_trust, parent,
			       error))) {
	    release_keylist(rcpt);
	    return NULL;
	}

	/* set the recipient */
	rcpt[num_rcpts] = key;
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
