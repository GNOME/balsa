/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * S/MIME application/pkcs7-mime support for gmime/balsa
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gmime/gmime-stream-filter.h>
#include <gmime/gmime-filter-crlf.h>
#include <gmime/gmime-filter-from.h>
#include <gmime/gmime-filter-basic.h>
#include <gmime/gmime-stream-mem.h>
#include <gmime/gmime-parser.h>
#include <gmime/gmime-message-part.h>
#include <gmime/gmime-multipart.h>
#include <gmime/gmime-multipart-signed.h>
#include <gmime/gmime-multipart-encrypted.h>
#include "gmime-application-pkcs7.h"


#ifdef HAVE_GETTEXT
#include <libintl.h>
#ifndef _
#define _(x)  gettext(x)
#endif
#else
#define _(x)  (x)
#endif


#define GMIME_PKCS7_ERR_QUARK (g_quark_from_static_string ("gmime-app-pkcs7"))


#ifdef HAS_APPLICATION_PKCS7_MIME_SIGNED_SUPPORT

/*
 * Note: this code has been shamelessly stolen from Jeff's multipart-signed implementation
 */
static void
sign_prepare (GMimeObject *mime_part)
{
    GMimePartEncodingType encoding;
    GMimeObject *subpart;
	
    if (GMIME_IS_MULTIPART (mime_part)) {
	GList *lpart;
		
	if (GMIME_IS_MULTIPART_SIGNED (mime_part) || GMIME_IS_MULTIPART_ENCRYPTED (mime_part)) {
	    /* must not modify these parts as they must be treated as opaque */
	    return;
	}
		
	lpart = GMIME_MULTIPART (mime_part)->subparts;
	while (lpart) {
	    subpart = GMIME_OBJECT (lpart->data);
	    sign_prepare (subpart);
	    lpart = lpart->next;
	}
    } else if (GMIME_IS_MESSAGE_PART (mime_part)) {
	subpart = GMIME_MESSAGE_PART (mime_part)->message->mime_part;
	sign_prepare (subpart);
    } else {
	encoding = g_mime_part_get_encoding (GMIME_PART (mime_part));
		
	if (encoding != GMIME_PART_ENCODING_BASE64)
	    g_mime_part_set_encoding (GMIME_PART (mime_part),
				      GMIME_PART_ENCODING_QUOTEDPRINTABLE);
    }
}


/*
 * Sign content using the context ctx for userid and write the signed
 * application/pkcs7-mime object to pkcs7. Return 0 on success and -1 on error.
 * In the latter case, fill err with more information about the reason.
 */
int
g_mime_application_pkcs7_sign (GMimePart *pkcs7, GMimeObject *content,
			       GMimeCipherContext *ctx, const char *userid,
			       GError **err)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *filtered_stream;
    GMimeStream *stream, *sig_data_stream;
    GMimeFilter *crlf_filter, *from_filter;
	
    g_return_val_if_fail (GMIME_IS_PART (pkcs7), -1);
    g_return_val_if_fail (GMIME_IS_CIPHER_CONTEXT (ctx), -1);
    g_return_val_if_fail (ctx->sign_protocol != NULL, -1);
    g_return_val_if_fail (GMIME_IS_OBJECT (content), -1);
	
    /* Prepare all the parts for signing... */
    sign_prepare (content);
	
    /* get the cleartext */
    stream = g_mime_stream_mem_new ();
    filtered_stream = g_mime_stream_filter_new_with_stream (stream);
	
    /* See RFC 2633, Sect. 3.1- the following op's are "SHOULD", so we do it */
    from_filter = g_mime_filter_from_new (GMIME_FILTER_FROM_MODE_ARMOR);
    g_mime_stream_filter_add (GMIME_STREAM_FILTER (filtered_stream), from_filter);
    g_object_unref (from_filter);
	
    g_mime_object_write_to_stream (content, filtered_stream);
    g_mime_stream_flush (filtered_stream);
    g_object_unref (filtered_stream);
    g_mime_stream_reset (stream);
	
    filtered_stream = g_mime_stream_filter_new_with_stream (stream);
    crlf_filter = g_mime_filter_crlf_new (GMIME_FILTER_CRLF_ENCODE,
					  GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add (GMIME_STREAM_FILTER (filtered_stream), crlf_filter);
    g_object_unref (crlf_filter);
	
    /* construct the signed data stream */
    sig_data_stream = g_mime_stream_mem_new ();
	
    /* get the signed content */
    if (g_mime_cipher_sign (ctx, userid, GMIME_CIPHER_HASH_DEFAULT, filtered_stream, sig_data_stream, err) == -1) {
	g_object_unref (filtered_stream);
	g_object_unref (sig_data_stream);
	g_object_unref (stream);
	return -1;
    }
	
    g_object_unref (filtered_stream);
    g_object_unref (stream);
    g_mime_stream_reset (sig_data_stream);
	
    /* set the pkcs7 mime part as content of the pkcs7 object */
    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, sig_data_stream);
    g_object_unref(sig_data_stream);
    g_mime_part_set_content_object(GMIME_PART(pkcs7), wrapper);
    g_mime_part_set_filename(GMIME_PART(pkcs7), "smime.p7m");
    g_mime_part_set_encoding(GMIME_PART(pkcs7),
			     GMIME_PART_ENCODING_BASE64);
    g_object_unref(wrapper);

    /* set the content-type params for this part */
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7),
					     "smime-type", "signed-data");
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7), "name",
					     "smime.p7m");
	
    return 0;
}
#endif /* HAS_APPLICATION_PKCS7_MIME_SIGNED_SUPPORT */



