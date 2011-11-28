/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004-2009 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "gmime-gpgme-context.h"

#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <gmime/gmime.h>
#ifdef HAVE_GMIME_2_5_7
#include <gmime/gmime-certificate.h>
#endif /* HAVE_GMIME_2_5_7 */
#include <gpgme.h>
#include <time.h>
#include <glib/gi18n.h>

#define GPGME_ERROR_QUARK (g_quark_from_static_string ("gmime-gpgme"))

static void g_mime_gpgme_context_class_init(GMimeGpgmeContextClass *
					    klass);
static void g_mime_gpgme_context_init(GMimeGpgmeContext * ctx,
				      GMimeGpgmeContextClass * klass);
static void g_mime_gpgme_context_finalize(GObject * object);
static gboolean g_mime_gpgme_context_check_protocol(GMimeGpgmeContextClass
						    * klass,
						    gpgme_protocol_t
						    protocol,
						    GError ** error);

#ifndef HAVE_GMIME_2_5_7
static GMimeCipherHash g_mime_gpgme_hash_id(GMimeCipherContext * ctx,
					    const char *hash);

static const char *g_mime_gpgme_hash_name(GMimeCipherContext * ctx,
					  GMimeCipherHash hash);

static int g_mime_gpgme_sign(GMimeCipherContext * ctx, const char *userid,
			     GMimeCipherHash hash, GMimeStream * istream,
			     GMimeStream * ostream, GError ** err);

static GMimeSignatureValidity *g_mime_gpgme_verify(GMimeCipherContext * ctx,
						   GMimeCipherHash hash,
						   GMimeStream * istream,
						   GMimeStream * sigstream,
						   GError ** err);

static int g_mime_gpgme_encrypt(GMimeCipherContext * ctx, gboolean sign,
				const char *userid, GPtrArray * recipients,
				GMimeStream * istream,
				GMimeStream * ostream, GError ** err);

static GMimeSignatureValidity *g_mime_gpgme_decrypt(GMimeCipherContext *
                                                    ctx,
                                                    GMimeStream * istream,
                                                    GMimeStream * ostream,
                                                    GError ** err);
#else /* HAVE_GMIME_2_5_7 */
static GMimeDigestAlgo g_mime_gpgme_digest_id(GMimeCryptoContext * ctx,
                                              const char *hash);

static const char *g_mime_gpgme_digest_name(GMimeCryptoContext * ctx,
                                            GMimeDigestAlgo hash);

static const char
    *g_mime_gpgme_get_signature_protocol(GMimeCryptoContext * context);
static const char
    *g_mime_gpgme_get_encryption_protocol(GMimeCryptoContext * context);
static const char
    *g_mime_gpgme_get_key_exchange_protocol(GMimeCryptoContext * context);

static int g_mime_gpgme_sign(GMimeCryptoContext * ctx,
                             const char * userid,
			     GMimeDigestAlgo hash,
                             GMimeStream * istream,
			     GMimeStream * ostream,
                             GError ** err);

static GMimeSignatureList *g_mime_gpgme_verify(GMimeCryptoContext * ctx,
                                               GMimeDigestAlgo hash,
                                               GMimeStream * istream,
                                               GMimeStream * sigstream,
                                               GError ** err);

static int g_mime_gpgme_encrypt(GMimeCryptoContext * ctx,
                                gboolean sign,
				const char *userid,
                                GMimeDigestAlgo digest,
                                GPtrArray * recipients,
				GMimeStream * istream,
				GMimeStream * ostream, GError ** err);

static GMimeDecryptResult *g_mime_gpgme_decrypt(GMimeCryptoContext * ctx,
                                                GMimeStream * istream,
                                                GMimeStream * ostream,
                                                GError ** err);
#endif /* HAVE_GMIME_2_5_7 */


/* internal passphrase callback */
static gpgme_error_t g_mime_session_passphrase(void *HOOK,
					       const char *UID_HINT,
					       const char *PASSPHRASE_INFO,
					       int PREV_WAS_BAD, int FD);


/* callbacks for gpgme file handling */
static ssize_t g_mime_gpgme_stream_rd(GMimeStream * stream, void *buffer,
				      size_t size);
static ssize_t g_mime_gpgme_stream_wr(GMimeStream * stream, void *buffer,
				      size_t size);
static void cb_data_release(void *handle);


/* some helper functions */
static gboolean gpgme_add_signer(GMimeGpgmeContext * ctx,
				 const gchar * signer, GError ** error);
static gpgme_key_t get_key_from_name(GMimeGpgmeContext * ctx,
				     const gchar * name,
				     gboolean secret_only,
				     GError ** error);
static gpgme_key_t *gpgme_build_recipients(GMimeGpgmeContext * ctx,
					   GPtrArray * rcpt_list,
					   GError ** error);
static void release_keylist(gpgme_key_t * keylist);
static void g_set_error_from_gpgme(GError ** error, gpgme_error_t gpgme_err,
				   const gchar * message);


#ifndef HAVE_GMIME_2_5_7
static GMimeCipherContextClass *parent_class = NULL;
#else /* HAVE_GMIME_2_5_7 */
static GMimeCryptoContextClass *parent_class = NULL;
#endif /* HAVE_GMIME_2_5_7 */


GType
g_mime_gpgme_context_get_type(void)
{
    static GType type = 0;

    if (!type) {
	static const GTypeInfo info = {
	    sizeof(GMimeGpgmeContextClass),
	    NULL,		/* base_class_init */
	    NULL,		/* base_class_finalize */
	    (GClassInitFunc) g_mime_gpgme_context_class_init,
	    NULL,		/* class_finalize */
	    NULL,		/* class_data */
	    sizeof(GMimeGpgmeContext),
	    0,			/* n_preallocs */
	    (GInstanceInitFunc) g_mime_gpgme_context_init,
	};

	type =
#ifndef HAVE_GMIME_2_5_7
	    g_type_register_static(GMIME_TYPE_CIPHER_CONTEXT,
				   "GMimeGpgmeContext", &info, 0);
#else /* HAVE_GMIME_2_5_7 */
	    g_type_register_static(GMIME_TYPE_CRYPTO_CONTEXT,
				   "GMimeGpgmeContext", &info, 0);
#endif /* HAVE_GMIME_2_5_7 */
    }

    return type;
}


