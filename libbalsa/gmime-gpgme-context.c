/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2002 Ximian, Inc. (www.ximian.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(HAVE_GPGME)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>

#include "gmime-gpgme-context.h"
#include "gmime/gmime-filter-charset.h"
#include "gmime/gmime-stream-filter.h"
#include "gmime/gmime-stream-mem.h"
#include "gmime/gmime-stream-fs.h"
#include "gmime/gmime-charset.h"
#include "gmime/gmime-error.h"

#define d(x) x
#define _(x) x

#define GMIME_ERROR_QUARK (g_quark_from_static_string ("gmime"))

static void g_mime_gpgme_context_class_init (GMimeGpgMEContextClass *klass);
static void g_mime_gpgme_context_init (GMimeGpgMEContext *ctx, GMimeGpgMEContextClass *klass);
static void g_mime_gpgme_context_finalize (GObject *object);

static GMimeCipherHash gpgme_hash_id (GMimeCipherContext *ctx, const char *hash);

static const char *gpgme_hash_name (GMimeCipherContext *ctx, GMimeCipherHash hash);

static int gpgme_sign (GMimeCipherContext *ctx, const char *userid,
		     GMimeCipherHash hash, GMimeStream *istream,
		     GMimeStream *ostream, GError **err);

static GMimeCipherValidity *gpgme_verify (GMimeCipherContext *ctx, GMimeCipherHash hash,
					GMimeStream *istream, GMimeStream *sigstream,
					GError **err);

static int gpgme_encrypt (GMimeCipherContext *ctx, gboolean sign,
			const char *userid, GPtrArray *recipients,
			GMimeStream *istream, GMimeStream *ostream,
			GError **err);

static int gpgme_decrypt (GMimeCipherContext *ctx, GMimeStream *istream,
			GMimeStream *ostream, GError **err);

#if NOT_USED
static int gpgme_import_keys (GMimeCipherContext *ctx, GMimeStream *istream,
			    GError **err);

static int gpgme_export_keys (GMimeCipherContext *ctx, GPtrArray *keys,
			    GMimeStream *ostream, GError **err);
#endif


static GMimeCipherContextClass *parent_class = NULL;


GType
g_mime_gpgme_context_get_type (void)
{
	static GType type = 0;

	if (!type) {
		static const GTypeInfo info = {
			sizeof (GMimeGpgMEContextClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc) g_mime_gpgme_context_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (GMimeGpgMEContext),
			0,    /* n_preallocs */
			(GInstanceInitFunc) g_mime_gpgme_context_init,
		};

		type = g_type_register_static (GMIME_TYPE_CIPHER_CONTEXT, "GMimeGpgMEContext", &info, 0);
	}

	return type;
}


static void
g_mime_gpgme_context_class_init (GMimeGpgMEContextClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GMimeCipherContextClass *cipher_class = GMIME_CIPHER_CONTEXT_CLASS (klass);

	parent_class = g_type_class_ref (G_TYPE_OBJECT);

	object_class->finalize = g_mime_gpgme_context_finalize;

	cipher_class->hash_id = gpgme_hash_id;
	cipher_class->hash_name = gpgme_hash_name;
	cipher_class->sign = gpgme_sign;
	cipher_class->verify = gpgme_verify;
	cipher_class->encrypt = gpgme_encrypt;
	cipher_class->decrypt = gpgme_decrypt;
//	cipher_class->import_keys = gpgme_import_keys;
//	cipher_class->export_keys = gpgme_export_keys;
}

static void
g_mime_gpgme_context_init (GMimeGpgMEContext *ctx, GMimeGpgMEContextClass *klass)
{
	GMimeCipherContext *cipher = (GMimeCipherContext *) ctx;
	GpgmeError error;

	ctx->sig_status = GPGME_SIG_STAT_NONE;
	ctx->key = 0;
	ctx->fingerprint = NULL;
	ctx->sign_time = 0;
	ctx->micalg = NULL;

	cipher->sign_protocol = "application/pgp-signature";
	cipher->encrypt_protocol = "application/pgp-encrypted";
	cipher->key_protocol = "application/pgp-keys";

	/* try to create a gpgme context */
	if ((error = gpgme_new(&ctx->gpgme_ctx)) != GPGME_No_Error) {
#if 0
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot sign message: "
			       "could not create gpgme context: %s"),
			     gpgme_strerror(error));
		return -1;
#endif
		ctx->gpgme_ctx = NULL;
	}

}