/*
 * Try to verify pkcs7 using the context ctx, fill validity with the resulting
 * validity and return the "readable" decrypted part. If anything fails, the
 * return value is NULL and err contains more information about the reason. If
 * a cached decrypted part is already present, return this one instead of
 * decrypting it again. In this case, validity is undefined.
 */
GMimeObject *
g_mime_application_pkcs7_verify(GMimePart * pkcs7,
				GMimeSignatureValidity ** validity,
				GMimeCipherContext * ctx, GError ** err)
{
    GMimeObject *decrypted;
    GMimeDataWrapper *wrapper;
    GMimeStream *stream, *ciphertext;
    GMimeStream *filtered_stream;
    GMimeFilter *crlf_filter;
    GMimeParser *parser;
    const char *smime_type;

    g_return_val_if_fail(GMIME_IS_PART(pkcs7), NULL);
    g_return_val_if_fail(GMIME_IS_CIPHER_CONTEXT(ctx), NULL);
    g_return_val_if_fail(ctx->encrypt_protocol != NULL, NULL);

    /* some sanity checks */
    smime_type =
	g_mime_object_get_content_type_parameter(GMIME_OBJECT(pkcs7),
						 "smime-type");
    if (g_ascii_strcasecmp(smime_type, "signed-data")) {
	return NULL;
    }

    /* get the ciphertext stream */
    wrapper = g_mime_part_get_content_object(GMIME_PART(pkcs7));
    ciphertext = g_mime_stream_mem_new ();
    g_mime_data_wrapper_write_to_stream (wrapper, ciphertext);
    g_mime_stream_reset(ciphertext);
    g_object_unref(wrapper);

    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new_with_stream(stream);
    crlf_filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
					 GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    /* get the cleartext */
    *validity = g_mime_cipher_verify(ctx, GMIME_CIPHER_HASH_DEFAULT,
				     ciphertext, filtered_stream, err);
    if (!*validity) {
	g_object_unref(filtered_stream);
	g_object_unref(ciphertext);
	g_object_unref(stream);

	return NULL;
    }

    g_mime_stream_flush(filtered_stream);
    g_object_unref(filtered_stream);
    g_object_unref(ciphertext);

    g_mime_stream_reset(stream);
    parser = g_mime_parser_new();
    g_mime_parser_init_with_stream(parser, stream);
    g_object_unref(stream);

    decrypted = g_mime_parser_construct_part(parser);
    g_object_unref(parser);

    if (decrypted)
	g_object_ref(decrypted);
    else
	g_set_error(err, GMIME_PKCS7_ERR_QUARK, 42,
		    _("Failed to decrypt MIME part: parse error"));

    return decrypted;
}


/*
 * Encrypt content for all recipients in recipients using the context ctx and
 * return the resulting application/pkcs7-mime object in pkcs7. Return 0 on
 * success and -1 on fail. In the latter case, fill err with more information
 * about the reason.
 */
