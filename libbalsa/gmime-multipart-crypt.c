/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme implementation for multipart/signed and multipart/encrypted
 * Copyright (C) 2011 Albrecht Dreß <albrecht.dress@arcor.de>
 *
 * The functions in this module were copied from the original GMime
 * implementation of multipart/signed and multipart/encrypted parts.
 * However, instead of using the complex GMime crypto contexts (which have
 * a varying API over the different versions), this module directly calls
 * the GpgME backend functions implemented in libbalsa-gpgme.[hc].
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
#include "config.h"
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <unistd.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>
#include "libbalsa.h"
#include "libbalsa-gpgme.h"
#include "gmime-multipart-crypt.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


/**
 * sign_prepare:
 * @mime_part: MIME part
 *
 * Prepare a part (and all subparts) to be signed. To do this we need
 * to set the encoding of all parts (that are not already encoded to
 * either QP or Base64 or 7-bit) to QP.
 *
 * Ref: RFC 3156, sect. 3.
 **/
static void
sign_prepare(GMimeObject * mime_part)
{
    GMimeContentEncoding encoding;
    GMimeObject *subpart;

    if (GMIME_IS_MULTIPART(mime_part)) {
        GMimeMultipart *multipart;
        int i, n;

	multipart = (GMimeMultipart *) mime_part;

	if (GMIME_IS_MULTIPART_SIGNED(multipart) ||
	    GMIME_IS_MULTIPART_ENCRYPTED(multipart)) {
	    /* must not modify these parts as they must be treated as opaque */
	    return;
	}

	n = g_mime_multipart_get_count(multipart);
	for (i = 0; i < n; i++) {
	    subpart = g_mime_multipart_get_part(multipart, i);
	    sign_prepare(subpart);
	}
    } else if (GMIME_IS_MESSAGE_PART(mime_part)) {
	subpart = GMIME_MESSAGE_PART(mime_part)->message->mime_part;
	sign_prepare(subpart);
    } else {
	encoding = g_mime_part_get_content_encoding(GMIME_PART(mime_part));
	if ((encoding != GMIME_CONTENT_ENCODING_BASE64) && (encoding != GMIME_CONTENT_ENCODING_7BIT))
	    g_mime_part_set_content_encoding(GMIME_PART(mime_part),
					     GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);
    }
}


gboolean
g_mime_gpgme_mps_sign(GMimeMultipartSigned * mps, GMimeObject * content,
		      const gchar * userid, gpgme_protocol_t protocol,
		      GtkWindow * parent, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *filtered;
    GMimeStream *sigstream;
    GMimeFilter *filter;
    GMimeContentType *content_type;
    GMimeDataWrapper *wrapper;
    GMimeParser *parser;
    GMimePart *signature;
    gchar *micalg;
    const gchar *proto_type;
    const gchar *sig_subtype;
    gpgme_hash_algo_t hash_algo;

    g_return_val_if_fail(GMIME_IS_MULTIPART_SIGNED(mps), FALSE);
    g_return_val_if_fail(GMIME_IS_OBJECT(content), FALSE);

    /* Prepare all the parts for signing... */
    sign_prepare(content);

    /* get the cleartext */
    stream = g_mime_stream_mem_new();
    filtered = g_mime_stream_filter_new(stream);

    /* Note: see rfc3156, section 3 - second note */
    filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_ARMOR);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered), filter);
    g_object_unref(filter);

    /* Note: see rfc3156, section 5.4 (this is the main difference between rfc2015 and rfc3156) */
    filter = g_mime_filter_strip_new();
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered), filter);
    g_object_unref(filter);

    g_mime_object_write_to_stream(content, NULL, filtered);
    g_mime_stream_flush(filtered);
    g_object_unref(filtered);
    g_mime_stream_reset(stream);

    /* Note: see rfc2015 or rfc3156, section 5.1 */
    filtered = g_mime_stream_filter_new(stream);
    filter = g_mime_filter_unix2dos_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered), filter);
    g_object_unref(filter);

    /* construct the signature stream */
    sigstream = g_mime_stream_mem_new();

    /* sign the content stream */
    hash_algo =
	libbalsa_gpgme_sign(userid, filtered, sigstream, protocol, FALSE,
			    parent, err);
    g_object_unref(filtered);
    if (hash_algo == GPGME_MD_NONE) {
	g_object_unref(sigstream);
	g_object_unref(stream);
	return FALSE;
    }

    g_mime_stream_reset(sigstream);
    g_mime_stream_reset(stream);

    /* set the multipart/signed protocol and micalg */
    content_type = g_mime_object_get_content_type(GMIME_OBJECT(mps));
    if (protocol == GPGME_PROTOCOL_OpenPGP) {
	micalg =
	    g_strdup_printf("PGP-%s", gpgme_hash_algo_name(hash_algo));
	proto_type = "application/pgp-signature";
	sig_subtype = "pgp-signature";
    } else {
	micalg = g_strdup(gpgme_hash_algo_name(hash_algo));
	proto_type = "application/pkcs7-signature";
	sig_subtype = "pkcs7-signature";
    }
    g_mime_content_type_set_parameter(content_type, "micalg", micalg);
    g_free(micalg);
    g_mime_content_type_set_parameter(content_type, "protocol", proto_type);
    g_mime_multipart_set_boundary(GMIME_MULTIPART(mps), NULL);

    /* construct the content part */
    parser = g_mime_parser_new_with_stream(stream);
    g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
    content = g_mime_parser_construct_part(parser, libbalsa_parser_options());
    g_object_unref(stream);
    g_object_unref(parser);

    /* construct the signature part */
    signature = g_mime_part_new_with_type("application", sig_subtype);

    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, sigstream);
    g_mime_part_set_content(signature, wrapper);
    g_object_unref(sigstream);
    g_object_unref(wrapper);

    /* FIXME: temporary hack, this info should probably be set in
     * the CipherContext class - maybe ::sign can take/output a
     * GMimePart instead. */
    if (protocol == GPGME_PROTOCOL_CMS) {
	g_mime_part_set_content_encoding(signature,
					 GMIME_CONTENT_ENCODING_BASE64);
	g_mime_part_set_filename(signature, "smime.p7m");
    } else {
	g_mime_object_set_content_type_parameter(GMIME_OBJECT(signature),
						 "name", "openpgp-digital-signature.asc");
    }
    g_mime_part_set_content_description(signature,
					"This is a digitally signed message part.");

    /* save the content and signature parts */
    /* FIXME: make sure there aren't any other parts?? */
    g_mime_multipart_add(GMIME_MULTIPART(mps), content);
    g_mime_multipart_add(GMIME_MULTIPART(mps), (GMimeObject *) signature);
    g_object_unref(signature);
    g_object_unref(content);

    return TRUE;
}