static void
g_mime_gpgme_context_finalize (GObject *object)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) object;

	gpgme_release(ctx->gpgme_ctx);
	g_free (ctx->fingerprint);
	g_free (ctx->micalg);
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GMimeCipherHash
gpgme_hash_id (GMimeCipherContext *ctx, const char *hash)
{
	if (hash == NULL)
		return GMIME_CIPHER_HASH_DEFAULT;

	if (!strcasecmp (hash, "pgp-"))
		hash += 4;

	if (!strcasecmp (hash, "md2"))
		return GMIME_CIPHER_HASH_MD2;
	else if (!strcasecmp (hash, "md5"))
		return GMIME_CIPHER_HASH_MD5;
	else if (!strcasecmp (hash, "sha1"))
		return GMIME_CIPHER_HASH_SHA1;
	else if (!strcasecmp (hash, "ripemd160"))
		return GMIME_CIPHER_HASH_RIPEMD160;
	else if (!strcasecmp (hash, "tiger192"))
		return GMIME_CIPHER_HASH_TIGER192;
	else if (!strcasecmp (hash, "haval-5-160"))
		return GMIME_CIPHER_HASH_HAVAL5160;

	return GMIME_CIPHER_HASH_DEFAULT;
}

static const char *
gpgme_hash_name (GMimeCipherContext *ctx, GMimeCipherHash hash)
{
	switch (hash) {
	case GMIME_CIPHER_HASH_MD2:
		return "pgp-md2";
	case GMIME_CIPHER_HASH_MD5:
		return "pgp-md5";
	case GMIME_CIPHER_HASH_SHA1:
		return "pgp-sha1";
	case GMIME_CIPHER_HASH_RIPEMD160:
		return "pgp-ripemd160";
	case GMIME_CIPHER_HASH_TIGER192:
		return "pgp-tiger192";
	case GMIME_CIPHER_HASH_HAVAL5160:
		return "pgp-haval-5-160";
	default:
		return NULL;
	}
}



static const char *gpgme_passphrase_cb(void *hook, const char *desc,
				       void **R_HD)
{
	GMimeCipherContext *ctx = GMIME_CIPHER_CONTEXT(hook);
	GMimeSession *session = ctx->session;
	GError *err;

	if (desc) {
		return g_mime_session_request_passwd(session, desc, TRUE,
						     desc, &err);
	}
	g_mime_session_forget_passwd(session, desc, &err);
	return NULL;
}

static int gpgme_read_from_stream(void *hook, char *buffer, size_t count,
				  size_t *nread)
{
	GMimeStream *stream = GMIME_STREAM(hook);
	ssize_t read;

	if (buffer == NULL && nread == NULL && count == 0) {
		g_mime_stream_reset(stream);
		return 0;
	}
	g_return_val_if_fail(buffer, -1);
	g_return_val_if_fail(nread, -1);

	read = *nread = g_mime_stream_read(stream, buffer, count);
	if (read == -1) {
		if (g_mime_stream_eos(stream)) {
			*nread = 0;
		}
	} else {
		read = 0;
	}
	return read;
}

/*
 * Parse the XML output gpgme_op_info generated by gpgme_get_op_info for
 * element in GnupgOperationInfo->signature and return it. If the result
 * is not NULL, it has to be freed.
 */
static gchar *
get_gpgme_parameter(const gchar *gpgme_op_info, const gchar *element)
{
    gchar *p, *q;

    if (!(p = strstr(gpgme_op_info, element)))
	return NULL;
    p += strlen(element) + 1;
    q = p + 1;

    while (*q && *q != '<')
	q++;
    *q = '\0';
    return g_strdup(p);
}


