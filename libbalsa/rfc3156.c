/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */
#include "config.h"

#ifdef HAVE_GPGME

#include <gnome.h>
#include <string.h>
#include <gpgme.h>
#include <pwd.h>
#include <sys/types.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "rfc3156.h"

#include "gmime-gpgme-context.h"
#include "gmime-gpgme-signature.h"
#include "gmime-part-rfc2440.h"

#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#  include "misc.h"
#endif

#include "padlock-keyhole.xpm"


/* FIXME: for the time being, the passphrase cache is disabled */
#undef USE_SSL

#ifndef USE_SSL
#  undef ENABLE_PCACHE
#else
/* 
 * local stuff for remembering the last passphrase (needs OpenSSL)
 */
#define ENABLE_PCACHE
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <signal.h>

/* FIXME: this is an evil hack to get a public function from libmutt which
 * is never defined in a header file... */
extern int ssl_init(void);

typedef struct _bf_key_T bf_key_T;
typedef struct _bf_crypt_T bf_crypt_T;
typedef struct _pcache_elem_T pcache_elem_T;
typedef struct _pcache_T pcache_T;

struct _bf_key_T {
    unsigned char key[16];	/* blowfish key */
    unsigned char iv[8];
};

struct _bf_crypt_T {
    unsigned char *buf;		/* encrypted buffer */
    gint len;			/* length of buf */
};

struct _pcache_elem_T {
    unsigned char name[MD5_DIGEST_LENGTH];	/* md5sum of name */
    bf_crypt_T *passphrase;	/* encrypted passphrase */
    time_t expires;		/* expiry time */
};

struct _pcache_T {
    gboolean enable;		/* using the cache is allowed */
    gint max_mins;		/* max minutes allowed to cache data */
    bf_key_T bf_key;		/* the (random) blowfish key */
    GList *cache;		/* list of pcache_elem_T elements */
};

static pcache_T *pcache = NULL;
static void (*segvhandler) (int);

#endif
/* local prototypes */
static const gchar *libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t
							   validity);
static gpgme_error_t get_passphrase_cb(void *opaque, const char *uid_hint,
				       const char *passph_info,
				       int prev_wasbad, int fd);
static gpgme_key_t select_key_from_list(const gchar * name,
					gboolean is_secret,
					GMimeGpgmeContext * ctx,
					GList * keys);
static gboolean gpg_updates_trustdb(void);

/* ==== public functions =================================================== */
gboolean
libbalsa_check_crypto_engine(gpgme_protocol_t protocol)
{
    gpgme_error_t err;

    err = gpgme_engine_check_version(protocol);
    if (gpgme_err_code(err) != GPG_ERR_NO_ERROR) {
	gpgme_engine_info_t info;
	GString *message = g_string_new("");
	err = gpgme_get_engine_info(&info);
	if (err == GPG_ERR_NO_ERROR) {
	    while (info && info->protocol != protocol)
		info = info->next;
	    if (!info)
		g_string_append_printf(message,
				       _
				       ("Gpgme has been compiled without support for protocol %s."),
				       gpgme_get_protocol_name(protocol));
	    else if (info->file_name && !info->version)
		g_string_append_printf(message,
				       _
				       ("Crypto engine %s is not installed properly."),
				       info->file_name);
	    else if (info->file_name && info->version && info->req_version)
		g_string_append_printf(message,
				       _
				       ("Crypto engine %s version %s is installed, but at least version %s is required."),
				       info->file_name, info->version,
				       info->req_version);

	    else
		g_string_append_printf(message,
				       _
				       ("Unknown problem with engine for protocol %s."),
				       gpgme_get_protocol_name(protocol));
	} else
	    g_string_append_printf(message,
				   _
				   ("%s: could not retreive crypto engine information: %s."),
				   gpgme_strsource(err),
				   gpgme_strerror(err));
	g_string_append_printf(message,
			       _("\nDisable support for protocol %s."),
			       gpgme_get_protocol_name(protocol));
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, message->str);
	g_string_free(message, TRUE);
	return FALSE;
    } else
	return TRUE;
}


static gboolean
body_is_type(LibBalsaMessageBody * body, const gchar * type,
	     const gchar * sub_type)
{
    gboolean retval;

    if (body->mime_part) {
	const GMimeContentType *content_type =
	    g_mime_object_get_content_type(body->mime_part);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
    } else {
	GMimeContentType *content_type =
	    g_mime_content_type_new_from_string(body->mime_type);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
	g_mime_content_type_destroy(content_type);
    }

    return retval;
}


/*
 * Check if body (and eventually its subparts) are RFC 2633 or RFC 3156 signed
 * or encrypted.
 */
gint
libbalsa_message_body_protection(LibBalsaMessageBody * body)
{
    gint result = 0;

    g_return_val_if_fail(body != NULL, 0);
    g_return_val_if_fail(body->mime_part != NULL, 0);
    g_return_val_if_fail(body->mime_type != NULL, 0);

    if (body_is_type(body, "multipart", "signed")) {
	gchar *protocol =
	    libbalsa_message_body_get_parameter(body, "protocol");
	gchar *micalg =
	    libbalsa_message_body_get_parameter(body, "micalg");

	result = LIBBALSA_PROTECT_SIGN;
	if (protocol && body->parts && body->parts->next) {
	    if (!g_ascii_strcasecmp
		("application/pkcs7-signature", protocol)
		&& body_is_type(body->parts->next, "application",
				"pkcs7-signature")) {
		result |= LIBBALSA_PROTECT_SMIMEV3;
		if (!micalg)
		    result |= LIBBALSA_PROTECT_ERROR;
	    } else
		if (!g_ascii_strcasecmp
		    ("application/pgp-signature", protocol)
		    && body_is_type(body->parts->next, "application",
				    "pgp-signature")) {
		result |= LIBBALSA_PROTECT_RFC3156;
		if (!micalg || g_ascii_strncasecmp("pgp-", micalg, 4))
		    result |= LIBBALSA_PROTECT_ERROR;
	    } else
		result |= LIBBALSA_PROTECT_ERROR;
	} else
	    result |= LIBBALSA_PROTECT_ERROR;
	g_free(micalg);
	g_free(protocol);
    } else if (body_is_type(body, "multipart", "encrypted")) {
	gchar *protocol =
	    libbalsa_message_body_get_parameter(body, "protocol");

	result = LIBBALSA_PROTECT_ENCRYPT | LIBBALSA_PROTECT_RFC3156;
	if (!protocol ||
	    g_ascii_strcasecmp("application/pgp-encrypted", protocol) ||
	    !body->parts || !body->parts->next ||
	    !body_is_type(body->parts, "application", "pgp-encrypted") ||
	    !body_is_type(body->parts->next, "application",
			  "octet-stream"))
	    result |= LIBBALSA_PROTECT_ERROR;
	g_free(protocol);
    } else if (body_is_type(body, "application", "pkcs7-mime")) {
	gchar *smime_type =
	    libbalsa_message_body_get_parameter(body, "smime-type");

	result = LIBBALSA_PROTECT_SMIMEV3;
	if (!g_ascii_strcasecmp("enveloped-data", smime_type))
	    result |= LIBBALSA_PROTECT_ENCRYPT;
	else if (!g_ascii_strcasecmp("signed-data", smime_type))
	    result |= LIBBALSA_PROTECT_SIGN;
	else
	    result |= LIBBALSA_PROTECT_ERROR;
	g_free(smime_type);
    }

    return result;
}


