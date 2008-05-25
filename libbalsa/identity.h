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


#ifndef __LIBBALSA_IDENTITY_H__
#define __LIBBALSA_IDENTITY_H__

#include "config.h"

#include <gtk/gtk.h>
#include <gmime/internet-address.h>

#if ENABLE_ESMTP
#include "smtp-server.h"
#endif

#ifdef __cpluscplus
extern "C" 
{
#endif /* __cplusplus */

    GType libbalsa_identity_get_type(void);

#define LIBBALSA_TYPE_IDENTITY \
    (libbalsa_identity_get_type ())
#define LIBBALSA_IDENTITY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST (obj, LIBBALSA_TYPE_IDENTITY, \
                                 LibBalsaIdentity))
#define LIBBALSA_IDENTITY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST (klass, LIBBALSA_TYPE_IDENTITY, \
                              LibBalsaIdentityClass))
#define LIBBALSA_IS_IDENTITY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE (obj, LIBBALSA_TYPE_IDENTITY))
#define LIBBALSA_IS_IDENTITY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE (klass, LIBBALSA_TYPE_IDENTITY))

    typedef struct _LibBalsaIdentity LibBalsaIdentity;
    typedef struct _LibBalsaIdentityClass LibBalsaIdentityClass;
    
    
    struct _LibBalsaIdentity 
    {
        GObject object;
        
        gchar* identity_name;
        
        InternetAddress *ia;
        gchar* replyto;
        gchar* domain;
        gchar* bcc;
        gchar* reply_string;
        gchar* forward_string;
        gboolean send_mp_alternative;

        gchar* signature_path;
        gboolean sig_executable;
	gboolean sig_sending;
        gboolean sig_whenforward;
        gboolean sig_whenreply;
        gboolean sig_separator;
        gboolean sig_prepend;
        gchar *face;
        gchar *x_face;
	gboolean request_mdn;

#ifdef HAVE_GPGME
	gboolean gpg_sign;
	gboolean gpg_encrypt;
	gboolean always_trust;
	gboolean warn_send_plain;
	gint crypt_protocol;
#endif
#if ENABLE_ESMTP
	LibBalsaSmtpServer *smtp_server;
#endif                          /* ENABLE_ESMTP */
    };

    struct _LibBalsaIdentityClass 
    {
        GObjectClass parent_class;
    };


/* Function prototypes */
    GObject* libbalsa_identity_new(void);
    GObject* libbalsa_identity_new_with_name(const gchar* ident_name);
    
    void libbalsa_identity_set_identity_name(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_address(LibBalsaIdentity*, InternetAddress*);
    void libbalsa_identity_set_replyto(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_domain(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_bcc(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_reply_string(LibBalsaIdentity* , const gchar*);
    void libbalsa_identity_set_forward_string(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_send_mp_alternative(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_signature_path(LibBalsaIdentity*, const gchar*);
    void libbalsa_identity_set_sig_executable(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_sig_sending(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_sig_whenforward(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_sig_whenreply(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_sig_separator(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_sig_prepend(LibBalsaIdentity*, gboolean);
    gchar* libbalsa_identity_get_signature(LibBalsaIdentity*, 
                                           GtkWindow *parent);
#if ENABLE_ESMTP
    void libbalsa_identity_set_smtp_server(LibBalsaIdentity * ident,
                                           LibBalsaSmtpServer *
                                           smtp_server);
#endif                          /* ENABLE_ESMTP */

#ifdef HAVE_GPGME
    void libbalsa_identity_set_gpg_sign(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_gpg_encrypt(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_crypt_protocol(LibBalsaIdentity* ident, gint);
#endif

    void libbalsa_identity_config_dialog(GtkWindow * parent,
                                         GList ** identities,
                                         LibBalsaIdentity ** current,
#if ENABLE_ESMTP
					 GSList * smtp_servers,
#endif                          /* ENABLE_ESMTP */
                                         void (*changed_cb)(gpointer));

    typedef void (*LibBalsaIdentityCallback) (gpointer data,
                                              LibBalsaIdentity * identity);
    void libbalsa_identity_select_dialog(GtkWindow * parent,
                                         const gchar * prompt,
                                         GList * identities,
                                         LibBalsaIdentity * initial_id,
                                         LibBalsaIdentityCallback update,
                                         gpointer data);

    LibBalsaIdentity* libbalsa_identity_new_config(const gchar* name);
    void libbalsa_identity_save(LibBalsaIdentity* id, const gchar* prefix);

#ifdef __cplusplus
}
#endif
#endif /* __LIBBALSA_IDENTITY_H__ */
