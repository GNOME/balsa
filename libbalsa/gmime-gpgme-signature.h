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
#ifndef __GMIME_GPGME_SIGNATURE_H__
#define __GMIME_GPGME_SIGNATURE_H__

#include <gpgme.h>
#include <glib-object.h>


#ifdef __cplusplus
extern "C" {
#  ifdef MAKE_EMACS_HAPPY
}
#  endif
#endif				/* __cplusplus */


/* the signature status as returned by gpgme as a GObject */
#define GMIME_TYPE_GPGME_SIGSTAT	    (g_mime_gpgme_sigstat_get_type())
#define GMIME_GPGME_SIGSTAT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstat))
#define GMIME_GPGME_SIGSTAT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstatClass))
#define GMIME_IS_GPGME_SIGSTAT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GMIME_TYPE_GPGME_SIGSTAT))
#define GMIME_IS_GPGME_SIGSTAT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GMIME_TYPE_GPGME_SIGSTAT))
#define GMIME_GPGME_SIGSTAT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstatClass))

typedef struct _GMimeGpgmeSigstat GMimeGpgmeSigstat;
typedef struct _GMimeGpgmeSigstatClass GMimeGpgmeSigstatClass;


struct _GMimeGpgmeSigstat {
    GObject parent;

    /* various data gathered by gpgme's verify */
    gpgme_protocol_t protocol;
    gpgme_error_t status;
    gpgme_validity_t validity;
    gpgme_validity_t trust;
    gchar *sign_name;
    gchar *sign_email;
    gchar *fingerprint;
    gchar *sign_uid;
    gchar *issuer_serial;
    gchar *issuer_name;
    gchar *chain_id;
    time_t key_created;
    time_t key_expires;
    gboolean key_revoked;
    gboolean key_expired;
    gboolean key_disabled;
    gboolean key_invalid;
    time_t sign_time;
};

struct _GMimeGpgmeSigstatClass {
    GObjectClass parent;
};


GType g_mime_gpgme_sigstat_get_type(void);
GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new(void);
GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t
							   ctx);

#ifdef __cplusplus
}
#endif				/* __cplusplus */

#endif				/* __GMIME_GPGME_SIGNATURE_H__ */
