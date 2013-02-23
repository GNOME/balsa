/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2013 Stuart Parmenter and others,
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
#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "rfc3156.h"

#ifdef HAVE_GPGME

#include <string.h>
#include <gpgme.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>

#include "libbalsa.h"
#include "libbalsa_private.h"

#include "gmime-multipart-crypt.h"
#include "gmime-gpgme-signature.h"
#include "gmime-part-rfc2440.h"

#ifdef HAVE_SMIME
#  include "gmime-application-pkcs7.h"
#endif

#ifdef BALSA_USE_THREADS
#  include <pthread.h>
#  include "misc.h"
#endif

#if HAVE_MACOSX_DESKTOP
#  include "macosx-helpers.h"
#endif

#include <glib/gi18n.h>


/* local prototypes */
static gboolean gpg_updates_trustdb(void);
static gboolean have_pub_key_for(gpgme_ctx_t gpgme_ctx,
				 InternetAddressList * recipients);


/* ==== public functions =================================================== */
static gboolean
body_is_type(LibBalsaMessageBody * body, const gchar * type,
	     const gchar * sub_type)
{
    gboolean retval;

    if (body->mime_part) {
	GMimeContentType *content_type =
	    g_mime_object_get_content_type(body->mime_part);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
    } else {
	GMimeContentType *content_type =
	    g_mime_content_type_new_from_string(body->content_type);
	retval = g_mime_content_type_is_type(content_type, type, sub_type);
	g_object_unref(content_type);
    }

    return retval;
}


/* return TRUE if we can encrypt for every recipient in the recipients list
 * using protocol */
gboolean
libbalsa_can_encrypt_for_all(InternetAddressList * recipients,
			     gpgme_protocol_t protocol)
{
    gpgme_ctx_t gpgme_ctx;
    gboolean result;

    /* silent paranoia checks */
    if (!recipients)
	return TRUE;  /* we can of course encrypt for nobody... */
#ifndef HAVE_SMIME
    if (protocol == GPGME_PROTOCOL_OpenPGP)
	return FALSE;
#endif

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* create the gpgme context and set the protocol */
    if (gpgme_new(&gpgme_ctx) != GPG_ERR_NO_ERROR)
	return FALSE;
    if (gpgme_set_protocol(gpgme_ctx, protocol) != GPG_ERR_NO_ERROR) {
	gpgme_release(gpgme_ctx);
	return FALSE;
    }

    /* loop over all recipients and try to find valid keys */
    result = have_pub_key_for(gpgme_ctx, recipients);
    gpgme_release(gpgme_ctx);

    return result;
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
			  gpgme_protocol_t protocol, GtkWindow * parent,
			  GError ** error)
{
    GMimeMultipartSigned *mps;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

    /* call gpgme to create the signature */
    if (!(mps = g_mime_multipart_signed_new())) {
	return FALSE;
    }

    if (g_mime_gpgme_mps_sign(mps, *content, rfc822_for, protocol, parent, error) != 0) {
	g_object_unref(mps);
	return FALSE;
    }

    g_object_unref(G_OBJECT(*content));
    *content = GMIME_OBJECT(mps);
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
			     GtkWindow * parent, GError ** error)
{
    GMimeObject *encrypted_obj = NULL;
    GPtrArray *recipients;
    int result = -1;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);
#ifndef HAVE_SMIME
    g_return_val_if_fail(protocol == GPGME_PROTOCOL_OpenPGP, FALSE);
#endif

    /* check if gpg is currently available */
    if (protocol == GPGME_PROTOCOL_OpenPGP && gpg_updates_trustdb())
	return FALSE;

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
	result = g_mime_gpgme_mpe_encrypt(mpe, *content, recipients, always_trust, parent, error);
    }
#ifdef HAVE_SMIME
    else {
	GMimePart *pkcs7 =
	    g_mime_part_new_with_type("application", "pkcs7-mime");

	result = g_mime_application_pkcs7_encrypt(pkcs7, *content, recipients, always_trust, parent, error);
    }
