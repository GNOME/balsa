/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/*
 * gmime/gpgme glue layer library
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#ifndef __GMIME_GPGME_SIGNATURE_H__
#define __GMIME_GPGME_SIGNATURE_H__

#include <gpgme.h>
#include <glib.h>


G_BEGIN_DECLS

/* the signature status as returned by gpgme as a GObject */
#define GMIME_TYPE_GPGME_SIGSTAT	    (g_mime_gpgme_sigstat_get_type())
#define GMIME_GPGME_SIGSTAT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstat))
#define GMIME_GPGME_SIGSTAT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstatClass))
#define GMIME_IS_GPGME_SIGSTAT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GMIME_TYPE_GPGME_SIGSTAT))
#define GMIME_IS_GPGME_SIGSTAT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GMIME_TYPE_GPGME_SIGSTAT))
#define GMIME_GPGME_SIGSTAT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GMIME_TYPE_GPGME_SIGSTAT, GMimeGpgmeSigstatClass))

typedef struct _GMimeGpgmeSigstat GMimeGpgmeSigstat;
typedef struct _GMimeGpgmeSigstatClass GMimeGpgmeSigstatClass;
typedef struct _sig_uid_t sig_uid_t;

struct _GMimeGpgmeSigstat {
    GObject parent;

    /* results form gpgme's verify operation */
    gpgme_protocol_t protocol;
    gpgme_sigsum_t summary;
    gpgme_error_t status;
    gpgme_validity_t validity;
    gchar *fingerprint;
    time_t sign_time;

    /* information about the key used to create the signature */
    gpgme_key_t key;
};

struct _GMimeGpgmeSigstatClass {
    GObjectClass parent;
};


GType g_mime_gpgme_sigstat_get_type(void);
GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new(gpgme_ctx_t ctx)
	G_GNUC_WARN_UNUSED_RESULT;
GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t ctx)
	G_GNUC_WARN_UNUSED_RESULT;
void g_mime_gpgme_sigstat_load_key(GMimeGpgmeSigstat *sigstat);

const gchar *g_mime_gpgme_sigstat_protocol_name(const GMimeGpgmeSigstat *sigstat);
gchar *g_mime_gpgme_sigstat_to_gchar(const GMimeGpgmeSigstat *info,
							  	  	 gboolean                 full_details,
									 const gchar             *date_string)
	G_GNUC_WARN_UNUSED_RESULT;

gchar *libbalsa_cert_subject_readable(const gchar *subject)
	G_GNUC_WARN_UNUSED_RESULT;


G_END_DECLS

#endif				/* __GMIME_GPGME_SIGNATURE_H__ */
