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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef LIBBALSA_GPGME_KEYSERVER_H_
#define LIBBALSA_GPGME_KEYSERVER_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <gtk/gtk.h>


G_BEGIN_DECLS


/** \brief List keys
 *
 * \param ctx GpgME context
 * \param keys filled with a list of gpgme_key_t items matching the search pattern
 * \param bad_keys filled with the number of matching keys which are expired, disabled, revoked or invalid, may be NULL
 * \param pattern key search pattern (e.g. name, fingerprint, ...), may be NULL to list all keys
 * \param secret TRUE to search for private keys, FALSE to search for public keys
 * \param on_keyserver TRUE to search on a key server, FALSE to search the local key ring
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE if any error occurred
 *
 * Use the passed context to search for keys matching the passed criteria.  Note that even if the function returns success, the
 * list of keys may be empty if no matching key could be found.
 *
 * \note Listing external (key server) keys for a fingerprint longer than 16 hex characters fails, so be sure to cut them
 *       appropriately when calling this function with \em on_keyserver == TRUE.\n
 *       The returned list of keys shall be freed by the caller.
 * \todo We might want to add flags for returning only keys which have the is_qualified (subkey can be used for qualified
 *       signatures according to local government regulations) and/or is_de_vs (complies with the rules for classified information
 *       in Germany at the restricted level, VS-NfD, requires gpgme >= 1.9.0) properties set.
 */
gboolean libbalsa_gpgme_list_keys(gpgme_ctx_t   ctx,
						 	 	  GList       **keys,
								  guint        *bad_keys,
								  const gchar  *pattern,
								  gboolean      secret,
								  gboolean      on_keyserver,
								  GError      **error);

/** \brief Search the key server for a key
 *
 * \param fingerprint key fingerprint to search for
 * \param parent parent window
 * \param error filled with error information on error, may be NULL
 * \return TRUE on success, or FALSE on error
 *
 * Launch a new thread for searching the passed fingerprint on the key servers.  If the key is found, it is imported or updated in
 * the local key ring, and a dialogue is displayed.
 *
 * \note The passed fingerprint may be longer than 16 hex characters (see \ref libbalsa_gpgme_list_keys) and is truncated
 *       appropriately by this function if necessary.
 */
gboolean libbalsa_gpgme_keyserver_op(const gchar  *fingerprint,
									 GtkWindow    *parent,
									 GError      **error);

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


G_END_DECLS



#endif /* LIBBALSA_GPGME_KEYSERVER_H_ */