static int
gpgme_sign (GMimeCipherContext *context, const char *userid, GMimeCipherHash hash,
	  GMimeStream *istream, GMimeStream *ostream, GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;

	GpgmeCtx gpgme_ctx = ctx->gpgme_ctx;
	GpgmeError error;
	GpgmeData in, out;
	size_t datasize;
	gchar *signature_buffer, *signature_info;

	ctx->micalg = NULL;

	/* let gpgme create the signature */
	gpgme_set_armor(gpgme_ctx, 1);
	gpgme_set_passphrase_cb(gpgme_ctx, gpgme_passphrase_cb, ctx);

	if ((error = gpgme_data_new_with_read_cb(&in, gpgme_read_from_stream,
					       istream)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot sign message: "
			       "could not get gpgme data from istream: %s"),
			     gpgme_strerror(error));
		return -1;
	}

	if ((error = gpgme_data_new(&out)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot sign message: "
			       "gpgme could not create new data: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(in);
		return -1;
	}

	if ((error = gpgme_op_sign(gpgme_ctx, in, out, GPGME_SIG_MODE_DETACH))
	    != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot sign message: "
			       "gpgme signing failed: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(out);
		gpgme_data_release(in);
		return -1;
	}

	/* get the info about the used hash algorithm */
	signature_info = gpgme_get_op_info(gpgme_ctx, 0);
	if (signature_info) {
		g_free(ctx->micalg);
		ctx->micalg = get_gpgme_parameter(signature_info, "micalg");
		g_free(signature_info);
	}

	gpgme_data_release(in);
	if (!(signature_buffer = gpgme_data_release_and_get_mem(out, &datasize))) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot sign message: "
			       "could not get gpgme data: %s"),
			     gpgme_strerror(error));
		return -1;
	}

	g_mime_stream_write(ostream, signature_buffer, datasize);
	g_free(signature_buffer);

	return 0;
}


static GMimeCipherValidity *
gpgme_verify (GMimeCipherContext *context, GMimeCipherHash hash,
	    GMimeStream *istream, GMimeStream *sigstream,
	    GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;
	GMimeCipherValidity *validity;
	const char *diagnostics;
	GpgmeCtx gpgme_ctx = ctx->gpgme_ctx;
	gboolean valid;
	GpgmeSigStat stat;
	GpgmeError error;
	GpgmeData sig, plain;

	if ((error = gpgme_data_new_with_read_cb(&plain, gpgme_read_from_stream,
					       istream)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot verify message signature: "
			       "could not get gpgme data from istream: %s"),
			     gpgme_strerror(error));
		goto exception;
	}

	if ((error = gpgme_data_new_with_read_cb(&sig, gpgme_read_from_stream,
					       sigstream)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot verify message signature: "
			       "could not get gpgme data from sigstream: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(plain);
		goto exception;
	}

	if ((error = gpgme_op_verify(gpgme_ctx, sig, plain, &ctx->sig_status))
	    != GPGME_No_Error) {
		g_message( _("gpgme signature verification failed: %s"),
			   gpgme_strerror(error));
		ctx->sig_status = GPGME_SIG_STAT_ERROR;
	}

	valid = ctx->sig_status == GPGME_SIG_STAT_GOOD;
	validity = g_mime_cipher_validity_new ();
	diagnostics = "";
	g_mime_cipher_validity_set_valid (validity, valid);
	g_mime_cipher_validity_set_description (validity, diagnostics);

	if (ctx->key)
		gpgme_key_unref(ctx->key);
	gpgme_get_sig_key(gpgme_ctx, 0, &ctx->key);
	ctx->fingerprint = g_strdup(gpgme_get_sig_status(gpgme_ctx, 0, &stat,
							 &ctx->sign_time));

	gpgme_data_release(sig);
	gpgme_data_release(plain);

	return validity;

 exception:

	return NULL;
}


