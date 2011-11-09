/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
 * Copyright (C) 2004 Albrecht Dreﬂ <albrecht.dress@arcor.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __GMIME_PART_RFC2440_H__
#define __GMIME_PART_RFC2440_H__


#include <gmime/gmime.h>
#include "gmime-gpgme-context.h"


#ifdef __cplusplus
extern "C" {

#  ifdef MAKE_EMACS_HAPPY
}
#  endif
#endif				/* __cplusplus */


typedef enum _GMimePartRfc2440Mode GMimePartRfc2440Mode;
enum _GMimePartRfc2440Mode { 
    GMIME_PART_RFC2440_NONE,
    GMIME_PART_RFC2440_SIGNED,
    GMIME_PART_RFC2440_ENCRYPTED
};


/* part status check */
GMimePartRfc2440Mode g_mime_part_check_rfc2440(GMimePart * part);

/* crypto routines */
int g_mime_part_rfc2440_sign_encrypt(GMimePart * part,
				     GMimeGpgmeContext * ctx,
				     GPtrArray * recipients,
				     const char *sign_userid,
				     GError ** err);
#ifndef HAVE_GMIME_2_5_7
GMimeSignatureValidity *g_mime_part_rfc2440_verify(GMimePart * part,
						   GMimeGpgmeContext * ctx,
						   GError ** err);
GMimeSignatureValidity *g_mime_part_rfc2440_decrypt(GMimePart * part,
                                                    GMimeGpgmeContext *
                                                    ctx, GError ** err);
#else /* HAVE_GMIME_2_5_7 */
GMimeSignatureList *g_mime_part_rfc2440_verify(GMimePart * part,
                                               GMimeGpgmeContext * ctx,
                                               GError ** err);
GMimeDecryptResult *g_mime_part_rfc2440_decrypt(GMimePart * part,
                                                GMimeGpgmeContext * ctx,
                                                GError ** err);
#endif /* HAVE_GMIME_2_5_7 */

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __GMIME_PART_RFC2440_H__ */
