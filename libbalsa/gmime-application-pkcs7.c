/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * S/MIME application/pkcs7-mime support for gmime/balsa
 * Copyright (C) 2004 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <gmime/gmime.h>
#include "libbalsa-gpgme.h"
#include "gmime-application-pkcs7.h"
#include <glib/gi18n.h>
#include "misc.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


#define GMIME_PKCS7_ERR_QUARK (g_quark_from_static_string ("gmime-app-pkcs7"))


/*
 * Try to verify pkcs7 using the context ctx, fill validity with the resulting
 * validity and return the "readable" decrypted part. If anything fails, the
 * return value is NULL and err contains more information about the reason. If
 * a cached decrypted part is already present, return this one instead of
 * decrypting it again. In this case, validity is undefined.
 */
GMimeObject *
g_mime_application_pkcs7_decrypt_verify(GMimePart * pkcs7,
					GMimeGpgmeSigstat ** signature,
					GError ** err)
{
    GMimeObject *decrypted;
    GMimeDataWrapper *wrapper;
    GMimeStream *stream, *ciphertext;
    GMimeStream *filtered_stream;
    GMimeFilter *crlf_filter;
    GMimeParser *parser;
    const char *smime_type;

    g_return_val_if_fail(GMIME_IS_PART(pkcs7), NULL);

    /* get the smime type */
    smime_type =
	g_mime_object_get_content_type_parameter(GMIME_OBJECT(pkcs7),
						 "smime-type");
    if (!smime_type
	|| (g_ascii_strcasecmp(smime_type, "enveloped-data")
	    && g_ascii_strcasecmp(smime_type, "signed-data"))) {
	return NULL;
    }

    /* get the ciphertext stream */
    wrapper = g_mime_part_get_content(GMIME_PART(pkcs7));
    g_return_val_if_fail(wrapper, NULL); /* Incomplete part. */
    ciphertext = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, ciphertext);
    g_mime_stream_reset(ciphertext);

    stream = g_mime_stream_mem_new();
    filtered_stream = g_mime_stream_filter_new(stream);
    crlf_filter = g_mime_filter_dos2unix_new(FALSE);
    g_mime_stream_filter_add(GMIME_STREAM_FILTER(filtered_stream),
			     crlf_filter);
    g_object_unref(crlf_filter);

    /* get the cleartext */
    if (g_ascii_strcasecmp(smime_type, "enveloped-data") == 0)
	*signature =
	    libbalsa_gpgme_decrypt(ciphertext, filtered_stream,
				   GPGME_PROTOCOL_CMS, err);
    else
	*signature =
	    libbalsa_gpgme_verify(ciphertext, filtered_stream,
				  GPGME_PROTOCOL_CMS, TRUE, err);
    if (!*signature) {
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

    if (decrypted)
	g_object_ref(decrypted);
    else
	g_set_error(err, GMIME_PKCS7_ERR_QUARK, 42,
		    _("Failed to decrypt MIME part: parse error"));

    return decrypted;
}


/*
 * Encrypt content for all recipients in recipients using the context ctx and
 * return the resulting application/pkcs7-mime object in pkcs7. Return TRUE on
 * success and FALSE on fail. In the latter case, fill err with more information
 * about the reason.
 */
gboolean
g_mime_application_pkcs7_encrypt(GMimePart * pkcs7, GMimeObject * content,
				 GPtrArray * recipients,
				 gboolean trust_all, GtkWindow * parent,
				 GError ** err)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *filtered_stream;
    GMimeStream *stream, *ciphertext;
    GMimeFilter *crlf_filter;
	
    g_return_val_if_fail(GMIME_IS_PART(pkcs7), FALSE);
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
	(recipients, NULL, stream, ciphertext, GPGME_PROTOCOL_CMS, TRUE,
	 trust_all, parent, err)) {
	g_object_unref(ciphertext);
	g_object_unref(stream);
	return FALSE;
    }
	
    g_object_unref(stream);
    g_mime_stream_reset(ciphertext);
	
    /* set the encrypted mime part as content of the pkcs7 object */
    wrapper = g_mime_data_wrapper_new();
    g_mime_data_wrapper_set_stream(wrapper, ciphertext);
    g_object_unref(ciphertext);
    g_mime_part_set_content(GMIME_PART(pkcs7), wrapper);
    g_mime_part_set_filename(GMIME_PART(pkcs7), "smime.p7m");
    g_mime_part_set_content_encoding(GMIME_PART(pkcs7),
				     GMIME_CONTENT_ENCODING_BASE64);
    g_object_unref(wrapper);

    /* set the content-type params for this part */
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7),
					     "smime-type",
					     "enveloped-data");
    g_mime_object_set_content_type_parameter(GMIME_OBJECT(pkcs7), "name",
					     "smime.p7m");
	
    return TRUE;
}