static int
gpgme_encrypt (GMimeCipherContext *context, gboolean sign, const char *userid,
	     GPtrArray *recipients, GMimeStream *istream, GMimeStream *ostream,
	     GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;
	GpgmeCtx gpgme_ctx = ctx->gpgme_ctx;
	GpgmeError error;
	GpgmeRecipients rcpt;
	GpgmeData in, out;
	gchar *databuf;
	size_t datasize;
	guint i;

	if (sign) {
		return -1;
	}

	/* build the list of recipients */
	if ((error = gpgme_recipients_new(&rcpt)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot encrypt message: "
			       "could not create gpgme recipients set: %s"),
			     gpgme_strerror(error));
		return -1;
	}
#if 0
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "encrypting for %d recipient(s)",
			     gpgme_recipients_count(*rcpt));
#endif

	for (i = 0; i < recipients->len; i++) {
		/* set the recipient */
		if ((error = gpgme_recipients_add_name_with_validity(rcpt,
			       recipients->pdata[i], 
			       GPGME_VALIDITY_FULL)) != GPGME_No_Error) {
			gpgme_recipients_release(rcpt);
			return -1;
		}
	}

	/* let gpgme encrypt the data */
	gpgme_set_armor(gpgme_ctx, 1);
	if ((error = gpgme_data_new_with_read_cb(&in, gpgme_read_from_stream,
					       istream)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot encrypt message: "
			       "could not get gpgme data from istream: %s"),
			     gpgme_strerror(error));
		gpgme_recipients_release(rcpt);
		return -1;
	}

	if ((error = gpgme_data_new(&out)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot encrypt message: "
			       "gpgme could not create new data: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(in);
		gpgme_recipients_release(rcpt);
		return -1;
	}

	if ((error = gpgme_op_encrypt(gpgme_ctx, rcpt, in, out))
	    != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot encrypt message: "
			       "gpgme encryption failed: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(out);
		gpgme_data_release(in);
		gpgme_recipients_release(rcpt);
		return -1;
	}

	/* save the result to a file and return its name */
	gpgme_data_release(in);
	gpgme_recipients_release(rcpt);

	if (!(databuf = gpgme_data_release_and_get_mem(out, &datasize))) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot encrypt message: "
			       "could not get gpgme data: %s"),
			     gpgme_strerror(error));
		return -1;
	}

	g_mime_stream_write(ostream, databuf, datasize);
	g_free(databuf);

	return 0;
}


static int
gpgme_decrypt (GMimeCipherContext *context, GMimeStream *istream,
	     GMimeStream *ostream, GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;

	GpgmeCtx gpgme_ctx = ctx->gpgme_ctx;
	GpgmeError error;
	GpgmeData cipher, plain;
	size_t datasize;
	gchar *plain_buffer;

	/* let gpgme create the signature */
	gpgme_set_passphrase_cb(gpgme_ctx, gpgme_passphrase_cb, ctx);

	if ((error = gpgme_data_new_with_read_cb(&cipher, gpgme_read_from_stream,
					       istream)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot decrypt message: "
			       "could not get gpgme data from istream: %s"),
			     gpgme_strerror(error));
		return -1;
	}

	if ((error = gpgme_data_new(&plain)) != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot decrypt message: "
			       "gpgme could not create new data: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(cipher);
		return -1;
	}

	if ((error = gpgme_op_decrypt(gpgme_ctx, cipher, plain))
	    != GPGME_No_Error) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot decrypt message: "
			       "gpgme decrypting failed: %s"),
			     gpgme_strerror(error));
		gpgme_data_release(plain);
		gpgme_data_release(cipher);
		return -1;
	}

	gpgme_data_release(cipher);
	if (!(plain_buffer = gpgme_data_release_and_get_mem(plain, &datasize))) {
		g_set_error (err, GMIME_ERROR_QUARK, error,
			     _("Cannot decrypt message: "
			       "could not get gpgme data: %s"),
			     gpgme_strerror(error));
		return -1;
	}

	g_mime_stream_write(ostream, plain_buffer, datasize);
	g_free(plain_buffer);
	/* BUGFIX for g_mime_multipart_encrypted_decrypt */
	g_mime_stream_flush(ostream);
	g_mime_stream_reset(ostream);

	return 0;
}

