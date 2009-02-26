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

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "gmime-part-rfc2440.h"

#include <string.h>
#include <gmime/gmime.h>


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
    if (!stream)
        return retval;
    if ((slen = g_mime_stream_length(stream)) == -1) {
        g_object_unref(stream);
	return retval;
    }
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
    GByteArray *cipherdata;
    gint result;

    g_return_val_if_fail(GMIME_IS_PART(part), -1);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), -1);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->sign_protocol != NULL,
			 -1);
    g_return_val_if_fail(recipients != NULL || sign_userid != NULL, -1);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(part);
    g_return_val_if_fail(wrapper, -1); /* Incomplete part. */
    stream = g_mime_data_wrapper_get_stream(wrapper);
    g_object_unref(wrapper);
    g_mime_stream_reset(stream);

    /* construct the stream for the crypto output */
    cipherdata = g_byte_array_new();
    cipherstream = g_mime_stream_mem_new_with_byte_array(cipherdata);
    g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(cipherstream), TRUE);

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

    /* add the headers to encrypted ascii armor output: as there is no "insert"
     * method for the byte array, first remove the leading "BEGIN PGP MESSAGE"
     * (27 chars) and prepend it again... */
    if (recipients && g_mime_object_get_content_type(GMIME_OBJECT(part))) {
	const gchar *charset =
	    g_mime_object_get_content_type_parameter(GMIME_OBJECT(part),
						     "charset");
	gchar * rfc2440header;
	
	rfc2440header =
	    g_strdup_printf("-----BEGIN PGP MESSAGE-----\nCharset: %s\n"
			    "Comment: created by Balsa " BALSA_VERSION
			    " (http://balsa.gnome.org)", charset);
	g_byte_array_remove_range(cipherdata, 0, 27);
	g_byte_array_prepend(cipherdata, (guint8 *) rfc2440header,
			     strlen(rfc2440header));
	g_free(rfc2440header);
    }

    /* replace the content of the part */
    g_mime_stream_reset(cipherstream);
    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, cipherstream);

    /*
     * Paranoia: set the encoding of a signed part to quoted-printable (not
     * requested by RFC2440, but it's safer...). If we encrypted use 7-bit
     * and set the charset to us-ascii instead, as gpg added it's own armor.
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
	g_mime_object_set_content_type_parameter(GMIME_OBJECT(part),
						 "charset", "US-ASCII");
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
    GMimeStream *stream, *plainstream, *wrapper_stream;
    GMimeDataWrapper * wrapper;
    GMimeSignatureValidity *valid;

    g_return_val_if_fail(GMIME_IS_PART(part), NULL);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), NULL);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->sign_protocol != NULL,
			 NULL);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(GMIME_PART(part));
    g_return_val_if_fail(wrapper, NULL); /* Incomplete part. */
    wrapper_stream = g_mime_data_wrapper_get_stream(wrapper);
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);
    g_object_unref(wrapper_stream);
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
    gchar *headbuf = g_malloc0(1024);

    g_return_val_if_fail(GMIME_IS_PART(part), -1);
    g_return_val_if_fail(GMIME_IS_GPGME_CONTEXT(ctx), -1);
    g_return_val_if_fail(GMIME_CIPHER_CONTEXT(ctx)->encrypt_protocol !=
			 NULL, -1);

    /* get the raw content */
    wrapper = g_mime_part_get_content_object(part);
    g_return_val_if_fail(wrapper, -1); /* Incomplete part. */
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);
    g_object_unref(wrapper);

    g_mime_stream_reset(stream);
    g_mime_stream_read(stream, headbuf, 1023);
    g_mime_stream_reset(stream);

    /* construct the stream for the decrypted output */
    plainstream = g_mime_stream_mem_new();

    /* decrypt and (if possible) verify the input */
    result =
	g_mime_cipher_decrypt(GMIME_CIPHER_CONTEXT(ctx), stream,
			      plainstream, err);

    if (result == 0) {
	GMimeStream *filter_stream;
	GMimeStream *out_stream;
	GMimeFilter *filter;
	GMimeDataWrapper *wrapper = g_mime_data_wrapper_new();

	/* strip crlf off encrypted stuff coming from Winbloze crap */
	filter_stream = g_mime_stream_filter_new_with_stream(plainstream);
	filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
					GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream), filter);
	g_object_unref(filter);

	/* replace the old contents by the decrypted stuff */
	out_stream = g_mime_stream_mem_new();
	g_mime_data_wrapper_set_stream(wrapper, out_stream);
	g_mime_part_set_content_object(part, wrapper);
	g_object_unref(wrapper);
	g_mime_stream_reset(filter_stream);
	g_mime_stream_write_to_stream(filter_stream, out_stream);
	g_object_unref(filter_stream);

	g_mime_part_set_encoding(part, GMIME_PART_ENCODING_8BIT);

	/*
 	 * Set the charset of the decrypted content to the RFC 2440 "Charset:"
 	 * header. If it is not present, RFC 2440 defines that the contents
 	 * should be utf-8, but real-life applications (e.g. pgp4pine) tend to
 	 * use "some" charset, so "unknown-8bit" is a safe choice in this case
 	 * and if no other charset is given.
	 */
	if (g_mime_object_get_content_type(GMIME_OBJECT(part))) {
 	    gchar *up_headbuf = g_ascii_strup(headbuf, -1);
 	    gchar *p;
 
 	    if ((p = strstr(up_headbuf, "CHARSET: "))) {
 		gchar *line_end;
 
 		p += 9;
 		line_end = p;
 		while (*line_end > ' ')
 		    line_end++;
 		*line_end = '\0';
		g_mime_object_set_content_type_parameter(GMIME_OBJECT(part),
							 "charset", p);
 	    } else {
		if (!g_ascii_strcasecmp("us-ascii",
			 g_mime_object_get_content_type_parameter(GMIME_OBJECT(part),
								  "charset")))
		    g_mime_object_set_content_type_parameter(GMIME_OBJECT(part),
							     "charset",
							     "unknown-8bit");
	    }
	    g_free(up_headbuf);
	}
    }
    g_object_unref(plainstream);
    g_object_unref(stream);
    g_free(headbuf);

    return result;
}