GMimeGpgmeSigstat *
g_mime_gpgme_mps_verify(GMimeMultipartSigned * mps, GError ** error)
{
    const gchar *protocol;
    gpgme_protocol_t crypto_prot;
    gchar *content_type;
    GMimeObject *content;
    GMimeObject *signature;
    GMimeStream *stream;
    GMimeStream *filtered_stream;
    GMimeFilter *crlf_filter;
    GMimeDataWrapper *wrapper;
    GMimeStream *sigstream;
    GMimeGpgmeSigstat *result;

    g_return_val_if_fail(GMIME_IS_MULTIPART_SIGNED(mps), NULL);

    if (g_mime_multipart_get_count(GMIME_MULTIPART(mps)) < 2) {
	g_set_error(error, GMIME_ERROR, GMIME_ERROR_PARSE_ERROR, "%s",
		    _
		    ("Cannot verify multipart/signed part due to missing subparts."));
	return NULL;
    }

    /* grab the protocol so we can configure the GpgME context */
    protocol =
	g_mime_object_get_content_type_parameter(GMIME_OBJECT(mps),
						 "protocol");
    if (protocol) {
	if (g_ascii_strcasecmp("application/pgp-signature", protocol) == 0)
	    crypto_prot = GPGME_PROTOCOL_OpenPGP;
	else if (g_ascii_strcasecmp
		 ("application/pkcs7-signature", protocol) == 0
		 || g_ascii_strcasecmp("application/x-pkcs7-signature",
				       protocol) == 0)
	    crypto_prot = GPGME_PROTOCOL_CMS;
	else
	    crypto_prot = GPGME_PROTOCOL_UNKNOWN;
    } else
	crypto_prot = GPGME_PROTOCOL_UNKNOWN;

    /* eject on unknown protocols */
    if (crypto_prot == GPGME_PROTOCOL_UNKNOWN) {
	g_set_error(error, GPGME_ERROR_QUARK, GPG_ERR_INV_VALUE,
		    _("unsupported protocol “%s”"), protocol);
	return NULL;
    }

    signature =
	g_mime_multipart_get_part(GMIME_MULTIPART(mps),
				  GMIME_MULTIPART_SIGNED_SIGNATURE);

    /* make sure the protocol matches the signature content-type */
    content_type = g_mime_content_type_get_mime_type(signature->content_type);
    if (g_ascii_strcasecmp(content_type, protocol) != 0) {
	g_set_error(error, GMIME_ERROR, GMIME_ERROR_PARSE_ERROR, "%s",
		    _
		    ("Cannot verify multipart/signed part: signature content-type does not match protocol."));
	g_free(content_type);
	return NULL;
    }
    g_free(content_type);

    content =
	g_mime_multipart_get_part(GMIME_MULTIPART(mps),
				  GMIME_MULTIPART_SIGNED_CONTENT);

    /* get the content stream */
    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new(stream);

    /* Note: see rfc2015 or rfc3156, section 5.1 */
    crlf_filter = g_mime_filter_unix2dos_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    g_mime_object_write_to_stream(content, NULL, filtered_stream);
    g_mime_stream_flush(filtered_stream);
    g_object_unref(filtered_stream);
    g_mime_stream_reset(stream);

    /* get the signature stream */
    wrapper = g_mime_part_get_content(GMIME_PART(signature));

    /* a s/mime signature is always encoded, a pgp signature shouldn't,
     * but there exist implementations which encode it... */
	sigstream = g_mime_stream_mem_new();
	g_mime_data_wrapper_write_to_stream(wrapper, sigstream);
    g_mime_stream_reset(sigstream);

    /* verify the signature */
    result =
	libbalsa_gpgme_verify(stream, sigstream, crypto_prot, FALSE,
			      error);
    g_object_unref(stream);
    g_object_unref(sigstream);

    return result;
}


