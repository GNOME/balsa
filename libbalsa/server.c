/* -*-mode:c; c-style:k&r; c-basic-offset:4; -*- */
/* Balsa E-Mail Client
 *
 * Copyright (C) 1997-2002 Stuart Parmenter and others,
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

#include "config.h"

#include "libbalsa.h"
#include "libbalsa_private.h"
#include "misc.h"

#include <libgnome/gnome-config.h> 
#include <libgnome/gnome-i18n.h> 


/* GTK_CLASS_TYPE for 1.2<->1.3/2.0 GTK+ compatibility */
#ifndef GTK_CLASS_TYPE
#define GTK_CLASS_TYPE(x) (GTK_OBJECT_CLASS(x)->type)
#endif /* GTK_CLASS_TYPE */

static GtkObjectClass *parent_class = NULL;
static void libbalsa_server_class_init(LibBalsaServerClass * klass);
static void libbalsa_server_init(LibBalsaServer * server);
static void libbalsa_server_destroy(GtkObject * object);

static void libbalsa_server_real_set_username(LibBalsaServer * server,
					      const gchar * username);
static void libbalsa_server_real_set_password(LibBalsaServer * server,
					      const gchar * passwd);
static void libbalsa_server_real_set_host(LibBalsaServer * server,
					  const gchar * host
#ifdef USE_SSL
					  ,gboolean use_ssl
#endif
					  );
/* static gchar* libbalsa_server_real_get_password(LibBalsaServer *server); */

enum {
    SET_USERNAME,
    SET_PASSWORD,
    SET_HOST,
    GET_PASSWORD,
    LAST_SIGNAL
};

static guint libbalsa_server_signals[LAST_SIGNAL];

GtkType libbalsa_server_get_type(void)
{
    static GtkType server_type = 0;

    if (!server_type) {
	static const GtkTypeInfo server_info = {
	    "LibBalsaServer",
	    sizeof(LibBalsaServer),
	    sizeof(LibBalsaServerClass),
	    (GtkClassInitFunc) libbalsa_server_class_init,
	    (GtkObjectInitFunc) libbalsa_server_init,
	    /* reserved_1 */ NULL,
	    /* reserved_2 */ NULL,
	    (GtkClassInitFunc) NULL,
	};

	server_type = gtk_type_unique(gtk_object_get_type(), &server_info);
    }

    return server_type;
}

static void
libbalsa_server_class_init(LibBalsaServerClass * klass)
{
    GtkObjectClass *object_class;

    object_class = GTK_OBJECT_CLASS(klass);

    parent_class = gtk_type_class(gtk_object_get_type());

    object_class->destroy = libbalsa_server_destroy;

    libbalsa_server_signals[SET_USERNAME] =
	gtk_signal_new("set-username",
		       GTK_RUN_FIRST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaServerClass,
					 set_username),
		       gtk_marshal_NONE__STRING, GTK_TYPE_NONE, 1,
		       GTK_TYPE_STRING);
    libbalsa_server_signals[SET_PASSWORD] =
	gtk_signal_new("set-password", GTK_RUN_FIRST,
                       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaServerClass,
					 set_password),
		       gtk_marshal_NONE__STRING, GTK_TYPE_NONE, 1,
		       GTK_TYPE_STRING);
    libbalsa_server_signals[SET_HOST] =
	gtk_signal_new("set-host", GTK_RUN_FIRST,
                       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaServerClass, set_host),
#ifdef USE_SSL
		       gtk_marshal_NONE__POINTER_INT, GTK_TYPE_NONE, 2,
		       GTK_TYPE_POINTER, GTK_TYPE_INT);
#else
                       gtk_marshal_NONE__POINTER, GTK_TYPE_NONE, 1,
	               GTK_TYPE_POINTER);
#endif


    libbalsa_server_signals[GET_PASSWORD] =
	gtk_signal_new("get-password",
		       GTK_RUN_LAST,
		       GTK_CLASS_TYPE(object_class),
		       GTK_SIGNAL_OFFSET(LibBalsaServerClass,
					 get_password),
		       libbalsa_marshal_POINTER__OBJECT, GTK_TYPE_POINTER,
		       1, LIBBALSA_TYPE_MAILBOX);

    klass->set_username = libbalsa_server_real_set_username;
    klass->set_password = libbalsa_server_real_set_password;
    klass->set_host = libbalsa_server_real_set_host;
    klass->get_password = NULL;	/* libbalsa_server_real_get_password; */
}

static void
libbalsa_server_init(LibBalsaServer * server)
{
    server->host = NULL;
    server->user = NULL;
    server->passwd = NULL;
    server->remember_passwd = TRUE;
#ifdef USE_SSL
    server->use_ssl = FALSE;
#endif
}

