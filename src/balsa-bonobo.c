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


#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H
# include "config.h"
#endif                          /* HAVE_CONFIG_H */
#include "balsa-bonobo.h"

#if HAVE_GNOME

#include <string.h>

#include <bonobo/bonobo-generic-factory.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-object.h>

#include "balsa-app.h"
#include "sendmsg-window.h"

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


static void balsa_application_class_init (BalsaApplicationClass *klass);
static void balsa_application_init (BalsaApplication *a);
static void balsa_application_object_finalize (GObject *object);
static GObjectClass *balsa_application_parent_class;
static void impl_balsa_application_checkmail (PortableServer_Servant _servant,
					      CORBA_Environment * ev);
static void impl_balsa_application_openMailbox (PortableServer_Servant _servant,
						const CORBA_char * name,
						CORBA_Environment * ev);
static void impl_balsa_application_openUnread (PortableServer_Servant _servant,
					       CORBA_Environment * ev);
static void impl_balsa_application_openInbox (PortableServer_Servant _servant,
					      CORBA_Environment * ev);
static void impl_balsa_application_getStats (PortableServer_Servant _servant,
                                             CORBA_long *unread,
                                             CORBA_long *unsent,
                                             CORBA_Environment * ev);
/* from main.c */
gboolean initial_open_unread_mailboxes();
gboolean initial_open_inbox();
void balsa_get_stats(long *unread, long *unsent);

/*
 *
 * Balsa Composer 
 *
 */
 
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
    snd = sendmsg_window_compose();
    gdk_threads_leave();

    if(strlen(to)) {
	if(g_ascii_strncasecmp(to, "mailto:", 7) == 0)
	    sendmsg_window_process_url(to+7, 
				       sendmsg_window_set_field, snd);
	else 
	    sendmsg_window_set_field(snd,"to", to);
    }
    
    for( i = 0; i < attachments->_length ; i++ ) {
	add_attachment(snd, g_strdup(attachments->_buffer[i]), FALSE, NULL);	
    }
    snd->quit_on_close = FALSE;
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

/*
 *
 * Balsa Application 
 *
 */


static BonoboObject *
balsa_application_factory (BonoboGenericFactory *this_factory,
			   const char *iid,
			   gpointer user_data)
{
    BalsaComposer *a;
         
    a  = g_object_new (BALSA_APPLICATION_TYPE, NULL);
 
    return BONOBO_OBJECT (a);
}

BonoboObject *
balsa_application_new (void) {
    BonoboGenericFactory *factory;
        
    factory = bonobo_generic_factory_new ("OAFIID:GNOME_Balsa_Application_Factory",
					  balsa_application_factory,
					  NULL);
    
    return BONOBO_OBJECT (factory);
}

static void
balsa_application_class_init (BalsaApplicationClass *klass)
{
    GObjectClass *object_class = (GObjectClass *) klass;
    POA_GNOME_Balsa_Application__epv *epv = &klass->epv;
        
    balsa_application_parent_class = g_type_class_peek_parent (klass);
    object_class->finalize = balsa_application_object_finalize;
 
    /* connect implementation callbacks */
    epv->checkmail = impl_balsa_application_checkmail;
    epv->openMailbox = impl_balsa_application_openMailbox;
    epv->openUnread = impl_balsa_application_openUnread;
    epv->openInbox = impl_balsa_application_openInbox;
    epv->getStats  = impl_balsa_application_getStats;
}


static void
impl_balsa_application_checkmail (PortableServer_Servant _servant,
				  CORBA_Environment * ev) {
    
    check_new_messages_real (NULL, TYPE_CALLBACK);
}

static void
impl_balsa_application_openMailbox (PortableServer_Servant _servant,
				    const CORBA_char * name,
				    CORBA_Environment * ev) {
    gchar **urls = g_strsplit(name, ";", 20);
    g_idle_add((GSourceFunc) open_mailboxes_idle_cb, urls);
    gtk_window_present(GTK_WINDOW(balsa_app.main_window));
}

static void
impl_balsa_application_openUnread (PortableServer_Servant _servant,
				   CORBA_Environment * ev) {
    g_idle_add((GSourceFunc) initial_open_unread_mailboxes, NULL);
    gtk_window_present(GTK_WINDOW(balsa_app.main_window));
}

static void
impl_balsa_application_openInbox (PortableServer_Servant _servant,
				  CORBA_Environment * ev) {
    initial_open_inbox();
    gtk_window_present(GTK_WINDOW(balsa_app.main_window));
}

static void
impl_balsa_application_getStats (PortableServer_Servant _servant,
                                 CORBA_long *unread,
                                 CORBA_long *unsent,
                                 CORBA_Environment * ev) {
    long r, s;
    balsa_get_stats(&r, &s);
    *unread = r;
    *unsent = s;
}
 
static void
balsa_application_init (BalsaApplication *c)
{
}


static void
balsa_application_object_finalize (GObject *object)
{
    BalsaApplication *a = BALSA_APPLICATION (object);
 
    balsa_application_parent_class->finalize (G_OBJECT (a));
}


BONOBO_TYPE_FUNC_FULL ( BalsaApplication,
			GNOME_Balsa_Application,
			BONOBO_TYPE_OBJECT,
			balsa_application );

#endif /* HAVE_GNOME */ 
