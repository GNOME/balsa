/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "rfc3156.h"

#include <string.h>
#include <gpgme.h>

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "libbalsa-gpgme-widgets.h"
#include "libbalsa-gpgme-keys.h"
#include "libbalsa-gpgme.h"

#include "gmime-multipart-crypt.h"
#include "gmime-gpgme-signature.h"
#include "gmime-part-rfc2440.h"

#include "gmime-application-pkcs7.h"

#include <glib/gi18n.h>


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


/* ==== public functions =================================================== */
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

    /* create the gpgme context and set the protocol */
    gpgme_ctx = libbalsa_gpgme_new_with_proto(protocol, NULL);
    if (gpgme_ctx == NULL) {
    	result = FALSE;
    } else {
    	/* loop over all recipients and try to find valid keys */
    	result = libbalsa_gpgme_have_all_keys(gpgme_ctx, recipients, NULL);
    	gpgme_release(gpgme_ctx);
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

    /* call gpgme to create the signature */
    if (!(mps = g_mime_multipart_signed_new())) {
	return FALSE;
    }

    if (!g_mime_gpgme_mps_sign(mps, *content, rfc822_for, protocol, parent, error)) {
	g_object_unref(mps);
	return FALSE;
    }

    g_object_unref(*content);
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
    gboolean result = FALSE;

    /* paranoia checks */
    g_return_val_if_fail(rfc822_for != NULL, FALSE);
    g_return_val_if_fail(content != NULL, FALSE);

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
    } else {
    	GMimePart *pkcs7 = g_mime_part_new_with_type("application", "pkcs7-mime");
    	encrypted_obj = GMIME_OBJECT(pkcs7);

    	result = g_mime_application_pkcs7_encrypt(pkcs7, *content, recipients, always_trust, parent, error);
    }
    g_ptr_array_unref(recipients);

    /* error checking */
    if (!result) {
	g_object_unref(encrypted_obj);
    } else {
    g_object_unref(*content);
    *content = GMIME_OBJECT(encrypted_obj);
    }
    return result;
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

    /* we want to be able to restore */
    signed_object = g_object_ref(*content);

    if (!libbalsa_sign_mime_object(&signed_object, rfc822_signer, protocol,
				   parent, error))
	return FALSE;

    if (!libbalsa_encrypt_mime_object(&signed_object, rfc822_for, protocol,
				      always_trust, parent, error)) {
	g_object_unref(signed_object);
	return FALSE;
    }
    g_object_unref(*content);
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
    LibBalsaMailbox *mailbox;
    GError *error = NULL;
    GMimeGpgmeSigstat *result;

    /* paranoia checks */
    g_return_val_if_fail(body, FALSE);
    g_return_val_if_fail(body->mime_part != NULL, FALSE);
    g_return_val_if_fail(body->message, FALSE);

    /* check if the body is really a multipart/signed */
    if (!GMIME_IS_MULTIPART_SIGNED(body->mime_part)
        || (g_mime_multipart_get_count
            (GMIME_MULTIPART(body->mime_part)) < 2))
        return FALSE;
    if (body->parts->next->sig_info)
	g_object_unref(body->parts->next->sig_info);

    /* verify the signature */
    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    result = g_mime_gpgme_mps_verify(GMIME_MULTIPART_SIGNED(body->mime_part), &error);
    if (!result) {
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
    libbalsa_mailbox_unlock_store(mailbox);
    return TRUE;
}


/*
 * Body points to an application/pgp-encrypted body. If decryption is
 * successful, it is freed, and the routine returns a pointer to the chain of
 * decrypted bodies. Otherwise, the original body is returned.
 */