/* === RFC 2633/ RFC 3156 crypto routines === */
/*
 * Signs the MIME object *content with the private key of rfc822_for using
 * protocol. Return TRUE on success (in which case *content is replaced by the
 * new MIME object).
 */
gboolean
libbalsa_sign_mime_object(GMimeObject ** content, const gchar * rfc822_for,
			  gpgme_protocol_t protocol, GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GMimeMultipartSigned *mps;
    GError *error = NULL;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session, protocol,
						       &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"),
				 error->message);
	g_object_unref(session);
	return FALSE;
    }

    /* set the callbacks for the passphrase entry and the key selection */
    ctx->passphrase_cb = get_passphrase_cb;
    g_object_set_data(G_OBJECT(ctx), "passphrase-info",
		      _
		      ("Enter passphrase to unlock the secret key for signing"));
    ctx->key_select_cb = select_key_from_list;
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);

    /* call gpgme to create the signature */
    if (!(mps = g_mime_multipart_signed_new())) {
	g_object_unref(ctx);
	g_object_unref(session);
	return FALSE;
    }

    if (g_mime_multipart_signed_sign
	(mps, *content, GMIME_CIPHER_CONTEXT(ctx), rfc822_for,
	 GMIME_CIPHER_HASH_DEFAULT, &error) != 0) {
	g_object_unref(mps);
	g_object_unref(ctx);
	g_object_unref(session);
	if (error) {
	    if (error->code != GPG_ERR_CANCELED)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				     _("signing failed"), error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("signing failed"));
	return FALSE;
    }

    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mps),
					     "micalg", ctx->micalg);
    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = GMIME_OBJECT(mps);
    g_object_unref(ctx);
    g_object_unref(session);
    return TRUE;
}


/*
 * Encrypts MIME object *content for every recipient in the array rfc822_for
 * using protocol. If successful, return TRUE and replace *content by the new
 * MIME object.
 */
gboolean
libbalsa_encrypt_mime_object(GMimeObject ** content, GList * rfc822_for,
			     gpgme_protocol_t protocol, GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GPtrArray *recipients;
    GMimeMultipartEncrypted *mpe;
    GError *error = NULL;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session, protocol,
						       &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	return FALSE;
    }

    /* set the callback for the key selection (no secret needed here) */
    ctx->key_select_cb = select_key_from_list;
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);

    /* convert the key list to a GPtrArray */
    recipients = g_ptr_array_new();
    while (rfc822_for) {
	g_ptr_array_add(recipients, rfc822_for->data);
	rfc822_for = g_list_next(rfc822_for);
    }

    /* encrypt */
    mpe = g_mime_multipart_encrypted_new();
    if (g_mime_multipart_encrypted_encrypt(mpe, *content,
					   GMIME_CIPHER_CONTEXT(ctx),
					   recipients, &error) != 0) {
	if (error) {
	    if (error->code != GPG_ERR_CANCELED)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				     _("encryption failed"),
				     error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("encryption failed"));
	g_ptr_array_free(recipients, FALSE);
	g_object_unref(ctx);
	g_object_unref(session);
	g_object_unref(mpe);
	return FALSE;
    }

    g_ptr_array_free(recipients, FALSE);
    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = GMIME_OBJECT(mpe);
    g_object_unref(ctx);
    g_object_unref(session);

    return TRUE;
}


/*
 * Signs the MIME object *content with the private key of rfc822_signer and
 * then encrypt the result for all recipients in rfc822_for using protocol.
 * Return TRUE on success (in which case *content is replaced by the new
 * MIME object).
 */
gboolean
libbalsa_sign_encrypt_mime_object(GMimeObject ** content,
				  const gchar * rfc822_signer,
				  GList * rfc822_for,
				  gpgme_protocol_t protocol,
				  GtkWindow * parent)
{
    GMimeObject *signed_object;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_signer != NULL, FALSE);
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* we want to be able to restore */
    signed_object = *content;
    g_mime_object_ref(GMIME_OBJECT(signed_object));

    if (!libbalsa_sign_mime_object(&signed_object, rfc822_signer, protocol,
				   parent))
	return FALSE;

    if (!libbalsa_encrypt_mime_object(&signed_object, rfc822_for, protocol,
				      parent)) {
	g_mime_object_unref(GMIME_OBJECT(signed_object));
	return FALSE;
    }
    g_mime_object_unref(GMIME_OBJECT(*content));
    *content = signed_object;

    return TRUE;
}


/*
 * Check the signature of body (which must be a multipart/signed). On success,
 * set the sig_info field of the signature part.
 */
gboolean
libbalsa_body_check_signature(LibBalsaMessageBody * body,
			      gpgme_protocol_t protocol)
{
    GMimeSession *session;
    GMimeCipherContext *ctx;
    GMimeCipherValidity *valid;
    GError *error = NULL;
    g_return_val_if_fail(body, FALSE);
    g_return_val_if_fail(body->parts, FALSE);
    g_return_val_if_fail(body->parts->next, FALSE);
    g_return_val_if_fail(body->message, FALSE);

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* check if the body is really a multipart/signed */
    if (!GMIME_IS_MULTIPART_SIGNED(body->mime_part))
	return FALSE;
    if (body->parts->next->sig_info)
	g_object_unref(G_OBJECT(body->parts->next->sig_info));

    /* try to create GMimeGpgMEContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = g_mime_gpgme_context_new(session, protocol, &error);
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	body->parts->next->sig_info = g_mime_gpgme_sigstat_new();
	body->parts->next->sig_info->status = GPGME_SIG_STAT_ERROR;
	return FALSE;
    }

    /* verify the signature */
    valid = g_mime_multipart_signed_verify(GMIME_MULTIPART_SIGNED
					   (body->mime_part), ctx, &error);
    if (valid == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("signature verification failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("signature verification failed"),
				 error->message);
    }
    body->parts->next->sig_info = GMIME_GPGME_CONTEXT(ctx)->sig_state;
    g_object_ref(G_OBJECT(body->parts->next->sig_info));
    g_mime_cipher_validity_free(valid);
    g_object_unref(ctx);
    g_object_unref(session);
    return TRUE;
}


