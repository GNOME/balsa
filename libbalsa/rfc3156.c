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

#include <string.h>
#include <gpgme.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libgnome/libgnome.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "rfc3156.h"

#include "gmime-gpgme-context.h"
#include "gmime-gpgme-signature.h"
#include "gmime-part-rfc2440.h"

#ifdef HAVE_SMIME
#  include "gmime-application-pkcs7.h"
#endif

#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#  include "misc.h"
#endif

#include "mime-stream-shared.h"
#include "padlock-keyhole.xpm"
#include "i18n.h"


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
static gboolean accept_low_trust_key(const gchar * name,
				     gpgme_user_id_t uid,
				     GMimeGpgmeContext * ctx);
static gboolean gpg_updates_trustdb(void);
static gchar *fix_EMail_info(gchar * str);


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
	    g_mime_content_type_new_from_string(body->content_type);
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
    g_return_val_if_fail(body->content_type != NULL, 0);

    if (body_is_type(body, "multipart", "signed")) {
	gchar *protocol =
	    libbalsa_message_body_get_parameter(body, "protocol");
	gchar *micalg =
	    libbalsa_message_body_get_parameter(body, "micalg");

	result = LIBBALSA_PROTECT_SIGN;
	if (protocol && body->parts && body->parts->next) {
	    if ((!g_ascii_strcasecmp("application/pkcs7-signature",
				     protocol)
		 && body_is_type(body->parts->next, "application", 
				 "pkcs7-signature")) ||
		(!g_ascii_strcasecmp("application/x-pkcs7-signature",
				     protocol)
		 && body_is_type(body->parts->next, "application",
				 "x-pkcs7-signature"))) {
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
	if (!g_ascii_strcasecmp("enveloped-data", smime_type) ||
	    !g_ascii_strcasecmp("signed-data", smime_type))
	    result |= LIBBALSA_PROTECT_ENCRYPT;
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
 * Note: In RFC 2633 mode (GPGME_PROTOCOL_CMS), this function creates a
 * multipart/signed instead of an application/pkcs7-mime, as the latter one
 * doesn't contain a cleartext also readable for MUA's without S/MIME support.
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
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

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
    if (g_getenv("GPG_AGENT_INFO"))
	ctx->passphrase_cb = NULL;  /* use gpg-agent */
    else {
	ctx->passphrase_cb = get_passphrase_cb;
	g_object_set_data(G_OBJECT(ctx), "passphrase-info",
			  _
			  ("Enter passphrase to unlock the secret key for signing"));
    }
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
    g_object_unref(G_OBJECT(*content));
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
			     gpgme_protocol_t protocol, gboolean always_trust,
			     GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GMimeObject *encrypted_obj = NULL;
    GPtrArray *recipients;
    int result = -1;
    GError *error = NULL;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

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
    if (!always_trust)
	ctx->key_trust_cb = accept_low_trust_key;
    ctx->always_trust_uid = always_trust;
    g_object_set_data(G_OBJECT(ctx), "parent-window", parent);

    /* convert the key list to a GPtrArray */
    recipients = g_ptr_array_new();
    while (rfc822_for) {
	g_ptr_array_add(recipients, rfc822_for->data);
	rfc822_for = g_list_next(rfc822_for);
    }

    /* encrypt: multipart/encrypted for RFC 3156, application/pkcs7-mime for
       RFC 2633 */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	GMimeMultipartEncrypted *mpe = g_mime_multipart_encrypted_new();

	encrypted_obj = GMIME_OBJECT(mpe);
	result = 
	    g_mime_multipart_encrypted_encrypt(mpe, *content,
					       GMIME_CIPHER_CONTEXT(ctx),
					       recipients, &error);
    }
#ifdef HAVE_SMIME
    else {
	GMimePart *pkcs7 =
	    g_mime_part_new_with_type("application", "pkcs7-mime");

	encrypted_obj = GMIME_OBJECT(pkcs7);
	ctx->singlepart_mode = TRUE;
	result = 
	    g_mime_application_pkcs7_encrypt(pkcs7, *content,
					     GMIME_CIPHER_CONTEXT(ctx),
					     recipients, &error);
    }
#endif

    /* error checking */
    if (result != 0) {
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
	g_object_unref(encrypted_obj);
	return FALSE;
    }

    g_ptr_array_free(recipients, FALSE);
    g_object_unref(G_OBJECT(*content));
    *content = GMIME_OBJECT(encrypted_obj);
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
				  gboolean always_trust,
				  GtkWindow * parent)
{
    GMimeObject *signed_object;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_signer != NULL, FALSE);
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* we want to be able to restore */
    signed_object = *content;
    g_object_ref(G_OBJECT(signed_object));

    if (!libbalsa_sign_mime_object(&signed_object, rfc822_signer, protocol,
				   parent))
	return FALSE;

    if (!libbalsa_encrypt_mime_object(&signed_object, rfc822_for, protocol,
				      always_trust, parent)) {
	g_object_unref(G_OBJECT(signed_object));
	return FALSE;
    }
    g_object_unref(G_OBJECT(*content));
    *content = signed_object;

    return TRUE;
}


