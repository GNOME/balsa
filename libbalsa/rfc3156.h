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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __RFC3156_H__
#define __RFC3156_H__

#include <gpgme.h>
#include "libbalsa.h"
#include "misc.h"
#include "gmime-gpgme-signature.h"


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
					   gpgme_protocol_t protocol);

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
				       GMimeGpgmeSigstat ** sig_info);

/* helper functions to convert states to human-readable form */
gchar *libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat)
	G_GNUC_WARN_UNUSED_RESULT;
const gchar *libbalsa_gpgme_validity_to_gchar(gpgme_validity_t validity);
const gchar *libbalsa_gpgme_validity_to_gchar_short(gpgme_validity_t validity);

#endif				/* __RFC3156_H__ */
