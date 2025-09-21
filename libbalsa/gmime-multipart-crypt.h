/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme implementation for multipart/signed and multipart/encrypted
 * Copyright (C) 2011 Albrecht Dreß <albrecht.dress@arcor.de>
 *
 * The functions in this module were copied from the original GMime
 * implementation of multipart/signed and multipart/encrypted parts.
 * However, instead of using the complex GMime crypto contexts (which have
 * a varying API over the different versions), this module directly calls
 * the GpgME backend functions implemented in libbalsa-gpgme.[hc].
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

#ifndef __GMIME_MULTIPART_CRYPT_H__
#define __GMIME_MULTIPART_CRYPT_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <gpgme.h>
#include "gmime-gpgme-signature.h"


G_BEGIN_DECLS


GMimeGpgmeSigstat *g_mime_gpgme_mps_verify(GMimeMultipartSigned * mps,
					   GError ** error);
gboolean g_mime_gpgme_mps_sign(GMimeMultipartSigned * mps,
			  GMimeObject * content, const gchar * userid,
			  gpgme_protocol_t protocol, GtkWindow * parent,
			  GError ** err);
gboolean g_mime_gpgme_mpe_encrypt(GMimeMultipartEncrypted * mpe,
			     GMimeObject * content, GPtrArray * recipients,
			     gboolean trust_all, GtkWindow * parent,
			     GError ** err);
GMimeObject *g_mime_gpgme_mpe_decrypt(GMimeMultipartEncrypted * mpe,
				      GMimeGpgmeSigstat ** signature,
				      GError ** err);


G_END_DECLS


#endif				/* __GMIME_MULTIPART_CRYPT_H__ */