/*
 * body points to an application/pgp-encrypted body. If decryption is
 * successful, it is freed, and the routine returns a pointer to the chain of
 * decrypted bodies. Otherwise, the original body is returned.
 */
LibBalsaMessageBody *
libbalsa_body_decrypt(LibBalsaMessageBody * body,
		      gpgme_protocol_t protocol, GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GMimeObject *mime_obj;
    GError *error = NULL;
    LibBalsaMessage *message;

    /* paranoia checks */
    g_return_val_if_fail(body != NULL, body);
    g_return_val_if_fail(body->message != NULL, body);

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return body;

    /* check if the body is really a multipart/signed */
    if (!GMIME_IS_MULTIPART_ENCRYPTED(body->mime_part))
	return body;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session, protocol,
						       &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	return body;
    }

    /* set the callback for the passphrase entry */
    ctx->passphrase_cb = get_passphrase_cb;
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);
    g_object_set_data(G_OBJECT(ctx), "passphrase-info",
		      _("Enter passphrase to decrypt message"));

    mime_obj =
	g_mime_multipart_encrypted_decrypt(GMIME_MULTIPART_ENCRYPTED
					   (body->mime_part),
					   GMIME_CIPHER_CONTEXT(ctx),
					   &error);
    if (mime_obj == NULL) {
	if (error) {
	    if (error->code != GPG_ERR_CANCELED)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				     _("decryption failed"),
				     error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("decryption failed"));
	g_object_unref(ctx);
	g_object_unref(session);
	return body;
    }
    g_object_unref(ctx);
    g_object_unref(session);
    message = body->message;
    libbalsa_message_body_free(body);
    body = libbalsa_message_body_new(message);
    libbalsa_message_body_set_mime_body(body, mime_obj);
    g_object_unref(G_OBJECT(mime_obj));

    return body;
}



/* routines dealing with RFC2440 */
gboolean
libbalsa_rfc2440_sign_encrypt(GMimePart * part, const gchar * sign_for,
			      GList * encrypt_for, GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GPtrArray *recipients;
    GError *error = NULL;
    gint result;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);
    g_return_val_if_fail(sign_for != NULL || encrypt_for != NULL, FALSE);

    /* check if gpg is currently available */
    if (gpg_updates_trustdb())
	return FALSE;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session,
						       GPGME_PROTOCOL_OpenPGP,
						       &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	return FALSE;
    }

    /* set the callback for the key selection and the passphrase */
    if (sign_for) {
	ctx->passphrase_cb = get_passphrase_cb;
	g_object_set_data(G_OBJECT(ctx), "passphrase-info",
			  _
			  ("Enter passphrase to unlock the secret key for signing"));
    }
    ctx->key_select_cb = select_key_from_list;
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);

    /* convert the key list to a GPtrArray */
    if (encrypt_for) {
	recipients = g_ptr_array_new();
	while (encrypt_for) {
	    g_ptr_array_add(recipients, encrypt_for->data);
	    encrypt_for = g_list_next(encrypt_for);
	}
    } else
	recipients = NULL;

    /* sign and/or encrypt */
    result =
	g_mime_part_rfc2440_sign_encrypt(part, ctx, recipients, sign_for,
					 &error);
    if (result != 0) {
	if (error) {
	    if (error->code != GPG_ERR_CANCELED) {
		if (sign_for && recipients)
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
					 _("signing and encryption failed"),
					 error->message);
		else if (sign_for)
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
					 _("signing failed"), error->message);
		else
		    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s :%s",
					 _("encryption failed"),
					 error->message);
	    }
	    g_error_free(error);
	} else {
	    if (sign_for && recipients)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				     _("signing and encryption failed"));
	    else if (sign_for)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				     _("signing failed: %s"));
	    else
		libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				     _("encryption failed: %s"));
	}
    }

    /* clean up */
    if (recipients)
	g_ptr_array_free(recipients, FALSE);
    g_object_unref(ctx);
    g_object_unref(session);
    return (result == 0) ? TRUE : FALSE;
}


/*
 * Check the signature of part and return the result of the crypto process. If
 * sig_info is not NULL, return the signature info object there.
 */
gpgme_error_t
libbalsa_rfc2440_verify(GMimePart * part, GMimeGpgmeSigstat ** sig_info)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GMimeCipherValidity *valid;
    GError *error = NULL;
    gpgme_error_t retval;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);

    /* check if gpg is currently available */
    if (gpg_updates_trustdb())
	return GPG_ERR_TRY_AGAIN;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx = GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new(session,
						       GPGME_PROTOCOL_OpenPGP,
						       &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	return FALSE;
    }

    /* verify */
    valid = g_mime_part_rfc2440_verify(part, ctx, &error);

    if (valid == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("signature verification failed"),
				 error->message);
	    retval = error->code;
	    g_error_free(error);
	} else {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("signature verification failed"));
	    retval = GPG_ERR_GENERAL;
	}
	g_object_unref(ctx);
	g_object_unref(session);
	return retval;
    }

    /* return the signature info if requested */
    if (sig_info) {
	if (*sig_info)
	    g_object_unref(*sig_info);
	g_object_ref(ctx->sig_state);
	*sig_info = ctx->sig_state;
    }

    /* clean up */
    g_mime_cipher_validity_free(valid);
    retval = ctx->sig_state->status;
    g_object_unref(ctx);
    g_object_unref(session);
    return retval;
}


/*
 * Decrypt part, if possible check the signature, and return the result of the
 * crypto process. If sig_info is not NULL and the part is signed, return the
 * signature info object there.
 */