gboolean
g_mime_gpgme_mpe_encrypt(GMimeMultipartEncrypted * mpe,
			 GMimeObject * content, GPtrArray * recipients,
			 gboolean trust_all, GtkWindow * parent,
			 GError ** err)
{
    GMimeStream *filtered_stream;
    GMimeStream *ciphertext;
    GMimeStream *stream;
    GMimePart *version_part;
    GMimePart *encrypted_part;
    GMimeDataWrapper *wrapper;
    GMimeFilter *crlf_filter;

    g_return_val_if_fail(GMIME_IS_MULTIPART_ENCRYPTED(mpe), FALSE);
    g_return_val_if_fail(GMIME_IS_OBJECT(content), FALSE);

    /* get the cleartext */
    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new(stream);

    crlf_filter = g_mime_filter_unix2dos_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    g_mime_object_write_to_stream(content, NULL, filtered_stream);
    g_mime_stream_flush(filtered_stream);
    g_object_unref(filtered_stream);

    /* reset the content stream */
    g_mime_stream_reset(stream);

    /* encrypt the content stream */
    ciphertext = g_mime_stream_mem_new();
    if (!libbalsa_gpgme_encrypt
	(recipients, NULL, stream, ciphertext, GPGME_PROTOCOL_OpenPGP,
	 FALSE, trust_all, parent, err)) {
	g_object_unref(ciphertext);
	g_object_unref(stream);
	return FALSE;
    }

    g_object_unref(stream);
    g_mime_stream_reset(ciphertext);

    /* construct the version part */
    version_part =
	g_mime_part_new_with_type("application", "pgp-encrypted");
    g_mime_part_set_content_encoding(version_part,
				     GMIME_CONTENT_ENCODING_7BIT);
    stream =
	g_mime_stream_mem_new_with_buffer("Version: 1\n",
					  strlen("Version: 1\n"));
    wrapper =
	g_mime_data_wrapper_new_with_stream(stream,
					    GMIME_CONTENT_ENCODING_7BIT);
    g_mime_part_set_content(version_part, wrapper);
    g_object_unref(wrapper);
    g_object_unref(stream);

    /* construct the encrypted mime part */
    encrypted_part =
	g_mime_part_new_with_type("application", "octet-stream");
    g_mime_part_set_content_encoding(encrypted_part,
				     GMIME_CONTENT_ENCODING_7BIT);
    wrapper =
	g_mime_data_wrapper_new_with_stream(ciphertext,
					    GMIME_CONTENT_ENCODING_7BIT);
    g_mime_part_set_content(encrypted_part, wrapper);
    g_object_unref(ciphertext);
    g_object_unref(wrapper);

    /* save the version and encrypted parts */
    /* FIXME: make sure there aren't any other parts?? */
    g_mime_multipart_add(GMIME_MULTIPART(mpe), GMIME_OBJECT(version_part));
    g_mime_multipart_add(GMIME_MULTIPART(mpe),
			 GMIME_OBJECT(encrypted_part));
    g_object_unref(encrypted_part);
    g_object_unref(version_part);

    /* set the content-type params for this multipart/encrypted part */
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(mpe), "protocol",
					     "application/pgp-encrypted");
    g_mime_multipart_set_boundary(GMIME_MULTIPART(mpe), NULL);

    return TRUE;
}


static GMimeStream *
g_mime_data_wrapper_get_decoded_stream(GMimeDataWrapper * wrapper)
{
    GMimeStream *decoded_stream;
    GMimeFilter *decoder;

    g_mime_stream_reset(wrapper->stream);

    switch (wrapper->encoding) {
    case GMIME_CONTENT_ENCODING_BASE64:
    case GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE:
    case GMIME_CONTENT_ENCODING_UUENCODE:
	decoder = g_mime_filter_basic_new(wrapper->encoding, FALSE);
	decoded_stream = g_mime_stream_filter_new(wrapper->stream);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(decoded_stream),
				 decoder);
	g_object_unref(decoder);
	break;
    default:
	decoded_stream = wrapper->stream;
	g_object_ref(wrapper->stream);
	break;
    }

    return decoded_stream;
}