/*
 * Check the signature of body (which must be a multipart/signed). On
 * success, set the sig_info field of the signature part. It succeeds
 * if all the data needed to verify the signature (gpg database, the
 * complete signed part itself) were available and the verification
 * was attempted. Please observe that failure means in this context a
 * temporary one. Information about failed signature verifications are
 * passed through LibBalsaBody::sig_info.
 */
gboolean
libbalsa_body_check_signature(LibBalsaMessageBody * body,
			      gpgme_protocol_t protocol)
{
    GMimeSession *session;
    GMimeCipherContext *ctx;
    GMimeSignatureValidity *valid;
    GError *error = NULL;
    GMimeStream *stream;

    /* paranoia checks */
    g_return_val_if_fail(body, FALSE);
    g_return_val_if_fail(body->mime_part != NULL, FALSE);
    g_return_val_if_fail(body->message, FALSE);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

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

    /* S/MIME uses the protocol application/pkcs7-signature, but some ancient
       mailers, not yet knowing RFC 2633, use application/x-pkcs7-signature,
       so tweak the context if necessary... */
    if (protocol == GPGME_PROTOCOL_CMS) {
	const char * cms_protocol = 
	    g_mime_object_get_content_type_parameter(GMIME_OBJECT (body->mime_part),
						     "protocol");
	if (!g_ascii_strcasecmp(cms_protocol, "application/x-pkcs7-signature"))
	    ctx->sign_protocol = cms_protocol;
    }

    /* verify the signature */

    stream = libbalsa_message_stream(body->message);
    libbalsa_mime_stream_shared_lock(stream);
    valid = g_mime_multipart_signed_verify(GMIME_MULTIPART_SIGNED
					   (body->mime_part), ctx, &error);
    libbalsa_mime_stream_shared_unlock(stream);
    g_object_unref(stream);

    if (valid == NULL) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("signature verification failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("signature verification failed"));
    }
    if (GMIME_GPGME_CONTEXT(ctx)->sig_state) {
	body->parts->next->sig_info = GMIME_GPGME_CONTEXT(ctx)->sig_state;
	g_object_ref(G_OBJECT(body->parts->next->sig_info));
    }
    g_mime_signature_validity_free(valid);
    g_object_unref(ctx);
    g_object_unref(session);
    return TRUE;
}


/*
 * Body points to an application/pgp-encrypted body. If decryption is
 * successful, it is freed, and the routine returns a pointer to the chain of
 * decrypted bodies. Otherwise, the original body is returned.
 */
LibBalsaMessageBody *
libbalsa_body_decrypt(LibBalsaMessageBody * body,
		      gpgme_protocol_t protocol, GtkWindow * parent)
{
    GMimeSession *session;
    GMimeGpgmeContext *ctx;
    GMimeObject *mime_obj = NULL;
    GError *error = NULL;
    LibBalsaMessage *message;
#ifdef HAVE_SMIME
    gboolean smime_signed = FALSE;
#endif

    /* paranoia checks */
    g_return_val_if_fail(body != NULL, body);
    g_return_val_if_fail(body->mime_part != NULL, body);
    g_return_val_if_fail(body->message != NULL, body);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return body;

    /* sanity checks... */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	if (!GMIME_IS_MULTIPART_ENCRYPTED(body->mime_part))
	    return body;
    }
