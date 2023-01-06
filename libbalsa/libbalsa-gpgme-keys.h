/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * Balsa E-Mail Client
 *
 * gpgme key listing and key server operations
 * Copyright (C) 2017 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef LIBBALSA_GPGME_KEYSERVER_H_
#define LIBBALSA_GPGME_KEYSERVER_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <gmime/internet-address.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS


/** \brief List keys
 *
 * \param ctx GpgME context
 * \param keys filled with a list of gpgme_key_t items matching the search pattern, filled with NULL on error
 * \param bad_keys filled with the number of matching keys which are expired, disabled, revoked or invalid, may be NULL
 * \param pattern key search pattern (e.g. name, fingerprint, ...), may be NULL to list all keys
 * \param secret TRUE to search for private keys, FALSE to search for public keys
 * \param list_bad_keys include expired, revoked, invalid and disabled keys
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE if any error occurred
 *
 * Use the passed context to search for keys matching the passed criteria.  Note that even if the function returns success, the
 * list of keys may be empty if no matching key could be found.
 *
 * \note The returned list of keys shall be freed by the caller.
 * \todo We might want to add flags for returning only keys which have the is_qualified (subkey can be used for qualified
 *       signatures according to local government regulations) and/or is_de_vs (complies with the rules for classified information
 *       in Germany at the restricted level, VS-NfD, requires gpgme >= 1.9.0) properties set.
 */
gboolean libbalsa_gpgme_list_keys(gpgme_ctx_t   ctx,
						 	 	  GList       **keys,
								  guint        *bad_keys,
								  const gchar  *pattern,
								  gboolean      secret,
								  gboolean      list_bad_keys,
								  GError      **error);

/** \brief List local public keys
 *
 * \param ctx GpgME context
 * \param keys filled with a list of gpgme_key_t items matching the internet mailbox addresses from the list, filled with NULL on
 *        error
 * \param addresses list on internet addresses for which all keys shall be returned
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE if any error occurred
 *
 * Use the passed context to search for all valid public keys in the local key ring matching any of the mailboxes in the passed
 * internet address list.  Note that even if the function returns success, the list of keys may be empty if no matching key could
 * be found.  If multiple addresses in in internet address list share the same public key, the duplicates are removed, i.e. every
 * fingerprint is guaranteed to occur only once in the returned list.
 */
gboolean libbalsa_gpgme_list_local_pubkeys(gpgme_ctx_t           ctx,
										   GList               **keys,
										   InternetAddressList  *addresses,
										   GError              **error);

/** \brief Check if a public keys are available
 *
 * \param ctx GpgME context
 * \param address list of internet addresses to check
 * \param error filled with error information on error, may be NULL
 * \return TRUE if a public key is available for \em every address in the passed list, or FALSE if not or if any error occurred
 * \note This function checks \em only the local key ring for the passed crypto protocol.
 */
gboolean libbalsa_gpgme_have_all_keys(gpgme_ctx_t           ctx,
									  InternetAddressList  *addresses,
									  GError              **error);

/** \brief Check if a public key is available
 *
 * \param ctx GpgME context
 * \param mailbox RFC 5322 internet mailbox to check
 * \param error filled with error information on error, may be NULL
 * \return TRUE if a public key is available for the passed address, or FALSE if not or if any error occurred
 * \note This function checks \em only the local key ring for the passed crypto protocol.
 */
gboolean libbalsa_gpgme_have_key(gpgme_ctx_t              ctx,
								 InternetAddressMailbox  *mailbox,
								 GError                 **error);

/** \brief Load a key
 *
 * \param ctx GpgME context
 * \param fingerprint key fingerprint to search for
 * \param error filled with error information on error, may be NULL
 * \return the key matching the passed fingerprint, or NULL on error
 *
 * Return the key matching the passed fingerprint from the local key ring. The function returns NULL if either no or more than one
 * key is available.
 */
gpgme_key_t libbalsa_gpgme_load_key(gpgme_ctx_t   ctx,
									const gchar  *fingerprint,
									GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Search the key server for a key
 *
 * \param fingerprint key fingerprint to search for, required
 * \param email_address email address to search for, may be NULL
 * \param parent parent window
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE on error
 *
 * Launch a new thread for searching a GnuPG key for the passed fingerprint on the key servers.  If no key is found and the email
 * address is known, try to locate the key by the email address which (if configured) will search the Web Key Directories (WKD).
 * If the key is found, it is imported or updated in the local key ring, and a dialogue is displayed.
 */
gboolean libbalsa_gpgme_keyserver_op(const gchar  *fingerprint,
									 const gchar  *email_address,
									 GtkWindow    *parent,
									 GError      **error);

/** \brief Search the key server for internet addresses and import public keys
 *
 * \param ctx GpgME context
 * \param addresses internet addresses
 * \param error filled with error information on error, may be NULL
 * \return the count of imported keys on success, -1 on error, 0 if no key has been imported
 *
 * Loop over all internet addresses in the passed list, resolving groups into individual mailboxes, and for each mailbox check if
 * a public key for the mailbox exists in the local key ring.  If not, check the key servers, and if this does not find and key and
 * the protocol is OpenPGP, the Web Key Directory (WKD).  Any matching keys are imported without further confirmation.
 */
gint libbalsa_gpgme_keyserver_import(gpgme_ctx_t           ctx,
									 InternetAddressList  *addresses,
									 GError              **error);

/** \brief Export a public key
 *
 * \param ctx GpgME context
 * \param key the key which shall be exported
 * \param name key description, used only for creating an error string on error
 * \param error filled with error information on error, may be NULL
 * \return a newly allocated string containing the key on success, NULL on error
 *
 * Export the passed key as ASCII armoured string.
 *
 * \note The returned string shall be freed by the caller.
 */
gchar *libbalsa_gpgme_export_key(gpgme_ctx_t   ctx,
								 gpgme_key_t   key,
								 const gchar  *name,
								 GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Export a key for Autocrypt
 *
 * \param fingerprint key fingerprint, may be NULL
 * \param mailbox key uid
 * \param error filled with error information on error
 * \return a newly allocated buffer containing the key on success, NULL on error
 *
 * Export the minimal key for using it in a Autocrypt: header.  If specified, the key is selected by the passed fingerprint,
 * otherwise the first key matching the passed mailbox is used.  Depending on the gpg backend version, all other uid's and all
 * subkeys which are not required are stripped.
 */
GBytes *libbalsa_gpgme_export_autocrypt_key(const gchar  *fingerprint,
										    const gchar  *mailbox,
										    GError      **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** \brief Import an ASCII-armoured key
 *
 * \param ctx GpgME context
 * \param key_buf ASCII-armoured GnuPG key buffer
 * \param import_info filled with human-readable information about the import, may be NULL
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE on error
 *
 * Import an ASCII-armoured GnuPG key into the key ring.
 */
gboolean libbalsa_gpgme_import_ascii_key(gpgme_ctx_t   ctx,
										 const gchar  *key_buf,
										 gchar       **import_info,
										 GError      **error);

/** \brief Import a binary key
 *
 * \param ctx GpgME context
 * \param key_buf binary GnuPG key buffer
 * \param import_info filled with human-readable information about the import, may be NULL
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE on error
 *
 * Import a binary GnuPG key into the key ring.
 */
gboolean libbalsa_gpgme_import_bin_key(gpgme_ctx_t   ctx,
									   GBytes       *key_buf,
									   gchar       **import_info,
									   GError      **error);

G_END_DECLS



#endif /* LIBBALSA_GPGME_KEYSERVER_H_ */
