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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  
 * 02111-1307, USA.
 */



#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
                                                                                
#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>


static void balsa_application_server_class_init (GeditApplicationServerClass *klass);
static void balsa_application_server_init (GeditApplicationServer *a);
static void balsa_application_server_object_finalize (GObject *object);
static GObjectClass *gedit_application_server_parent_class;
 
static BonoboObject *
balsa_application_server_factory (BonoboGenericFactory *this_factory,
                           const char *iid,
                           gpointer user_data)
{
        GeditApplicationServer *a;
         
        a  = g_object_new (GEDIT_APPLICATION_SERVER_TYPE, NULL);
 
        return BONOBO_OBJECT (a);
}
                                                                                
BonoboObject *
balsa_application_server_new (GdkScreen *screen)
{
        BonoboGenericFactory *factory;
        char                 *display_name;
        char                 *registration_id;
                                                                                
	registration_id = 
	  bonobo_activation_make_registration_id ( "OAFIID:GNOME_Balsa_Factory",
						   "0");
                                                                                
        factory = bonobo_generic_factory_new (registration_id,
                                              balsa_application_server_factory,
                                              NULL);
                                                                                
        g_free (registration_id);
                                                                                
        return BONOBO_OBJECT (factory);
}
                                                                                
                                                                                
static void
balsa_application_server_class_init (GeditApplicationServerClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;
        POA_GNOME_Balsa_Application__epv *epv = &klass->epv;
                                                                                
        balsa_application_server_parent_class = g_type_class_peek_parent (klass);
                                                                                
        object_class->finalize = gedit_application_server_object_finalize;
 
        /* connect implementation callbacks */

}
 
static void
balsa_application_server_init (BalsaApplicationServer *c)
{
}
 

static void
impl_balsa_application_server_setHeaders (PortableServer_Servant _servant,
                                               CORBA_Environment * ev)
{
}

static void
impl_balsa_application_server_attachMIME (PortableServer_Servant _servant,
                                               CORBA_Environment * ev)
{
}


static void
impl_balsa_application_server_show (PortableServer_Servant _servant,
				    CORBA_Environment * ev)
{
}



static void
balsa_application_server_object_finalize (GObject *object)
{
        BalsaApplicationServer *a = BALSA_APPLICATION_SERVER (object);
 
        gedit_application_server_parent_class->finalize (G_OBJECT (a));
}
 
BONOBO_TYPE_FUNC_FULL (
        BalsaApplicationServer,
        GNOME_Balsa_Application,
        BONOBO_TYPE_OBJECT,
        balsa_application_server);