gpgme_error_t
libbalsa_rfc2440_decrypt(GMimePart * part, GMimeGpgmeSigstat ** sig_info,
			 GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GError *error = NULL;
    gpgme_error_t retval;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);

    /* check if gpg is currently available */
    if (gpg_updates_trustdb())
	return GPG_ERR_TRY_AGAIN;

    /* create a session and a GMimeGpgmeContext */
    session = g_object_new(g_mime_session_get_type(), NULL, NULL);
    ctx =
	GMIME_GPGME_CONTEXT(g_mime_gpgme_context_new
			    (session, GPGME_PROTOCOL_OpenPGP, &error));
    if (ctx == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("creating a gpgme context failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("creating a gpgme context failed"));
	g_object_unref(session);
	return FALSE;
    }

    /* set the callback for the passphrase */
    ctx->passphrase_cb = get_passphrase_cb;
    g_object_set_data(G_OBJECT(ctx), "passphrase-info",
		      _("Enter passphrase to decrypt message"));
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);

    /* decrypt */
    if (g_mime_part_rfc2440_decrypt(part, ctx, &error) == -1) {
	if (error) {
	    if (error->code != GPG_ERR_CANCELED)
		libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				     _("decryption and signature verification failed"),
				     error->message);
	    retval = error->code;
	    g_error_free(error);
	} else {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _
				 ("decryption and signature verification failed"));
	    retval = GPG_ERR_GENERAL;
	}
	g_object_unref(ctx);
	g_object_unref(session);
	return retval;
    }

    retval = GPG_ERR_NO_ERROR;
    if (ctx->sig_state) {
	retval = ctx->sig_state->status;
	/* return the signature info if requested & available */
	if (sig_info) {
	    if (*sig_info)
		g_object_unref(*sig_info);
	    if (ctx->sig_state->status != GPG_ERR_NOT_SIGNED) {
		g_object_ref(ctx->sig_state);
		*sig_info = ctx->sig_state;
	    } else
		*sig_info = NULL;
	}
    }

    /* clean up */
    g_object_unref(ctx);
    g_object_unref(session);
    return retval;
}


/* conversion of status values to human-readable messages */
const gchar *
libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat)
{
    switch (stat) {
    case GPG_ERR_NO_ERROR:
	return _("The signature is valid.");
    case GPG_ERR_SIG_EXPIRED:
	return _("The signature is valid but expired.");
    case GPG_ERR_KEY_EXPIRED:
	return _
	    ("The signature is valid but the key used to verify the signature has expired.");
    case GPG_ERR_BAD_SIGNATURE:
	return _
	    ("The signature is invalid (Note: might be caused by gmime bug!).");
    case GPG_ERR_NO_PUBKEY:
	return
	    _("The signature could not be verified due to a missing key.");
    case GPG_ERR_NO_DATA:
	return _("This part is not a real PGP signature.");
    case GPG_ERR_INV_ENGINE:
	return _
	    ("The signature could not be verified due to an invalid crypto engine.");
    case GPG_ERR_TRY_AGAIN:
	return _
	    ("GnuPG rebuilds the trust database and is currently not available.");
    default:
	return _("An error prevented the signature verification.");
    }
}
const gchar *
libbalsa_gpgme_validity_to_gchar(gpgme_validity_t validity)
{
    switch (validity) {
    case GPGME_VALIDITY_UNKNOWN:
	return _("The user ID is of unknown validity.");
    case GPGME_VALIDITY_UNDEFINED:
	return _("The validity of the user ID is undefined.");
    case GPGME_VALIDITY_NEVER:
	return _("The user ID is never valid.");
    case GPGME_VALIDITY_MARGINAL:
	return _("The user ID is marginally valid.");
    case GPGME_VALIDITY_FULL:
	return _("The user ID is fully valid.");
    case GPGME_VALIDITY_ULTIMATE:
	return _("The user ID is ultimately valid.");
    default:
	return _("bad validity");
    }
}
const gchar *
libbalsa_gpgme_sig_protocol_name(gpgme_protocol_t protocol)
{
    switch (protocol) {
    case GPGME_PROTOCOL_OpenPGP:
	return _("PGP signature: ");
    case GPGME_PROTOCOL_CMS:
	return _("S/MIME signature: ");
    default:
	return _("(unknown protocol) ");
    }
}

gchar *
libbalsa_signature_info_to_gchar(GMimeGpgmeSigstat * info,
				 const gchar * date_string)
{
    GString *msg;
    gchar *retval;
    g_return_val_if_fail(info != NULL, NULL);
    g_return_val_if_fail(date_string != NULL, NULL);
    msg = g_string_new(libbalsa_gpgme_sig_protocol_name(info->protocol));
    msg =
	g_string_append(msg,
			libbalsa_gpgme_sig_stat_to_gchar(info->status));
    if (info->sign_uid && strlen(info->sign_uid))
	g_string_append_printf(msg, _("\nUser ID: %s"), info->sign_uid);

    else if (info->sign_name && strlen(info->sign_name)) {
	g_string_append_printf(msg, _("\nSigned by: %s"), info->sign_name);
	if (info->sign_email && strlen(info->sign_email))
	    g_string_append_printf(msg, " <%s>", info->sign_email);
    } else if (info->sign_email && strlen(info->sign_email))
	g_string_append_printf(msg, _("\nMail address: %s"),
			       info->sign_email);
    if (info->sign_time) {
	gchar buf[128];
	strftime(buf, 127, date_string, localtime(&info->sign_time));
	g_string_append_printf(msg, _("\nSigned on: %s"), buf);
    }
    g_string_append_printf(msg, _("\nValidity: %s"),
			   libbalsa_gpgme_validity_to_gchar(info->
							    validity));
    if (info->protocol == GPGME_PROTOCOL_OpenPGP)
	g_string_append_printf(msg, _("\nOwner trust: %s"),
			       libbalsa_gpgme_validity_to_gchar_short
			       (info->trust));
    if (info->fingerprint)
	g_string_append_printf(msg, _("\nKey fingerprint: %s"),
			       info->fingerprint);
    if (info->key_created) {
	gchar buf[128];
	strftime(buf, 127, date_string, localtime(&info->key_created));
	g_string_append_printf(msg, _("\nKey created on: %s"), buf);
    }
    if (info->issuer_name)
	g_string_append_printf(msg, _("\nIssuer name: %s"),
			       info->issuer_name);
    if (info->issuer_serial)
	g_string_append_printf(msg, _("\nIssuer serial number: %s"),
			       info->issuer_serial);
    if (info->chain_id)
	g_string_append_printf(msg, _("\nChain ID: %s"), info->chain_id);
    retval = msg->str;
    g_string_free(msg, FALSE);
    return retval;
}


#ifdef HAVE_GPG

#include <sys/wait.h>
#include <fcntl.h>

