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

#include <string.h>

#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-object.h>

#include "balsa-app.h"
#include "sendmsg-window.h"

#include "balsa-bonobo.h"


static void balsa_composer_class_init (BalsaComposerClass *klass);
static void balsa_composer_init (BalsaComposer *a);
static void balsa_composer_object_finalize (GObject *object);
static GObjectClass *balsa_composer_parent_class;
static void impl_balsa_composer_sendMessage (PortableServer_Servant _servant,
					     const CORBA_char *from,  
					     const CORBA_char *to, 
					     const CORBA_char *cc,
					     const CORBA_char *subject, 
					     const GNOME_Balsa_Composer_attachs *attachments,
					     const CORBA_boolean nogui,
					     CORBA_Environment * ev);
 
static BonoboObject *
balsa_composer_factory (BonoboGenericFactory *this_factory,
			const char *iid,
			gpointer user_data)
{
    BalsaComposer *a;
         
    a  = g_object_new (BALSA_COMPOSER_TYPE, NULL);
 
    return BONOBO_OBJECT (a);
}

BonoboObject *
balsa_composer_new (void) {
    BonoboGenericFactory *factory;
        
    factory = bonobo_generic_factory_new ("OAFIID:GNOME_Balsa_Composer_Factory",
					  balsa_composer_factory,
					  NULL);
    
    return BONOBO_OBJECT (factory);
}

static void
balsa_composer_class_init (BalsaComposerClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;
    POA_GNOME_Balsa_Composer__epv *epv = &klass->epv;
        
    balsa_composer_parent_class = g_type_class_peek_parent (klass);
    object_class->finalize = balsa_composer_object_finalize;
 
    /* connect implementation callbacks */
    epv->sendMessage = impl_balsa_composer_sendMessage;
}
 
static void
balsa_composer_init (BalsaComposer *c)
{
}


static void
impl_balsa_composer_sendMessage (PortableServer_Servant _servant,
				 const CORBA_char *from,  
				 const CORBA_char *to, 
				 const CORBA_char *cc,
				 const CORBA_char *subject, 
				 const GNOME_Balsa_Composer_attachs *attachments,
				 const CORBA_boolean nogui,
				 CORBA_Environment * ev)
{
    BalsaSendmsg *snd;
    guint i;
    
    gdk_threads_enter();
    snd = sendmsg_window_new(GTK_WIDGET(balsa_app.main_window), 
			     NULL, SEND_NORMAL); 
    gdk_threads_leave();

    if(strlen(to)) {
	if(g_ascii_strncasecmp(to, "mailto:", 7) == 0)
	    sendmsg_window_process_url(to+7, 
				       sendmsg_window_set_field, snd);
	else 
	    sendmsg_window_set_field(snd,"to", to);
    }
    
    for( i = 0; i < attachments->_length ; i++ ) {
	add_attachment(GNOME_ICON_LIST(snd->attachments[1]), 
		       attachments->_buffer[i], FALSE, NULL);	
    }
}



static void
balsa_composer_object_finalize (GObject *object)
{
    BalsaComposer *a = BALSA_COMPOSER (object);
 
    balsa_composer_parent_class->finalize (G_OBJECT (a));
}


BONOBO_TYPE_FUNC_FULL ( BalsaComposer,
			GNOME_Balsa_Composer,
			BONOBO_TYPE_OBJECT,
			balsa_composer );

