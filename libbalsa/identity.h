/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2016 Stuart Parmenter and others,
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#ifndef __LIBBALSA_IDENTITY_H__
#define __LIBBALSA_IDENTITY_H__

#ifndef BALSA_VERSION
#   error "Include config.h before this file."
#endif

#include <gmime/internet-address.h>

#include "libbalsa.h"


G_BEGIN_DECLS

#define LIBBALSA_TYPE_IDENTITY (libbalsa_identity_get_type ())
G_DECLARE_FINAL_TYPE(LibBalsaIdentity,
                     libbalsa_identity,
                     LIBBALSA,
                     IDENTITY,
                     GObject)

/* Function prototypes */
LibBalsaIdentity * libbalsa_identity_new(void);
LibBalsaIdentity *libbalsa_identity_new_with_name(const gchar *ident_name);
LibBalsaIdentity *libbalsa_identity_new_from_config(const gchar *name);
void              libbalsa_identity_save(LibBalsaIdentity *ident,
                                         const gchar      *prefix);

/* Setters */
void libbalsa_identity_set_identity_name(LibBalsaIdentity *ident,
                                         const gchar      *name);
void libbalsa_identity_set_address(LibBalsaIdentity *ident,
                                   InternetAddress  *ia);
void libbalsa_identity_set_replyto(LibBalsaIdentity *ident,
                                   const gchar      *replyto);
void libbalsa_identity_set_domain(LibBalsaIdentity *ident,
                                  const gchar      *text);
void libbalsa_identity_set_bcc(LibBalsaIdentity *ident,
                               const gchar      *text);
void libbalsa_identity_set_reply_string(LibBalsaIdentity *ident,
                                        const gchar      *text);
void libbalsa_identity_set_forward_string(LibBalsaIdentity *ident,
                                          const gchar      *text);
void libbalsa_identity_set_send_mp_alternative(LibBalsaIdentity *ident,
                                               gboolean          set);
void libbalsa_identity_set_signature_path(LibBalsaIdentity *ident,
                                          const gchar      *text);
void libbalsa_identity_set_sig_executable(LibBalsaIdentity *ident,
                                          gboolean          set);
void libbalsa_identity_set_sig_sending(LibBalsaIdentity *ident,
                                       gboolean          set);
void libbalsa_identity_set_sig_whenforward(LibBalsaIdentity *ident,
                                           gboolean          set);
void libbalsa_identity_set_sig_whenreply(LibBalsaIdentity *ident,
                                         gboolean          set);
void libbalsa_identity_set_sig_separator(LibBalsaIdentity *ident,
                                         gboolean          set);
void libbalsa_identity_set_sig_prepend(LibBalsaIdentity *ident,
                                       gboolean          set);
void libbalsa_identity_set_face_path(LibBalsaIdentity *ident,
                                     const gchar      *text);
void libbalsa_identity_set_x_face_path(LibBalsaIdentity *ident,
                                       const gchar      *text);
void libbalsa_identity_set_request_mdn(LibBalsaIdentity *ident,
                                       gboolean          set);
void libbalsa_identity_set_request_dsn(LibBalsaIdentity *ident,
                                       gboolean          set);
void libbalsa_identity_set_always_trust(LibBalsaIdentity *ident,
                                        gboolean          set);
void libbalsa_identity_set_warn_send_plain(LibBalsaIdentity *ident,
                                           gboolean          set);
void libbalsa_identity_set_force_gpg_key_id(LibBalsaIdentity *ident,
                                            const gchar      *text);
void libbalsa_identity_set_force_smime_key_id(LibBalsaIdentity *ident,
                                              const gchar      *text);

gchar *libbalsa_identity_get_signature(LibBalsaIdentity *ident,
                                       GError          **error);
void   libbalsa_identity_set_smtp_server(LibBalsaIdentity   *ident,
                                         LibBalsaSmtpServer *smtp_server);

void libbalsa_identity_set_gpg_sign(LibBalsaIdentity *ident,
                                    gboolean          set);
void libbalsa_identity_set_gpg_encrypt(LibBalsaIdentity *ident,
                                       gboolean          set);
void libbalsa_identity_set_crypt_protocol(LibBalsaIdentity *ident,
                                          gint              proto);


/*
 * Getters
 */

gboolean            libbalsa_identity_get_sig_prepend(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_sig_whenreply(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_sig_whenforward(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_sig_sending(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_send_mp_alternative(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_request_mdn(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_request_dsn(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_warn_send_plain(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_always_trust(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_gpg_sign(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_gpg_encrypt(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_sig_executable(LibBalsaIdentity *ident);
gboolean            libbalsa_identity_get_sig_separator(LibBalsaIdentity *ident);
gint                libbalsa_identity_get_crypt_protocol(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_identity_name(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_force_gpg_key_id(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_force_smime_key_id(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_replyto(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_bcc(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_reply_string(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_forward_string(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_domain(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_face_path(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_x_face_path(LibBalsaIdentity *ident);
const gchar        *libbalsa_identity_get_signature_path(LibBalsaIdentity *ident);
InternetAddress    *libbalsa_identity_get_address(LibBalsaIdentity *ident);
LibBalsaSmtpServer *libbalsa_identity_get_smtp_server(LibBalsaIdentity *ident);

G_END_DECLS


#endif /* __LIBBALSA_IDENTITY_H__ */