static void
g_mime_gpgme_context_class_init(GMimeGpgmeContextClass * klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
#ifndef HAVE_GMIME_2_5_7
    GMimeCipherContextClass *cipher_class =
	GMIME_CIPHER_CONTEXT_CLASS(klass);
#else /* HAVE_GMIME_2_5_7 */
    GMimeCryptoContextClass *crypto_class =
	GMIME_CRYPTO_CONTEXT_CLASS(klass);
#endif /* HAVE_GMIME_2_5_7 */

    parent_class = g_type_class_ref(G_TYPE_OBJECT);

    object_class->finalize = g_mime_gpgme_context_finalize;

#ifndef HAVE_GMIME_2_5_7
    cipher_class->hash_id = g_mime_gpgme_hash_id;
    cipher_class->hash_name = g_mime_gpgme_hash_name;
    cipher_class->sign = g_mime_gpgme_sign;
    cipher_class->verify = g_mime_gpgme_verify;
    cipher_class->encrypt = g_mime_gpgme_encrypt;
    cipher_class->decrypt = g_mime_gpgme_decrypt;
#else /* HAVE_GMIME_2_5_7 */
    crypto_class->digest_id = g_mime_gpgme_digest_id;
    crypto_class->digest_name = g_mime_gpgme_digest_name;
    crypto_class->get_signature_protocol =
        g_mime_gpgme_get_signature_protocol;
    crypto_class->get_encryption_protocol =
        g_mime_gpgme_get_encryption_protocol;
    crypto_class->get_key_exchange_protocol =
        g_mime_gpgme_get_key_exchange_protocol;
    crypto_class->sign = g_mime_gpgme_sign;
    crypto_class->verify = g_mime_gpgme_verify;
    crypto_class->encrypt = g_mime_gpgme_encrypt;
    crypto_class->decrypt = g_mime_gpgme_decrypt;
#endif /* HAVE_GMIME_2_5_7 */

    if (gpgme_engine_check_version(GPGME_PROTOCOL_OpenPGP) ==
	GPG_ERR_NO_ERROR)
	klass->has_proto_openpgp = TRUE;
    else
	klass->has_proto_openpgp = FALSE;
    if (gpgme_engine_check_version(GPGME_PROTOCOL_CMS) == GPG_ERR_NO_ERROR)
	klass->has_proto_cms = TRUE;
    else
	klass->has_proto_cms = FALSE;
}


static void
g_mime_gpgme_context_init(GMimeGpgmeContext * ctx,
			  GMimeGpgmeContextClass * klass)
{
    ctx->singlepart_mode = FALSE;
    ctx->always_trust_uid = FALSE;
    ctx->micalg = NULL;
    ctx->sig_state = NULL;
    ctx->key_select_cb = NULL;
    ctx->key_trust_cb = NULL;
    ctx->passphrase_cb = NULL;
}


static void
g_mime_gpgme_context_finalize(GObject * object)
{
    GMimeGpgmeContext *ctx = (GMimeGpgmeContext *) object;

    gpgme_release(ctx->gpgme_ctx);
    g_free(ctx->micalg);
    ctx->micalg = NULL;
    if (ctx->sig_state) {
	g_object_unref(G_OBJECT(ctx->sig_state));
	ctx->sig_state = NULL;
    }

#if !defined(HAVE_GMIME_2_6)
#ifndef HAVE_GMIME_2_5_7
    g_object_unref(GMIME_CIPHER_CONTEXT(ctx)->session);
#else /* HAVE_GMIME_2_5_7 */
    g_object_unref(GMIME_CRYPTO_CONTEXT(ctx)->session);
#endif /* HAVE_GMIME_2_5_7 */
#endif                          /* HAVE_GMIME_2_6 */

    G_OBJECT_CLASS(parent_class)->finalize(object);
}


/*
 * Convert a hash algorithm name to a number
 */
#ifndef HAVE_GMIME_2_5_7
static GMimeCipherHash
g_mime_gpgme_hash_id(GMimeCipherContext * ctx, const char *hash)
#else /* HAVE_GMIME_2_5_7 */
static GMimeDigestAlgo
g_mime_gpgme_digest_id(GMimeCryptoContext * ctx, const char *hash)
#endif /* HAVE_GMIME_2_5_7 */
{
#ifndef HAVE_GMIME_2_5_7
    if (hash == NULL)
	return GMIME_CIPHER_HASH_DEFAULT;
#else /* HAVE_GMIME_2_5_7 */
    if (hash == NULL)
	return GMIME_DIGEST_ALGO_DEFAULT;
#endif /* HAVE_GMIME_2_5_7 */

    if (!g_ascii_strcasecmp(hash, "pgp-"))
	hash += 4;

#ifndef HAVE_GMIME_2_5_7
    if (!g_ascii_strcasecmp(hash, "md2"))
	return GMIME_CIPHER_HASH_MD2;
    else if (!g_ascii_strcasecmp(hash, "md5"))
	return GMIME_CIPHER_HASH_MD5;
    else if (!g_ascii_strcasecmp(hash, "sha1"))
	return GMIME_CIPHER_HASH_SHA1;
    else if (!g_ascii_strcasecmp(hash, "ripemd160"))
	return GMIME_CIPHER_HASH_RIPEMD160;
    else if (!g_ascii_strcasecmp(hash, "tiger192"))
	return GMIME_CIPHER_HASH_TIGER192;
    else if (!g_ascii_strcasecmp(hash, "haval-5-160"))
	return GMIME_CIPHER_HASH_HAVAL5160;

    return GMIME_CIPHER_HASH_DEFAULT;
#else /* HAVE_GMIME_2_5_7 */
    if (!g_ascii_strcasecmp(hash, "md2"))
	return GMIME_DIGEST_ALGO_MD2;
    else if (!g_ascii_strcasecmp(hash, "md5"))
	return GMIME_DIGEST_ALGO_MD5;
    else if (!g_ascii_strcasecmp(hash, "sha1"))
	return GMIME_DIGEST_ALGO_SHA1;
    else if (!g_ascii_strcasecmp(hash, "ripemd160"))
	return GMIME_DIGEST_ALGO_RIPEMD160;
    else if (!g_ascii_strcasecmp(hash, "tiger192"))
	return GMIME_DIGEST_ALGO_TIGER192;
    else if (!g_ascii_strcasecmp(hash, "haval-5-160"))
	return GMIME_DIGEST_ALGO_HAVAL5160;

    return GMIME_DIGEST_ALGO_DEFAULT;
#endif /* HAVE_GMIME_2_5_7 */
}


