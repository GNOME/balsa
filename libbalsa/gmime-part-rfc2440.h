/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme implementation for RFC2440 parts
 * Copyright (C) 2004-2016 Albrecht Dreß <albrecht.dress@arcor.de>
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

#ifndef __GMIME_PART_RFC2440_H__
#define __GMIME_PART_RFC2440_H__


#include <glib.h>
#include <gtk/gtk.h>
#include <gmime/gmime.h>


G_BEGIN_DECLS


typedef enum _GMimePartRfc2440Mode GMimePartRfc2440Mode;
enum _GMimePartRfc2440Mode { 
    GMIME_PART_RFC2440_NONE,
    GMIME_PART_RFC2440_SIGNED,
    GMIME_PART_RFC2440_ENCRYPTED
};


/* part status check */
GMimePartRfc2440Mode g_mime_part_check_rfc2440(GMimePart * part);

/* crypto routines */
gboolean g_mime_part_rfc2440_sign_encrypt(GMimePart * part,
				     const char *sign_userid,
				     GPtrArray * recipients,
				     gboolean trust_all,
				     GtkWindow * parent, GError ** err);
GMimeGpgmeSigstat *g_mime_part_rfc2440_verify(GMimePart * part,
				     GError ** err);
GMimeGpgmeSigstat *g_mime_part_rfc2440_decrypt(GMimePart *part,
                                               GError   **err);

G_END_DECLS


#endif				/* __GMIME_PART_RFC2440_H__ */
