/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __RFC3156_H__
#define __RFC3156_H__

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

/* bits to define the protection method; needed even when we don't
 * HAVE_GPGME */
#define LIBBALSA_PROTECT_OPENPGP       (1 << 2)	/* RFC 2440 (OpenPGP) */
#define LIBBALSA_PROTECT_SMIMEV3       (1 << 3)	/* RFC 2633 (S/MIME v3) */
#define LIBBALSA_PROTECT_RFC3156       (1 << 4)	/* RFC 3156 (PGP/MIME) */

#ifdef HAVE_GPGME

#include <gpgme.h>
#include "libbalsa.h"
#include "misc.h"
#include "gmime-gpgme-signature.h"


/* bits to define the protection mode: signed or encrypted */
#define LIBBALSA_PROTECT_SIGN          (1 << 0)
#define LIBBALSA_PROTECT_ENCRYPT       (1 << 1)
#define LIBBALSA_PROTECT_MODE          (3 << 0)

/* bits to define the protection method */
#define LIBBALSA_PROTECT_PROTOCOL      (7 << 2)

/* indicate broken structure */
#define LIBBALSA_PROTECT_ERROR         (1 << 5)

/* indicate that uid's should always be trusted */
#define LIBBALSA_PROTECT_ALWAYS_TRUST  (1 << 6)


/* some custom error messages */
#define GPG_ERR_TRY_AGAIN          GPG_ERR_USER_15
#define GPG_ERR_NOT_SIGNED         GPG_ERR_USER_16


gint libbalsa_message_body_protection(LibBalsaMessageBody * body);
gboolean libbalsa_can_encrypt_for_all(InternetAddressList * recipients,
				      gpgme_protocol_t protocol);

/* routines dealing with RFC 2633 and RFC 3156 stuff */
gboolean libbalsa_sign_mime_object(GMimeObject ** content,
				   const gchar * rfc822_for,
				   gpgme_protocol_t protocol,
				   GtkWindow * parent,
				   GError ** error);
gboolean libbalsa_encrypt_mime_object(GMimeObject ** content,
				      GList * rfc822_for,
				      gpgme_protocol_t protocol,
				      gboolean always_trust,
				      GtkWindow * parent,
				      GError ** error);
gboolean libbalsa_sign_encrypt_mime_object(GMimeObject ** content,
					   const gchar * rfc822_signer,
					   GList * rfc822_for,
					   gpgme_protocol_t protocol,
					   gboolean always_trust,
					   GtkWindow * parent,
					   GError ** error);
gboolean libbalsa_body_check_signature(LibBalsaMessageBody * body,
				       gpgme_protocol_t protocol);
LibBalsaMessageBody *libbalsa_body_decrypt(LibBalsaMessageBody * body,
					   gpgme_protocol_t protocol,
					   GtkWindow * parent);

/* routines dealing with RFC 2440 stuff */
gboolean libbalsa_rfc2440_sign_encrypt(GMimePart * part,
				       const gchar * sign_for,
				       GList * encrypt_for,
				       gboolean always_trust,
				       GtkWindow * parent,
				       GError ** error);
gpgme_error_t libbalsa_rfc2440_verify(GMimePart * part,
				      GMimeGpgmeSigstat ** sig_info);
gpgme_error_t libbalsa_rfc2440_decrypt(GMimePart * part,
				       GMimeGpgmeSigstat ** sig_info,
				       GtkWindow * parent);

/* helper functions to convert states to human-readable form */
const gchar *libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat);
const gchar *libbalsa_gpgme_validity_to_gchar(gpgme_validity_t validity);
const gchar *libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t validity);

#endif				/* HAVE_GPGME */
#endif				/* __RFC3156_H__ */