/*
 * Convert a hash algorithm number to a string
 */
static const char *
#ifndef HAVE_GMIME_2_5_7
g_mime_gpgme_hash_name(GMimeCipherContext * context, GMimeCipherHash hash)
#else /* HAVE_GMIME_2_5_7 */
g_mime_gpgme_digest_name(GMimeCryptoContext * context, GMimeDigestAlgo hash)
#endif /* HAVE_GMIME_2_5_7 */
{
    GMimeGpgmeContext *ctx = GMIME_GPGME_CONTEXT(context);
    char *p;

    g_return_val_if_fail(ctx, NULL);
    g_return_val_if_fail(ctx->gpgme_ctx, NULL);

    /* note: this is only a subset of the hash algorithms gpg(me) supports */
#ifndef HAVE_GMIME_2_5_7
    switch (hash) {
    case GMIME_CIPHER_HASH_MD2:
	p = "pgp-md2";
	break;
    case GMIME_CIPHER_HASH_MD5:
	p = "pgp-md5";
	break;
    case GMIME_CIPHER_HASH_SHA1:
	p = "pgp-sha1";
	break;
    case GMIME_CIPHER_HASH_RIPEMD160:
	p = "pgp-ripemd160";
	break;
    case GMIME_CIPHER_HASH_TIGER192:
	p = "pgp-tiger192";
	break;
    case GMIME_CIPHER_HASH_HAVAL5160:
	p = "pgp-haval-5-160";
	break;
#else /* HAVE_GMIME_2_5_7 */
    switch (hash) {
    case GMIME_DIGEST_ALGO_MD2:
	p = "pgp-md2";
	break;
    case GMIME_DIGEST_ALGO_MD5:
	p = "pgp-md5";
	break;
    case GMIME_DIGEST_ALGO_SHA1:
	p = "pgp-sha1";
	break;
    case GMIME_DIGEST_ALGO_RIPEMD160:
	p = "pgp-ripemd160";
	break;
    case GMIME_DIGEST_ALGO_TIGER192:
	p = "pgp-tiger192";
	break;
    case GMIME_DIGEST_ALGO_HAVAL5160:
	p = "pgp-haval-5-160";
	break;
#endif /* HAVE_GMIME_2_5_7 */
    default:
	if (!(p = ctx->micalg))
	    return p;
    }

    /* S/MIME (RFC2633) doesn't like the leading "pgp-" */
    if (gpgme_get_protocol(ctx->gpgme_ctx) == GPGME_PROTOCOL_CMS)
	p += 4;

    return p;
}

#ifdef HAVE_GMIME_2_5_7
static const char *
g_mime_gpgme_get_signature_protocol(GMimeCryptoContext * context)
{
    GMimeGpgmeContext *ctx = GMIME_GPGME_CONTEXT(context);
    return ctx->sign_protocol;
}

static const char *
g_mime_gpgme_get_encryption_protocol(GMimeCryptoContext * context)
{
    GMimeGpgmeContext *ctx = GMIME_GPGME_CONTEXT(context);
    return ctx->encrypt_protocol;
}

static const char *
g_mime_gpgme_get_key_exchange_protocol(GMimeCryptoContext * context)
{
    GMimeGpgmeContext *ctx = GMIME_GPGME_CONTEXT(context);
    return ctx->key_protocol;
}

#endif /* HAVE_GMIME_2_5_7 */

/*
 * Wrapper to convert the passphrase returned from the gmime session to gpgme.
 */
static gpgme_error_t
g_mime_session_passphrase(void *HOOK, const char *UID_HINT,
			  const char *PASSPHRASE_INFO, int PREV_WAS_BAD,
			  int FD)
{
#ifndef HAVE_GMIME_2_5_7
    GMimeCipherContext *ctx = GMIME_CIPHER_CONTEXT(HOOK);
#else /* HAVE_GMIME_2_5_7 */
    GMimeCryptoContext *ctx = GMIME_CRYPTO_CONTEXT(HOOK);
#endif /* HAVE_GMIME_2_5_7 */
#if defined(HAVE_GMIME_2_6)
    GMimeStream *stream;
    gboolean rc;

    stream = g_mime_stream_fs_new(FD);
    rc = (*ctx->request_passwd) (ctx, UID_HINT, PASSPHRASE_INFO,
                                 PREV_WAS_BAD, stream, NULL);
    g_object_unref(stream);

    return rc ? GPG_ERR_NO_ERROR : GPG_ERR_CANCELED;
#else                           /* HAVE_GMIME_2_6 */
    GMimeSession *session = ctx->session;
    gchar *msg, *passphrase;

    if (PREV_WAS_BAD)
	msg =
	    g_strdup_printf(_
			    ("The passphrase for this key was bad, please try again!\n\nKey: %s"),
			    UID_HINT);
    else
	msg =
	    g_strdup_printf(_
			    ("Please enter the passphrase for the secret key!\n\nKey: %s"),
			    UID_HINT);
    passphrase =
	g_mime_session_request_passwd(session, PASSPHRASE_INFO, TRUE, msg,
				      NULL);
    if (passphrase) {
	if (write(FD, passphrase, strlen(passphrase)) < 0
	    || write(FD, "\n", 1) < 0)
            perror(__func__);
	g_mime_session_forget_passwd(session, msg, NULL);
	return GPG_ERR_NO_ERROR;
    } else {
	if (write(FD, "\n", 1) < 0)
            perror(__func__);
	return GPG_ERR_CANCELED;
    }
#endif                          /* HAVE_GMIME_2_6 */
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
 * Sign istream with the key userid and write the detached signature (standard
 * mode) or the cleartext signed input (RFC 2440 mode) to ostream. Return 0 on
 * success and -1 on error. In the latter case, set error to the reason. Note
 * that gpgme selects the hash algorithm automatically, so we don't use the
 * arg, but set the value in the context.
 */
static int
#ifndef HAVE_GMIME_2_5_7
g_mime_gpgme_sign(GMimeCipherContext * context, const char *userid,
		  GMimeCipherHash hash, GMimeStream * istream,
		  GMimeStream * ostream, GError ** error)
#else /* HAVE_GMIME_2_5_7 */
g_mime_gpgme_sign(GMimeCryptoContext * context, const char *userid,
		  GMimeDigestAlgo hash, GMimeStream * istream,
		  GMimeStream * ostream, GError ** error)
#endif /* HAVE_GMIME_2_5_7 */
{
    GMimeGpgmeContext *ctx = (GMimeGpgmeContext *) context;
    gpgme_sig_mode_t sig_mode;
    gpgme_ctx_t gpgme_ctx;
    gpgme_protocol_t protocol;
    gpgme_error_t err;
    gpgme_data_t in, out;
    gpgme_sign_result_t sign_result;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* some paranoia checks */
    g_return_val_if_fail(ctx, -1);
    g_return_val_if_fail(ctx->gpgme_ctx, -1);
    gpgme_ctx = ctx->gpgme_ctx;
    protocol = gpgme_get_protocol(gpgme_ctx);

    /* set the signature mode */
    if (ctx->singlepart_mode) {
	if (protocol == GPGME_PROTOCOL_OpenPGP)
	    sig_mode = GPGME_SIG_MODE_CLEAR;
	else
	    sig_mode = GPGME_SIG_MODE_NORMAL;
    } else
	sig_mode = GPGME_SIG_MODE_DETACH;

    /* find the secret key for the "sign_for" address */
    if (!gpgme_add_signer(ctx, userid, error))
	return -1;

    /* OpenPGP signatures are ASCII armored */
    gpgme_set_armor(gpgme_ctx, protocol == GPGME_PROTOCOL_OpenPGP);

    /* set passphrase callback */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	if (ctx->passphrase_cb == GPGME_USE_GMIME_SESSION_CB)
	    gpgme_set_passphrase_cb(gpgme_ctx, g_mime_session_passphrase,
				    context);
	else
	    gpgme_set_passphrase_cb(gpgme_ctx, ctx->passphrase_cb,
				    context);
    } else
	gpgme_set_passphrase_cb(gpgme_ctx, NULL, context);

    /* create gpgme data objects */
    if ((err =
	 gpgme_data_new_from_cbs(&in, &cbs,
				 istream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not get data from stream"));
	return -1;
    }
    if ((err = gpgme_data_new_from_cbs(&out, &cbs,
				       ostream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not create new data object"));
	gpgme_data_release(in);
	return -1;
    }
    if ((err =
	 gpgme_op_sign(gpgme_ctx, in, out,
		       sig_mode)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("signing failed"));
	gpgme_data_release(out);
	gpgme_data_release(in);
	return -1;
    }

    /* get the info about the used hash algorithm */
    sign_result = gpgme_op_sign_result(gpgme_ctx);
    g_free(ctx->micalg);
    if (protocol == GPGME_PROTOCOL_OpenPGP)
	ctx->micalg =
	    g_strdup_printf("PGP-%s",
			    gpgme_hash_algo_name(sign_result->signatures->
						 hash_algo));
    else
	ctx->micalg =
	    g_strdup(gpgme_hash_algo_name
		     (sign_result->signatures->hash_algo));

    /* release gpgme data buffers */
    gpgme_data_release(in);
    gpgme_data_release(out);

    return 0;
}


#ifndef HAVE_GMIME_2_5_7
/*
 * In standard mode, verify that sigstream contains a detached signature for
 * istream. In single-part mode (RFC 2440, RFC 2633 application/pkcs7-mime),
 * istream contains clearsigned data, and sigstream will be filled with the
 * verified plaintext. The routine returns a validity object. More information
 * is saved in the context's signature object. On error error ist set accordingly.
 */
static GMimeSignatureValidity *
g_mime_gpgme_verify(GMimeCipherContext * context, GMimeCipherHash hash,
		    GMimeStream * istream, GMimeStream * sigstream,
		    GError ** error)
#else /* HAVE_GMIME_2_5_7 */
/*
 * In standard mode, verify that sigstream contains a detached signature for
 * istream. In single-part mode (RFC 2440, RFC 2633 application/pkcs7-mime),
 * istream contains clearsigned data, and sigstream will be filled with the
 * verified plaintext. The routine returns a GMimeSignatureList object.
 * More information is saved in the context's signature object.
 * On error error is set accordingly.
 */
static GMimeSignatureList *
g_mime_gpgme_verify(GMimeCryptoContext * context, GMimeDigestAlgo hash,
		    GMimeStream * istream, GMimeStream * sigstream,
		    GError ** error)
#endif /* HAVE_GMIME_2_5_7 */
{
    GMimeGpgmeContext *ctx = (GMimeGpgmeContext *) context;
    gpgme_ctx_t gpgme_ctx;
    gpgme_protocol_t protocol;
    gpgme_error_t err;
    gpgme_data_t msg, sig;
#ifndef HAVE_GMIME_2_5_7
    GMimeSignatureValidity *validity;
#else /* HAVE_GMIME_2_5_7 */
    GMimeSignatureList *list;
    GMimeSignature *signature;

#endif /* HAVE_GMIME_2_5_7 */
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* some paranoia checks */
    g_return_val_if_fail(ctx, NULL);
    g_return_val_if_fail(ctx->gpgme_ctx, NULL);
    gpgme_ctx = ctx->gpgme_ctx;
    protocol = gpgme_get_protocol(gpgme_ctx);

    /* create the message stream */
    if ((err = gpgme_data_new_from_cbs(&msg, &cbs, istream)) !=
	GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not get data from stream"));
	return NULL;
    }

    /* create data object for the detached signature stream or the "decrypted"
     * plaintext */
    if ((err = gpgme_data_new_from_cbs(&sig, &cbs, sigstream)) !=
	GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not get data from stream"));
	gpgme_data_release(msg);
	return NULL;
    }

    /* verify the signature */
    if (ctx->singlepart_mode)
	err = gpgme_op_verify(gpgme_ctx, msg, NULL, sig);
    else
	err = gpgme_op_verify(gpgme_ctx, sig, msg, NULL);
    if (err != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("signature verification failed"));
	ctx->sig_state = g_mime_gpgme_sigstat_new();
	ctx->sig_state->status = err;
	ctx->sig_state->protocol = protocol;
    } else
	ctx->sig_state =
	    g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx);

#ifndef HAVE_GMIME_2_5_7
    validity = g_mime_signature_validity_new();
    if (ctx->sig_state) {
	switch (ctx->sig_state->status)
	    {
	    case GPG_ERR_NO_ERROR:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_GOOD);
		break;
	    case GPG_ERR_NOT_SIGNED:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_NONE);
		break;
	    default:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_BAD);
	    }
    } else
	g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_UNKNOWN);
