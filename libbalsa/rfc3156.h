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


/* bits to define the protection mode: signed or encrypted */
#define LIBBALSA_PROTECT_SIGN      (1 << 0)
#define LIBBALSA_PROTECT_ENCRYPT   (1 << 1)
#define LIBBALSA_PROTECT_MODE      (3 << 0)

/* bits to define the protection method */
#define LIBBALSA_PROTECT_OPENPGP   (1 << 2)     /* RFC 2440 (OpenPGP) */
#define LIBBALSA_PROTECT_SMIMEV3   (1 << 3)     /* RFC 2633 (S/MIME v3) */
#define LIBBALSA_PROTECT_RFC3156   (1 << 4)     /* RFC 3156 (PGP/MIME) */
#define LIBBALSA_PROTECT_PROTOCOL  (7 << 2)

/* indicate broken structure */
#define LIBBALSA_PROTECT_ERROR     (1 << 5)


typedef struct _LibBalsaSignatureInfo LibBalsaSignatureInfo;

struct _LibBalsaSignatureInfo {
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
    time_t sign_time;
};


gboolean libbalsa_check_crypto_engine(gpgme_protocol_t protocol);

gint libbalsa_message_body_protection(LibBalsaMessageBody *body);

gboolean libbalsa_sign_mutt_body(MuttBody **sign_body, const gchar *rfc822_for,
				 gchar **micalg, GtkWindow *parent);
gboolean libbalsa_encrypt_mutt_body(MuttBody **encrypt_body,
				    GList *rfc822_for);
gboolean libbalsa_sign_encrypt_mutt_body(MuttBody **se_body,
					 const gchar *rfc822_signer,
					 GList *rfc822_for,
					 GtkWindow *parent);

gboolean libbalsa_body_check_signature(LibBalsaMessageBody *body);
LibBalsaMessageBody *libbalsa_body_decrypt(LibBalsaMessageBody *body,
					   GtkWindow *parent);

gint libbalsa_rfc2440_check_buffer(const gchar *buffer);
gchar *libbalsa_rfc2440_sign_buffer(const gchar *buffer,
				    const gchar *sign_for,
				    GtkWindow *parent);
gpgme_error_t libbalsa_rfc2440_check_signature(gchar **buffer,
					       const gchar *charset,
					       gboolean append_info,
					       LibBalsaSignatureInfo **sig_info,
					       const gchar * date_string);
gchar *libbalsa_rfc2440_encrypt_buffer(const gchar *buffer,
				       const gchar *sign_for,
				       GList *encrypt_for,
				       GtkWindow *parent);
gpgme_error_t libbalsa_rfc2440_decrypt_buffer(gchar **buffer,
					      const gchar *charset,
					      gboolean fallback,
					      LibBalsaCodeset codeset,
					      gboolean append_info,
					      LibBalsaSignatureInfo **sig_info,
					      const gchar *date_string,
					      GtkWindow *parent);

LibBalsaSignatureInfo *libbalsa_signature_info_destroy(LibBalsaSignatureInfo* info);
const gchar *libbalsa_gpgme_sig_stat_to_gchar(gpgme_error_t stat);
const gchar *libbalsa_gpgme_validity_to_gchar(gpgme_validity_t validity);
gchar *libbalsa_signature_info_to_gchar(LibBalsaSignatureInfo * info, 
					const gchar * date_string);

#ifdef HAVE_GPG
gboolean gpg_run_import_key(const gchar *fingerprint, GtkWindow *parent);
#endif

#endif /* HAVE_GPGME */

#endif /* __RFC3156_GPG_H__ */