int
g_mime_application_pkcs7_encrypt (GMimePart *pkcs7, GMimeObject *content,
				  GMimeCipherContext *ctx, GPtrArray *recipients,
				  GError **err)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *filtered_stream;
    GMimeStream *stream, *ciphertext;
    GMimeFilter *crlf_filter;
	
    g_return_val_if_fail (GMIME_IS_PART (pkcs7), -1);
    g_return_val_if_fail (GMIME_IS_CIPHER_CONTEXT (ctx), -1);
    g_return_val_if_fail (ctx->encrypt_protocol != NULL, -1);
    g_return_val_if_fail (GMIME_IS_OBJECT (content), -1);
	
    /* get the cleartext */
    stream = g_mime_stream_mem_new ();
    filtered_stream = g_mime_stream_filter_new_with_stream (stream);
	
    crlf_filter = g_mime_filter_crlf_new (GMIME_FILTER_CRLF_ENCODE,
					  GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add (GMIME_STREAM_FILTER (filtered_stream), crlf_filter);
    g_object_unref (crlf_filter);
	
    g_mime_object_write_to_stream (content, filtered_stream);
    g_mime_stream_flush (filtered_stream);
    g_object_unref (filtered_stream);
	
    /* reset the content stream */
    g_mime_stream_reset (stream);
	
    /* encrypt the content stream */
    ciphertext = g_mime_stream_mem_new ();
    if (g_mime_cipher_encrypt (ctx, FALSE, NULL, recipients, stream, ciphertext, err) == -1) {
	g_object_unref (ciphertext);
	g_object_unref (stream);
	return -1;
    }
	
    g_object_unref (stream);
    g_mime_stream_reset (ciphertext);
	
    /* set the encrypted mime part as content of the pkcs7 object */
    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, ciphertext);
    g_object_unref(ciphertext);
    g_mime_part_set_content_object(GMIME_PART(pkcs7), wrapper);
    g_mime_part_set_filename(GMIME_PART(pkcs7), "smime.p7m");
    g_mime_part_set_encoding(GMIME_PART(pkcs7), GMIME_PART_ENCODING_BASE64);
    g_object_unref(wrapper);

    /* set the content-type params for this part */
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7),
					     "smime-type", "enveloped-data");
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7), "name",
					     "smime.p7m");
	
    return 0;
}


/*
 * Decrypt the application/pkcs7-mime part pkcs7 unsing the context ctx and
 * return the decrypted gmime object or NULL on error. In the latter case, fill
 * err with more information about the reason.
 */
GMimeObject *
g_mime_application_pkcs7_decrypt (GMimePart *pkcs7, GMimeCipherContext *ctx,
				  GError **err)
{
    GMimeObject *decrypted;
    GMimeDataWrapper *wrapper;
    GMimeStream *stream, *ciphertext;
    GMimeStream *filtered_stream;
    GMimeFilter *crlf_filter;
    GMimeParser *parser;
    const char *smime_type;

    g_return_val_if_fail(GMIME_IS_PART(pkcs7), NULL);
    g_return_val_if_fail(GMIME_IS_CIPHER_CONTEXT(ctx), NULL);
    g_return_val_if_fail(ctx->encrypt_protocol != NULL, NULL);

    /* some sanity checks */
    smime_type =
	g_mime_object_get_content_type_parameter(GMIME_OBJECT(pkcs7),
						 "smime-type");
    if (!smime_type || (g_ascii_strcasecmp(smime_type, "enveloped-data") &&
			g_ascii_strcasecmp(smime_type, "signed-data"))) {
	return NULL;
    }

    /* get the ciphertext stream */
    wrapper = g_mime_part_get_content_object(GMIME_PART(pkcs7));
    ciphertext = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream (wrapper, ciphertext);
    g_mime_stream_reset(ciphertext);
    g_object_unref(wrapper);

    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new_with_stream(stream);
    crlf_filter = g_mime_filter_crlf_new(GMIME_FILTER_CRLF_DECODE,
					 GMIME_FILTER_CRLF_MODE_CRLF_ONLY);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    /* get the cleartext */
    if (g_mime_cipher_decrypt(ctx, ciphertext, filtered_stream, err) == -1) {
	g_object_unref(filtered_stream);
	g_object_unref(ciphertext);
	g_object_unref(stream);

	return NULL;
    }

    g_mime_stream_flush(filtered_stream);
    g_object_unref(filtered_stream);
    g_object_unref(ciphertext);

    g_mime_stream_reset (stream);
    parser = g_mime_parser_new();
    g_mime_parser_init_with_stream(parser, stream);
    g_object_unref(stream);

    decrypted = g_mime_parser_construct_part(parser);
    g_object_unref(parser);

    if (decrypted)
	g_object_ref(decrypted);
    else
	g_set_error(err, GMIME_PKCS7_ERR_QUARK, 42,
		    _("Failed to decrypt MIME part: parse error"));

    return decrypted;
}