#else /* HAVE_GMIME_2_5_7 */
    list = g_mime_signature_list_new();
    signature = g_mime_signature_new();
    g_mime_signature_list_add(list, signature);

    if (ctx->sig_state) {
	switch (ctx->sig_state->status)
	    {
	    case GPG_ERR_NO_ERROR:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_GOOD);
		break;
	    case GPG_ERR_NOT_SIGNED:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_ERROR);
		g_mime_signature_set_errors(signature,
                                            GMIME_SIGNATURE_ERROR_NONE);
		break;
	    default:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_BAD);
	    }
    } else {
	g_mime_signature_set_status(signature,
                                    GMIME_SIGNATURE_STATUS_ERROR);
        g_mime_signature_set_errors(signature, GMIME_SIGNATURE_ERROR_NONE);
    }
#endif /* HAVE_GMIME_2_5_7 */

    /* release gmgme data buffers */
    gpgme_data_release(msg);
    gpgme_data_release(sig);

#ifndef HAVE_GMIME_2_5_7
    return validity;
#else /* HAVE_GMIME_2_5_7 */
    return list;
#endif /* HAVE_GMIME_2_5_7 */
}


/*
 * Encrypt istream to ostream for recipients. If sign is set, sign by userid.
 */
static int
#ifndef HAVE_GMIME_2_5_7
g_mime_gpgme_encrypt(GMimeCipherContext * context, gboolean sign,
		     const char *userid, GPtrArray * recipients,
		     GMimeStream * istream, GMimeStream * ostream,
		     GError ** error)
