/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * S/MIME application/pkcs7-mime support for gmime/balsa
 * Copyright (C) 2004 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef __GMIME_APPLICATION_PKCS7_H__
#define __GMIME_APPLICATION_PKCS7_H__

G_BEGIN_DECLS

#include <gmime/gmime.h>


GMimeObject *g_mime_application_pkcs7_decrypt_verify(GMimePart          *pkcs7,
													 GMimeGpgmeSigstat **signature,
													 GError            **err);
	gboolean g_mime_application_pkcs7_encrypt(GMimePart * pkcs7,
				  GMimeObject * content,
					 GPtrArray * recipients,
					 gboolean trust_all,
					 GtkWindow * parent,
					 GError ** err);

G_END_DECLS

#endif				/* __GMIME_APPLICATION_PKCS7_H__ */