/* run gpg asynchronously to import a key */
typedef struct _spawned_gpg_T {
    gint child_pid;
    gint standard_error;
    GString *stderr_buf;
    GtkWindow *parent;
} spawned_gpg_T;

static gboolean check_gpg_child(gpointer data);

gboolean
gpg_run_import_key(const gchar * fingerprint, GtkWindow * parent)
{
    gchar **argv;
    spawned_gpg_T *spawned_gpg;
    gboolean spawnres;

    /* launch gpg... */
    argv = g_new(gchar *, 5);
    argv[0] = g_strdup(GPG_PATH);
    argv[1] = g_strdup("--no-greeting");
    argv[2] = g_strdup("--recv-keys");
    argv[3] = g_strdup(fingerprint);
    argv[4] = NULL;
    spawned_gpg = g_new0(spawned_gpg_T, 1);
    spawnres =
	g_spawn_async_with_pipes(NULL, argv, NULL,
				 G_SPAWN_DO_NOT_REAP_CHILD |
				 G_SPAWN_STDOUT_TO_DEV_NULL, NULL, NULL,
				 &spawned_gpg->child_pid, NULL, NULL,
				 &spawned_gpg->standard_error, NULL);
    g_strfreev(argv);
    if (spawnres == FALSE) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _
			     ("Could not launch %s to get the public key %s."),
			     GPG_PATH, fingerprint);
	g_free(spawned_gpg);
	return FALSE;
    }

    /* install an idle handler to check if the child returnd successfully. */
    fcntl(spawned_gpg->standard_error, F_SETFL, O_NONBLOCK);
    spawned_gpg->stderr_buf = g_string_new("");
    spawned_gpg->parent = parent;
    g_timeout_add(250, check_gpg_child, spawned_gpg);

    return TRUE;
}


static gboolean
check_gpg_child(gpointer data)
{
    spawned_gpg_T *spawned_gpg = (spawned_gpg_T *) data;
    int status, bytes_read;
    gchar buffer[1024], *gpg_message;
    GtkWidget *dialog;

    /* read input from the child and append it to the buffer */
    while ((bytes_read =
	    read(spawned_gpg->standard_error, buffer, 1023)) > 0) {
	buffer[bytes_read] = '\0';
	g_string_append(spawned_gpg->stderr_buf, buffer);
    }

    /* check if the child exited */
    if (waitpid(spawned_gpg->child_pid, &status, WNOHANG) !=
	spawned_gpg->child_pid)
	return TRUE;

    /* child exited, display some information... */
    close(spawned_gpg->standard_error);

    gpg_message =
	g_locale_to_utf8(spawned_gpg->stderr_buf->str, -1, NULL,
			 &bytes_read, NULL);
    gdk_threads_enter();
    if (WEXITSTATUS(status) > 0)
	dialog =
	    gtk_message_dialog_new(spawned_gpg->parent,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   _
				   ("Running gpg failed with return value %d:\n%s"),
				   WEXITSTATUS(status), gpg_message);
    else
	dialog =
	    gtk_message_dialog_new(spawned_gpg->parent,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   _("Running gpg successful:\n%s"),
				   gpg_message);
    g_free(gpg_message);
    g_string_free(spawned_gpg->stderr_buf, TRUE);
    g_free(spawned_gpg);

    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    gdk_threads_leave();

    return FALSE;
}

#endif				/* HAVE_GPG */


/* ==== local stuff ======================================================== */


static const gchar *
libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t validity)
{
    switch (validity) {
    case GPGME_VALIDITY_UNKNOWN:
	return _("unknown");
    case GPGME_VALIDITY_UNDEFINED:
	return _("undefined");
    case GPGME_VALIDITY_NEVER:
	return _("never");
    case GPGME_VALIDITY_MARGINAL:
	return _("marginal");
    case GPGME_VALIDITY_FULL:
	return _("full");
    case GPGME_VALIDITY_ULTIMATE:
	return _("ultimate");
    default:
	return _("bad validity");
    }
}


static gchar *
fix_EMail_info(gchar * str)
{
    gchar *p = strstr(str, "1.2.840.113549.1.9.1=#");
    GString *result;
    if (!p)
	return str;

    *p = '\0';
    p += 22;
    result = g_string_new(str);
    result = g_string_append(result, "EMail=");
    while (*p != '\0' && *p != ',') {
	gchar x = 0;

	if (*p >= 'A' && *p <= 'F')
	    x = (*p - 'A' + 10) << 4;
	else if (*p >= 'a' && *p <= 'f')
	    x = (*p - 'a' + 10) << 4;
	else if (*p >= '0' && *p <= '9')
	    x = (*p - '0') << 4;
	p++;
	if (*p != '\0' && *p != ',') {
	    if (*p >= 'A' && *p <= 'F')
		x += *p - 'A' + 10;
	    else if (*p >= 'a' && *p <= 'f')
		x += *p - 'a' + 10;
	    else if (*p >= '0' && *p <= '9')
		x += *p - '0';
	    p++;
	}
	result = g_string_append_c(result, x);
    }
    result = g_string_append(result, p);
    g_free(str);
    p = result->str;
    g_string_free(result, FALSE);
    return p;
}


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_PTR_COLUMN = 0,
    GPG_KEY_USER_ID_COLUMN,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_TRUST_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

static gchar *col_titles[] =
    { NULL, N_("User ID"), N_("Key ID"), N_("Length"), N_("Validity"),
    N_("Owner trust")
};

/* callback function if a new row is selected in the list */
static void
key_selection_changed_cb(GtkTreeSelection * selection, gpgme_key_t * key)
{
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
	gtk_tree_model_get(model, &iter, GPG_KEY_PTR_COLUMN, key, -1);
}


/*
 * Select a key for the mail address for_address from the gpgme_key_t's in keys
 * and return either the selected key or NULL if the dialog was cancelled.
 * secret_only controls the dialog message.
 */