#else /* HAVE_GMIME_2_5_7 */
g_mime_gpgme_encrypt(GMimeCryptoContext * context, gboolean sign,
		     const char *userid,
                     GMimeDigestAlgo digest,
                     GPtrArray * recipients,
		     GMimeStream * istream, GMimeStream * ostream,
		     GError ** error)
#endif /* HAVE_GMIME_2_5_7 */
{
    GMimeGpgmeContext *ctx = (GMimeGpgmeContext *) context;
    gpgme_ctx_t gpgme_ctx;
    gpgme_key_t *rcpt;
    gpgme_protocol_t protocol;
    gpgme_error_t err;
    gpgme_data_t plain, crypt;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };

    /* some paranoia checks */
    g_return_val_if_fail(ctx, -1);
    g_return_val_if_fail(ctx->gpgme_ctx, -1);
    gpgme_ctx = ctx->gpgme_ctx;
    protocol = gpgme_get_protocol(gpgme_ctx);

    /* sign & encrypt is valid only for single-part OpenPGP */
    if (sign && (!ctx->singlepart_mode || protocol != GPGME_PROTOCOL_OpenPGP)) {
	if (error)
	    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_INV_ENGINE,
			_("combined signing and encryption is defined only for RFC 2440"));
	return -1;
    }

    /* if requested, find the secret key for "userid" */
    if (sign) {
	if (!gpgme_add_signer(ctx, userid, error))
	    return -1;

	/* set passphrase callback */
	if (protocol == GPGME_PROTOCOL_OpenPGP) {
	    if (ctx->passphrase_cb == GPGME_USE_GMIME_SESSION_CB)
		gpgme_set_passphrase_cb(gpgme_ctx,
					g_mime_session_passphrase,
					context);
	    else
		gpgme_set_passphrase_cb(gpgme_ctx, ctx->passphrase_cb,
					context);
	} else
	    gpgme_set_passphrase_cb(gpgme_ctx, NULL, context);
    }

    /* build the list of recipients */
    if (!(rcpt = gpgme_build_recipients(ctx, recipients, error)))
	return -1;

    /* create the data objects */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	gpgme_set_armor(gpgme_ctx, 1);
	gpgme_set_textmode(gpgme_ctx, ctx->singlepart_mode);
    }
    if ((err =
	 gpgme_data_new_from_cbs(&plain, &cbs,
				 istream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not get data from stream"));
	release_keylist(rcpt);
	return -1;
    }
    if ((err = gpgme_data_new_from_cbs(&crypt, &cbs, ostream)) !=
	GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not create new data object"));
	gpgme_data_release(plain);
	release_keylist(rcpt);
	return -1;
    }

    /* do the encrypt or sign and encrypt operation
     * Note: we set "always trust" here, as if we detected an untrusted key
     * earlier, the user already accepted it */
    if (sign)
	err = 
	    gpgme_op_encrypt_sign(gpgme_ctx, rcpt, GPGME_ENCRYPT_ALWAYS_TRUST,
				  plain, crypt);
    else
	err =
	    gpgme_op_encrypt(gpgme_ctx, rcpt, GPGME_ENCRYPT_ALWAYS_TRUST,
			     plain, crypt);

    gpgme_data_release(plain);
    gpgme_data_release(crypt);
    release_keylist(rcpt);
    if (err != GPG_ERR_NO_ERROR) {
	if (sign)
	    g_set_error_from_gpgme(error, err, _("signing and encryption failed"));
	else
	    g_set_error_from_gpgme(error, err, _("encryption failed"));
	return -1;
    } else
	return 0;
}


/*
 * Decrypt istream to ostream. In RFC 2440 mode, also try to check an included
 * signature (if any).
 */
#ifndef HAVE_GMIME_2_5_7
static GMimeSignatureValidity *
g_mime_gpgme_decrypt(GMimeCipherContext * context, GMimeStream * istream,
		     GMimeStream * ostream, GError ** error)
#else /* HAVE_GMIME_2_5_7 */
static GMimeDecryptResult *
g_mime_gpgme_decrypt(GMimeCryptoContext * context, GMimeStream * istream,
		     GMimeStream * ostream, GError ** error)
#endif /* HAVE_GMIME_2_5_7 */
{
    GMimeGpgmeContext *ctx = (GMimeGpgmeContext *) context;
    gpgme_ctx_t gpgme_ctx;
    gpgme_error_t err;
    gpgme_protocol_t protocol;
    gpgme_data_t plain, crypt;
    struct gpgme_data_cbs cbs = {
	(gpgme_data_read_cb_t) g_mime_gpgme_stream_rd,	/* read method */
	(gpgme_data_write_cb_t) g_mime_gpgme_stream_wr,	/* write method */
	NULL,			/* seek method */
	cb_data_release		/* release method */
    };
#ifndef HAVE_GMIME_2_5_7
    GMimeSignatureValidity *validity;
#else /* HAVE_GMIME_2_5_7 */
    GMimeDecryptResult *result;
    GMimeSignatureList *list;
    GMimeSignature *signature;
#endif /* HAVE_GMIME_2_5_7 */

    /* some paranoia checks */
    g_return_val_if_fail(ctx, NULL);
    g_return_val_if_fail(ctx->gpgme_ctx, NULL);
    gpgme_ctx = ctx->gpgme_ctx;
    protocol = gpgme_get_protocol(gpgme_ctx);

    /* set passphrase callback (only OpenPGP) */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	if (ctx->passphrase_cb == GPGME_USE_GMIME_SESSION_CB)
	    gpgme_set_passphrase_cb(gpgme_ctx, g_mime_session_passphrase,
				    context);
	else
	    gpgme_set_passphrase_cb(gpgme_ctx, ctx->passphrase_cb,
				    context);
    } else
	gpgme_set_passphrase_cb(gpgme_ctx, NULL, context);

    /* create the data streams */
    if ((err =
	 gpgme_data_new_from_cbs(&crypt, &cbs,
				 istream)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not get data from stream"));
	return NULL;
    }
    if ((err = gpgme_data_new_from_cbs(&plain, &cbs, ostream)) !=
	GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not create new data object"));
	gpgme_data_release(crypt);
	return NULL;
    }

    /* try to decrypt */
    if ((err =
	 gpgme_op_decrypt_verify(gpgme_ctx, crypt,
				 plain)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("decryption failed"));
	gpgme_data_release(plain);
	gpgme_data_release(crypt);
	return NULL;
    }
    gpgme_data_release(plain);
    gpgme_data_release(crypt);

    /* try to get information about the signature (if any) */
    ctx->sig_state = g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx);

#ifndef HAVE_GMIME_2_5_7
    validity = g_mime_signature_validity_new();
    if (ctx->sig_state) {
	switch (ctx->sig_state->status)
	    {
	    case GPG_ERR_NO_ERROR:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_GOOD);
		break;
	    case GPG_ERR_NOT_SIGNED:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_UNKNOWN);
		break;
	    default:
		g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_BAD);
	    }
    } else
	g_mime_signature_validity_set_status(validity, GMIME_SIGNATURE_STATUS_UNKNOWN);

    return validity;
