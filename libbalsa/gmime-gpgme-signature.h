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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */
#ifndef __GMIME_GPGME_SIGNATURE_H__
#define __GMIME_GPGME_SIGNATURE_H__

#include <gpgme.h>
#include <glib.h>


G_BEGIN_DECLS

/* the signature status as returned by gpgme as a GObject */
#define GMIME_TYPE_GPGME_SIGSTAT	    (g_mime_gpgme_sigstat_get_type())
G_DECLARE_FINAL_TYPE(GMimeGpgmeSigstat, g_mime_gpgme_sigstat, GMIME, GPGME_SIGSTAT, GObject)


GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new(gpgme_ctx_t ctx)
	G_GNUC_WARN_UNUSED_RESULT;
GMimeGpgmeSigstat *g_mime_gpgme_sigstat_new_from_gpgme_ctx(gpgme_ctx_t ctx)
	G_GNUC_WARN_UNUSED_RESULT;
void g_mime_gpgme_sigstat_set_status(GMimeGpgmeSigstat *sigstat,
									 gpgme_error_t      status);

gchar *g_mime_gpgme_sigstat_info(GMimeGpgmeSigstat *info,
								 gboolean                 with_signer)
	G_GNUC_WARN_UNUSED_RESULT;
gchar *g_mime_gpgme_sigstat_to_gchar(GMimeGpgmeSigstat *info,
							  	  	 gboolean                 full_details,
									 const gchar             *date_string)
	G_GNUC_WARN_UNUSED_RESULT;
gchar *g_mime_gpgme_sigstat_signer(GMimeGpgmeSigstat *sigstat)
	G_GNUC_WARN_UNUSED_RESULT;
gpgme_protocol_t g_mime_gpgme_sigstat_protocol(GMimeGpgmeSigstat *sigstat);
gpgme_error_t g_mime_gpgme_sigstat_status(GMimeGpgmeSigstat *sigstat);
gpgme_sigsum_t g_mime_gpgme_sigstat_summary(GMimeGpgmeSigstat *sigstat);
gpgme_key_t g_mime_gpgme_sigstat_key(GMimeGpgmeSigstat *sigstat);
const gchar *g_mime_gpgme_sigstat_fingerprint(GMimeGpgmeSigstat *sigstat);

gchar *libbalsa_cert_subject_readable(const gchar *subject)
	G_GNUC_WARN_UNUSED_RESULT;


G_END_DECLS

#endif				/* __GMIME_GPGME_SIGNATURE_H__ */
