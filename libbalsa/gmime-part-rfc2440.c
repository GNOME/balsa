/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme implementation for RFC2440 parts
 * Copyright (C) 2004-2016 Albrecht Dreß <albrecht.dress@arcor.de>
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
#endif                          /* HAVE_CONFIG_H */

#include <string.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "libbalsa-gpgme.h"
#include "mime-stream-shared.h"
#include "gmime-part-rfc2440.h"


#ifdef G_LOG_DOMAIN
#  undef G_LOG_DOMAIN
#endif
#define G_LOG_DOMAIN "crypto"


#define RFC2440_BUF_LEN    4096


/** \brief Check for a part's RFC 2440 protection
 *
 * \param part GMime part which shall be inspected.
 * \return The OpenPGP (RFC 2440) protection of the part.
 *
 * Check if the passed part is RFC 2440 signed or encrypted by looking for
 * the "magic" strings defined there.  Note that parts which include extra
 * data before the beginning or after the end of signed or encrypted matter
 * are always classified as GMIME_PART_RFC2440_NONE.
 */
GMimePartRfc2440Mode
g_mime_part_check_rfc2440(GMimePart * part)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *stream;
    ssize_t slen;
    gchar buf[RFC2440_BUF_LEN];
    GMimePartRfc2440Mode retval = GMIME_PART_RFC2440_NONE;
    static const char begin_pgp_message[]        = "-----BEGIN PGP MESSAGE-----";
    static const char end_pgp_message[]          = "-----END PGP MESSAGE-----";
    static const char begin_pgp_signed_message[] = "-----BEGIN PGP SIGNED MESSAGE-----";
    static const char begin_pgp_signature[]      = "-----BEGIN PGP SIGNATURE-----";
    static const char end_pgp_signature[]        = "-----END PGP SIGNATURE-----";

    /* try to get the content stream */
    wrapper = g_mime_part_get_content(part);
    g_return_val_if_fail(wrapper, GMIME_PART_RFC2440_NONE);

    stream = g_mime_data_wrapper_get_stream(wrapper);
    if (!stream || (slen = g_mime_stream_length(stream)) < 0)
	return retval;

    /* note: the following is a noop if the stream is not a shared stream */
    libbalsa_mime_stream_shared_lock(stream);

    g_mime_stream_reset(stream);

    /* check if the complete stream fits in the buffer */
    if (slen < RFC2440_BUF_LEN - 1) {
	g_mime_stream_read(stream, buf, slen);
	buf[slen] = '\0';

	if (g_str_has_prefix(buf, begin_pgp_message) &&
	    strstr(buf, end_pgp_message) != NULL) {
	    retval = GMIME_PART_RFC2440_ENCRYPTED;
        } else if (g_str_has_prefix(buf, begin_pgp_signed_message)) {
	    gchar *p1, *p2;

	    p1 = strstr(buf, begin_pgp_signature);
	    p2 = strstr(buf, end_pgp_signature);
	    if (p1 != NULL && p2 != NULL && p2 > p1)
		retval = GMIME_PART_RFC2440_SIGNED;
	}
    } else {
	/* check if the beginning of the stream matches */
	g_mime_stream_read(stream, buf, strlen(begin_pgp_signed_message));
	g_mime_stream_seek(stream, 1 - RFC2440_BUF_LEN,
			   GMIME_STREAM_SEEK_END);
	if (g_str_has_prefix(buf, begin_pgp_message)) {
	    g_mime_stream_read(stream, buf, RFC2440_BUF_LEN - 1);
	    buf[RFC2440_BUF_LEN - 1] = '\0';
	    if (strstr(buf, end_pgp_message) != NULL)
		retval = GMIME_PART_RFC2440_ENCRYPTED;
	} else if (g_str_has_prefix(buf, begin_pgp_signed_message)) {
	    gchar *p1, *p2;

	    g_mime_stream_read(stream, buf, RFC2440_BUF_LEN - 1);
	    buf[RFC2440_BUF_LEN - 1] = '\0';
	    p1 = strstr(buf, begin_pgp_signature);
	    p2 = strstr(buf, end_pgp_signature);
	    if (p1 != NULL && p2 != NULL && p2 > p1)
		retval = GMIME_PART_RFC2440_SIGNED;
	}
    }

    libbalsa_mime_stream_shared_unlock(stream);

    return retval;
}


