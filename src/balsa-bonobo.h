/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 * Copyright (C) 1997-2003 Stuart Parmenter and others,
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



#ifndef __BALSA_BONOBO_H
#define __BALSA_BONOBO_H

#ifndef BALSA_VERSION
# error "Include config.h before this file."
#endif

#if HAVE_GNOME

#include "Balsa.h" 
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-object.h>
 
G_BEGIN_DECLS
 
#define BALSA_COMPOSER_TYPE         (balsa_composer_get_type ())
#define BALSA_COMPOSER(o)           \
    (G_TYPE_CHECK_INSTANCE_CAST ((o), BALSA_COMPOSER_TYPE, BalsaComposer))
#define BALSA_COMPOSER_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), BALSA_COMPOSER_TYPE, BalsaComposerClass))
#define BALSA_COMPOSER_IS_OBJECT(o) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((o), BALSA_COMPOSER_TYPE))
#define BALSA_COMPOSER_IS_CLASS(k)  \
    (G_TYPE_CHECK_CLASS_TYPE ((k), BALSA_COMPOSER_TYPE))
#define BALSA_COMPOSER_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), BALSA_COMPOSER_TYPE, BalsaComposerClass))

#define BALSA_APPLICATION_TYPE         (balsa_application_get_type ())
#define BALSA_APPLICATION(o)           \
    (G_TYPE_CHECK_INSTANCE_CAST ((o), BALSA_APPLICATION_TYPE, BalsaApplication))
#define BALSA_APPLICATION_CLASS(k)     \
    (G_TYPE_CHECK_CLASS_CAST((k), BALSA_APPLICATION_TYPE, BalsaApplicationClass))
#define BALSA_APPLICATION_IS_OBJECT(o) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((o), BALSA_APPLICATION_TYPE))
#define BALSA_APPLICATION_IS_CLASS(k)  \
    (G_TYPE_CHECK_CLASS_TYPE ((k), BALSA_APPLICATION_TYPE))
#define BALSA_APPLICATION_GET_CLASS(o) \
    (G_TYPE_INSTANCE_GET_CLASS ((o), BALSA_APPLICATION_TYPE, BalsaApplicationClass))

G_END_DECLS

 
typedef struct
{
        BonoboObject parent;
} BalsaComposer;
 
typedef struct
{
        BonoboObjectClass parent_class;
 
        POA_GNOME_Balsa_Composer__epv epv;
} BalsaComposerClass;
 
GType          balsa_composer_get_type (void);
BonoboObject  *balsa_composer_new      (void);


typedef struct
{
        BonoboObject parent;
} BalsaApplication;
 
typedef struct
{
        BonoboObjectClass parent_class;
 
        POA_GNOME_Balsa_Application__epv epv;
} BalsaApplicationClass;
 
GType          balsa_application_get_type (void);
BonoboObject  *balsa_application_new      (void);

#endif /* HAVE_GNOME */ 
 
#endif /* __BALSA_BONOBO_H */
