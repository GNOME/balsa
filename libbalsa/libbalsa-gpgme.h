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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
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
#include "libbalsa-gpgme-cb.h"
#include "gmime-gpgme-signature.h"


G_BEGIN_DECLS


#define GPG_ERR_MULT_SIGNATURES        GPG_ERR_USER_13
#define GPG_ERR_KEY_SELECTION          GPG_ERR_USER_14
#define GPG_ERR_NOT_SIGNED             GPG_ERR_USER_15

#define GPGME_ERROR_QUARK (g_quark_from_static_string("gmime-gpgme"))


struct _gpg_capabilities {
	const gchar *gpg_path;				/**< OpenPGP engine path */
	gboolean export_filter_uid;			/**< OpenPGP engine supports the 'keep-uid=...' export-filter option. */
	gboolean export_filter_subkey;		/**< OpenPGP engine supports the 'drop-subkey=...' export-filter option. */
};

typedef struct _gpg_capabilities gpg_capabilities;


/** Callback to select a key from a list
 * Parameters:
 * - user name
 * - key selection mode
 * - list of available keys (gpgme_key_t data elements)
 * - protocol
 * - parent window
 * Return: the key the user selected, or NULL if the operation shall be cancelled
 */
typedef gpgme_key_t(*lbgpgme_select_key_cb) (const gchar *,
						 	 	 	 	 	 lb_key_sel_md_t,
											 GList *,
											 gpgme_protocol_t,
											 GtkWindow *);

/** Callback to ask the user whether a key with low trust shall be accepted
 * Parameters:
 * - recipient user name (email address)
 * - the key with insufficient trust
 * - parent window
 * Return: TRUE to accept the key, FALSE to reject it
 */
typedef gboolean(*lbgpgme_accept_low_trust_cb) (const gchar *,
												gpgme_key_t,
												GtkWindow *);



void libbalsa_gpgme_init(lbgpgme_select_key_cb       select_key_cb,
						 lbgpgme_accept_low_trust_cb accept_low_trust);
const gchar *libbalsa_gpgme_protocol_name(gpgme_protocol_t protocol);
gboolean libbalsa_gpgme_check_crypto_engine(gpgme_protocol_t protocol);
const gpg_capabilities *libbalsa_gpgme_gpg_capabilities(void);
gpgme_ctx_t libbalsa_gpgme_new_with_proto(gpgme_protocol_t        protocol,
										  GError                **error)
	G_GNUC_WARN_UNUSED_RESULT;
gpgme_ctx_t libbalsa_gpgme_temp_with_proto(gpgme_protocol_t   protocol,
										   gchar            **home_dir,
										   GError           **error)
	G_GNUC_WARN_UNUSED_RESULT;

GMimeGpgmeSigstat *libbalsa_gpgme_verify(GMimeStream * content,
					 GMimeStream * sig_plain,
					 gpgme_protocol_t protocol,
					 gboolean singlepart_mode,
					 GError ** error)
	G_GNUC_WARN_UNUSED_RESULT;

gpgme_hash_algo_t libbalsa_gpgme_sign(const gchar * userid,
				      GMimeStream * istream,
				      GMimeStream * ostream,
				      gpgme_protocol_t protocol,
				      gboolean singlepart_mode,
				      GtkWindow * parent, GError ** error);

gboolean libbalsa_gpgme_encrypt(GPtrArray * recipients,
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
					  GError ** error)
	G_GNUC_WARN_UNUSED_RESULT;

gchar *libbalsa_gpgme_get_pubkey(gpgme_protocol_t   protocol,
								 const gchar       *name,
								 GtkWindow 		   *parent,
								 GError           **error)
	G_GNUC_WARN_UNUSED_RESULT;

gchar *libbalsa_gpgme_get_seckey(gpgme_protocol_t   protocol,
  	  	  	  	  	  	  	  	 const gchar       *name,
								 GtkWindow 		   *parent,
								 GError           **error)
	G_GNUC_WARN_UNUSED_RESULT;

void libbalsa_gpgme_set_error(GError        **error,
					          gpgme_error_t   gpgme_err,
							  const gchar    *format,
							  ...)
	G_GNUC_PRINTF(3, 4);


G_END_DECLS


#endif				/* LIBBALSA_GPGME_H_ */