#ifdef HAVE_SMIME
    else {
	const char * smime_type = 
	    g_mime_object_get_content_type_parameter(body->mime_part,
						     "smime-type");

	if (!smime_type || !GMIME_IS_PART(body->mime_part))
	    return body;
	if (!g_ascii_strcasecmp(smime_type, "signed-data"))
	    smime_signed = TRUE;
	else if (!g_ascii_strcasecmp(smime_type, "enveloped-data"))
	    smime_signed = FALSE;
	else
	    return body;
    }
#endif

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
    if (g_getenv("GPG_AGENT_INFO"))
	ctx->passphrase_cb = NULL;  /* use gpg-agent */
    else {
	ctx->passphrase_cb = get_passphrase_cb;
	g_object_set_data(G_OBJECT(ctx), "parent-window", parent);
	g_object_set_data(G_OBJECT(ctx), "passphrase-info",
			  _("Enter passphrase to decrypt message"));
    }

    if (protocol == GPGME_PROTOCOL_OpenPGP)
	mime_obj =
	    g_mime_multipart_encrypted_decrypt(GMIME_MULTIPART_ENCRYPTED(body->mime_part),
					       GMIME_CIPHER_CONTEXT(ctx),
					       &error);
#ifdef HAVE_SMIME
    else if (smime_signed) {
	GMimeSignatureValidity *valid;

	ctx->singlepart_mode = TRUE;
	mime_obj =
	    g_mime_application_pkcs7_verify(GMIME_PART(body->mime_part),
					    &valid,
					    GMIME_CIPHER_CONTEXT(ctx),
					    &error);
	g_mime_signature_validity_free(valid);
    } else
	mime_obj =
	    g_mime_application_pkcs7_decrypt(GMIME_PART(body->mime_part),
					       GMIME_CIPHER_CONTEXT(ctx),
					       &error);
#endif

    /* check the result */
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
    message = body->message;
    libbalsa_message_body_free(body);
    body = libbalsa_message_body_new(message);

    /* remember that is was encrypted */
    if (protocol == GPGME_PROTOCOL_OpenPGP)
	body->was_encrypted = TRUE;
#ifdef HAVE_SMIME
    else
	body->was_encrypted = !smime_signed;
#endif

    libbalsa_message_body_set_mime_body(body, mime_obj);
    g_object_unref(G_OBJECT(mime_obj));
    if (ctx->sig_state && ctx->sig_state->status != GPG_ERR_NOT_SIGNED) {
	g_object_ref(ctx->sig_state);
	body->sig_info = ctx->sig_state;
    }
    g_object_unref(ctx);
    g_object_unref(session);

    return body;
}



