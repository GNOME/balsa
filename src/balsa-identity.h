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


#ifndef __BALSA_IDENTITY_H__
#define __BALSA_IDENTITY_H__

#include <gnome.h>
#include "libbalsa.h"
#include "balsa-app.h"

#ifdef __cpluscplus
extern "C" 
{
#endif /* __cplusplus */

    GtkType balsa_identity_get_type(void);

#define BALSA_TYPE_IDENTITY          (balsa_identity_get_type ())
#define BALSA_IDENTITY(obj)          (GTK_CHECK_CAST (obj, balsa_identity_get_type (), BalsaIdentity))
#define BALSA_IDENTITY_CLASS(klass)  (GTK_CHECK_CLASS_CAST (klass, balsa_identity_get_type (), BalsaIdentityClass))
#define BALSA_IS_IDENTITY(obj)       (GTK_CHECK_TYPE (obj, balsa_identity_get_type ()))
#define BALSA_IS_IDENTITY_CLASS(klass) (GTK_CHECK_CLASS_TYPE (klass, BALSA_TYPE_IDENTITY))

    typedef struct _BalsaIdentity BalsaIdentity;
    typedef struct _BalsaIdentityClass BalsaIdentityClass;
    
    
    struct _BalsaIdentity 
    {
        GtkObject object;
        
        gchar* identity_name;
        
        LibBalsaAddress* address;
        gchar* replyto;
        gchar* domain;
        gchar* bcc;
        gchar* reply_string;
        gchar* forward_string;

        gchar* signature_path;
        gboolean sig_sending;
        gboolean sig_whenforward;
        gboolean sig_whenreply;
        gboolean sig_separator;
        gboolean sig_prepend;
    };

    struct _BalsaIdentityClass 
    {
        GtkObjectClass parent_class;
    };


/* Function prototypes */
    GtkObject* balsa_identity_new(void);
    GtkObject* balsa_identity_new_with_name(const gchar* ident_name);
    
    void balsa_identity_set_current(BalsaIdentity* ident);

    void balsa_identity_set_identity_name(BalsaIdentity*, const gchar*);
    void balsa_identity_set_address(BalsaIdentity*, LibBalsaAddress*);
    void balsa_identity_set_replyto(BalsaIdentity*, const gchar*);
    void balsa_identity_set_domain(BalsaIdentity*, const gchar*);
    void balsa_identity_set_bcc(BalsaIdentity*, const gchar*);
    void balsa_identity_set_reply_string(BalsaIdentity* , const gchar*);
    void balsa_identity_set_forward_string(BalsaIdentity*, const gchar*);
    void balsa_identity_set_signature_path(BalsaIdentity*, const gchar*);
    void balsa_identity_set_sig_sending(BalsaIdentity*, gboolean);
    void balsa_identity_set_sig_whenforward(BalsaIdentity*, gboolean);
    void balsa_identity_set_sig_whenreply(BalsaIdentity*, gboolean);
    void balsa_identity_set_sig_separator(BalsaIdentity*, gboolean);
    void balsa_identity_set_sig_prepend(BalsaIdentity*, gboolean);

    GtkWidget* balsa_identity_config_frame(gboolean with_buttons);
    GtkWidget* balsa_identity_config_dialog(void);
    BalsaIdentity* balsa_identity_select_dialog(const gchar* prompt);

    

#ifdef __cplusplus
}
#endif
#endif /* __BALSA_IDENTITY_H__ */