#endif
    g_ptr_array_free(recipients, FALSE);

    /* error checking */
    if (result != 0) {
	g_object_unref(encrypted_obj);
	return FALSE;
    } else {
    g_object_unref(G_OBJECT(*content));
    *content = GMIME_OBJECT(encrypted_obj);
    return TRUE;
    }
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
				  GtkWindow * parent,
				  GError ** error)
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
				   parent, error))
	return FALSE;

    if (!libbalsa_encrypt_mime_object(&signed_object, rfc822_for, protocol,
				      always_trust, parent, error)) {
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
    GError *error = NULL;
    GMimeGpgmeSigstat *result;

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
    if (!GMIME_IS_MULTIPART_SIGNED(body->mime_part)
        || (g_mime_multipart_get_count
            (((GMimeMultipart *) body->mime_part))) < 2)
        return FALSE;
    if (body->parts->next->sig_info)
	g_object_unref(G_OBJECT(body->parts->next->sig_info));

    /* verify the signature */
    libbalsa_mailbox_lock_store(body->message->mailbox);
    result = g_mime_gpgme_mps_verify(GMIME_MULTIPART_SIGNED(body->mime_part), &error);
    if (!result || result->status != GPG_ERR_NO_ERROR) {
	if (error) {
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s: %s",
				 _("signature verification failed"),
				 error->message);
	    g_error_free(error);
	} else
	    libbalsa_information(LIBBALSA_INFORMATION_ERROR,
				 _("signature verification failed"));
    }

    body->parts->next->sig_info = result;
    libbalsa_mailbox_unlock_store(body->message->mailbox);
    return TRUE;
}


/*
 * Body points to an application/pgp-encrypted body. If decryption is
 * successful, it is freed, and the routine returns a pointer to the chain of
 * decrypted bodies. Otherwise, the original body is returned.
 */
LibBalsaMessageBody *
libbalsa_body_decrypt(LibBalsaMessageBody *body, gpgme_protocol_t protocol, GtkWindow *parent)
{
    GMimeObject *mime_obj = NULL;
    GError *error = NULL;
    LibBalsaMessage *message;
    GMimeGpgmeSigstat *sig_state = NULL;
#ifdef HAVE_SMIME
    gboolean smime_encrypted = FALSE;
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
	if (!g_ascii_strcasecmp(smime_type, "enveloped-data"))
	    smime_encrypted = FALSE;
    }
#endif

    libbalsa_mailbox_lock_store(body->message->mailbox);
    if (protocol == GPGME_PROTOCOL_OpenPGP)
	mime_obj =
	    g_mime_gpgme_mpe_decrypt(GMIME_MULTIPART_ENCRYPTED(body->mime_part),
				     &sig_state, parent, &error);
#ifdef HAVE_SMIME
    else
	mime_obj =
	    g_mime_application_pkcs7_decrypt_verify(GMIME_PART(body->mime_part),
						    &sig_state, parent, &error);
#endif
    libbalsa_mailbox_unlock_store(body->message->mailbox);

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
	body->was_encrypted = smime_encrypted;
#endif

    libbalsa_message_body_set_mime_body(body, mime_obj);
    if (sig_state) {
	if (sig_state->status != GPG_ERR_NOT_SIGNED)
	    body->sig_info = sig_state;
	else
	    g_object_unref(G_OBJECT(sig_state));
    }

    return body;
}



/* routines dealing with RFC2440 */
gboolean
libbalsa_rfc2440_sign_encrypt(GMimePart *part, const gchar *sign_for,
			      GList *encrypt_for, gboolean always_trust,
			      GtkWindow *parent, GError **error)
{
    GPtrArray *recipients;
    gint result;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);
    g_return_val_if_fail(sign_for != NULL || encrypt_for != NULL, FALSE);

    /* check if gpg is currently available */
    if (gpg_updates_trustdb())
	return FALSE;

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
    result = g_mime_part_rfc2440_sign_encrypt(part, sign_for, recipients,
					      always_trust, parent, error);
    /* clean up */
    if (recipients)
	g_ptr_array_free(recipients, FALSE);
    return (result == 0) ? TRUE : FALSE;
}


/*
 * Check the signature of part and return the result of the crypto process. If
 * sig_info is not NULL, return the signature info object there.
 */