static gpgme_key_t
select_key_from_list(const gchar * name, gboolean is_secret,
		     GMimeGpgmeContext * ctx, GList * keys)
{
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
    gpgme_protocol_t protocol;
    GtkWindow *parent;
    gpgme_key_t use_key = NULL;

    g_return_val_if_fail(ctx != NULL, NULL);
    g_return_val_if_fail(keys != NULL, NULL);
    protocol = gpgme_get_protocol(ctx->gpgme_ctx);
    parent = GTK_WINDOW(g_object_get_data(G_OBJECT(ctx), "parent-window"));

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(_("Select key"),
					 parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_CANCEL, NULL);
    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), vbox);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    if (is_secret)
	prompt =
	    g_strdup_printf(_("Select the private key for the signer %s"),
			    name);
    else
	prompt = g_strdup_printf(_
				 ("Select the public key for the recipient %s"),
				 name);
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

    model =
	gtk_tree_store_new(GPG_KEY_NUM_COLUMNS, G_TYPE_POINTER,
			   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT,
			   G_TYPE_STRING, G_TYPE_STRING);

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(key_selection_changed_cb), &use_key);

    /* add the keys */
    while (keys) {
	gpgme_key_t key = (gpgme_key_t) keys->data;
	if (key->subkeys) {
	    gchar *uid_info;
	    if (key->uids && key->uids->uid)
		uid_info = fix_EMail_info(g_strdup(key->uids->uid));
	    else
		uid_info = g_strdup("");
	    gtk_tree_store_append(GTK_TREE_STORE(model), &iter, NULL);
	    gtk_tree_store_set(GTK_TREE_STORE(model), &iter,
			       GPG_KEY_PTR_COLUMN, key,
			       GPG_KEY_USER_ID_COLUMN, uid_info,
			       GPG_KEY_ID_COLUMN, key->subkeys->keyid,
			       GPG_KEY_LENGTH_COLUMN,
			       key->subkeys->length,
			       GPG_KEY_VALIDITY_COLUMN,
			       libbalsa_gpgme_validity_to_gchar_short
			       (key->uids->validity),
			       GPG_KEY_TRUST_COLUMN,
			       libbalsa_gpgme_validity_to_gchar_short
			       (key->owner_trust), -1);
	    g_free(uid_info);
	}
	keys = g_list_next(keys);
    }

    g_object_unref(G_OBJECT(model));
    last_col = (protocol == GPGME_PROTOCOL_CMS) ? GPG_KEY_LENGTH_COLUMN :
	GPG_KEY_TRUST_COLUMN;
    for (i = 1; i <= last_col; i++) {
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();
	column =
	    gtk_tree_view_column_new_with_attributes(col_titles[i],
						     renderer, "text", i,
						     NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
	gtk_tree_view_column_set_resizable(column, TRUE);
    }

    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 300);
    gtk_widget_show_all(GTK_DIALOG(dialog)->vbox);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_OK)
	use_key = NULL;
    gtk_widget_destroy(dialog);

    return use_key;
}


#ifdef ENABLE_PCACHE
/* helper functions for the passphrase cache */
/*
 * destroy a pcache_elem_T object by first overwriting the encrypted data
 * with random crap and then freeing all allocated stuff
 */
static void
bf_destroy(pcache_elem_T * pcache_elem)
{
    bf_crypt_T *pf = pcache_elem->passphrase;
    gint n;

    fprintf(stderr, "%s for (pcache_elem_T *)%p\n", __FUNCTION__,
	    pcache_elem);
    if (pf) {
	unsigned char *p = pf->buf;

	if (p) {
	    while (pf->len--)
		*p++ = random();
	    g_free(pf->buf);
	}
	g_free(pf);
	pcache_elem->passphrase = NULL;
    }
    for (n = 0; n < MD5_DIGEST_LENGTH; n++)
	pcache_elem->name[n] = random();
    g_free(pcache_elem);
}


/*
 * Encrypt cleartext using the key bf_key and return the encrypted stuff as
 * a pointer to a bf_crypt_T struct or NULL on error
 */
