/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2018 Stuart Parmenter and others,
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
 *
 * Note: see https://autocrypt.org/level1.html for the Autocrypt specs
 */

#ifndef LIBBALSA_AUTOCRYPT_H_
#define LIBBALSA_AUTOCRYPT_H_

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#ifdef ENABLE_AUTOCRYPT

#include "libbalsa.h"


#define AUTOCRYPT_ERROR_QUARK			(g_quark_from_static_string("autocrypt"))


enum _AutocryptMode {
	AUTOCRYPT_DISABLE,					/**< Disable Autocrypt support. */
	AUTOCRYPT_NOPREFERENCE,				/**< Enable Autocrypt support, but do not request "prefer-encrypt=mutual". */
	AUTOCRYPT_PREFER_ENCRYPT			/**< Enable Autocrypt support and request "prefer-encrypt=mutual". */
};

typedef enum _AutocryptMode AutocryptMode;


enum _AutocryptRecommend {
	AUTOCRYPT_ENCR_ERROR,				/**< An error occurred when calculating the recommendation for encryption. */
	AUTOCRYPT_ENCR_DISABLE,				/**< Encryption is not possible due to a missing usable key. */
	AUTOCRYPT_ENCR_DISCOURAGE,			/**< Encryption is possible but discouraged by Autocrypt. */
	AUTOCRYPT_ENCR_AVAIL,				/**< Encryption is possible, but at least one recipient does not request
										 * "prefer-encrypt=mutual". */
	AUTOCRYPT_ENCR_AVAIL_MUTUAL			/**< Encryption is possible, and all recipients request "prefer-encrypt=mutual". */
};

typedef enum _AutocryptRecommend AutocryptRecommend;


/** \brief Initialise the Autocrypt subsystem
 *
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, FALSE if any error coourred
 *
 * Open and if necessary initialise the Autocrypt SQLite3 database <tt>autocrypt.db</tt> in the user's Balsa folder.
 */
gboolean autocrypt_init(GError **error);

/** \brief Update the Autocrypt database from a received message
 *
 * \param message Balsa message
 * \param error filled with error information on error, may be NULL
 *
 * Scan the headers of the passed message and update the Autocrypt database according to the Autocrypt specifications, section 2.3
 * <em>Updating Autocrypt Peer State</em>.
 *
 * \todo Spam messages should be ignored, but how can we detect them?
 */
void autocrypt_from_message(LibBalsaMessage  *message,
							GError          **error);

/** \brief Create an Autocrypt header value
 *
 * \param identity the identity for which the Autocrypt header shall be created
 * \param error filled with error information on error, may be NULL
 * \return a newly allocated string containing the properly folded Autocrypt header
 *
 * Create a an Autocrypt header value according to the Autocrypt specifications.  Note that the included key data may or may not be
 * minimalistic, depending upon the export capabilities of the gpg backend being used.  It is an error to call this function if the
 * Autocrypt mode of the passed identity is AUTOCRYPT_DISABLE.
 */
gchar *autocrypt_header(LibBalsaIdentity  *identity,
						GError           **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Check if a media type shall be ignored for Autocrypt
 *
 * \param content_type message content type
 * \return TRUE if the media type shall be ignored
 *
 * The standard requests that multipart/report shall be ignored.  This function also blocks text/calendar which is not required
 * by the standard (see https://lists.mayfirst.org/pipermail/autocrypt/2018-November/000441.html for a discussion).
 */
gboolean autocrypt_ignore(GMimeContentType *content_type);

/** \brief Get a key from the Autocrypt database by fingerprint
 *
 * \param fingerprint key fingerprint
 * \param error filled with error information on error, may be NULL
 * \return a new object containing the raw key data on success, or NULL if the key is not in the Autocrypt database
 *
 * If available, returns the key whose fingerprint ends in the passed value from the Autocrypt database.
 */
GBytes *autocrypt_get_key(const gchar  *fingerprint,
						  GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Import missing keys from the Autocrypt database for a list of internet addresses
 *
 * \param addresses Internet addresses
 * \param error filled with error information on error, may be NULL
 * \return the count of imported keys (>= 0) on success, -1 on error
 *
 * Check for every mailbox in the passed list if a valid key exists in the Autocrypt database, but not in the local key ring, and
 * import them into the latter.
 */
gint autocrypt_import_keys(InternetAddressList  *addresses,
						   GError              **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Get the recommendation for encryption
 *
 * \param recipients message recipients
 * \param missing_keys filled with a list of GBytes *, containing all Autocrypt keys missing in the key ring, may be NULL
 * \param error filled with error information on error, may be NULL
 * \return the result of the recommendation check
 *
 * Calculate the Autocrypt recommendation for encryption, according to sect. 2.4 of the standard.  Note that all recipients which
 * are not listed in the Autocrypt database, but for which a valid key exists in the GnuPG key ring, are treated as if they
 * requested "prefer-encrypt=mutual".
 *
 * \sa https://autocrypt.org/level1.html#provide-a-recommendation-for-message-encryption
 */
AutocryptRecommend autocrypt_recommendation(InternetAddressList  *recipients,
											GList 				**missing_keys,
											GError              **error);

/** \brief Show the Autocrypt database
 *
 * \param date_string time stamp formatting template
 * \param parent parent window
 *
 * Display a modal dialog with the contents of the Autocrypt database.
 */
void autocrypt_db_dialog_run(const gchar *date_string,
							 GtkWindow   *parent);


#endif	/* ENABLE_AUTOCRYPT */


#endif	/* LIBBALSA_AUTOCRYPT_H_ */