gpgme_error_t
libbalsa_rfc2440_verify(GMimePart * part, GMimeGpgmeSigstat ** sig_info)
{
    GMimeGpgmeSigstat *result;
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

    /* verify */
    result = g_mime_part_rfc2440_verify(part, &error);
    if (!result || result->status != GPG_ERR_NO_ERROR) {
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
    } else
	retval = result->status;

    /* return the signature info if requested */
    if (result) {
	if (sig_info)
	    *sig_info = result;
	else
	    g_object_unref(G_OBJECT(result));
    }
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
    GError *error = NULL;
    GMimeGpgmeSigstat *result;
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

    /* decrypt */
    result = g_mime_part_rfc2440_decrypt(part, parent, &error);
    if (result == NULL) {
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
    } else {
	if (result->status == GPG_ERR_NOT_SIGNED)
	    retval = GPG_ERR_NO_ERROR;
	else
	    retval = result->status;

	/* return the signature info if requested */
	if (sig_info && result->status != GPG_ERR_NOT_SIGNED)
	    *sig_info = result;
	else
	    g_object_unref(G_OBJECT(result));
    }

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

static inline void
append_time_t(GString *str, const gchar *format, time_t *when,
              const gchar * date_string)
{
    if (*when != (time_t) 0) {
        gchar *tbuf = libbalsa_date_to_utf8(when, date_string);
        g_string_append_printf(str, format, tbuf);
        g_free(tbuf);
    } else {
        g_string_append_printf(str, format, _("never"));
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
    g_string_append_printf(msg, _("\nSignature validity: %s"),
			   libbalsa_gpgme_validity_to_gchar(info->
							    validity));
    append_time_t(msg, _("\nSigned on: %s"), &info->sign_time, date_string);
    if (info->protocol == GPGME_PROTOCOL_OpenPGP && info->key)
	g_string_append_printf(msg, _("\nKey owner trust: %s"),
			       libbalsa_gpgme_validity_to_gchar_short
			       (info->key->owner_trust));
    if (info->fingerprint)
	g_string_append_printf(msg, _("\nKey fingerprint: %s"),
			       info->fingerprint);

    /* add key information */
    if (info->key) {
        gpgme_user_id_t uid;
        gpgme_subkey_t subkey;

        /* user ID's */
        if ((uid = info->key->uids)) {
            gchar *lead_text;

            uid = info->key->uids;
            if (uid->next) {
        	msg = g_string_append(msg, _("\nUser ID's:"));
        	lead_text = "\n\342\200\242";
            } else {
        	msg = g_string_append(msg, _("\nUser ID:"));
        	lead_text = "";
            }

            /* Note: there is no way to determine which user id has been used
             * to create the signature.  A broken client may even use an
             * invalid and/or revoked one.  We therefore add all to the
             * result. */
            while (uid) {
        	msg = g_string_append(msg, lead_text);
        	if (uid->revoked)
        	    msg = g_string_append(msg, _(" [Revoked]"));
        	if (uid->invalid)
        	    msg = g_string_append(msg, _(" [Invalid]"));

        	if (uid->uid && *(uid->uid)) {
        	    gchar *uid_readable =
        	    	libbalsa_cert_subject_readable(uid->uid);
        	    g_string_append_printf(msg, " %s", uid_readable);
        	    g_free(uid_readable);
        	} else {
        	    if (uid->name && *(uid->name))
        		g_string_append_printf(msg, " %s", uid->name);
        	    if (uid->email && *(uid->email))
        		g_string_append_printf(msg, " <%s>", uid->email);
        	    if (uid->comment && *(uid->comment))
        		g_string_append_printf(msg, " (%s)", uid->comment);
        	}

        	uid = uid->next;
            }
        }

        /* subkey */
        if ((subkey = info->key->subkeys)) {
            /* find the one which can sign */
            while (subkey && !subkey->can_sign)
        	subkey = subkey->next;

            if (subkey) {
        	append_time_t(msg, _("\nSubkey created on: %s"),
        		      &subkey->timestamp, date_string);
        	append_time_t(msg, _("\nSubkey expires on: %s"),
        		      &subkey->expires, date_string);
        	if (subkey->revoked || subkey->expired || subkey->disabled ||
        	    subkey->invalid) {
       GString * attrs = g_string_new("");
       int count = 0;

        	    if (subkey->revoked) {
           count++;
           attrs = g_string_append(attrs, _(" revoked"));
       }
        	    if (subkey->expired) {
           if (count++)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" expired"));
       }
        	    if (subkey->disabled) {
           if (count)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" disabled"));
       }
        	    if (subkey->invalid) {
           if (count)
               attrs = g_string_append_c(attrs, ',');
           attrs = g_string_append(attrs, _(" invalid"));
       }
        	    /* ngettext: string begins with a single space, so no space
        	     * after the colon is correct punctuation (in English). */
       g_string_append_printf(msg, ngettext("\nSubkey attribute:%s",
                                            "\nSubkey attributes:%s",
                                            count),
                              attrs->str);
       g_string_free(attrs, TRUE);
    }
            }
        }

        if (info->key->issuer_name) {
            gchar *issuer_name =
        	libbalsa_cert_subject_readable(info->key->issuer_name);
            g_string_append_printf(msg, _("\nIssuer name: %s"), issuer_name);
            g_free(issuer_name);
    }
        if (info->key->issuer_serial)
	g_string_append_printf(msg, _("\nIssuer serial number: %s"),
				   info->key->issuer_serial);
        if (info->key->chain_id)
            g_string_append_printf(msg, _("\nChain ID: %s"), info->key->chain_id);
    }

    retval = msg->str;
    g_string_free(msg, FALSE);
    return retval;
}


