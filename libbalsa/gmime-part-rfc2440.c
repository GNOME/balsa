/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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

#include <string.h>

#include <gmime/gmime.h>
#include "gmime-part-rfc2440.h"


#define RFC2440_BUF_LEN    4096

GMimePartRfc2440Mode
g_mime_part_check_rfc2440(GMimePart * part)
{
    GMimeDataWrapper * wrapper;
    GMimeStream * stream;
    ssize_t slen;
    gchar buf[RFC2440_BUF_LEN];
    GMimePartRfc2440Mode retval = GMIME_PART_RFC2440_NONE;

    /* try to get the content stream */
    wrapper = g_mime_part_get_content_object(part);
    g_return_val_if_fail(wrapper, GMIME_PART_RFC2440_NONE);
    stream = g_mime_data_wrapper_get_stream(wrapper);
    g_object_unref(wrapper);
    if (!stream || (slen = g_mime_stream_length(stream)) == -1)
	return retval;
    g_mime_stream_reset(stream);

    /* check if the complete stream fits in the buffer */
    if (slen < RFC2440_BUF_LEN - 1) {
	g_mime_stream_read(stream, buf, slen);
	buf[slen] = '\0';

	if (!strncmp(buf, "-----BEGIN PGP MESSAGE-----", 27) &&
	    strstr(buf, "-----END PGP MESSAGE-----"))
	    retval = GMIME_PART_RFC2440_ENCRYPTED;
	else if (!strncmp(buf, "-----BEGIN PGP SIGNED MESSAGE-----", 34)) {
	    gchar * p1, * p2;

	    p1 = strstr(buf, "-----BEGIN PGP SIGNATURE-----");
	    p2 = strstr(buf, "-----END PGP SIGNATURE-----");
	    if (p1 && p2 && p2 > p1)
		retval = GMIME_PART_RFC2440_SIGNED;
	}
    } else {
	/* check if the beginning of the stream matches */
	g_mime_stream_read(stream, buf, 34);
	g_mime_stream_seek(stream, 1 - RFC2440_BUF_LEN,  GMIME_STREAM_SEEK_END);
	if (!strncmp(buf, "-----BEGIN PGP MESSAGE-----", 27)) {
	    g_mime_stream_read(stream, buf, RFC2440_BUF_LEN - 1);
	    buf[RFC2440_BUF_LEN - 1] = '\0';
	    if (strstr(buf, "-----END PGP MESSAGE-----"))
		retval = GMIME_PART_RFC2440_ENCRYPTED;
	} else if (!strncmp(buf, "-----BEGIN PGP SIGNED MESSAGE-----", 34)) {
	    gchar * p1, * p2;

	    g_mime_stream_read(stream, buf, RFC2440_BUF_LEN - 1);
	    buf[RFC2440_BUF_LEN - 1] = '\0';
	    p1 = strstr(buf, "-----BEGIN PGP SIGNATURE-----");
	    p2 = strstr(buf, "-----END PGP SIGNATURE-----");
	    if (p1 && p2 && p2 > p1)
		retval = GMIME_PART_RFC2440_SIGNED;
	}
    }

    g_object_unref(stream);
    return retval;
}


/*
 * RFC2440 sign, encrypt or sign and encrypt a gmime part using the
 * gmime gpgme context ctx. If sign_userid is not NULL, part will be
 * signed. If recipients is not NULL, encrypt part for recipients. If
 * both are not null, part will be first signed and then
 * encrypted. Returns 0 on success or -1 on fail. If any operation
 * failed, an exception will be set on err to provide more
 * information.
 */
int
g_mime_part_rfc2440_sign_encrypt(GMimePart * part,
				 GMimeGpgmeContext * ctx,
				 GPtrArray * recipients,
				 const char *sign_userid, GError ** err)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *stream, *cipherstream;
    gint result;

    g_return_val_if_fail(GMIME_IS_PART(part), -1);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), -1);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->sign_protocol != NULL,
			 -1);
    g_return_val_if_fail(recipients != NULL || sign_userid != NULL, -1);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(part);
    stream = g_mime_data_wrapper_get_stream(wrapper);
    g_object_unref(wrapper);
    g_mime_stream_reset(stream);

    /* construct the stream for the crypto output */
    cipherstream = g_mime_stream_mem_new();

    /* do the crypto operation */
    ctx->singlepart_mode = TRUE;
    if (recipients == NULL)
	result =
	    g_mime_cipher_sign(GMIME_CIPHER_CONTEXT(ctx), sign_userid,
			       GMIME_CIPHER_HASH_DEFAULT, stream,
			       cipherstream, err);
    else
	result =
	    g_mime_cipher_encrypt(GMIME_CIPHER_CONTEXT(ctx),
				  sign_userid != NULL, sign_userid,
				  recipients, stream, cipherstream, err);
    g_object_unref(stream);
    if (result == -1) {
	g_object_unref(cipherstream);
	return -1;
    }
    g_mime_stream_reset(cipherstream);

    /* replace the content of the part */
    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, cipherstream);

    /*
     * Paranoia: set the encoding of a signed part to quoted-printable (not
     * requested by RFC2440, but it's safer...). If we encrypted use 7-bit
     * instead, as gpg added it's own armor.
     */
    if (recipients == NULL) {
	if (g_mime_part_get_encoding(part) != GMIME_PART_ENCODING_BASE64)
	    g_mime_part_set_encoding(part,
				     GMIME_PART_ENCODING_QUOTEDPRINTABLE);
	g_mime_data_wrapper_set_encoding(wrapper,
					 GMIME_PART_ENCODING_DEFAULT);
    } else {
	g_mime_part_set_encoding(part, GMIME_PART_ENCODING_7BIT);
	g_mime_data_wrapper_set_encoding(wrapper,
					 GMIME_PART_ENCODING_7BIT);
    }

    g_mime_part_set_content_object(part, wrapper);
    g_object_unref(cipherstream);
    g_object_unref(wrapper);

    return 0;
}