/** \brief RFC 2440 sign and/or encrypt a GMime part
 *
 * \param part GMime part which shall be signed and/or encrypted.
 * \param sign_userid User ID of the signer, or NULL fo no signature.
 * \param recipients Array of User ID for which the matter shall be
 *        encrypted using their public keys, or NULL for no encryption.
 * \param trust_all_keys TRUE if all low-truct keys shall be accepted for
 *        encryption.
 * \param parent Parent window to be passed to the callback functions.
 * \param error Filled with error information on error.
 * \return TRUE on success, or FALSE on error.
 *
 * RFC2440 sign, encrypt or sign and encrypt a gmime part.  If sign_userid
 * is not NULL, part will be signed.  If recipients is not NULL, encrypt
 * part for recipients.  If both are not null, part will be both signed and
 * encrypted. Returns TRUE on success or FALSE on fail. If any operation
 * failed, an exception will be set on err to provide more information.
 */
gboolean
g_mime_part_rfc2440_sign_encrypt(GMimePart * part, const char *sign_userid,
				 GPtrArray * recipients,
				 gboolean trust_all, GtkWindow * parent,
				 GError ** err)
{
    GMimeDataWrapper *wrapper;
    GMimeStream *stream, *cipherstream;
    GByteArray *cipherdata;
    gboolean result;

    g_return_val_if_fail(GMIME_IS_PART(part), FALSE);
    g_return_val_if_fail(recipients != NULL || sign_userid != NULL, FALSE);

    /* get the raw content */
    wrapper = g_mime_part_get_content(part);
    g_return_val_if_fail(wrapper, FALSE); /* Incomplete part. */
    stream = g_mime_data_wrapper_get_stream(wrapper);
    g_mime_stream_reset(stream);

    /* construct the stream for the crypto output */
    cipherdata = g_byte_array_new();
    cipherstream = g_mime_stream_mem_new_with_byte_array(cipherdata);
    g_mime_stream_mem_set_owner(GMIME_STREAM_MEM(cipherstream), TRUE);

    /* do the crypto operation */
    if (recipients == NULL) {
	if (libbalsa_gpgme_sign
	    (sign_userid, stream, cipherstream, GPGME_PROTOCOL_OpenPGP,
	     TRUE, parent, err) == GPGME_MD_NONE)
	    result = FALSE;
    else
	    result = TRUE;
    } else
	result =
	    libbalsa_gpgme_encrypt(recipients, sign_userid, stream,
				   cipherstream, GPGME_PROTOCOL_OpenPGP,
				   TRUE, trust_all, parent, err);
    if (!result) {
	g_object_unref(cipherstream);
	return result;
    }

    /* add the headers to encrypted ascii armor output: as there is no
     * "insert" method for the byte array, first remove the leading "BEGIN
     * PGP MESSAGE" (27 chars) and prepend it again... */
    if (recipients && g_mime_object_get_content_type(GMIME_OBJECT(part))) {
	const gchar *charset =
	    g_mime_object_get_content_type_parameter(GMIME_OBJECT(part),
						     "charset");
	gchar *rfc2440header;
	
	rfc2440header =
	    g_strdup_printf("-----BEGIN PGP MESSAGE-----\nCharset: %s\n"
			    "Comment: created by Balsa " BALSA_VERSION
			    " (https://gitlab.gnome.org/GNOME/balsa)", charset);
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
     * and set the charset to us-ascii instead, as gpg added it's own
     * armor.
     */
    if (recipients == NULL) {
	if (g_mime_part_get_content_encoding(part) !=
	    GMIME_CONTENT_ENCODING_BASE64)
	    g_mime_part_set_content_encoding(part,
				     GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE);
	g_mime_data_wrapper_set_encoding(wrapper,
					 GMIME_CONTENT_ENCODING_DEFAULT);
    } else {
	g_mime_part_set_content_encoding(part,
					 GMIME_CONTENT_ENCODING_7BIT);
	g_mime_data_wrapper_set_encoding(wrapper,
					 GMIME_CONTENT_ENCODING_7BIT);
	g_mime_object_set_content_type_parameter(GMIME_OBJECT(part),
						 "charset", "US-ASCII");
    }

    g_mime_part_set_content(part, wrapper);
    g_object_unref(cipherstream);
    g_object_unref(wrapper);

    return result;
}


/* \brief Verify a RFC 2440 signed part
 *
 * \param part GMime part which shall be verified.
 * \param error Filled with error information on error.
 * \return A new signature status object on success, or NULL on error.
 *
 * Verify the signature of the RFC 2440 signed part and return the
 * signature status as determined by the crypto routines or NULL on error.
 * In the latter case, an exception will be set on err to provide more
 * information.  Upon success, the content of part is replaced by the
 * verified output of the crypto engine.
 */
GMimeGpgmeSigstat *
g_mime_part_rfc2440_verify(GMimePart * part, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *plainstream;
    GMimeDataWrapper *wrapper;
    GMimeGpgmeSigstat *result;

    g_return_val_if_fail(GMIME_IS_PART(part), NULL);

    /* get the raw content */
    wrapper = g_mime_part_get_content(GMIME_PART(part));
    g_return_val_if_fail(wrapper, NULL); /* Incomplete part. */
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);
    g_mime_stream_reset(stream);

    /* construct the stream for the checked output */
    plainstream = g_mime_stream_mem_new();

    /* verify the signature */
    result =
	libbalsa_gpgme_verify(stream, plainstream, GPGME_PROTOCOL_OpenPGP,
			      TRUE, err);

    /* upon success, replace the signed content by the checked one */
    if (result && g_mime_stream_length(plainstream) > 0) {
	wrapper = g_mime_data_wrapper_new();
	g_mime_data_wrapper_set_stream(wrapper, plainstream);
	g_mime_part_set_content(GMIME_PART(part), wrapper);
	g_object_unref(wrapper);
    }
    g_object_unref(plainstream);

    return result;
}