#else /* HAVE_GMIME_2_5_7 */
    list = g_mime_signature_list_new();
    signature = g_mime_signature_new();
    g_mime_signature_list_add(list, signature);
    result = g_mime_decrypt_result_new();
    g_mime_decrypt_result_set_signatures(result, list);

    if (ctx->sig_state) {
	switch (ctx->sig_state->status)
	    {
	    case GPG_ERR_NO_ERROR:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_GOOD);
		break;
	    case GPG_ERR_NOT_SIGNED:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_ERROR);
		g_mime_signature_set_errors(signature,
                                            GMIME_SIGNATURE_ERROR_NONE);
		break;
	    default:
		g_mime_signature_set_status(signature,
                                            GMIME_SIGNATURE_STATUS_BAD);
	    }
    } else {
	g_mime_signature_set_status(signature,
                                    GMIME_SIGNATURE_STATUS_ERROR);
        g_mime_signature_set_errors(signature, GMIME_SIGNATURE_ERROR_NONE);
    }

    return result;
#endif /* HAVE_GMIME_2_5_7 */
}


#ifndef HAVE_GMIME_2_5_7
/*
 * Create a new gpgme cipher context with protocol. If anything fails, return
 * NULL and set error.
 */
#else /* HAVE_GMIME_2_5_7 */
/*
 * Create a new gpgme crypto context with protocol. If anything fails, return
 * NULL and set error.
 */