/* leave object in sane state (NULLified fields) */
static void
libbalsa_server_destroy(GtkObject * object)
{
    LibBalsaServer *server;

    g_return_if_fail(LIBBALSA_IS_SERVER(object));

    server = LIBBALSA_SERVER(object);

    g_free(server->host);   server->host = NULL;
    g_free(server->user);   server->user = NULL;
    g_free(server->passwd); server->passwd = NULL;

    if (GTK_OBJECT_CLASS(parent_class)->destroy)
	(*GTK_OBJECT_CLASS(parent_class)->destroy) (GTK_OBJECT(object));
}

GtkObject *
libbalsa_server_new(LibBalsaServerType type)
{
    LibBalsaServer *server;
    server = gtk_type_new(LIBBALSA_TYPE_SERVER);
    server->type = type;

    return GTK_OBJECT(server);
}

void
libbalsa_server_set_username(LibBalsaServer * server,
			     const gchar * username)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    gtk_signal_emit(GTK_OBJECT(server),
		    libbalsa_server_signals[SET_USERNAME], username);
}

void
libbalsa_server_set_password(LibBalsaServer * server, const gchar * passwd)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    gtk_signal_emit(GTK_OBJECT(server),
		    libbalsa_server_signals[SET_PASSWORD], passwd);
}

void
libbalsa_server_set_host(LibBalsaServer * server, const gchar * host
#ifdef USE_SSL
			 , gboolean use_ssl
#endif
)
{
    g_return_if_fail(server != NULL);
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    gtk_signal_emit(GTK_OBJECT(server), libbalsa_server_signals[SET_HOST],
		    host
#ifdef USE_SSL
		    , use_ssl
#endif
		    );

}

gchar *
libbalsa_server_get_password(LibBalsaServer * server,
			     LibBalsaMailbox * mbox)
{
    gchar *retval = NULL;

    g_return_val_if_fail(server != NULL, NULL);
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    gtk_signal_emit(GTK_OBJECT(server),
		    libbalsa_server_signals[GET_PASSWORD], mbox, &retval);
    return retval;
}

static void
libbalsa_server_real_set_username(LibBalsaServer * server,
				  const gchar * username)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->user);
    server->user = g_strdup(username);
}

static void
libbalsa_server_real_set_password(LibBalsaServer * server,
				  const gchar * passwd)
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->passwd);
    if(passwd[0])
	server->passwd = g_strdup(passwd);
    else server->passwd = NULL;
}

static void
libbalsa_server_real_set_host(LibBalsaServer * server, const gchar * host
#ifdef USE_SSL
			      , gboolean use_ssl
#endif
			      )
{
    g_return_if_fail(LIBBALSA_IS_SERVER(server));

    g_free(server->host);
    server->host = g_strdup(host);
#ifdef USE_SSL
    server->use_ssl = use_ssl;
#endif 
}


#if 0
static gchar *
libbalsa_server_real_get_password(LibBalsaServer * server)
{
    g_return_val_if_fail(LIBBALSA_IS_SERVER(server), NULL);

    return g_strdup(server->passwd);
}
#endif


/* libbalsa_server_load_config:
   load the server configuration using gnome-config.
   Try to use sensible defaults. 
   FIXME: Port field is kept here only for compatibility, drop after 1.4.x
   release.
*/
void
libbalsa_server_load_config(LibBalsaServer * server)
{
    gboolean d;
    server->host = gnome_config_get_string("Server");
    if(strrchr(server->host, ':') == NULL) {
        gint port;
        port = gnome_config_get_int_with_default("Port", &d);
        if (!d) {
            gchar *newhost = g_strdup_printf("%s:%d", server->host, port);
            g_free(server->host);
            server->host = newhost;
        }
    }       
#ifdef USE_SSL
    server->use_ssl = gnome_config_get_bool_with_default("SSL", &d);
    if (d)
	server->use_ssl = FALSE;
#endif
    server->user = gnome_config_private_get_string("Username");
    server->remember_passwd = gnome_config_get_bool("RememberPasswd=true");
    if(server->remember_passwd)
        server->passwd = gnome_config_private_get_string("Password");
    if(server->passwd && server->passwd[0] == '\0') {
	g_free(server->passwd);
	server->passwd = NULL;
    }
	
    if (!server->user)
	server->user = g_strdup(getenv("USER"));

    if (server->passwd != NULL) {
	gchar *buff = libbalsa_rot(server->passwd);
	g_free(server->passwd);
	server->passwd = buff;
    }
}

void
libbalsa_server_save_config(LibBalsaServer * server)
{
    gnome_config_set_string("Server", server->host);
    gnome_config_private_set_string("Username", server->user);
    gnome_config_set_bool("RememberPasswd", server->remember_passwd);

    if (server->remember_passwd && server->passwd != NULL) {
	gchar *buff = libbalsa_rot(server->passwd);
	gnome_config_private_set_string("Password", buff);
	g_free(buff);
    }
#ifdef USE_SSL
    gnome_config_set_bool("SSL", server->use_ssl);
#endif
}