#ifdef HAVE_GPG

#include <sys/wait.h>
#include <fcntl.h>

/* run gpg asynchronously to import or update a key */
typedef struct _spawned_gpg_T {
    gint child_pid;
    gint standard_error;
    GString *stderr_buf;
    GtkWindow *parent;
} spawned_gpg_T;

static gboolean check_gpg_child(gpointer data);

gboolean
gpg_keyserver_op(const gchar * fingerprint, gpg_keyserver_action_t action,
                 GtkWindow * parent)
{
    gchar **argv;
    spawned_gpg_T *spawned_gpg;
    gboolean spawnres;

    /* launch gpg... */
    argv = g_new(gchar *, 5);
    argv[0] = g_strdup(GPG_PATH);
    argv[1] = g_strdup("--no-greeting");
    switch (action) {
    case GPG_KEYSERVER_IMPORT:
        argv[2] = g_strdup("--recv-keys");
        break;
    case GPG_KEYSERVER_UPDATE:
        argv[2] = g_strdup("--refresh-keys");
        break;
    default:
        g_assert_not_reached();
    }
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
			     ("Could not launch %s to query the public key %s."),
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
				   GTK_MESSAGE_WARNING, GTK_BUTTONS_CLOSE,
				   _
				   ("Running %s failed with return value %d:\n%s"),
				   GPG_PATH, WEXITSTATUS(status), gpg_message);
    else
	dialog =
	    gtk_message_dialog_new(spawned_gpg->parent,
				   GTK_DIALOG_DESTROY_WITH_PARENT,
				   GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE,
				   _("Running %s successful:\n%s"),
				   GPG_PATH, gpg_message);
#if HAVE_MACOSX_DESKTOP
    libbalsa_macosx_menu_for_parent(dialog, spawned_gpg->parent);
#endif
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
	libbalsa_information(LIBBALSA_INFORMATION_ERROR, "%s%s",
			     _
			     ("GnuPG is rebuilding the trust database and is currently unavailable."),
			     _("Try again later."));
	return TRUE;
    } else
	return FALSE;
}


/* check if the context contains a public key for the passed recipients */
#define KEY_IS_OK(k)   (!((k)->expired || (k)->revoked || \
                          (k)->disabled || (k)->invalid))
static gboolean
have_pub_key_for(gpgme_ctx_t gpgme_ctx, InternetAddressList * recipients)
{
    gpgme_key_t key;
    gboolean result = TRUE;
    time_t now = time(NULL);
    gint i;

    for (i = 0; result && i < internet_address_list_length(recipients);
         i++) {
        InternetAddress *ia =
            internet_address_list_get_address(recipients, i);

	/* check all entries in the list, handle groups recursively */
	if (INTERNET_ADDRESS_IS_GROUP(ia))
	    result =
                have_pub_key_for(gpgme_ctx,
                                 INTERNET_ADDRESS_GROUP(ia)->members);
	else {
	    if (gpgme_op_keylist_start(gpgme_ctx,
                                       INTERNET_ADDRESS_MAILBOX(ia)->addr,
                                       FALSE) != GPG_ERR_NO_ERROR)
		return FALSE;

	    result = FALSE;
	    while (!result &&
		   gpgme_op_keylist_next(gpgme_ctx, &key) == GPG_ERR_NO_ERROR) {
		/* check if this key and the relevant subkey are usable */
		if (KEY_IS_OK(key)) {
		    gpgme_subkey_t subkey = key->subkeys;

		    while (subkey && !subkey->can_encrypt)
			subkey = subkey->next;

		    if (subkey && KEY_IS_OK(subkey) && 
			(subkey->expires == 0 || subkey->expires > now))
			result = TRUE;
		}
		gpgme_key_unref(key);
	    }
	    gpgme_op_keylist_end(gpgme_ctx);
	}
    }

    return result;
}

#endif				/* HAVE_GPGME */
