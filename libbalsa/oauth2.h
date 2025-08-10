/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2020 Stuart Parmenter and others,
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

#ifndef LIBBALSA_OAUTH2_H_
#define LIBBALSA_OAUTH2_H_


#include "config.h"
#include "server.h"


#if defined(HAVE_OAUTH2)


#define LIBBALSA_OAUTH2_ERROR_QUARK			(g_quark_from_static_string("libbalsa-oauth2"))


#define LIBBALSA_OAUTH2_TYPE				(libbalsa_oauth2_get_type())
G_DECLARE_FINAL_TYPE(LibBalsaOauth2, libbalsa_oauth2, LIBBALSA, OAUTH2, GObject)


/** @brief Load the configuration of OAuth2 providers
 *
 * The function looks for files named @c balsa-oauth2.cfg in this order in the subfolder @c balsa of all folders defined in
 * <em>XDG_DATA_DIRS</em>, the user's @em XDG_CONFIG_HOME (<c>~/.config/balsa</c>) and the folder from which Balsa is launched. See
 * the file distributed with Balsa for the format.  Note that items with the same section identifier overwrite previously loaded
 * definitions.
 */
void libbalsa_oauth2_load_providers(void);

/** @brief Check for a provider supporting OAuth2
 *
 * @param[in] mailbox email address
 * @return TRUE if the passed email address belongs to a known provider supporting OAuth2, FALSE if not
 */
gboolean libbalsa_oauth2_supported(const gchar *mailbox);

/** @brief Get the OAuth2 context for a POP3, IMAP or SMTP server
 *
 * @param[in] server server configuration
 * @param[out] error location for error, may be NULL
 * @return the OAuth2 context on success, NULL on error
 */
LibBalsaOauth2 *libbalsa_oauth2_new(LibBalsaServer  *server,
									GError         **error)
	G_GNUC_WARN_UNUSED_RESULT;

/** @brief Get the OAuth2 access token
 *
 * @param[in] oauth OAuth2 context returned by calling libbalsa_oauth2_new()
 * @param[in] parent transient parent of the authorisation dialogue
 * @param[out] error location for error, may be NULL
 * @return a newly allocated string containing the OAuth2 access token on success, NULL on error
 */
gchar *libbalsa_oauth2_token(LibBalsaOauth2  *oauth,
							 GtkWindow       *parent,
							 GError         **error)
	G_GNUC_WARN_UNUSED_RESULT;


#endif	/* defined(HAVE_OAUTH2) */


#endif /* LIBBALSA_OAUTH2_H_ */