LibBalsaMessageBody *
libbalsa_body_decrypt(LibBalsaMessageBody *body, gpgme_protocol_t protocol)
{
    LibBalsaMailbox *mailbox;
    GMimeObject *mime_obj = NULL;
    GError *error = NULL;
    LibBalsaMessage *message;
    GMimeGpgmeSigstat *sig_state = NULL;
    gboolean smime_encrypted = FALSE;
    LibBalsaMessageBody *parent;

    /* paranoia checks */
    g_return_val_if_fail(body != NULL, body);
    g_return_val_if_fail(body->mime_part != NULL, body);
    g_return_val_if_fail(body->message != NULL, body);

    /* sanity checks... */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	if (!GMIME_IS_MULTIPART_ENCRYPTED(body->mime_part))
	    return body;
    } else {
    	const char * smime_type =
    		g_mime_object_get_content_type_parameter(body->mime_part,
    			"smime-type");

    	if (!smime_type || !GMIME_IS_PART(body->mime_part))
    		return body;
    	if (!g_ascii_strcasecmp(smime_type, "enveloped-data"))
    		smime_encrypted = TRUE;
    	else
    		smime_encrypted = body->was_encrypted;
    }

    mailbox = libbalsa_message_get_mailbox(body->message);
    libbalsa_mailbox_lock_store(mailbox);
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
    	mime_obj =
    		g_mime_gpgme_mpe_decrypt(GMIME_MULTIPART_ENCRYPTED(body->mime_part),
    			&sig_state, &error);
    } else {
    	mime_obj =
    		g_mime_application_pkcs7_decrypt_verify(GMIME_PART(body->mime_part),
    			&sig_state, &error);
    }
    libbalsa_mailbox_unlock_store(mailbox);

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
    parent = body->parent;
    libbalsa_message_body_free(body);
    body = libbalsa_message_body_new(message);

    /* remember that is was encrypted */
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
    	body->was_encrypted = TRUE;
    } else {
    	body->was_encrypted = smime_encrypted;
    }
    if (body->was_encrypted)
        libbalsa_message_set_crypt_mode(body->message, LIBBALSA_PROTECT_ENCRYPT);

    /* check for a draft-autocrypt-lamps-protected-headers content-type parameter and fix the subject if it is present */
    if (g_strcmp0(g_mime_object_get_content_type_parameter(mime_obj, "protected-headers"), "v1") == 0) {
    	const gchar *orig_subject;

    	orig_subject = g_mime_object_get_header(mime_obj, "subject");
    	if (orig_subject != NULL) {
    		g_debug("%s: original message subject '%s'", __func__, orig_subject);
    		if (parent != NULL) {
    			/* embedded message */
    			if (parent->embhdrs != NULL) {
    				g_free(parent->embhdrs->subject);
    				parent->embhdrs->subject = g_strdup(orig_subject);
    				libbalsa_utf8_sanitize(&parent->embhdrs->subject, TRUE, NULL);
    			}
    		} else {
    			/* top-level subject */
    			libbalsa_message_set_subject(message, orig_subject);
    		}
    	}
    }

    libbalsa_message_body_set_mime_body(body, mime_obj);
    if (sig_state) {
	if (g_mime_gpgme_sigstat_status(sig_state) != GPG_ERR_NOT_SIGNED)
	    body->sig_info = sig_state;
	else
	    g_object_unref(sig_state);
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
    gboolean result;

    /* paranoia checks */
    g_return_val_if_fail(part != NULL, FALSE);
    g_return_val_if_fail(sign_for != NULL || encrypt_for != NULL, FALSE);

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
	g_ptr_array_unref(recipients);
    return result;
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

    /* verify */
    result = g_mime_part_rfc2440_verify(part, &error);
    if (!result) {
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
	retval = g_mime_gpgme_sigstat_status(result);

    /* return the signature info if requested */
    if (result) {
	if (sig_info)
	    *sig_info = result;
	else
	    g_object_unref(result);
    }
    return retval;
}


/*
 * Decrypt part, if possible check the signature, and return the result of the
 * crypto process. If sig_info is not NULL and the part is signed, return the
 * signature info object there.
 */
gpgme_error_t
libbalsa_rfc2440_decrypt(GMimePart * part, GMimeGpgmeSigstat ** sig_info)
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

    /* decrypt */
    result = g_mime_part_rfc2440_decrypt(part, &error);
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
	if (g_mime_gpgme_sigstat_status(result) == GPG_ERR_NOT_SIGNED)
	    retval = GPG_ERR_NO_ERROR;
	else
	    retval = g_mime_gpgme_sigstat_status(result);

	/* return the signature info if requested */
	if (sig_info && g_mime_gpgme_sigstat_status(result) != GPG_ERR_NOT_SIGNED)
	    *sig_info = result;
	else
	    g_object_unref(result);
    }

    return retval;
}


/* conversion of status values to human-readable messages */
gchar *
libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat)
{
	switch (stat) {
	case GPG_ERR_NO_ERROR:
		return g_strdup(_("The signature is valid."));
	case GPG_ERR_SIG_EXPIRED:
		return g_strdup(_("The signature is valid but expired."));
	case GPG_ERR_KEY_EXPIRED:
		return g_strdup(_("The signature is valid but the key used to verify the signature has expired."));
	case GPG_ERR_CERT_REVOKED:
		return g_strdup(_("The signature is valid but the key used to verify the signature has been revoked."));
	case GPG_ERR_BAD_SIGNATURE:
		return g_strdup(_("The signature is invalid."));
	case GPG_ERR_NO_PUBKEY:
		return g_strdup(_("The signature could not be verified due to a missing key."));
	case GPG_ERR_NO_DATA:
		return g_strdup(_("This part is not a real signature."));
	case GPG_ERR_INV_ENGINE:
		return g_strdup(_("The signature could not be verified due to an invalid crypto engine."));
	case GPG_ERR_MULT_SIGNATURES:
		return g_strdup(_("The signature contains multiple signers, this may be a forgery."));
	default: {
		gchar errbuf[4096];		/* should be large enough... */

		gpgme_strerror_r(stat, errbuf, sizeof(errbuf));
		if (gpgme_err_source(stat) != GPG_ERR_SOURCE_UNKNOWN) {
			/* Translators: #1 error source; #2 error message */
			return g_strdup_printf(_("An error prevented the signature verification: %s: %s"), gpgme_strsource(stat), errbuf);
		} else {
			/* Translators: #1 error message */
			return g_strdup_printf(_("An error prevented the signature verification: %s"), errbuf);
		}
	}
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