#endif /* HAVE_GMIME_2_5_7 */
#ifndef HAVE_GMIME_2_5_7
GMimeCipherContext *
#else /* HAVE_GMIME_2_5_7 */
GMimeCryptoContext *
#endif /* HAVE_GMIME_2_5_7 */
#if defined(HAVE_GMIME_2_6)
g_mime_gpgme_context_new(GMimePasswordRequestFunc request_passwd,
                         gpgme_protocol_t protocol, GError ** error)
#else                           /* HAVE_GMIME_2_6 */
g_mime_gpgme_context_new(GMimeSession * session,
			 gpgme_protocol_t protocol, GError ** error)
#endif                          /* HAVE_GMIME_2_6 */
{
#ifndef HAVE_GMIME_2_5_7
    GMimeCipherContext *cipher;
#else /* HAVE_GMIME_2_5_7 */
    GMimeCryptoContext *crypto;
#endif /* HAVE_GMIME_2_5_7 */
    GMimeGpgmeContext *ctx;
    gpgme_error_t err;
    gpgme_ctx_t gpgme_ctx;

#if defined(HAVE_GMIME_2_6)
    g_return_val_if_fail(request_passwd, NULL);
#else                           /* HAVE_GMIME_2_6 */
    g_return_val_if_fail(GMIME_IS_SESSION(session), NULL);
#endif                          /* HAVE_GMIME_2_6 */

    /* creating the gpgme context may fail, so do this first to get the info */
    if ((err = gpgme_new(&gpgme_ctx)) != GPG_ERR_NO_ERROR) {
	g_set_error_from_gpgme(error, err, _("could not create context"));
	return NULL;
    }

#ifndef HAVE_GMIME_2_5_7
    /* create the cipher context */
#else /* HAVE_GMIME_2_5_7 */
    /* create the crypto context */
#endif /* HAVE_GMIME_2_5_7 */
    ctx = g_object_new(GMIME_TYPE_GPGME_CONTEXT, NULL, NULL);
    if (!ctx) {
	gpgme_release(gpgme_ctx);
	return NULL;
    } else
	ctx->gpgme_ctx = gpgme_ctx;
#ifndef HAVE_GMIME_2_5_7
    cipher = (GMimeCipherContext *) ctx;
#else /* HAVE_GMIME_2_5_7 */
    crypto = (GMimeCryptoContext *) ctx;
#endif /* HAVE_GMIME_2_5_7 */

    /* check if the requested protocol is available */
    if (!g_mime_gpgme_context_check_protocol
	(GMIME_GPGME_CONTEXT_GET_CLASS(ctx), protocol, error)) {
	gpgme_release(gpgme_ctx);
	g_object_unref(G_OBJECT(ctx));
	return NULL;
    }

    /* setup according to requested protocol */
#if defined(HAVE_GMIME_2_6)
#ifndef HAVE_GMIME_2_5_7
    cipher->request_passwd = request_passwd;
#else /* HAVE_GMIME_2_5_7 */
    crypto->request_passwd = request_passwd;
#endif /* HAVE_GMIME_2_5_7 */
#else                           /* HAVE_GMIME_2_6 */
#ifndef HAVE_GMIME_2_5_7
    cipher->session = session;
#else /* HAVE_GMIME_2_5_7 */
    crypto->session = session;
#endif /* HAVE_GMIME_2_5_7 */
    g_object_ref(session);
#endif                          /* HAVE_GMIME_2_6 */
    gpgme_set_protocol(gpgme_ctx, protocol);
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
#ifndef HAVE_GMIME_2_5_7
	cipher->sign_protocol = "application/pgp-signature";
	cipher->encrypt_protocol = "application/pgp-encrypted";
	cipher->key_protocol = NULL;	/* FIXME */
#else /* HAVE_GMIME_2_5_7 */
	ctx->sign_protocol = "application/pgp-signature";
	ctx->encrypt_protocol = "application/pgp-encrypted";
	ctx->key_protocol = NULL;	/* FIXME */
#endif /* HAVE_GMIME_2_5_7 */
    } else {
#ifndef HAVE_GMIME_2_5_7
	cipher->sign_protocol = "application/pkcs7-signature";
	cipher->encrypt_protocol = "application/pkcs7-mime";
	cipher->key_protocol = NULL;	/* FIXME */
#else /* HAVE_GMIME_2_5_7 */
	ctx->sign_protocol = "application/pkcs7-signature";
	ctx->encrypt_protocol = "application/pkcs7-mime";
	ctx->key_protocol = NULL;	/* FIXME */
#endif /* HAVE_GMIME_2_5_7 */
    }

#ifndef HAVE_GMIME_2_5_7
    return cipher;
#else /* HAVE_GMIME_2_5_7 */
    return crypto;
#endif /* HAVE_GMIME_2_5_7 */
}


/*
 * Verify that protocol is valid and available in klass. Return TRUE on success
 * and FALSE on fail and set error (if not NULL) in the latter case.
 */