GMimeObject *
g_mime_gpgme_mpe_decrypt(GMimeMultipartEncrypted * mpe,
			 GMimeGpgmeSigstat ** signature,
			 GError ** err)
{
    GMimeObject *decrypted, *version, *encrypted;
    GMimeStream *stream, *ciphertext;
    GMimeStream *filtered_stream;
    GMimeContentType *mime_type;
    GMimeGpgmeSigstat *sigstat;
    GMimeDataWrapper *wrapper;
    GMimeFilter *crlf_filter;
    GMimeParser *parser;
    const char *protocol;
    char *content_type;

    g_return_val_if_fail(GMIME_IS_MULTIPART_ENCRYPTED(mpe), NULL);

    if (signature && *signature) {
	g_object_unref(*signature);
	*signature = NULL;
    }

    protocol =
	g_mime_object_get_content_type_parameter(GMIME_OBJECT(mpe),
						 "protocol");

    /* make sure the protocol is present and matches the cipher encrypt protocol */
    if (!protocol
	|| g_ascii_strcasecmp("application/pgp-encrypted",
			      protocol) != 0) {
	g_set_error(err, GMIME_ERROR, GMIME_ERROR_PROTOCOL_ERROR,
		    _
		    ("Cannot decrypt multipart/encrypted part: unsupported encryption protocol “%s”."),
		    protocol ? protocol : _("(none)"));
	return NULL;
    }

    version =
	g_mime_multipart_get_part(GMIME_MULTIPART(mpe),
				  GMIME_MULTIPART_ENCRYPTED_VERSION);

    /* make sure the protocol matches the version part's content-type */
    content_type = g_mime_content_type_get_mime_type(version->content_type);
    if (g_ascii_strcasecmp(content_type, protocol) != 0) {
	g_set_error(err, GMIME_ERROR, GMIME_ERROR_PROTOCOL_ERROR, "%s",
		    _
		    ("Cannot decrypt multipart/encrypted part: content-type does not match protocol."));
	g_free(content_type);
	return NULL;
    }
    g_free(content_type);

    /* get the encrypted part and check that it is of type application/octet-stream */
    encrypted =
	g_mime_multipart_get_part(GMIME_MULTIPART(mpe),
				  GMIME_MULTIPART_ENCRYPTED_CONTENT);
    mime_type = g_mime_object_get_content_type(encrypted);
    if (!g_mime_content_type_is_type
	(mime_type, "application", "octet-stream")) {
	g_set_error(err, GMIME_ERROR, GMIME_ERROR_PROTOCOL_ERROR, "%s",
		    _
		    ("Cannot decrypt multipart/encrypted part: unexpected content type"));
	return NULL;
    }

    /* get the ciphertext stream */
    wrapper = g_mime_part_get_content(GMIME_PART(encrypted));
    ciphertext = g_mime_data_wrapper_get_decoded_stream(wrapper);
    g_mime_stream_reset(ciphertext);

    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new(stream);
    crlf_filter = g_mime_filter_dos2unix_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    /* get the cleartext */
    sigstat =
	libbalsa_gpgme_decrypt(ciphertext, filtered_stream,
			       GPGME_PROTOCOL_OpenPGP, err);
    if (!sigstat) {
	g_object_unref(filtered_stream);
	g_object_unref(ciphertext);
	g_object_unref(stream);
	return NULL;
    }

    g_mime_stream_flush(filtered_stream);
    g_object_unref(filtered_stream);
    g_object_unref(ciphertext);

    g_mime_stream_reset(stream);
    parser = g_mime_parser_new_with_stream(stream);
    g_mime_parser_set_format(parser, GMIME_FORMAT_MESSAGE);
    g_object_unref(stream);

    decrypted = g_mime_parser_construct_part(parser, libbalsa_parser_options());
    g_object_unref(parser);

    if (!decrypted) {
	g_set_error(err, GMIME_ERROR, GMIME_ERROR_PARSE_ERROR, "%s",
		    _
		    ("Cannot decrypt multipart/encrypted part: failed to parse decrypted content"));
	g_object_unref(sigstat);
	return NULL;
    }


    /* cache the decrypted part */
    if (signature) {
	if (g_mime_gpgme_sigstat_status(sigstat) != GPG_ERR_NOT_SIGNED)
	    *signature = sigstat;
	else
	    g_object_unref(sigstat);
    }

    return decrypted;
}