static bf_crypt_T *
bf_encrypt(const gchar * cleartext, bf_key_T * bf_key)
{
    EVP_CIPHER_CTX ctx;
    unsigned char *outbuf;
    gint outlen, tmplen;
    bf_crypt_T *result;

    if (!cleartext)
	return NULL;

    EVP_CIPHER_CTX_init(&ctx);
    EVP_EncryptInit(&ctx, EVP_bf_cbc(), bf_key->key, bf_key->iv);
    outbuf = g_malloc(strlen(cleartext) + 9);	/* FIXME: correct/safe? */
    if (!EVP_EncryptUpdate
	(&ctx, outbuf, &outlen, (unsigned char *) cleartext,
	 strlen(cleartext) + 1)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    if (!EVP_EncryptFinal(&ctx, outbuf + outlen, &tmplen)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    outlen += tmplen;
    EVP_CIPHER_CTX_cleanup(&ctx);

    result = g_malloc(sizeof(bf_crypt_T));
    result->buf = outbuf;
    result->len = outlen;
    return result;
}


/*
 * decrypt crypttext using bf_key and return the string on success or NULL
 * on error
 */
static gchar *
bf_decrypt(bf_crypt_T * crypttext, bf_key_T * bf_key)
{
    EVP_CIPHER_CTX ctx;
    gchar *outbuf;
    gint outlen, tmplen;

    if (!crypttext)
	return NULL;

    EVP_CIPHER_CTX_init(&ctx);
    EVP_DecryptInit(&ctx, EVP_bf_cbc(), bf_key->key, bf_key->iv);
    outbuf = g_malloc(crypttext->len);	/* FIXME: correct/safe? */
    if (!EVP_DecryptUpdate(&ctx, outbuf, &outlen, crypttext->buf,
			   crypttext->len)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    if (!EVP_DecryptFinal(&ctx, outbuf + outlen, &tmplen)) {
	g_free(outbuf);
	EVP_CIPHER_CTX_cleanup(&ctx);
	return NULL;
    }
    EVP_CIPHER_CTX_cleanup(&ctx);

    return outbuf;
}


/*
 * timeout function to check for expired cached passphrases
 */
static gboolean
pcache_timeout(pcache_T * cache)
{
    time_t now = time(NULL);
    GList *list = cache->cache;

    while (list) {
	pcache_elem_T *elem = (pcache_elem_T *) list->data;
	if (elem->expires <= now) {
	    GList *next = g_list_next(list);

	    cache->cache = g_list_remove(cache->cache, elem);
	    bf_destroy(elem);
	    list = next;
	} else
	    list = g_list_next(list);
    }

    return TRUE;
}


/*
 * Check if the passphrase requested for uid_hint is already in cache. If it
 * is there and gpgme complained about a bad passphrase, erase it and return
 * NULL, otherwise return the passphrase. If it is not in the cache, the
 * routine also returns NULL.
 */
static gchar *
check_cache(pcache_T * cache, const gchar * uid_hint, int prev_was_bad)
{
    unsigned char tofind[MD5_DIGEST_LENGTH];
    GList *list;

    /* exit immediately if no data is present */
    if (!cache->enable || !cache->cache)
	return NULL;

    /* try to find the passphrase in the cache */
    MD5(uid_hint, strlen(uid_hint), tofind);
    list = cache->cache;
    while (list) {
	pcache_elem_T *elem = (pcache_elem_T *) list->data;
	if (!memcmp(tofind, elem->name, MD5_DIGEST_LENGTH)) {
	    /* check if the last entry was bad */
	    if (prev_was_bad) {
		cache->cache = g_list_remove(cache->cache, elem);
		bf_destroy(elem);
		return NULL;
	    } else
		return bf_decrypt(elem->passphrase, &cache->bf_key);
	}
	list = g_list_next(list);
    }

    /* not found */
    return NULL;
}


/*
 * Try to destroy the cache info (called upon SIGSEGV)
 */
static void
destroy_cache(int signo)
{
    if (pcache) {
	int n;

	fprintf(stderr,
		"caught signal %d, destroy passphrase cache keys...\n",
		signo);
	for (n = 0; n < 16; n++)
	    pcache->bf_key.key[n] = 0;
	for (n = 0; n < 8; n++)
	    pcache->bf_key.iv[n] = 0;
	pcache->cache = NULL;
    }
    segvhandler(signo);
}


/*
 * destroy all cached passphrases and the cache itself (called upon exiting
 * the main gtk loop)
 */
static gint
clear_pcache(pcache_T * cache)
{
    gint n;

    if (cache) {
	fprintf(stderr, "erasing password cache at (pcache_T *)%p\n",
		cache);
	if (cache->cache) {
	    g_list_foreach(cache->cache, (GFunc) bf_destroy, NULL);
	    g_list_free(cache->cache);
	    cache->cache = NULL;
	}

	for (n = 0; n < 16; n++)
	    cache->bf_key.key[n] = random();
	for (n = 0; n < 8; n++)
	    cache->bf_key.iv[n] = random();
    }

    return 0;
}


/*
 * initialise the passphrase cache
 */
static pcache_T *
init_pcache()
{
    pcache_T *cache;
    struct stat cfg_stat;

    /* be sure that openssl is initialised */
    ssl_init();			/* from libmutt */

    /* initialise the internal passphrase cache stuff */
    cache = g_new0(pcache_T, 1);

    /* the cfg file must be owned by root and not group/world writable */
    if (stat(BALSA_DATA_PREFIX "/gpg-cache", &cfg_stat) == -1) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "file %s/gpg-cache not found, disable passphrase cache",
			     BALSA_DATA_PREFIX);
	return cache;
    }
    if (cfg_stat.st_uid != 0 || !S_ISREG(cfg_stat.st_mode) ||
	(cfg_stat.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
	libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			     "%s/gpg-cache must be a regular file, owened by root with (max) permissions 0644",
			     BALSA_DATA_PREFIX);
	return cache;
    }

    /* get the passphrase cache config */
    gnome_config_push_prefix("=" BALSA_DATA_PREFIX
			     "/gpg-cache=/PassphraseCache/");
    cache->enable =
	gnome_config_get_bool_with_default("enable", &cache->enable);
    cache->max_mins =
	gnome_config_get_int_with_default("minutesMax", &cache->max_mins);
    if (cache->max_mins <= 0)
	cache->enable = FALSE;
    gnome_config_pop_prefix();
    libbalsa_information(LIBBALSA_INFORMATION_DEBUG,
			 "passphrase cache is %s",
			 (cache->enable) ? "enabled" : "disabled");

    if (cache->enable) {
	/* generate a random blowfish key */
	RAND_bytes(cache->bf_key.key, 16);
	RAND_bytes(cache->bf_key.iv, 8);

	/* destroy the cache when exiting the application */
	gtk_quit_add(0, (GtkFunction) clear_pcache, cache);

	/* install a segv handler to destroy the cache on crash */
	segvhandler = signal(SIGSEGV, destroy_cache);

	/* add a timeout function (called every 5 secs) to erase expired
	   passphrases */
	g_timeout_add(5000, (GSourceFunc) pcache_timeout, cache);
    }

    return cache;
}


/*
 * callback for (de)activating the timeout spinbutton
 */
static void
activate_mins(GtkToggleButton * btn, GtkWidget * target)
{
    gtk_widget_set_sensitive(target, gtk_toggle_button_get_active(btn));
}

#endif				/* ENABLE_PCACHE */


/*
 * display a dialog to read the passphrase
 */
static gchar *
#ifdef ENABLE_PCACHE
get_passphrase_real(GMimeGpgmeContext * ctx, const gchar * uid_hint,
		    int prev_was_bad, pcache_T * pcache)
#else
get_passphrase_real(GMimeGpgmeContext * ctx, const gchar * uid_hint,
		    int prev_was_bad)
#endif
{
    static GdkPixbuf *padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    gchar *prompt, *passwd;
#ifdef ENABLE_PCACHE
    GtkWidget *cache_but = NULL, *cache_min = NULL;
#endif
    const gchar *title =
	g_object_get_data(G_OBJECT(ctx), "passphrase-title");
    GtkWindow *parent = g_object_get_data(G_OBJECT(ctx), "parent-window");

    /* FIXME: create dialog according to the Gnome HIG */
    dialog = gtk_dialog_new_with_buttons(title, parent,
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_STOCK_OK, GTK_RESPONSE_OK,
					 GTK_STOCK_CANCEL,
					 GTK_RESPONSE_CANCEL, NULL);
    hbox = gtk_hbox_new(FALSE, 12);
    gtk_container_set_border_width(GTK_CONTAINER(hbox), 12);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog)->vbox), hbox);

    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (!padlock_keyhole)
	padlock_keyhole =
	    gdk_pixbuf_new_from_xpm_data(padlock_keyhole_xpm);
    gtk_box_pack_start(GTK_BOX(vbox),
		       gtk_image_new_from_pixbuf(padlock_keyhole), FALSE,
		       FALSE, 0);
    vbox = gtk_vbox_new(FALSE, 12);
    gtk_container_add(GTK_CONTAINER(hbox), vbox);
    if (prev_was_bad)
	prompt =
	    g_strdup_printf(_
			    ("The passphrase for this key was bad, please try again!\n\nKey: %s"),
			    uid_hint);
    else
	prompt =
	    g_strdup_printf(_
			    ("Please enter the passphrase for the secret key!\n\nKey: %s"),
			    uid_hint);
    gtk_container_add(GTK_CONTAINER(vbox), gtk_label_new(prompt));
    g_free(prompt);
    entry = gtk_entry_new();
    gtk_container_add(GTK_CONTAINER(vbox), entry);

