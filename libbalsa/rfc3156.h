/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2001 Stuart Parmenter and others,
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */

#ifndef __RFC3156_H__
#define __RFC3156_H__

#include "config.h"

#ifdef HAVE_GPGME

#include <gnome.h>
#include <gpgme.h>
#include "libbalsa.h"
#include "misc.h"

#define LIBBALSA_GPG_SIGN      (1 << 0)
#define LIBBALSA_GPG_ENCRYPT   (1 << 1)
#define LIBBALSA_GPG_RFC2440   (1 << 2)

typedef struct _LibBalsaSignatureInfo LibBalsaSignatureInfo;
typedef enum _LibBalsaMessageBodyRFC2440Mode LibBalsaMessageBodyRFC2440Mode;

struct _LibBalsaSignatureInfo {
    GpgmeSigStat status;
    GpgmeValidity validity;
    GpgmeValidity trust;
    gchar *sign_name;
    gchar *sign_email;
    gchar *fingerprint;
    time_t key_created;
    time_t sign_time;
};

enum _LibBalsaMessageBodyRFC2440Mode {
    LIBBALSA_BODY_RFC2440_NONE = 0,
    LIBBALSA_BODY_RFC2440_SIGNED,
    LIBBALSA_BODY_RFC2440_ENCRYPTED
};


gint libbalsa_is_pgp_signed(LibBalsaMessageBody *body);
gint libbalsa_is_pgp_encrypted(LibBalsaMessageBody *body);

gboolean libbalsa_sign_mime_object(GMimeObject **content,
				   const gchar *rfc822_for,
				   GtkWindow *parent);
gboolean libbalsa_encrypt_mime_object(GMimeObject **content, GList *rfc822_for);
gboolean libbalsa_sign_encrypt_mime_object(GMimeObject **content,
					   const gchar *rfc822_signer,
					   GList *rfc822_for,
					   GtkWindow *parent);

gboolean libbalsa_body_check_signature(LibBalsaMessageBody *body);
LibBalsaMessageBody *libbalsa_body_decrypt(LibBalsaMessageBody *body,
					   GtkWindow *parent);

LibBalsaMessageBodyRFC2440Mode libbalsa_rfc2440_check_buffer(const gchar *buffer);
gchar *libbalsa_rfc2440_sign_buffer(const gchar *buffer,
				    const gchar *sign_for,
				    GtkWindow *parent);
GpgmeSigStat libbalsa_rfc2440_check_signature(gchar **buffer,
					      const gchar *charset,
					      gboolean append_info,
					      LibBalsaSignatureInfo **sig_info,
					      const gchar * date_string);
gchar *libbalsa_rfc2440_encrypt_buffer(const gchar *buffer,
				       const gchar *sign_for,
				       GList *encrypt_for,
				       GtkWindow *parent);
GpgmeSigStat libbalsa_rfc2440_decrypt_buffer(gchar **buffer,
					     const gchar *charset,
					     gboolean fallback,
					     LibBalsaCodeset codeset,
					     gboolean append_info,
					     LibBalsaSignatureInfo **sig_info,
					     const gchar *date_string,
					     GtkWindow *parent);

LibBalsaSignatureInfo *libbalsa_signature_info_destroy(LibBalsaSignatureInfo* info);
const gchar *libbalsa_gpgme_sig_stat_to_gchar(GpgmeSigStat stat);
const gchar *libbalsa_gpgme_validity_to_gchar(GpgmeValidity validity);
gchar *libbalsa_signature_info_to_gchar(LibBalsaSignatureInfo * info, 
					const gchar * date_string);

#ifdef HAVE_GPG
gboolean gpg_ask_import_key(const gchar *message, GtkWindow *parent,
			    const gchar *fingerprint);
#endif

#endif /* HAVE_GPGME */

#endif /* __RFC3156_GPG_H__ */