/* routines dealing with RFC2440 */
gboolean
libbalsa_rfc2440_sign_encrypt(GMimePart * part, const gchar * sign_for,
			      GList * encrypt_for, gboolean always_trust,
			      GtkWindow * parent)
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
	if (g_getenv("GPG_AGENT_INFO"))
	    ctx->passphrase_cb = NULL;  /* use gpg-agent */
	else {
	    ctx->passphrase_cb = get_passphrase_cb;
	    g_object_set_data(G_OBJECT(ctx), "passphrase-info",
			      _
			      ("Enter passphrase to unlock the secret key for signing"));
	}
    }
    ctx->key_select_cb = select_key_from_list;
    if (!always_trust)
	ctx->key_trust_cb = accept_low_trust_key;
    ctx->always_trust_uid = always_trust;
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
    GMimeSignatureValidity *valid;
    GError *error = NULL;
    gpgme_error_t retval;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);

    /* free any old signature */
    if (sig_info && *sig_info) {
	g_object_unref(*sig_info);
	*sig_info = NULL;
    }

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
	g_object_ref(ctx->sig_state);
	*sig_info = ctx->sig_state;
    }

    /* clean up */
    g_mime_signature_validity_free(valid);
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

    /* free any old signature */
    if (sig_info && *sig_info) {
	g_object_unref(*sig_info);
	*sig_info = NULL;
    }

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
	return GPG_ERR_GENERAL;
    }

    /* set the callback for the passphrase */
    if (g_getenv("GPG_AGENT_INFO"))
	ctx->passphrase_cb = NULL;  /* use gpg-agent */
    else {
	ctx->passphrase_cb = get_passphrase_cb;
	g_object_set_data(G_OBJECT(ctx), "passphrase-info",
			  _("Enter passphrase to decrypt message"));
	g_object_set_data(G_OBJECT(ctx), "parent-window", parent);
    }

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
	if (sig_info && ctx->sig_state->status != GPG_ERR_NOT_SIGNED) {
	    g_object_ref(ctx->sig_state);
	    *sig_info = ctx->sig_state;
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
    case GPG_ERR_CERT_REVOKED:
	return _
	    ("The signature is valid but the key used to verify the signature has been revoked.");
    case GPG_ERR_BAD_SIGNATURE:
	return _
	    ("The signature is invalid.");
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
	    ("GnuPG is rebuilding the trust database and is currently unavailable.");
    default:
	g_message("stat %d: %s %s", stat, gpgme_strsource(stat), gpgme_strerror(stat));
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
    struct tm date;
    char buf[128];
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
	localtime_r(&info->sign_time, &date);
	strftime(buf, sizeof(buf), date_string, &date);
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
	localtime_r(&info->key_created, &date);
	strftime(buf, sizeof(buf), date_string, &date);
	g_string_append_printf(msg, _("\nKey created on: %s"), buf);
    }
    if (info->key_expires) {
	localtime_r(&info->key_expires, &date);
	strftime(buf, sizeof(buf), date_string, &date);
	g_string_append_printf(msg, _("\nKey expires on: %s"), buf);
    }
    if (info->key_revoked || info->key_expired || info->key_disabled ||
       info->key_invalid) {
       GString * attrs = g_string_new("");
       int count = 0;

       if (info->key_revoked) {
           count++;
           attrs = g_string_append(attrs, _(" revoked"));
       }
       if (info->key_expired) {
           if (count++)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" expired"));
       }
       if (info->key_disabled) {
           if (count)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" disabled"));
       }
       if (info->key_invalid) {
           if (count)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" invalid"));
       }
       if (count > 1)
           g_string_append_printf(msg, _("\nKey attributes:%s"), attrs->str);
       else
           g_string_append_printf(msg, _("\nKey attribute:%s"), attrs->str);
       g_string_free(attrs, TRUE);
    }
    if (info->issuer_name) {
	gchar * issuer = fix_EMail_info(g_strdup(info->issuer_name));

	g_string_append_printf(msg, _("\nIssuer name: %s"), issuer);
	g_free(issuer);
    }
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
    int status;
    ssize_t bytes_read;
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
			 NULL, NULL);
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


#define OID_EMAIL       "1.2.840.113549.1.9.1=#"
#define OID_EMAIL_LEN   22
static gchar *
fix_EMail_info(gchar * str)
{
    gchar *p;
    GString *result;

    /* check for any EMail info */
    p = strstr(str, OID_EMAIL);
    if (!p)
	return str;

    *p = '\0';
    p += OID_EMAIL_LEN;
    result = g_string_new(str);
    while (p) {
	gchar *next;

	result = g_string_append(result, "EMail=");
	/* convert the info from hex until we reach some other char */
	while (g_ascii_isxdigit(*p)) {
	    gchar c = g_ascii_xdigit_value(*p++) << 4;

	    if (g_ascii_isxdigit(*p))
		result =
		    g_string_append_c(result, c + g_ascii_xdigit_value(*p++));
	}
	
	/* find more */
	next = strstr(p, OID_EMAIL);
	if (next) {
	    *next = '\0';
	    next += OID_EMAIL_LEN;
	}
	result = g_string_append(result, p);
	p = next;
    }
    g_free(str);
    p = result->str;
    g_string_free(result, FALSE);
    return p;
}


/* stuff to get a key fingerprint from a selection list */
enum {
    GPG_KEY_USER_ID_COLUMN = 0,
    GPG_KEY_ID_COLUMN,
    GPG_KEY_LENGTH_COLUMN,
    GPG_KEY_VALIDITY_COLUMN,
    GPG_KEY_PTR_COLUMN,
    GPG_KEY_NUM_COLUMNS
};