/* \brief Decrypt a RFC 2440 encrypted part
 *
 * \param part GMime part which shall be verified.
 * \param error Filled with error information on error.
 * \return A new signature status object on success, or NULL on error.
 *
 * Decrypt the RFC 2440 encrypted part and return the signature status as
 * determined by the crypto routines or NULL on error.  In the latter case,
 * an exception will be set on err to provide more information.  Upon
 * success, the content of part is replaced by the decrypted output of
 * the crypto engine. If the input was also signed, the signature is
 * verified.
 */
GMimeGpgmeSigstat *
g_mime_part_rfc2440_decrypt(GMimePart * part, GError ** err)
{
    GMimeStream *stream;
    GMimeStream *plainstream;
    GMimeDataWrapper *wrapper;
    GMimeGpgmeSigstat *result;
    gchar *headbuf = g_malloc0(1024);

    g_return_val_if_fail(GMIME_IS_PART(part), NULL);

    /* get the raw content */
    wrapper = g_mime_part_get_content(part);
    g_return_val_if_fail(wrapper, NULL); /* Incomplete part. */
    stream = g_mime_stream_mem_new();
    g_mime_data_wrapper_write_to_stream(wrapper, stream);

    g_mime_stream_reset(stream);
    g_mime_stream_read(stream, headbuf, 1023);
    g_mime_stream_reset(stream);

    /* construct the stream for the decrypted output */
    plainstream = g_mime_stream_mem_new();

    /* decrypt and (if possible) verify the input */
    result =
	libbalsa_gpgme_decrypt(stream, plainstream, GPGME_PROTOCOL_OpenPGP,
			       err);

    if (result != NULL) {
	GMimeStream *filter_stream;
	GMimeStream *out_stream;
	GMimeFilter *filter;

	/* strip crlf off encrypted stuff coming from Winbloze crap */
	filter_stream = g_mime_stream_filter_new(plainstream);
	filter = g_mime_filter_dos2unix_new(FALSE);
	g_mime_stream_filter_add(GMIME_STREAM_FILTER(filter_stream),
				 filter);
	g_object_unref(filter);

	/* replace the old contents by the decrypted stuff */
	out_stream = g_mime_stream_mem_new();
	wrapper = g_mime_data_wrapper_new();
	g_mime_data_wrapper_set_stream(wrapper, out_stream);
	g_mime_part_set_content(part, wrapper);
        g_object_unref(wrapper);
	g_mime_stream_reset(filter_stream);
	g_mime_stream_write_to_stream(filter_stream, out_stream);
	g_object_unref(filter_stream);

	g_mime_part_set_content_encoding(part,
					 GMIME_CONTENT_ENCODING_8BIT);

	/*
	 * Set the charset of the decrypted content to the RFC 2440
	 * "Charset:" header.  If it is not present, RFC 2440 defines that
	 * the contents should be utf-8, but real-life applications (e.g.
	 * pgp4pine) tend to use "some" charset, so "unknown-8bit" is a
	 * safe choice in this case and if no other charset is given.
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
		g_mime_object_set_content_type_parameter(GMIME_OBJECT
							 (part), "charset",
							 p);
 	    } else {
		if (!g_ascii_strcasecmp
		    ("us-ascii",
		     g_mime_object_get_content_type_parameter(GMIME_OBJECT
							      (part),
								  "charset")))
		    g_mime_object_set_content_type_parameter(GMIME_OBJECT
							     (part),
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