/*
 * Verify the signature of the RFC 2440 signed part using the gpgme
 * context ctx and return the validity as determined by the crypto
 * routines or NULL on error. In the latter case, an exception will be
 * set on err to provide more information. Upon success, the content
 * of part is replaced by the verified output of the crypto engine.
 */
GMimeSignatureValidity *
g_mime_part_rfc2440_verify(GMimePart * part,
			   GMimeGpgmeContext * ctx, GError ** err)
{
    GMimeStream *stream, *plainstream;
    GMimeDataWrapper * wrapper;
    GMimeSignatureValidity *valid;

    g_return_val_if_fail(GMIME_IS_PART(part), NULL);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), NULL);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->sign_protocol != NULL,
			 NULL);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(GMIME_PART(part));
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);
    g_mime_stream_reset(stream);
    g_object_unref(wrapper);

    /* construct the stream for the checked output */
    plainstream = g_mime_stream_mem_new();

    /* verify the signature */
    ctx->singlepart_mode = TRUE;
    valid =
	g_mime_cipher_verify(GMIME_CIPHER_CONTEXT(ctx),
			     GMIME_CIPHER_HASH_DEFAULT, stream,
			     plainstream, err);
    g_object_unref(stream);

    /* upon success, replace the signed content by the checked one */
    if (valid) {
	GMimeDataWrapper *wrapper = g_mime_data_wrapper_new();

	g_mime_data_wrapper_set_stream(wrapper, plainstream);
	g_mime_part_set_content_object(GMIME_PART(part), wrapper);
	g_object_unref(wrapper);
    }
    g_object_unref(plainstream);

    return valid;
}


/*
 * Decrypt the RFC 2440 encrypted part using the gmime gpgme context
 * ctx and return 0 on success or -1 for fail. In the latter case, an
 * exception will be set on err to provide more information. Upon
 * success, the content of part is replaced by the decrypted output of
 * the crypto engine. If the input was also signed, the signature is
 * verified and the result is placed in ctx by the underlying gpgme
 * context.
 */
int
g_mime_part_rfc2440_decrypt(GMimePart * part,
			    GMimeGpgmeContext * ctx, GError ** err)
{
    GMimeStream *stream, *plainstream;
    GMimeDataWrapper * wrapper;
    gint result;

    g_return_val_if_fail(GMIME_IS_PART(part), -1);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), -1);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->encrypt_protocol !=
			 NULL, -1);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(GMIME_PART(part));
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);
    g_mime_stream_reset(stream);
    g_object_unref(wrapper);

    /* construct the stream for the decrypted output */
    plainstream = g_mime_stream_mem_new();

    /* decrypt and (if possible) verify the input */
    result =
	g_mime_cipher_decrypt(GMIME_CIPHER_CONTEXT(ctx), stream,
			      plainstream, err);

    if (result == 0) {
	const GMimeContentType *type;
	GMimeStream *filter_stream;
	GMimeFilter *filter;
	GMimeDataWrapper *wrapper = g_mime_data_wrapper_new();

	/* strip crlf off encrypted stuff coming from Winbloze crap */
	filter_stream = g_mime_stream_filter_new_with_stream(plainstream);
	filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
					GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
	g_object_unref(filter);

	/* replace the old contents by the decrypted stuff */
	g_mime_data_wrapper_set_stream(wrapper, filter_stream);
	g_mime_part_set_content_object(GMIME_PART(part), wrapper);
	g_object_unref(wrapper);
	g_object_unref(filter_stream);

	g_mime_part_set_encoding(part, GMIME_PART_ENCODING_8BIT);

	/*
	 * The following is a hack for mailers like pgp4pine which calculate
	 * the charset after encryption (which is always us-ascii), even if the
	 * decrypted part contains 8-bit characters.
	 */
	if ((type = g_mime_object_get_content_type(GMIME_OBJECT(part)))) {
	    const gchar *charset =
		g_mime_content_type_get_parameter(type, "charset");

	    if (!g_ascii_strcasecmp(charset, "us-ascii"))
		g_mime_content_type_set_parameter((GMimeContentType *)type,
						  "charset",
						  "unknown-8bit");
	}
    }
    g_object_unref(plainstream);
    g_object_unref(stream);

    return result;
}
