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

#ifndef __GMIME_GPGME_CONTEXT_H__
#define __GMIME_GPGME_CONTEXT_H__

#ifdef __cplusplus
extern "C" {
#ifdef MAKE_EMACS_HAPPY
}
#endif
#endif				/* __cplusplus */
#include <gmime/gmime.h>
#include <gpgme.h>
#include "gmime-gpgme-signature.h"
#define GMIME_TYPE_GPGME_CONTEXT            (g_mime_gpgme_context_get_type ())
#define GMIME_GPGME_CONTEXT(obj)            (GMIME_CHECK_CAST ((obj), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgmeContext))
#define GMIME_GPGME_CONTEXT_CLASS(klass)    (GMIME_CHECK_CLASS_CAST ((klass), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgmeContextClass))
#define GMIME_IS_GPGME_CONTEXT(obj)         (GMIME_CHECK_TYPE ((obj), GMIME_TYPE_GPGME_CONTEXT))
#define GMIME_IS_GPGME_CONTEXT_CLASS(klass) (GMIME_CHECK_CLASS_TYPE ((klass), GMIME_TYPE_GPGME_CONTEXT))
#define GMIME_GPGME_CONTEXT_GET_CLASS(obj)  (GMIME_CHECK_GET_CLASS ((obj), GMIME_TYPE_GPGME_CONTEXT, GMimeGpgmeContextClass))
#define GPG_ERR_KEY_SELECTION          GPG_ERR_USER_14
#define GPG_ERR_TRY_AGAIN              GPG_ERR_USER_15
#define GPG_ERR_NOT_SIGNED             GPG_ERR_USER_16
#define GPGME_USE_GMIME_SESSION_CB     (gpgme_passphrase_cb_t)(-1)
typedef struct _GMimeGpgmeContext GMimeGpgmeContext;
typedef struct _GMimeGpgmeContextClass GMimeGpgmeContextClass;


/* callback handler to select a key from a list */
typedef gpgme_key_t(*GMimeGpgmeKeySelectCB) (const gchar * name,
					     gboolean is_secret,
					     GMimeGpgmeContext * ctx,
					     GList * keys);

struct _GMimeGpgmeContext {
    GMimeCipherContext parent_object;

    gpgme_ctx_t gpgme_ctx;	/* gpgme context */
    gboolean rfc2440_mode;	/* set context to RFC 2440 mode */
    gchar *micalg;		/* hash algorithm (signing only) */
    GMimeGpgmeSigstat *sig_state;	/* signature status */
    GMimeGpgmeKeySelectCB key_select_cb;	/* key selection callback */
    gpgme_passphrase_cb_t passphrase_cb;	/* passphrase callback */
};


struct _GMimeGpgmeContextClass {
    GMimeCipherContextClass parent_class;

    gboolean has_proto_openpgp;
    gboolean has_proto_cms;
};


GType g_mime_gpgme_context_get_type(void);
GMimeCipherContext *g_mime_gpgme_context_new(GMimeSession * session,
					     gpgme_protocol_t protocol,
					     GError ** error);


#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __GMIME_GPGME_CONTEXT_H__ */