#ifdef ENABLE_PCACHE
    if (pcache->enable) {
	hbox = gtk_hbox_new(FALSE, 12);
	gtk_container_add(GTK_CONTAINER(vbox), hbox);
	cache_but =
	    gtk_check_button_new_with_label(_("remember passphrase for"));
	gtk_box_pack_start(GTK_BOX(hbox), cache_but, FALSE, FALSE, 0);
	cache_min =
	    gtk_spin_button_new_with_range(1.0, pcache->max_mins, 1.0);
	gtk_widget_set_sensitive(cache_min, FALSE);
	gtk_box_pack_start(GTK_BOX(hbox), cache_min, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_("minutes")),
			   FALSE, FALSE, 0);
	g_signal_connect(G_OBJECT(cache_but), "clicked",
			 (GCallback) activate_mins, cache_min);
    }
#endif

    gtk_widget_show_all(GTK_WIDGET(GTK_DIALOG(dialog)->vbox));
    gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
    gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);

    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
    gtk_widget_grab_focus(entry);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK)
	passwd = g_strdup(gtk_entry_get_text(GTK_ENTRY(entry)));
    else
	passwd = NULL;

#ifdef ENABLE_PCACHE
    if (pcache->enable) {
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(cache_but))) {
	    pcache_elem_T *elem = g_new(pcache_elem_T, 1);

	    MD5(uid_hint, strlen(uid_hint), elem->name);
	    elem->passphrase = bf_encrypt(passwd, &pcache->bf_key);
	    elem->expires = time(NULL) +
		60 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(cache_min));
	    pcache->cache = g_list_append(pcache->cache, elem);
	}
    }
#endif

    gtk_widget_destroy(dialog);

    return passwd;
}


#ifdef BALSA_USE_THREADS
/* FIXME: is this really necessary? */
typedef struct {
    pthread_cond_t cond;
    GMimeGpgmeContext *ctx;
    const gchar *desc;
    gint was_bad;
#ifdef ENABLE_PCACHE
    pcache_T *pcache;
#endif
    gchar *res;
} AskPassphraseData;

/* get_passphrase_idle:
   called in MT mode by the main thread.
 */
static gboolean
get_passphrase_idle(gpointer data)
{
    AskPassphraseData *apd = (AskPassphraseData *) data;
    gdk_threads_enter();
#ifdef ENABLE_PCACHE
    apd->res = get_passphrase_real(apd->ctx, apd->desc, apd->was_bad,
				   apd->pcache);
#else
    apd->res = get_passphrase_real(apd->ctx, apd->desc, apd->was_bad);
#endif
    gdk_threads_leave();
    pthread_cond_signal(&apd->cond);
    return FALSE;
}
#endif


/*
 * Helper function: overwrite a sting in memory with random data
 */
static inline void
wipe_string(gchar * password)
{
    while (*password)
	*password++ = random();
}


/*
 * Called by gpgme to get the passphrase for a key.
 */
static gpgme_error_t
get_passphrase_cb(void *opaque, const char *uid_hint,
		  const char *passph_info, int prev_was_bad, int fd)
{
    GMimeGpgmeContext *context;
    gchar *passwd = NULL;

    if (!opaque || !GMIME_IS_GPGME_CONTEXT(opaque)) {
	write(fd, "\n", 1);
	return GPG_ERR_USER_1;
    }
    context = GMIME_GPGME_CONTEXT(opaque);

#ifdef ENABLE_PCACHE
    if (!pcache)
	pcache = init_pcache();

    /* check if we have the passphrase already cached... */
    if ((passwd = check_cache(pcache, uid_hint, prev_was_bad))) {
	write(fd, passwd, strlen(passwd));
	write(fd, "\n", 1);
	wipe_string(passwd);
	g_free(passwd);
	return GPG_ERR_NO_ERROR;
    }
#endif

#ifdef BALSA_USE_THREADS
    if (pthread_self() == libbalsa_get_main_thread())
#ifdef ENABLE_PCACHE
	passwd =
	    get_passphrase_real(context, uid_hint, prev_was_bad, pcache);

#else
	passwd = get_passphrase_real(context, uid_hint, prev_was_bad);
#endif
    else {
	static pthread_mutex_t get_passphrase_lock =
	    PTHREAD_MUTEX_INITIALIZER;
	AskPassphraseData apd;

	pthread_mutex_lock(&get_passphrase_lock);
	pthread_cond_init(&apd.cond, NULL);
	apd.ctx = context;
	apd.desc = uid_hint;
	apd.was_bad = prev_was_bad;
#ifdef ENABLE_PCACHE
	apd.pcache = pcache;
#endif
	g_idle_add(get_passphrase_idle, &apd);
	pthread_cond_wait(&apd.cond, &get_passphrase_lock);

	pthread_cond_destroy(&apd.cond);
	pthread_mutex_unlock(&get_passphrase_lock);
	pthread_mutex_destroy(&get_passphrase_lock);
	passwd = apd.res;
    }
#else
#ifdef ENABLE_PCACHE
    passwd = get_passphrase_real(context, uid_hint, prev_was_bad, pcache);
#else
    passwd = get_passphrase_real(context, uid_hint, prev_was_bad);
#endif				/* ENABLE_PCACHE */
#endif				/* BALSA_USE_THREADS */

    if (!passwd) {
	write(fd, "\n", 1);
	return GPG_ERR_CANCELED;
    }

    /* send the passphrase and erase the string */
    write(fd, passwd, strlen(passwd));
    wipe_string(passwd);
    g_free(passwd);
    write(fd, "\n", 1);
    return GPG_ERR_NO_ERROR;
}


/*
 * return TRUE is gpg is currently updating the trust database (indicated by
 * the file ~/.gnupg/trustdb.gpg.lock)
 */
static gboolean
gpg_updates_trustdb(void)
{
    static gchar *lockname = NULL;
    struct passwd *pwent;
    struct stat stat_buf;

    if (!lockname)
	if ((pwent = getpwuid(getuid())))
	    lockname =
		g_strdup_printf("%s/.gnupg/trustdb.gpg.lock",
				pwent->pw_dir);
    if (!stat(lockname, &stat_buf)) {
	libbalsa_information(LIBBALSA_INFORMATION_ERROR,
			     _
			     ("GnuPG is rebuilding the trust database and is currently unavailable."),
			     _("Try again later."));
	return TRUE;
    } else
	return FALSE;
}

#endif				/* HAVE_GPGME */