#if NOT_USED
static int
gpgme_import_keys (GMimeCipherContext *context, GMimeStream *istream, GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;
	struct _GpgMECtx *gpgme;

	gpgme = gpgme_ctx_new (context->session, ctx->path);
	gpgme_ctx_set_mode (gpgme, GPGME_CTX_MODE_IMPORT);
	gpgme_ctx_set_istream (gpgme, istream);

	if (gpgme_ctx_op_start (gpgme) == -1) {
		g_set_error (err, GMIME_ERROR_QUARK, errno,
			     _("Failed to execute gpgme: %s"),
			     errno ? g_strerror (errno) : _("Unknown"));
		gpgme_ctx_free (gpgme);

		return -1;
	}

	while (!gpgme_ctx_op_complete (gpgme)) {
		if (gpgme_ctx_op_step (gpgme, err) == -1) {
			gpgme_ctx_op_cancel (gpgme);
			gpgme_ctx_free (gpgme);

			return -1;
		}
	}

	if (gpgme_ctx_op_wait (gpgme) != 0) {
		const char *diagnostics;
		int save;

		save = errno;
		diagnostics = gpgme_ctx_get_diagnostics (gpgme);
		errno = save;

		g_set_error (err, GMIME_ERROR_QUARK, errno, diagnostics);
		gpgme_ctx_free (gpgme);

		return -1;
	}

	gpgme_ctx_free (gpgme);

	return 0;
}
#endif

#if NOT_USED
static int
gpgme_export_keys (GMimeCipherContext *context, GPtrArray *keys, GMimeStream *ostream, GError **err)
{
	GMimeGpgMEContext *ctx = (GMimeGpgMEContext *) context;
	struct _GpgMECtx *gpgme;
	int i;

	gpgme = gpgme_ctx_new (context->session, ctx->path);
	gpgme_ctx_set_mode (gpgme, GPGME_CTX_MODE_EXPORT);
	gpgme_ctx_set_armor (gpgme, TRUE);
	gpgme_ctx_set_ostream (gpgme, ostream);

	for (i = 0; i < keys->len; i++) {
		gpgme_ctx_add_recipient (gpgme, keys->pdata[i]);
	}

	if (gpgme_ctx_op_start (gpgme) == -1) {
		g_set_error (err, GMIME_ERROR_QUARK, errno,
			     _("Failed to execute gpgme: %s"),
			     errno ? g_strerror (errno) : _("Unknown"));
		gpgme_ctx_free (gpgme);

		return -1;
	}

	while (!gpgme_ctx_op_complete (gpgme)) {
		if (gpgme_ctx_op_step (gpgme, err) == -1) {
			gpgme_ctx_op_cancel (gpgme);
			gpgme_ctx_free (gpgme);

			return -1;
		}
	}

	if (gpgme_ctx_op_wait (gpgme) != 0) {
		const char *diagnostics;
		int save;

		save = errno;
		diagnostics = gpgme_ctx_get_diagnostics (gpgme);
		errno = save;

		g_set_error (err, GMIME_ERROR_QUARK, errno, diagnostics);
		gpgme_ctx_free (gpgme);

		return -1;
	}

	gpgme_ctx_free (gpgme);

	return 0;
}
#endif


/**
 * g_mime_gpgme_context_new:
 * @session: session
 *
 * Creates a new gpgme cipher context object.
 *
 * Returns a new gpgme cipher context object.
 **/
GMimeCipherContext *
g_mime_gpgme_context_new (GMimeSession *session)
{
	GMimeCipherContext *cipher;
	GMimeGpgMEContext *ctx;

	g_return_val_if_fail (GMIME_IS_SESSION (session), NULL);

	ctx = g_object_new (GMIME_TYPE_GPGME_CONTEXT, NULL, NULL);

	cipher = (GMimeCipherContext *) ctx;
	cipher->session = session;
	g_object_ref (session);

	return cipher;
}

#endif /* HAVE_GPGME */
