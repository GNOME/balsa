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
# error "Include config.h before this file."
#endif

#include <gtk/gtk.h>
#include <gmime/internet-address.h>

#include "libbalsa.h"


G_BEGIN_DECLS


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
	gboolean request_dsn;

	gboolean gpg_sign;
	gboolean gpg_encrypt;
	gboolean always_trust;
	gboolean warn_send_plain;
	gint crypt_protocol;
        gchar *force_key_id;
	LibBalsaSmtpServer *smtp_server;
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
    void libbalsa_identity_set_smtp_server(LibBalsaIdentity * ident,
                                           LibBalsaSmtpServer *
                                           smtp_server);

    void libbalsa_identity_set_gpg_sign(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_gpg_encrypt(LibBalsaIdentity*, gboolean);
    void libbalsa_identity_set_crypt_protocol(LibBalsaIdentity* ident, gint);

    void libbalsa_identity_config_dialog(GtkWindow * parent,
                                         GList ** identities,
                                         LibBalsaIdentity ** current,
										 GSList * smtp_servers,
                                         void (*changed_cb)(gpointer));

    typedef void (*LibBalsaIdentityCallback) (gpointer data,
                                              LibBalsaIdentity * identity);
    void libbalsa_identity_select_dialog(GtkWindow * parent,
                                         const gchar * prompt,
                                         GList * identities,
                                         LibBalsaIdentity * initial_id,
                                         LibBalsaIdentityCallback update,
                                         gpointer data);
    GtkWidget * libbalsa_identity_combo_box(GList       * identities,
                                            const gchar * active_name,
                                            GCallback     changed_cb,
                                            gpointer      changed_data);

    LibBalsaIdentity* libbalsa_identity_new_config(const gchar* name);
    void libbalsa_identity_save(LibBalsaIdentity* id, const gchar* prefix);


G_END_DECLS


#endif /* __LIBBALSA_IDENTITY_H__ */
