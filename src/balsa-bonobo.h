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



#ifndef __BALSA_BONOBO_H
#define __BALSA_BONBO_H
 
#include <GNOME_Balsa.h>
#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-object.h>
 
G_BEGIN_DECLS
 
#define BALSA_APPLICATION_SERVER_TYPE         (balsa_application_server_get_type ())
#define BALSA_APPLICATION_SERVER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o),
BALSA_APPLICATION_SERVER_TYPE, BalsaApplicationServer))
#define BALSA_APPLICATION_SERVER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), BALSA_APPLICATION_SERVER_TYPE, BalsaApplicationServerClass))
#define BALSA_APPLICATION_SERVER_IS_OBJECT(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o),
BALSA_APPLICATION_SERVER_TYPE))
#define BALSA_APPLICATION_SERVER_IS_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), BALSA_APPLICATION_SERVER_TYPE))
#define BALSA_APPLICATION_SERVER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), BALSA_APPLICATION_SERVER_TYPE, BalsaApplicationServerClass))
 
typedef struct
{
        BonoboObject parent;
} GeditApplicationServer;
 
typedef struct
{
        BonoboObjectClass parent_class;
 
        POA_GNOME_Balsa_Application__epv epv;
} GeditApplicationServerClass;
 
GType          balsa_application_server_get_type (void);
 
BonoboObject  *balsa_application_server_new      (GdkScreen *screen);
 
G_END_DECLS
 
#endif /* __BALSA_BONOBO_H */