static gboolean
g_mime_gpgme_context_check_protocol(GMimeGpgmeContextClass * klass,
				    gpgme_protocol_t protocol,
				    GError ** error)
{
    switch (protocol) {
    case GPGME_PROTOCOL_OpenPGP:
	if (!klass->has_proto_openpgp) {
	    if (error)
		g_set_error(error, GPGME_ERROR_QUARK,
			    GPG_ERR_INV_ENGINE,
			    _
			    ("the crypto engine for protocol OpenPGP is not available"));
	    return FALSE;
	}
	break;
    case GPGME_PROTOCOL_CMS:
	if (!klass->has_proto_cms) {
	    if (error)
		g_set_error(error, GPGME_ERROR_QUARK,
			    GPG_ERR_INV_ENGINE,
			    _
			    ("the crypto engine for protocol CMS is not available"));
	    return FALSE;
	}
	break;
    default:
	if (error)
	    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_INV_ENGINE,
			_("invalid crypto engine %d"), protocol);
	return FALSE;
    }

    return TRUE;
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
get_key_from_name(GMimeGpgmeContext * ctx, const gchar * name,
		  gboolean secret_only, GError ** error)
{
    GList *keys = NULL;
    gpgme_ctx_t gpgme_ctx = ctx->gpgme_ctx;
    gpgme_key_t key;
    gpgme_error_t err;
    gboolean found_bad;
    time_t now = time(NULL);

    /* let gpgme list keys */
    if ((err = gpgme_op_keylist_start(gpgme_ctx, name,
				      secret_only)) != GPG_ERR_NO_ERROR) {
	gchar * msg = g_strdup_printf(_("could not list keys for \"%s\""), name);

	g_set_error_from_gpgme(error, err, msg);
	g_free(msg);
	return NULL;
    }

    found_bad = FALSE;
    while ((err =
	    gpgme_op_keylist_next(gpgme_ctx, &key)) == GPG_ERR_NO_ERROR)
	/* check if this key and the relevant subkey are usable */
	if (KEY_IS_OK(key)) {
	    gpgme_subkey_t subkey = key->subkeys;

	    while (subkey && ((secret_only && !subkey->can_sign) ||
			      (!secret_only && !subkey->can_encrypt)))
		subkey = subkey->next;

	    if (subkey && KEY_IS_OK(subkey) && 
		(subkey->expires == 0 || subkey->expires > now))
		keys = g_list_append(keys, key);
	    else
		found_bad = TRUE;
	} else
	    found_bad = TRUE;

    if (gpg_err_code(err) != GPG_ERR_EOF) {
	gchar * msg = g_strdup_printf(_("could not list keys for \"%s\""), name);

	g_set_error_from_gpgme(error, err, msg);
	g_free(msg);
	gpgme_op_keylist_end(gpgme_ctx);
	g_list_foreach(keys, (GFunc) gpgme_key_unref, NULL);
	g_list_free(keys);
	return NULL;
    }
    gpgme_op_keylist_end(gpgme_ctx);

    if (!keys) {
	if (error) {
            if (strchr(name, '@')) {
                if (found_bad)
                    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
                                _("%s: a key for %s is present, but it is expired, disabled, revoked or invalid"),
                                "gmime-gpgme", name);
                else
                    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
                                _("%s: could not find a key for %s"),
                                "gmime-gpgme", name);
            } else {
                if (found_bad)
                    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
                                _("%s: a key with id %s is present, but it is expired, disabled, revoked or invalid"),
                                "gmime-gpgme", name);
                else
                    g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_KEY_SELECTION,
                                _("%s: could not find a key with id %s"),
                                "gmime-gpgme", name);
            }
	}
	return NULL;
    }

    if (g_list_length(keys) > 1) {
	if (ctx->key_select_cb)
	    key = ctx->key_select_cb(name, secret_only, ctx, keys);
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
    if (key && !secret_only && !ctx->always_trust_uid &&
	gpgme_get_protocol(gpgme_ctx) == GPGME_PROTOCOL_OpenPGP) {
	gpgme_user_id_t uid = key->uids;
	gchar * upcase_name = g_ascii_strup(name, -1);
	gboolean found = FALSE;

	while (!found && uid) {
	    /* check the email field which may or may not be present */
	    if (uid->email && !g_ascii_strcasecmp(uid->email, name))
		found = TRUE;
	    else {
		/* no email or no match, check the uid */
		gchar * upcase_uid = g_ascii_strup(uid->uid, -1);
		
		if (strstr(upcase_uid, upcase_name))
		    found = TRUE;
		else
		    uid = uid->next;
		g_free(upcase_uid);
	    }
	}
	g_free(upcase_name);

	/* ask the user if a low-validity key shall be used */
	if (uid && uid->validity < GPGME_VALIDITY_FULL)
	    if (!ctx->key_trust_cb ||
		!ctx->key_trust_cb(name, uid, ctx)) {
		gpgme_key_unref(key);
		key = NULL;
		if (error)
		    g_set_error(error, GPGME_ERROR_QUARK,
				GPG_ERR_KEY_SELECTION,
				_("%s: insufficient validity for uid %s"),
				"gmime-gpgme", name);
	    }
    }

    return key;
}


/*
 * Add signer to ctx's list of signers and return TRUE on success or FALSE
 * on error.
 */
static gboolean
gpgme_add_signer(GMimeGpgmeContext * ctx, const gchar * signer,
		 GError ** error)
{
    gpgme_key_t key = NULL;

    if (!(key = get_key_from_name(ctx, signer, TRUE, error)))
	return FALSE;

    /* set the key (the previous operation guaranteed that it exists, no need
     * 2 check return values...) */
    gpgme_signers_add(ctx->gpgme_ctx, key);
    gpgme_key_unref(key);

    return TRUE;
}


/*
 * Build a NULL-terminated array of keys for all recipients in rcpt_list and
 * return it. The caller has to take care that it's released. If something
 * goes wrong, NULL is returned.
 */
static gpgme_key_t *
gpgme_build_recipients(GMimeGpgmeContext * ctx, GPtrArray * rcpt_list,
		       GError ** error)
{
    gpgme_key_t *rcpt = g_new0(gpgme_key_t, rcpt_list->len + 1);
    guint num_rcpts;

    /* try to find the public key for every recipient */
    for (num_rcpts = 0; num_rcpts < rcpt_list->len; num_rcpts++) {
	gchar *name = (gchar *) g_ptr_array_index(rcpt_list, num_rcpts);
	gpgme_key_t key;

	if (!(key = get_key_from_name(ctx, name, FALSE, error))) {
	    release_keylist(rcpt);
	    return NULL;
	}

	/* set the recipient */
	rcpt[num_rcpts] = key;
	g_message("encrypt for %s with fpr %s", name, key->subkeys->fpr);
    }
    g_message("encrypting for %d recipient(s)", num_rcpts);

    return rcpt;
}


/*
 * helper function: unref all keys in the NULL-terminated array keylist
 * and finally release the array itself
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


static void
g_set_error_from_gpgme(GError ** error, gpgme_error_t gpgme_err,
		       const gchar * message)
{
    gchar * errstr;
    gchar * srcstr;

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