static gchar *col_titles[] =
    { N_("User ID"), N_("Key ID"), N_("Length"), N_("Validity") };

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
    gchar *upcase_name;
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
	gtk_tree_store_new(GPG_KEY_NUM_COLUMNS,
			   G_TYPE_STRING,   /* user ID */
			   G_TYPE_STRING,   /* key ID */
			   G_TYPE_INT,      /* length */
			   G_TYPE_STRING,   /* validity (gpg encrypt only) */
			   G_TYPE_POINTER); /* key */

    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(G_OBJECT(selection), "changed",
		     G_CALLBACK(key_selection_changed_cb), &use_key);

    /* add the keys */
    upcase_name = g_ascii_strup(name, -1);
    while (keys) {
	gpgme_key_t key = (gpgme_key_t) keys->data;
	gpgme_subkey_t subkey = key->subkeys;
	gpgme_user_id_t uid = key->uids;
	gchar *uid_info = NULL;
	gboolean uid_found;

	/* find the relevant subkey */
	while (subkey && ((is_secret && !subkey->can_sign) ||
			  (!is_secret && !subkey->can_encrypt)))
	    subkey = subkey->next;

	/* find the relevant uid */
	uid_found = FALSE;
	while (uid && !uid_found) {
	    g_free(uid_info);
	    uid_info = fix_EMail_info(g_strdup(uid->uid));

	    /* check the email field which may or may not be present */
	    if (uid->email && !g_ascii_strcasecmp(uid->email, name))
		uid_found = TRUE;
	    else {
		/* no email or no match, check the uid */
		gchar * upcase_uid = g_ascii_strup(uid_info, -1);
		
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
			       libbalsa_gpgme_validity_to_gchar_short
			       (uid->validity),
			       GPG_KEY_PTR_COLUMN, key,
			       -1);
	}
	g_free(uid_info);
	keys = g_list_next(keys);
    }
    g_free(upcase_name);

    g_object_unref(G_OBJECT(model));
    /* show the validity only if we are asking for a gpg public key */
    last_col = (protocol == GPGME_PROTOCOL_CMS || is_secret) ?
	GPG_KEY_LENGTH_COLUMN :	GPG_KEY_VALIDITY_COLUMN;
    for (i = 0; i <= last_col; i++) {
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


/*
 * Display a dialog to select whether a key with a low trust level shall be accepted
 */
static gboolean
accept_low_trust_key(const gchar * name, gpgme_user_id_t uid,
		     GMimeGpgmeContext * ctx)
{
    GtkWidget *dialog;
    GtkWindow *parent;
    gint result;
    gchar *message1;
    gchar *message2;

    /* paranoia checks */
    g_return_val_if_fail(ctx != NULL, FALSE);
    g_return_val_if_fail(uid != NULL, FALSE);
    parent = GTK_WINDOW(g_object_get_data(G_OBJECT(ctx), "parent-window"));
    
    /* build the message */
    message1 =
	g_strdup_printf(_("Insufficient trust for recipient %s"), name);
    message2 =
	g_strdup_printf(_("The validity of the key with user ID \"%s\" is \"%s\"."),
			uid->uid,
			libbalsa_gpgme_validity_to_gchar_short(uid->validity));
    dialog = 
	gtk_message_dialog_new_with_markup(parent,
					   GTK_DIALOG_DESTROY_WITH_PARENT,
					   GTK_MESSAGE_WARNING,
					   GTK_BUTTONS_YES_NO,
					   "<b>%s</b>\n\n%s\n%s",
					   message1,
					   message2,
					   _("Use this key anyway?"));
			      
    /* ask the user */
    result = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return result == GTK_RESPONSE_YES;
}


/*
 * display a dialog to read the passphrase
 */
static gchar *
get_passphrase_real(GMimeGpgmeContext * ctx, const gchar * uid_hint,
		    int prev_was_bad)
{
    static GdkPixbuf *padlock_keyhole = NULL;
    GtkWidget *dialog, *entry, *vbox, *hbox;
    gchar *prompt, *passwd;
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
    apd->res = get_passphrase_real(apd->ctx, apd->desc, apd->was_bad);
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
    if (!libbalsa_am_i_subthread())
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
