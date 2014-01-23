/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gpgme low-level stuff for gmime/balsa
 * Copyright (C) 2011 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef LIBBALSA_GPGME_H_
#define LIBBALSA_GPGME_H_


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gpgme.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include "gmime-gpgme-signature.h"


#ifdef __cplusplus
extern "C" {
#ifdef MAKE_EMACS_HAPPY
}
#endif
#endif				/* __cplusplus */


#define GPG_ERR_KEY_SELECTION          GPG_ERR_USER_14
#define GPG_ERR_TRY_AGAIN              GPG_ERR_USER_15
#define GPG_ERR_NOT_SIGNED             GPG_ERR_USER_16

#define GPGME_ERROR_QUARK (g_quark_from_static_string("gmime-gpgme"))


/** Callback to select a key from a list
 * Parameters:
 * - user name
 * - TRUE is a secret key shall be selected
 * - list of available keys (gpgme_key_t data elements)
 * - protocol
 * - parent window
 */ typedef gpgme_key_t(*lbgpgme_select_key_cb) (const gchar *,
						 gboolean,
						 GList *,
						 gpgme_protocol_t,
						 GtkWindow *);

/** Callback to ask the user whether a key with low trust shall be accepted
 * Parameters:
 * - user name
 * - GpgME user ID
 * - parent window
 */
typedef gboolean(*lbgpgme_accept_low_trust_cb) (const gchar *,
						const gpgme_user_id_t,
						GtkWindow *);



void libbalsa_gpgme_init(gpgme_passphrase_cb_t get_passphrase,
			 lbgpgme_select_key_cb select_key_cb,
			 lbgpgme_accept_low_trust_cb accept_low_trust);
gboolean libbalsa_gpgme_check_crypto_engine(gpgme_protocol_t protocol);

GMimeGpgmeSigstat *libbalsa_gpgme_verify(GMimeStream * content,
					 GMimeStream * sig_plain,
					 gpgme_protocol_t protocol,
					 gboolean singlepart_mode,
					 GError ** error);

gpgme_hash_algo_t libbalsa_gpgme_sign(const gchar * userid,
				      GMimeStream * istream,
				      GMimeStream * ostream,
				      gpgme_protocol_t protocol,
				      gboolean singlepart_mode,
				      GtkWindow * parent, GError ** error);

int libbalsa_gpgme_encrypt(GPtrArray * recipients,
			   const char *sign_for,
			   GMimeStream * istream,
			   GMimeStream * ostream,
			   gpgme_protocol_t protocol,
			   gboolean singlepart_mode,
			   gboolean trust_all_keys,
			   GtkWindow * parent, GError ** error);

GMimeGpgmeSigstat *libbalsa_gpgme_decrypt(GMimeStream * crypted,
					  GMimeStream * plain,
					  gpgme_protocol_t protocol,
					  GtkWindow * parent,
					  GError ** error);


#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* LIBBALSA_GPGME_H_ */
